#define THEMIS_DECLARE_STATE
#include "themis.h"
#include "themis_prototypes.h"

static const struct themis_alias aliases[] = {
    {"desk.cpl", "display"},
    {"mmsys.cpl", "audio"},
    {"ms-settings:display", "display"},
    {"ms-settings:sound", "audio"},
    {"ms-settings:about", "about"},
};

static const struct themis_page pages[] = {
    {"system", "System", "General", themis_page_system},
    {"display", "Display", "General", themis_page_display},
    {"appearance", "Appearance", "General", themis_page_appearance},
    {"audio", "Audio", "General", themis_page_audio},
    {"about", "About", "Information", themis_page_about},
};

const struct themis_page *themis_pages(size_t *count) {
  if (count)
    *count = sizeof(pages) / sizeof(pages[0]);

  return pages;
}

const struct themis_alias *themis_alias(size_t *count) {
  if (count)
    *count = sizeof(pages) / sizeof(pages[0]);

  return aliases;
}

const struct themis_page *themis_find_page(const char *id) {
  size_t count;
  const struct themis_page *pages = themis_pages(&count);

  for (size_t i = 0; i < count; i++) {
    if (strcmp(pages[i].id, id) == 0)
      return &pages[i];
  }

  return NULL;
}

const struct themis_page *themis_page_alias(const char *id) {
  if (!id)
    return NULL;

  size_t alias_count;
  const struct themis_alias *aliases = themis_alias(&alias_count);

  for (size_t i = 0; i < alias_count; i++) {
    if (strcmp(aliases[i].alias, id) == 0)
      return themis_find_page(aliases[i].page_id);
  }

  return NULL;
}

const struct themis_page *themis_resolve_page(const char *id) {
  if (!id)
    return NULL;

  const struct themis_page *page = themis_find_page(id);

  if (page)
    return page;

  return themis_page_alias(id);
}
