/* Host-testable PE32+ validation and fixups. All offsets are untrusted. */
#ifndef GAIA_PE_IMAGE_H
#define GAIA_PE_IMAGE_H
#include <loader/pe.h>
#define PE_MAX_IMAGE (16u * 1024u * 1024u)
struct pe_image {
  struct IMAGE_NT_HEADERS64 nt;
  const unsigned char *file;
  usize file_size;
  u32 sections_offset;
};
int pe_image_parse(struct pe_image *image, const void *file, usize size);
int pe_image_materialize(const struct pe_image *image, void *memory);
int pe_image_relocate(const struct pe_image *image, void *memory, u64 base);
/* Returns an RVA; a forwarder points into the export directory. */
u32 pe_image_export(const struct pe_image *image, const void *memory, const char *name, u32 ordinal,
                    const char **forwarder);
int pe_image_range(u32 size, u32 rva, u64 bytes);
const char *pe_image_string(const void *memory, u32 size, u32 rva);
#endif
