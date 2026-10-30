/* Validate production PE parsing/fixups with malformed disk fixtures. */
#include <assert.h>
#include <loader/pe_image.h>
#include <stdio.h>
#include <string.h>
static unsigned char file[8192], mapped[8192], saved[8192];
static struct IMAGE_NT_HEADERS64 *nt(void) { return (void *)(file + 128); }
static struct IMAGE_SECTION_HEADER *section(void) {
  return (void *)(file + 128 + sizeof(*nt()));
}
static void build(void) {
  memset(file, 0, sizeof(file));
  struct IMAGE_DOS_HEADER *dos = (void *)file;
  dos->e_magic = 0x5a4d;
  dos->e_lfanew = 128;
  struct IMAGE_NT_HEADERS64 *n = nt();
  n->signature = 0x4550;
  n->fileHeader.machine = 0x8664;
  n->fileHeader.numberOfSections = 1;
  n->fileHeader.sizeOfOptionalHeader = 240;
  n->optionalHeader.magic = 0x20b;
  n->optionalHeader.imageBase = 0x140000000ULL;
  n->optionalHeader.sectionAlignment = 4096;
  n->optionalHeader.fileAlignment = 512;
  n->optionalHeader.sizeOfImage = 8192;
  n->optionalHeader.sizeOfHeaders = 512;
  n->optionalHeader.addressOfEntryPoint = 4096;
  n->optionalHeader.numberOfRvaAndSizes = 16;
  *section() = (struct IMAGE_SECTION_HEADER){.virtualAddress = 4096,
                                             .virtualSize = 4096,
                                             .sizeOfRawData = 4096,
                                             .pointerToRawData = 512,
                                             .characteristics = 0xe0000000};
  n->optionalHeader.dataDirectory[0] =
      (struct IMAGE_DATA_DIRECTORY){0x1100, 0x100};
  uint32_t e[10] = {0, 0, 0, 0, 1, 2, 1, 0x1150, 0x1160, 0x1170};
  memcpy(file + 512 + 0x100, e, 40);
  uint32_t f[2] = {0x1000, 0x11b0}, name = 0x11a0;
  uint16_t ordinal = 0;
  memcpy(file + 512 + 0x150, f, 8);
  memcpy(file + 512 + 0x160, &name, 4);
  memcpy(file + 512 + 0x170, &ordinal, 2);
  memcpy(file + 512 + 0x1a0, "Example", 8);
  memcpy(file + 512 + 0x1b0, "other.Target", 13);
  n->optionalHeader.dataDirectory[5] =
      (struct IMAGE_DATA_DIRECTORY){0x1300, 12};
  uint32_t reloc[2] = {0x1000, 12};
  uint16_t entries[2] = {0xa200, 0};
  uint64_t absolute = 0x140000123ULL;
  memcpy(file + 512 + 0x300, reloc, 8);
  memcpy(file + 512 + 0x308, entries, 4);
  memcpy(file + 512 + 0x200, &absolute, 8);
}
int main(void) {
  struct pe_image image;
  build();
  memcpy(saved, file, sizeof(file));
  assert(!pe_image_parse(&image, file, sizeof(file)));
  assert(!pe_image_materialize(&image, mapped));
  const char *forward = 0;
  assert(pe_image_export(&image, mapped, "Example", 0, &forward) == 0x1000 &&
         !forward);
  assert(pe_image_export(&image, mapped, 0, 2, &forward) == 0x11b0 &&
         !strcmp(forward, "other.Target"));
  assert(!pe_image_export(&image, mapped, "missing", 0, &forward));
  assert(!pe_image_relocate(&image, mapped, 0x180000000ULL));
  uint64_t relocated;
  memcpy(&relocated, mapped + 0x1200, 8);
  assert(relocated == 0x180000123ULL);
#define RESET() memcpy(file, saved, sizeof(file))
#define BAD() assert(pe_image_parse(&image, file, sizeof(file)) < 0)
  RESET();
  nt()->optionalHeader.magic = 0x10b;
  BAD();
  RESET();
  ((struct IMAGE_DOS_HEADER *)file)->e_lfanew = 0xfffffff0;
  BAD();
  RESET();
  section()->virtualAddress = 0xfffff000;
  BAD();
  RESET();
  section()->pointerToRawData = 0xfffffff0;
  BAD();
  RESET();
  section()->characteristics = 0x40000000;
  BAD();
  RESET();
  nt()->optionalHeader.dataDirectory[1] =
      (struct IMAGE_DATA_DIRECTORY){8190, 20};
  BAD();
  RESET();
  nt()->optionalHeader.dataDirectory[9] =
      (struct IMAGE_DATA_DIRECTORY){0x1400, 40};
  BAD();
  RESET();
  nt()->fileHeader.numberOfSections = 2;
  section()[1] = section()[0];
  BAD();
  RESET();
  assert(pe_image_parse(&image, file, 511) < 0);
  RESET();
  assert(!pe_image_parse(&image, file, sizeof(file)));
  pe_image_materialize(&image, mapped);
  uint32_t bad = 7;
  memcpy(mapped + 0x1304, &bad, 4);
  assert(pe_image_relocate(&image, mapped, 0x180000000ULL) < 0);
  pe_image_materialize(&image, mapped);
  uint16_t bad_type = 0x3200;
  memcpy(mapped + 0x1308, &bad_type, 2);
  assert(pe_image_relocate(&image, mapped, 0x180000000ULL) < 0);
  pe_image_materialize(&image, mapped);
  memset(mapped + 0x11b0, 'x', 0x50);
  assert(!pe_image_export(&image, mapped, 0, 2, &forward));
  puts("PE image validation, relocations, named/ordinal/forwarded exports: "
       "passed");
  return 0;
}
