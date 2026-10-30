#include <loader/pe_image.h>
#include <utilities/string.h>

int pe_image_range(u32 size, u32 rva, u64 bytes) {
  return rva <= size && bytes <= size - rva;
}
const char *pe_image_string(const void *memory, u32 size, u32 rva) {
  if (rva >= size)
    return 0;
  const char *s = (const char *)memory + rva;
  for (u32 i = rva; i < size; ++i)
    if (!s[i - rva])
      return s;
  return 0;
}
static int file_range(usize size, u64 offset, u64 length) {
  return offset <= size && length <= size - offset;
}
int pe_image_parse(struct pe_image *im, const void *file, usize size) {
  const u8 *bytes = file;
  struct IMAGE_DOS_HEADER dos;
  if (!im || !file || size < sizeof(dos))
    return -1;
  memcpy(&dos, bytes, sizeof(dos));
  if (dos.e_magic != 0x5a4d || !file_range(size, dos.e_lfanew, sizeof(im->nt)))
    return -1;
  memset(im, 0, sizeof(*im));
  memcpy(&im->nt, bytes + dos.e_lfanew, sizeof(im->nt));
  const struct IMAGE_OPTIONAL_HEADER64 *o = &im->nt.optionalHeader;
  if (im->nt.signature != 0x4550 || im->nt.fileHeader.machine != 0x8664 ||
      o->magic != 0x20b ||
      im->nt.fileHeader.sizeOfOptionalHeader != sizeof(*o) ||
      !im->nt.fileHeader.numberOfSections ||
      im->nt.fileHeader.numberOfSections > 96 || !o->sizeOfImage ||
      o->sizeOfImage > PE_MAX_IMAGE || (o->sizeOfImage & 4095) || o->sectionAlignment != 4096 ||
      !o->fileAlignment || o->fileAlignment > 4096 ||
      (o->fileAlignment & (o->fileAlignment - 1)) || !o->sizeOfHeaders ||
      o->sizeOfHeaders > o->sizeOfImage || o->sizeOfHeaders > size ||
      o->numberOfRvaAndSizes > 16 || o->addressOfEntryPoint >= o->sizeOfImage)
    return -1;
  u64 sections = (u64)dos.e_lfanew + sizeof(im->nt);
  if (!file_range(size, sections,
                  (u64)im->nt.fileHeader.numberOfSections * 40) ||
      sections + (u64)im->nt.fileHeader.numberOfSections * 40 >
          o->sizeOfHeaders)
    return -1;
  im->sections_offset = (u32)sections;
  im->file = file;
  im->file_size = size;
  for (u32 i = 0; i < o->numberOfRvaAndSizes; i++) {
    /* Security directory is a file offset, not a mapped RVA. */
    if (i == 4)
      continue;
    if (!pe_image_range(o->sizeOfImage, o->dataDirectory[i].virtualAddress,
                        o->dataDirectory[i].size))
      return -1;
  }
  for (u32 i=o->numberOfRvaAndSizes;i<16;i++)
    memset(&im->nt.optionalHeader.dataDirectory[i],0,sizeof(o->dataDirectory[i]));
  /* PE TLS templates/callbacks and delay imports need a separate runtime
   * contract. */
  if (o->dataDirectory[9].size || o->dataDirectory[13].size)
    return -1;
  for (u32 i = 0; i < im->nt.fileHeader.numberOfSections; i++) {
    struct IMAGE_SECTION_HEADER sh;
    memcpy(&sh, bytes + sections + i * 40, 40);
    u32 span =
        sh.virtualSize > sh.sizeOfRawData ? sh.virtualSize : sh.sizeOfRawData;
    if (!span)
      continue;
    if ((sh.virtualAddress & 4095) ||
        sh.virtualAddress < ((o->sizeOfHeaders + 4095) & ~4095u) ||
        !pe_image_range(o->sizeOfImage, sh.virtualAddress, span) ||
        !file_range(size, sh.pointerToRawData, sh.sizeOfRawData))
      return -1;
    u64 end = ((u64)sh.virtualAddress + span + 4095) & ~4095ULL;
    for (u32 j = 0; j < i; j++) {
      struct IMAGE_SECTION_HEADER prev;
      memcpy(&prev, bytes + sections + j * 40, 40);
      u32 n = prev.virtualSize > prev.sizeOfRawData ? prev.virtualSize
                                                    : prev.sizeOfRawData;
      u64 pe = ((u64)prev.virtualAddress + n + 4095) & ~4095ULL;
      if (n && sh.virtualAddress < pe && prev.virtualAddress < end)
        return -1;
    }
  }
  if (o->addressOfEntryPoint) {
    int executable = 0;
    for (u32 i = 0; i < im->nt.fileHeader.numberOfSections; i++) {
      struct IMAGE_SECTION_HEADER sh;
      memcpy(&sh, bytes + sections + i * 40, 40);
      if ((sh.characteristics & 0x20000000) &&
          o->addressOfEntryPoint >= sh.virtualAddress &&
          o->addressOfEntryPoint - sh.virtualAddress < sh.virtualSize)
        executable = 1;
    }
    if (!executable)
      return -1;
  }
  return 0;
}
int pe_image_materialize(const struct pe_image *im, void *memory) {
  if (!im || !memory)
    return -1;
  memset(memory, 0, im->nt.optionalHeader.sizeOfImage);
  memcpy(memory, im->file, im->nt.optionalHeader.sizeOfHeaders);
  for (u32 i = 0; i < im->nt.fileHeader.numberOfSections; i++) {
    struct IMAGE_SECTION_HEADER sh;
    memcpy(&sh, im->file + im->sections_offset + i * 40, 40);
    memcpy((u8 *)memory + sh.virtualAddress, im->file + sh.pointerToRawData,
           sh.sizeOfRawData);
  }
  return 0;
}
int pe_image_relocate(const struct pe_image *im, void *memory, u64 base) {
  u64 delta = base - im->nt.optionalHeader.imageBase;
  if (!delta)
    return 0;
  struct IMAGE_DATA_DIRECTORY d = im->nt.optionalHeader.dataDirectory[5];
  u32 size = im->nt.optionalHeader.sizeOfImage;
  if (!d.size || (im->nt.fileHeader.characteristics & 1))
    return -1;
  u8 *m = memory;
  u32 at = 0;
  while (at < d.size) {
    u32 page, block;
    if (d.size - at < 8)
      return -1;
    memcpy(&page, m + d.virtualAddress + at, 4);
    memcpy(&block, m + d.virtualAddress + at + 4, 4);
    if (block < 8 || (block & 1) || block > d.size - at)
      return -1;
    for (u32 p = 8; p < block; p += 2) {
      u16 entry;
      memcpy(&entry, m + d.virtualAddress + at + p, 2);
      u32 kind = entry >> 12;
      if (!kind)
        continue;
      u64 target = (u64)page + (entry & 4095);
      if (kind != 10 || target > size || !pe_image_range(size, (u32)target, 8))
        return -1;
      u64 value;
      memcpy(&value, m + target, 8);
      value += delta;
      memcpy(m + target, &value, 8);
    }
    at += block;
  }
  return 0;
}
u32 pe_image_export(const struct pe_image *im, const void *memory,
                    const char *name, u32 ordinal, const char **forwarder) {
  struct IMAGE_DATA_DIRECTORY d = im->nt.optionalHeader.dataDirectory[0];
  u32 size = im->nt.optionalHeader.sizeOfImage;
  const u8 *m = memory;
  *forwarder = 0;
  if (d.size < 40 || !pe_image_range(size, d.virtualAddress, 40))
    return 0;
  u32 e[10];
  memcpy(e, m + d.virtualAddress, 40);
  u32 base = e[4], functions = e[5], names = e[6], fn = e[7], nm = e[8],
      ord = e[9], index = ~0u;
  if (!pe_image_range(size, fn, (u64)functions * 4) ||
      !pe_image_range(size, nm, (u64)names * 4) ||
      !pe_image_range(size, ord, (u64)names * 2))
    return 0;
  if (name) {
    for (u32 i = 0; i < names; i++) {
      u32 r;
      u16 n;
      memcpy(&r, m + nm + i * 4, 4);
      memcpy(&n, m + ord + i * 2, 2);
      const char *s = pe_image_string(memory, size, r);
      if (!s)
        return 0;
      if (!strcmp(s, name)) {
        index = n;
        break;
      }
    }
  } else if (ordinal >= base)
    index = ordinal - base;
  if (index >= functions)
    return 0;
  u32 rva;
  memcpy(&rva, m + fn + index * 4, 4);
  if (!rva || rva >= size)
    return 0;
  if (rva >= d.virtualAddress && rva - d.virtualAddress < d.size) {
    const char *s = pe_image_string(memory, d.virtualAddress + d.size, rva);
    if (!s)
      return 0;
    *forwarder = s;
  }
  return rva;
}
