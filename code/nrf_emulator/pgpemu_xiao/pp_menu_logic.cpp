#include "pp_menu_logic.h"

namespace {

static constexpr PpMenuResult kNone = { PP_MENU_OUT_NONE, 0, 0 };
static constexpr PpMenuResult kRedraw = { PP_MENU_OUT_REDRAW, 0, 0 };
static constexpr PpMenuResult kClosed = { PP_MENU_OUT_CLOSED, 0, 0 };

const PpMenuPage* pageAt(const PpMenu& menu, uint8_t index)
{
  if (menu.pages == nullptr || index >= menu.pageCount) {
    return nullptr;
  }
  return &menu.pages[index];
}

const PpMenuPage* currentPage(const PpMenuState& st, const PpMenu& menu)
{
  if (!st.open || st.depth >= PP_MENU_MAX_DEPTH) {
    return nullptr;
  }
  return pageAt(menu, st.page[st.depth]);
}

bool isListPage(const PpMenuPage& page)
{
  return page.items == nullptr;
}

// Rows of a list page, not counting Back. Kept below 255 so Back + rows fits a byte.
uint8_t listRows(const PpMenu& menu, const PpMenuPage& page)
{
  if (menu.provider == nullptr || menu.provider->listCount == nullptr) {
    return 0;
  }
  uint8_t count = menu.provider->listCount(page.listId);
  return count > 254 ? 254 : count;
}

uint8_t listActive(const PpMenu& menu, const PpMenuPage& page)
{
  if (menu.provider == nullptr || menu.provider->listActive == nullptr) {
    return PP_MENU_NONE;
  }
  return menu.provider->listActive(page.listId);
}

uint8_t rowsOnPage(const PpMenu& menu, const PpMenuPage& page)
{
  if (isListPage(page)) {
    return static_cast<uint8_t>(1 + listRows(menu, page));
  }
  uint8_t count = page.itemCount > 254 ? 254 : page.itemCount;
  return static_cast<uint8_t>(1 + count);
}

// Where the cursor starts on a page: the first row after Back, or the active
// row of a list. A page with only Back starts on Back.
uint8_t startCursor(const PpMenu& menu, const PpMenuPage& page)
{
  uint8_t rows = rowsOnPage(menu, page);
  if (rows <= 1) {
    return 0;
  }
  if (isListPage(page)) {
    uint8_t active = listActive(menu, page);
    if (active < rows - 1) {
      return static_cast<uint8_t>(1 + active);
    }
  }
  return 1;
}

PpMenuResult pop(PpMenuState& st)
{
  if (st.depth == 0) {
    st.open = false;
    return kClosed;
  }
  --st.depth;
  return kRedraw;
}

PpMenuResult selectChoice(const PpMenu& menu, uint8_t id)
{
  const PpMenuProvider* p = menu.provider;
  if (p == nullptr || p->choiceCount == nullptr || p->choiceSet == nullptr) {
    return kNone;
  }
  uint8_t count = p->choiceCount(id);
  if (count == 0) {
    return kNone;
  }
  uint8_t current = p->choiceCurrent != nullptr ? p->choiceCurrent(id) : 0;
  if (current >= count) {
    current = 0;
  }
  p->choiceSet(id, static_cast<uint8_t>((current + 1) % count));
  return kRedraw;
}

PpMenuResult selectItem(PpMenuState& st, const PpMenu& menu, const PpMenuItem& item)
{
  switch (item.kind) {
    case PP_MENU_CHOICE: {
      return selectChoice(menu, item.id);
    }
    case PP_MENU_SUBMENU: {
      const PpMenuPage* target = pageAt(menu, item.page);
      if (target == nullptr || st.depth + 1 >= PP_MENU_MAX_DEPTH) {
        return kNone;
      }
      ++st.depth;
      st.page[st.depth] = item.page;
      st.cursor[st.depth] = startCursor(menu, *target);
      return kRedraw;
    }
    case PP_MENU_ACTION: {
      PpMenuResult r = { PP_MENU_OUT_ACTION, item.id, 0 };
      return r;
    }
    default: {
      return kNone;
    }
  }
}

PpMenuResult select(PpMenuState& st, const PpMenu& menu, const PpMenuPage& page)
{
  uint8_t row = st.cursor[st.depth];
  if (row == 0 || row >= rowsOnPage(menu, page)) {
    return pop(st);
  }
  uint8_t n = static_cast<uint8_t>(row - 1);
  if (isListPage(page)) {
    if (n == listActive(menu, page)) {
      return pop(st);
    }
    PpMenuResult r = { PP_MENU_OUT_ACTION, page.listId, n };
    return r;
  }
  return selectItem(st, menu, page.items[n]);
}

}  // namespace

void ppMenuOpen(PpMenuState& st, const PpMenu& menu, uint32_t now)
{
  st.open = false;
  st.depth = 0;
  for (uint8_t i = 0; i < PP_MENU_MAX_DEPTH; ++i) {
    st.page[i] = 0;
    st.cursor[i] = 0;
  }
  st.lastInputAt = now;
  const PpMenuPage* top = pageAt(menu, 0);
  if (top == nullptr) {
    return;
  }
  st.open = true;
  st.cursor[0] = startCursor(menu, *top);
}

PpMenuResult ppMenuInput(PpMenuState& st, const PpMenu& menu, PpMenuInput in, uint32_t now)
{
  const PpMenuPage* page = currentPage(st, menu);
  if (page == nullptr) {
    return kNone;
  }
  st.lastInputAt = now;
  switch (in) {
    case PP_MENU_IN_NEXT: {
      uint8_t rows = rowsOnPage(menu, *page);
      if (rows <= 1) {
        st.cursor[st.depth] = 0;
        return kNone;
      }
      st.cursor[st.depth] = static_cast<uint8_t>((st.cursor[st.depth] + 1) % rows);
      return kRedraw;
    }
    case PP_MENU_IN_SELECT: {
      return select(st, menu, *page);
    }
    case PP_MENU_IN_BACK: {
      return pop(st);
    }
    default: {
      return kNone;
    }
  }
}

PpMenuResult ppMenuSelectRow(PpMenuState& st, const PpMenu& menu, uint8_t row, uint32_t now)
{
  const PpMenuPage* page = currentPage(st, menu);
  if (page == nullptr || row >= rowsOnPage(menu, *page)) {
    return kNone;
  }
  st.cursor[st.depth] = row;
  return ppMenuInput(st, menu, PP_MENU_IN_SELECT, now);
}

PpMenuResult ppMenuTick(PpMenuState& st, uint32_t now)
{
  if (!st.open) {
    return kNone;
  }
  // Unsigned subtraction: correct across the millis() wrap.
  if (static_cast<uint32_t>(now - st.lastInputAt) >= PP_MENU_TIMEOUT_MS) {
    st.open = false;
    return kClosed;
  }
  return kNone;
}

uint8_t ppMenuRowCount(const PpMenuState& st, const PpMenu& menu)
{
  const PpMenuPage* page = currentPage(st, menu);
  if (page == nullptr) {
    return 0;
  }
  return rowsOnPage(menu, *page);
}
