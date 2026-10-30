/* PE graph mapping stays in the loader. API translation runs in real user DLLs.
 */
#include <arch/win32_abi.h>
#include <fs/stdio.h>
#include <loader/pe_image.h>
#include <loader/process.h>
#include <memory/heap.h>
#include <memory/hhdm.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <sched/sched.h>
#include <utilities/log.h>
#include <utilities/string.h>

struct pe_module {
  struct pe_image image;
  u8 *memory;
  u64 base;
  char name[64];
  u32 dependencies;
  int mapped, visit, linked;
};
struct pe_process {
  u64 *pml4;
  struct pe_module modules[GAIA_WIN32_MAX_MODULES];
  struct gaia_win32_boot boot;
  u32 count;
  char directory[128];
  int busy;
};
static int copy_name(char *out, usize cap, const char *s) {
  if (!s)
    return -1;
  usize n = 0;
  while (s[n]) {
    char c = s[n];
    if (n + 1 >= cap || c == '/' || c == '\\' || c == ':' || c == ' ' ||
        !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
          c == '_' || c == '-'))
      return -1;
    out[n] = c >= 'A' && c <= 'Z' ? c + 32 : c;
    n++;
  }
  out[n] = 0;
  if (!n || !strcmp(out, ".") || !strcmp(out, ".."))
    return -1;
  out[n] = 0;
  return 0;
}
static int user_copy(u64 *pml4, u64 va, const void *data, usize n) {
  const u8 *p = data;
  while (n) {
    u64 phys = vmm_translate_in(pml4, va);
    if (!phys)
      return -1;
    usize k = 4096 - (va & 4095);
    if (k > n)
      k = n;
    memcpy(phys_to_virt(phys), p, k);
    va += k;
    p += k;
    n -= k;
  }
  return 0;
}
static int map_bytes(u64 *pml4, u64 va, const void *data, usize n, u64 flags) {
  for (usize i = 0; i < n; i += 4096) {
    if (vmm_translate_in(pml4, va + i))
      return -1;
    u64 phys = pmm_alloc_frame();
    if (!phys)
      return -1;
    memset(phys_to_virt(phys), 0, 4096);
    usize k = n - i;
    if (k > 4096)
      k = 4096;
    if (data)
      memcpy(phys_to_virt(phys), (const u8 *)data + i, k);
    if (vmm_map_in(pml4, va + i, phys, flags)) {
      pmm_free_frame(phys);
      return -1;
    }
    if ((i & 0x1ffff) == 0x1f000)
      task_yield();
  }
  return 0;
}
static void unmap_module(struct pe_process *p, struct pe_module *m) {
  u32 size = m->image.nt.optionalHeader.sizeOfImage;
  for (u64 i = 0; i < size; i += 4096) {
    u64 phys = vmm_translate_in(p->pml4, m->base + i);
    if (phys) {
      vmm_unmap_in(p->pml4, m->base + i);
      pmm_free_frame(phys & ~4095ULL);
    }
  }
}
static int range_free(struct pe_process *p, u64 base, u32 size) {
  if ((base & 65535) || base < 0x40000000ULL || size > USER_VA_MAX || base > USER_VA_MAX - size ||
      (base < GAIA_WIN32_TEB + 0x10000 && base + size > GAIA_WIN32_TEB))
    return 0;
  for (u32 i = 0; i < p->count; i++) {
    struct pe_module *m = &p->modules[i];
    if (base < m->base + m->image.nt.optionalHeader.sizeOfImage && m->base < base + size)
      return 0;
  }
  for (u64 i = 0; i < size; i += 4096)
    if (vmm_translate_in(p->pml4, base + i))
      return 0;
  return 1;
}
static int read_image(const char *path, u8 **out, usize *size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -2;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return -1;
  }
  long length = ftell(f);
  if (length <= 0 || length > PE_MAX_IMAGE) {
    fclose(f);
    return -1;
  }
  fseek(f, 0, SEEK_SET);
  u8 *data = kmalloc(length);
  if (!data) {
    fclose(f);
    return -1;
  }
  int ok = fread(data, 1, length, f) == (usize)length;
  fclose(f);
  if (!ok) {
    kfree(data);
    return -1;
  }
  *out  = data;
  *size = length;
  return 0;
}
static int discover(struct pe_process *, const char *, const char *);
static int module_named(struct pe_process *p, const char *name) {
  char lower[64];
  if (copy_name(lower, sizeof(lower), name))
    return -1;
  for (u32 i = 0; i < p->count; i++)
    if (!strcmp(p->modules[i].name, lower))
      return i;
  char path[256];
  usize d = strlen(p->directory), n = strlen(lower);
  if (d + n + 1 >= sizeof(path))
    return -1;
  memcpy(path, p->directory, d);
  memcpy(path + d, lower, n + 1);
  int result = discover(p, path, lower);
  if (result != -2)
    return result;
  const char *prefix = "/system/win32/";
  d                  = strlen(prefix);
  memcpy(path, prefix, d);
  memcpy(path + d, lower, n + 1);
  return discover(p, path, lower);
}
static int discover(struct pe_process *p, const char *path, const char *name) {
  if (p->count == GAIA_WIN32_MAX_MODULES)
    return -1;
  u8 *file     = 0;
  usize length = 0;
  struct pe_image image;
  int read_result = read_image(path, &file, &length);
  if (read_result)
    return read_result;
  if (pe_image_parse(&image, file, length)) {
    kfree(file);
    return -1;
  }
  u32 size = image.nt.optionalHeader.sizeOfImage;
  u64 base = image.nt.optionalHeader.imageBase;
  if (!range_free(p, base, size)) {
    if (!image.nt.optionalHeader.dataDirectory[5].size) {
      kfree(file);
      return -1;
    }
    base = 0x200000000ULL;
    while (!range_free(p, base, size)) {
      base += 0x1000000;
      if (base >= 0x300000000ULL) {
        kfree(file);
        return -1;
      }
    }
  }
  u8 *memory = kmalloc(size);
  if (!memory) {
    kfree(file);
    return -1;
  }
  pe_image_materialize(&image, memory);
  if (pe_image_relocate(&image, memory, base)) {
    kfree(file);
    kfree(memory);
    return -1;
  }
  int index           = p->count++;
  struct pe_module *m = &p->modules[index];
  memset(m, 0, sizeof(*m));
  m->image  = image;
  m->memory = memory;
  m->base   = base;
  if (copy_name(m->name, sizeof(m->name), name)) {
    kfree(file);
    return -1;
  }
  m->image.file = memory; /* Header/section table offsets are identical after mapping. */
  kfree(file);
  struct IMAGE_DATA_DIRECTORY d = image.nt.optionalHeader.dataDirectory[1];
  if (!d.size)
    return index;
  int terminated = 0;
  for (u32 at = 0; at + 20 <= d.size; at += 20) {
    u32 imp[5];
    memcpy(imp, memory + d.virtualAddress + at, 20);
    if (!(imp[0] | imp[1] | imp[2] | imp[3] | imp[4])) {
      terminated = 1;
      break;
    }
    const char *dll = pe_image_string(memory, size, imp[3]);
    int dep         = dll ? module_named(p, dll) : -1;
    if (dep < 0) {
      log_write_string("pe: missing dependency", dll ? dll : "invalid name", KERNEL, LOG_ERROR);
      return -1;
    }
    m->dependencies |= 1u << dep;
  }
  return terminated ? index : -1;
}
static u64 resolve(struct pe_process *p, int index, const char *name, u32 ordinal, int depth) {
  if (index < 0 || (u32)index >= p->count || depth > 32)
    return 0;
  struct pe_module *m = &p->modules[index];
  const char *forward = 0;
  u32 rva             = pe_image_export(&m->image, m->memory, name, ordinal, &forward);
  if (!rva)
    return 0;
  if (!forward)
    return m->base + rva;
  char dll[64];
  usize len       = 0;
  const char *dot = 0;
  for (const char *s = forward; *s; s++)
    if (*s == '.')
      dot = s;
  if (!dot || dot == forward || (usize)(dot - forward) + 5 >= sizeof(dll))
    return 0;
  len = dot - forward;
  memcpy(dll, forward, len);
  dll[len] = 0;
  if (len < 4 || strcmp(dll + len - 4, ".dll")) {
    memcpy(dll + len, ".dll", 5);
  }
  int dep = module_named(p, dll);
  if (dep < 0)
    return 0;
  m->dependencies |= 1u << dep;
  if (dot[1] == '#') {
    u32 n         = 0;
    const char *s = dot + 2;
    if (!*s)
      return 0;
    for (; *s; s++) {
      if (*s < '0' || *s > '9' || n > 6553)
        return 0;
      n = n * 10 + *s - '0';
    }
    return resolve(p, dep, 0, n, depth + 1);
  }
  return resolve(p, dep, dot + 1, 0, depth + 1);
}
static int link_module(struct pe_process *p, u32 index) {
  struct pe_module *m = &p->modules[index];
  if (m->linked)
    return 0;
  u32 size                      = m->image.nt.optionalHeader.sizeOfImage;
  struct IMAGE_DATA_DIRECTORY d = m->image.nt.optionalHeader.dataDirectory[1];
  for (u32 at = 0; at + 20 <= d.size; at += 20) {
    u32 imp[5];
    memcpy(imp, m->memory + d.virtualAddress + at, 20);
    if (!(imp[0] | imp[1] | imp[2] | imp[3] | imp[4]))
      break;
    const char *dll = pe_image_string(m->memory, size, imp[3]);
    int dep         = dll ? module_named(p, dll) : -1;
    if (dep < 0 || !imp[4])
      return -1;
    u32 lookup     = imp[0] ? imp[0] : imp[4];
    int terminated = 0;
    for (u64 slot = 0; slot < size / 8; slot++) {
      u64 from = (u64)lookup + slot * 8, to = (u64)imp[4] + slot * 8;
      if (from > size || to > size || !pe_image_range(size, from, 8) ||
          !pe_image_range(size, to, 8))
        return -1;
      u64 value;
      memcpy(&value, m->memory + from, 8);
      if (!value) {
        terminated = 1;
        break;
      }
      const char *name = 0;
      u32 ordinal      = 0;
      if (value >> 63)
        ordinal = (u16)value;
      else {
        if (value > size - 2)
          return -1;
        name = pe_image_string(m->memory, size, (u32)value + 2);
        if (!name)
          return -1;
      }
      u64 address = resolve(p, dep, name, ordinal, 0);
      if (!address) {
        log_write_string("pe: unresolved import", name ? name : "ordinal", KERNEL, LOG_ERROR);
        return -1;
      }
      memcpy(m->memory + to, &address, 8);
    }
    if (!terminated)
      return -1;
  }
  m->linked = 1;
  return 0;
}
static int map_module(struct pe_process *p, struct pe_module *m) {
  u32 size = m->image.nt.optionalHeader.sizeOfImage;
  for (u64 i = 0; i < size; i += 4096) {
    u64 flags = VMM_PRESENT | VMM_USER | VMM_NX;
    for (u32 j = 0; j < m->image.nt.fileHeader.numberOfSections; j++) {
      struct IMAGE_SECTION_HEADER sh;
      memcpy(&sh, m->memory + m->image.sections_offset + j * 40, 40);
      u32 n = sh.virtualSize > sh.sizeOfRawData ? sh.virtualSize : sh.sizeOfRawData;
      if (i >= sh.virtualAddress && i < ((u64)sh.virtualAddress + n + 4095) / 4096 * 4096) {
        if (sh.characteristics & 0x80000000)
          flags |= VMM_WRITE;
        if (sh.characteristics & 0x20000000)
          flags &= ~VMM_NX;
      }
    }
    usize n = size - i;
    if (n > 4096)
      n = 4096;
    if (map_bytes(p->pml4, m->base + i, m->memory + i, n, flags))
      return -1;
  }
  m->mapped = 1;
  return 0;
}
static void init_order(struct pe_process *p, u32 index) {
  struct pe_module *m = &p->modules[index];
  if (m->visit)
    return;
  m->visit = 1;
  for (u32 i = 0; i < p->count; i++)
    if (m->dependencies & (1u << i))
      init_order(p, i);
  if (index && m->image.nt.optionalHeader.addressOfEntryPoint)
    p->boot.init_order[p->boot.init_count++] = index;
}
static int finalize(struct pe_process *p) {
  for (u32 i = 0; i < p->count; i++)
    if (link_module(p, i))
      return -1;
  for (u32 i = 0; i < p->count; i++)
    if (!p->modules[i].mapped && map_module(p, &p->modules[i]))
      return -1;
  p->boot.module_count = p->count;
  for (u32 i = 0; i < p->count; i++) {
    struct pe_module *m      = &p->modules[i];
    p->boot.modules[i].base  = m->base;
    p->boot.modules[i].entry = m->image.nt.optionalHeader.addressOfEntryPoint
                                   ? m->base + m->image.nt.optionalHeader.addressOfEntryPoint
                                   : 0;
    memcpy(p->boot.modules[i].name, m->name, 64);
  }
  for (u32 i = 0; i < p->count; i++)
    init_order(p, i);
  return 0;
}
void pe_process_free(struct pe_process *p) {
  if (!p)
    return;
  for (u32 i = 0; i < p->count; i++)
    kfree(p->modules[i].memory);
  kfree(p);
}
u64 pe_load_process(const char *path, u64 *pml4, struct pe_process **out) {
  *out                 = 0;
  struct pe_process *p = kmalloc(sizeof(*p));
  if (!p)
    return 0;
  memset(p, 0, sizeof(*p));
  p->pml4          = pml4;
  const char *base = path;
  for (const char *s = path; *s; s++)
    if (*s == '/')
      base = s + 1;
  usize d = base - path;
  if (d >= sizeof(p->directory))
    goto fail;
  memcpy(p->directory, path, d);
  if (discover(p, path, base) != 0)
    goto fail;
  struct IMAGE_DATA_DIRECTORY imports = p->modules[0].image.nt.optionalHeader.dataDirectory[1];
  u32 descriptor[5]                   = {0};
  if (imports.size >= sizeof(descriptor))
    memcpy(descriptor, p->modules[0].memory + imports.virtualAddress, sizeof(descriptor));
  /* MinGW emits an empty import directory even for self-contained Gaia PE. */
  int windows =
      (descriptor[0] | descriptor[1] | descriptor[2] | descriptor[3] | descriptor[4]) != 0;
  int nt = windows ? module_named(p, "ntdll.dll") : -1;
  if (windows && nt < 0)
    goto fail;
  u64 entry = p->modules[0].base + p->modules[0].image.nt.optionalHeader.addressOfEntryPoint;
  if (!p->modules[0].image.nt.optionalHeader.addressOfEntryPoint)
    goto fail;
  u64 startup = windows ? resolve(p, nt, "GaiaProcessStart", 0, 0) : entry;
  if (!startup || finalize(p))
    goto fail;
  if (!windows) {
    pe_process_free(p);
    return entry;
  }
  p->boot.image_entry = entry;
  u8 *teb = kmalloc(8192), *peb = kmalloc(4096);
  if (!teb || !peb) {
    kfree(teb);
    kfree(peb);
    goto fail;
  }
  memset(teb, 0, 8192);
  memset(peb, 0, 4096);
  u64 v = USER_STACK_TOP;
  memcpy(teb + 8, &v, 8);
  v = USER_STACK_LOW;
  memcpy(teb + 16, &v, 8);
  v = GAIA_WIN32_TEB;
  memcpy(teb + 0x30, &v, 8);
  v = GAIA_WIN32_PEB;
  memcpy(teb + 0x60, &v, 8);
  v = p->modules[0].base;
  memcpy(peb + 0x10, &v, 8);
  v = GAIA_WIN32_BOOT;
  memcpy(peb + 0x100, &v, 8);
  if (map_bytes(pml4, GAIA_WIN32_TEB, teb, 8192, VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NX) ||
      map_bytes(pml4, GAIA_WIN32_PEB, peb, 4096, VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NX) ||
      map_bytes(pml4,
                GAIA_WIN32_BOOT,
                &p->boot,
                sizeof(p->boot),
                VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NX)) {
    kfree(teb);
    kfree(peb);
    goto fail;
  }
  kfree(teb);
  kfree(peb);
  *out = p;
  return startup;
fail:
  log_write_string("pe: image/dependency validation failed", path, KERNEL, LOG_ERROR);
  pe_process_free(p);
  return 0;
}
u64 pe_load(const char *path, u64 *pml4) {
  struct pe_process *p = 0;
  u64 entry            = pe_load_process(path, pml4, &p);
  /* Legacy callers cannot install a Windows personality. */
  if (p) {
    pe_process_free(p);
    return 0;
  }
  return entry;
}
int pe_process_command_line(struct pe_process *p, const char *command) {
  if (!p)
    return 0;
  usize n = strlen(command);
  if (n >= sizeof(p->boot.command_line))
    return -1;
  memcpy(p->boot.command_line, command, n + 1);
  return user_copy(p->pml4, GAIA_WIN32_BOOT, &p->boot, sizeof(p->boot));
}
u64 pe_process_module(struct pe_process *p, const char *name, int load) {
  if (!p)
    return 0;
  if (!name)
    return p->modules[0].base;
  char lower[64];
  if (copy_name(lower, sizeof(lower), name))
    return 0;
  for (u32 i = 0; i < p->count; i++)
    if (!strcmp(lower, p->modules[i].name))
      return p->modules[i].base;
  if (!load || p->busy)
    return 0;
  p->busy = 1;
  u32 old = p->count, old_init = p->boot.init_count;
  int index = module_named(p, lower);
  if (index < 0 || finalize(p) || user_copy(p->pml4, GAIA_WIN32_BOOT, &p->boot, sizeof(p->boot))) {
    for (u32 i = old; i < p->count; i++) {
      unmap_module(p, &p->modules[i]);
      kfree(p->modules[i].memory);
      memset(&p->modules[i], 0, sizeof(p->modules[i]));
    }
    for (u32 i = 0; i < old; i++)
      p->modules[i].dependencies &= old == 32 ? ~0u : ((1u << old) - 1);
    p->count             = old;
    p->boot.module_count = old;
    p->boot.init_count   = old_init;
    p->busy              = 0;
    return 0;
  }
  p->busy = 0;
  return p->modules[index].base;
}
u64 pe_process_symbol(struct pe_process *p, u64 module, const char *name, u32 ordinal) {
  if (!p || p->busy)
    return 0;
  for (u32 i = 0; i < p->count; i++)
    if (p->modules[i].base == module) {
      /* Forwarders must target the already-linked graph during GetProcAddress.
       */
      u32 old    = p->count;
      u64 result = resolve(p, i, name, ordinal, 0);
      if (p->count != old) {
        for (u32 j = old; j < p->count; j++)
          kfree(p->modules[j].memory);
        p->count = old;
        return 0;
      }
      return result;
    }
  return 0;
}
