# Okular Part/ Directory — Architecture & RTL Guide

> **Target audience:** Developers working on RTL text fixes in Okular's part/ layer.
> **Last updated:** 2026-07-24

---

## 1. Overview of the part/ Directory

The `part/` directory contains Okular's GUI layer — the main KPart that embeds the document viewer. This includes:
- The **page view** (`pageview.h/cpp`) — the central document display widget
- **Search UI** — sidebar search, in-document find bar, presentation search
- **Mouse modes** — browse, zoom, text select, area select, table select, magnifier
- **RTL reading direction** — a layout-only toggle for page arrangement
- **Annotation tools**, form widgets, sidebar panels, etc.

---

## 2. Key UI Components

### 2.1 PageView (pageview.h / pageview.cpp ~5904 lines)

The main document display widget. Inherits `QAbstractScrollArea`, `Okular::DocumentObserver`, and `Okular::View`.

**PageViewPrivate** (internal state struct, cpp line 145):
```cpp
bool mouseSelecting;          // true during area/rect selection drag
bool mouseTextSelecting;      // true during text selection drag
QSet<int> pagesWithTextSelection; // page numbers with active text selection
int mouseMode;                // current MouseMode enum value
bool rtl_Mode;                // cached RTL reading direction from settings
```

**Mouse modes** (enum `Okular::Settings::EnumMouseMode`):
- `Browse` (0) — normal: pan, click links, interact with form widgets
- `Zoom` (1) — drag to zoom in; right-click zooms out
- `RectSelect` (2) — rectangle area selection for image copy
- `TextSelect` (3) — drag to select text; double-click = word, triple-click = line, quad-click = clear
- `TableSelect` (4) — select table cells for copy as CSV/HTML
- `Magnifier` (5) — loupe overlay
- `TrimSelect` (6) — select area to trim margins

These modes are set up as actions with keyboard shortcuts:
- Ctrl+1: Browse
- Ctrl+2: Zoom
- Ctrl+3: Area Selection
- Ctrl+4: Text Selection

**Key signal:** `triggerSearch(const QString &text)` — emitted when the user right-clicks selected text and chooses "Search for '...' in this document". Connected in `part.cpp` to `m_findBar->startSearch()`.

---

## 3. Search Interface

### 3.1 Three Search Widgets

| Widget | File | Use Case | Search Type | Highlight Color | Min Length |
|--------|------|----------|-------------|-----------------|------------|
| `SearchWidget` | searchwidget.h/cpp | Sidebar thumbnail filter | `GoogleAll` | Cyan (0,183,255) | 3 |
| `FindBar` | findbar.h/cpp | In-document find (Ctrl+F) | `NextMatch` | Yellow (255,255,64) | 0 |
| `PresentationSearchBar` | presentationsearchbar.h/cpp | Presentation mode find | `NextMatch` | Yellow (255,255,64) | 0 |

### 3.2 SearchLineEdit (searchlineedit.h/cpp — 325 lines)

The core search engine. Wraps `KLineEdit` and connects to `Okular::Document`.

**Key properties:**
```
SearchType type: NextMatch | PreviousMatch | AllDocument | GoogleAll | GoogleAny
int searchId:     unique ID for this search instance
QColor color:     highlight color for results
bool moveViewport: whether to scroll to matches
bool fromStart:   search from beginning or current position
bool findAsYouType: incremental search on keystrokes
int minLength:    minimum chars before search triggers
```

**Search flow:**
```
slotTextChanged(text)
  └─ if findAsYouType: restartSearch() [700ms debounce timer → startSearch()]
  └─ else: mark m_changed = true

startSearch()
  └─ if text.length() >= max(minLength, 1):
       document->searchText(m_id, text, fromStart, caseSensitivity, type, moveViewport, color)
  └─ else:
       document->resetSearch(m_id)

searchFinished(id, status)
  └─ On NoMatchFound: set warning color on line edit
  └─ On MatchFound: reset to normal palette
  └─ emit searchStopped()
```

**Next/Previous navigation** (Enter / Shift+Enter):
- `findNext()` → `document->continueSearch(m_id, NextMatch)`
- `findPrev()` → `document->continueSearch(m_id, PreviousMatch)`

### 3.3 FindBar (findbar.cpp — 201 lines)

The in-document search bar (Ctrl+F). Layout:
```
[Close] [F&ind:] [SearchLineEdit] [Next] [Previous] [Options ▼]
```

Options menu:
- **Case sensitive** — toggles `Qt::CaseSensitive`
- **From current page** — toggles `fromStart` parameter
- **Find as you type** — enables incremental search

### 3.4 SearchWidget (sidebar thumbnail filter)

Located in the sidebar above thumbnails. Uses `GoogleAll` search type (all words, google-style). Search mode options:
- **Match Phrase** → `AllDocument`
- **Match All Words** → `GoogleAll` (default)
- **Match Any Word** → `GoogleAny`

### 3.5 Connection: Context Menu → FindBar

When the user selects text and right-clicks:
1. `mouseReleaseEvent` (TextSelect/mouseRightButton) creates a context menu
2. Menu has "Search for '%1' in this document" action
3. Action triggers `addSearchWithinDocumentAction()` → emits `triggerSearch(text)`
4. `Part::slotShowFindBar()` → `m_findBar->startSearch(text)` → shows find bar

---

## 4. Text Selection and Copy

### 4.1 Data Flow

```
User drags mouse in TextSelect mode
        │
        ▼
mousePressEvent  → records mouseSelectPos
mouseMoveEvent   → if distance > 5px, sets mouseTextSelecting = true
                 → updateSelection(pos)
                      │
                      ▼
                 textSelections(start, end, firstpage)
                      │ (divides drag rect across visible page items)
                      │ calls textSelectionForItem(item, start, end) per page
                      │     │
                      │     ▼
                      │  page->textArea(TextSelection{start, end})
                      │  → returns RegularAreaRect*
                      │
                      ▼
                 document->setPageTextSelection(pageNum, area, color)
                 pagesWithTextSelection.insert(pageNum)
        │
mouseReleaseEvent → mouseTextSelecting = false
                 → copy to X11 Selection clipboard
                 → or right-click: context menu with Copy Text / Copy w/o line breaks / Search
```

### 4.2 Double/Triple/Quad Click

In TextSelect mode (`mouseDoubleClickEvent` → `mousePressEvent`):

| Click | Action | Method |
|-------|--------|--------|
| Double-click | Select word | `page->wordAt(normalizedPoint)` |
| Triple-click | Select entire line | `page->lineAt(normalizedPoint)` |
| Quad-click | Clear selection | `textSelectionClear()` |

### 4.3 copyTextSelection() (line 1110)

Two copy modes:
- `TextCopyMode::AsProvided` — copies text as-is from `selectedText()`
- `TextCopyMode::WithoutLineBreaks` — passes through `Okular::removeLineBreaks()`

Handles different mouse modes:
- **Browse**: copies focused annotation text
- **TableSelect**: copies as `QMimeData` with HTML table and plain text
- **TextSelect**: copies `selectedText()` from multiple pages

### 4.4 selectedText() (line 1005)

```cpp
QString PageViewPrivate::selectedText() const {
    // Gets sorted page numbers from pagesWithTextSelection
    // For each page:
    //   - first page:   pg->text(pg->textSelection(), CentralPixelTextAreaInclusionBehaviour)
    //   - middle pages: pg->text(nullptr, ...)  // selects ALL text on page
    //   - last page:    pg->text(pg->textSelection(), ...)
    // Trims trailing '\n'
}
```

### 4.5 textSelectionForItem() (line 3954)

Maps pixel coordinates to normalized page coordinates accounting for rotation:
- Creates `Okular::TextSelection` from start/end points
- Ensures page has a text page (requests one if needed)
- Calls `okularPage->textArea(mouseTextSelectionInfo)`
- Coordinates are rotated via `rotateInNormRect()` to handle page rotation (0°, 90°, 180°, 270°)

---

## 5. RTL Reading Direction

### 5.1 What It Does

`rtlReadingDirection` is a **page layout** setting — it ONLY affects how pages are arranged on screen, NOT text content or text direction.

**Setting location:** `Okular::Settings::rtlReadingDirection()` — a KConfig-backed property. Checkbox in `dlggeneral.cpp` labeled "Right to left reading direction".

### 5.2 How It's Used

**1. Initialization** (pageview.cpp:351):
```cpp
d->rtl_Mode = Okular::Settings::rtlReadingDirection();
```

**2. Config change detection** (pageview.cpp:952):
```cpp
if (Okular::Settings::rtlReadingDirection() != d->rtl_Mode) {
    d->rtl_Mode = Okular::Settings::rtlReadingDirection();
    slotRelayoutPages();  // triggers full page re-layout
}
```

**3. Layout calculation** (pageview.cpp:4834-4848) — the actual effect:
```cpp
// Facing pages mode:
if (Okular::Settings::rtlReadingDirection()) {
    // RTL: right page first, then left
    actualX = ((centerFirstPage && pageNum % 2 == 0) || ...)
              ? (fullWidth/2) - croppedWidth - 1   // right side
              : (fullWidth/2) + 1;                  // left side
} else {
    // LTR: left page first, then right
    actualX = ((centerFirstPage && pageNum % 2 == 1) || ...)
              ? (fullWidth/2) - croppedWidth - 1   // left side
              : (fullWidth/2) + 1;                  // right side
}

// Single/continuous pages mode:
if (Okular::Settings::rtlReadingDirection()) {
    actualX = fullWidth - insertX - colWidth + ((colWidth - croppedWidth) / 2);
} else {
    actualX = insertX + (colWidth - croppedWidth) / 2;
}
```

### 5.3 What It Does NOT Affect

- **Text content**: No effect on text extraction, copying, or display
- **Text selection**: Selection coordinates are unaffected
- **Search**: Search results are unaffected
- **Text direction in PDFs**: PDF content direction is determined by the PDF file itself

### 5.4 Toggle Action

(pageview.cpp:643-648):
```cpp
d->aReadingDirection = new KToggleAction(
    QIcon::fromTheme("format-text-direction-rtl"),
    i18nc("@action page layout", "Use Right to Left Reading Direction"),
    this
);
// Connected to:
//   slotReadingDirectionToggled(bool) → Okular::Settings::setRtlReadingDirection()
//   slotUpdateReadingDirectionAction() → updates check state on config change
```

---

## 6. Page Layout (slotRelayoutPages)

The page layout loop (line 4824) iterates over all `PageViewItem`s and positions them. The layout engine handles:
- **Column/row grid**: Pages placed in a grid determined by `viewColumns()` and continuous mode
- **Facing pages**: Two pages side-by-side with mirroring
- **Center first/last**: First and last pages can be centered across the full viewport
- **RTL mode**: Flips the horizontal arrangement (right-to-left ordering)
- **Crop/trim**: Pages can be trimmed to bounding boxes

---

## 7. Context Menu Flow

### RectSelect mode (right-click after area selection):
1. Extracts text from pages intersecting selection rect
2. Shows popup: "Text (N characters)" + copy/image options
3. "Search for '...' in this document" → emitted `triggerSearch()`
4. "Search for '...' with <web shortcut>" → web shortcuts submenu

### TextSelect mode (right-click on existing text selection):
1. Checks `item->page()->textSelection()` for existing selection
2. Shows popup: Copy Text / Copy w/o line breaks / Search / Go to URL
3. If pasting over a link: also shows "Follow this link" action

---

## 8. Key Files Summary

| File | Purpose | Lines |
|------|---------|-------|
| `pageview.cpp` | Main document view: layout, mouse, selection, RTL | 5904 |
| `pageview.h` | PageView class declaration | 350 |
| `pageviewutils.h/cpp` | PageViewItem + PageViewMessage | ~200 |
| `searchlineedit.h/cpp` | Core search engine (KLineEdit wrapper) | 325 |
| `findbar.h/cpp` | In-document find bar (Ctrl+F) | 201 |
| `searchwidget.h/cpp` | Sidebar thumbnail filter widget | 101 |
| `presentationsearchbar.h/cpp` | Presentation mode search bar | 140 |
| `part.h/cpp` | Main KPart: wires everything together | 3993 |
| `dlggeneral.cpp` | General settings dialog (RTL checkbox) | 252 |

---

## 9. RTL Text Fix: What to Know

If you're working on RTL text selection/copy fixes:

1. **The `rtlReadingDirection` setting is irrelevant to your task.** It's purely for page layout (which side of the screen pages appear on). Do NOT use it for text direction.

2. **Text selection is driven by `TextPage`** (in `core/`): `page->textArea()`, `page->wordAt()`, `page->lineAt()` return `RegularAreaRect` objects that define geometric selection regions.

3. **Text extraction** (`page->text()`) takes a `RegularAreaRect` and returns the string. The text direction within the page is embedded in the PDF content itself.

4. **The RTL relevant code in part/** is:
   - `textSelections()` (line 3525): Divides drag rectangles across visible pages — handles multi-page selection ordering
   - `textSelectionForItem()` (line 3954): Converts pixel coords to normalized page coords (handles rotation)
   - `selectedText()` (line 1005): Aggregates text across pages
   - `copyTextSelection()` (line 1110): Copy with/without line breaks

5. **For Bidi/RTL text fixes, look at:**
   - `core/textpage.cpp` — text layout and area generation
   - `core/page.cpp` — `text()` method for extraction
   - `Okular::TextSelection` and `Okular::RegularAreaRect` types
