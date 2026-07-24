# Okular RTL Support Research Report
**Date:** 2026-07-24
**Author:** Hermes Senior Dev Agent
**Target:** Okular (KDE PDF viewer) - graphics/okular @ KDE Invent (project #355)

---

## 1. Bug History

### Known Bugs (KDE Bugzilla)
| Bug ID | Title | Status |
|--------|-------|--------|
| [184399](https://bugs.kde.org/184399) | Copy/paste of Hebrew text is backwards | CONFIRMED (since 2009) |
| [207748](https://bugs.kde.org/207748) | Searching Hebrew text requires typing backwards | CONFIRMED (since 2009) |
| [282850](https://bugs.kde.org/282850) | Hebrew content copied in reverse letters order | DUPLICATE of 184399 |
| [282849](https://bugs.kde.org/282849) | Unable to search Hebrew words | DUPLICATE of 207748 |
| [331785](https://bugs.kde.org/331785) | Left-margin languages search | DUPLICATE of 207748 |
| [345512](https://bugs.kde.org/345512) | Hebrew text selection wrong direction | DUPLICATE of 184399 |
| [396757](https://bugs.kde.org/396757) | Texts copying generates mirrored texts | DUPLICATE of 184399 |
| [412487](https://bugs.kde.org/412487) | Copying Hebrew copies it backwards | DUPLICATE of 184399 |
| [460281](https://bugs.kde.org/460281) | RTL reading direction configuration misplaced | REPORTED |
| [465683](https://bugs.kde.org/465683) | RTL annotation typing slow/high CPU | REPORTED |

These two core bugs (184399 + 207748) have been open since **2009 (17 years)** and have accumulated 5 duplicates each.

### Related Poppler Work
- **[Freedesktop #55977](https://bugs.freedesktop.org/55977)**: Patch submitted to add ICU/fribidi-based text reordering to Poppler's TextOutputDev. Patch author: Alex. Status: never merged. The patch uses ICU's bidi algorithm to properly reorder text from visual to logical order, with fribidi as fallback.
- **Review Board [r/125442](https://git.reviewboard.kde.org/r/125442/)**: A quick patch for Okular search to handle RTL. Status: not merged.

---

## 2. Root Cause Analysis

### The Core Problem
PDFs store glyphs in **visual (display) order**. For RTL scripts like Arabic, Persian, Hebrew:
- Visual order: glyphs appear left-to-right on screen
- Logical order: characters are stored right-to-left for reading

Example: The Persian word "سلام" (s-l-a-m):
- Visual order in PDF: م ا ل س (positioned left-to-right)
- Logical order for text: س ل ا م (read right-to-left)

### Architecture Walkthrough

```
PDF File
  |
  v
Poppler Core (TextOutputDev.cc)
  |-- reorderText() [line 205] -- EXISTS but only used by pdftotext CLI, NOT Qt bindings
  |-- makeWordList() -- creates words in visual order
  v
Poppler Qt6 Binding (poppler-page.cc)
  |-- Page::textList() [line 772] -- iterates TextWords, returns QList<TextBox*>
  |   DOES NOT call reorderText() -- returns text in VISUAL ORDER
  v
Okular PDF Generator (generator_pdf.cpp)
  |-- PDFGenerator::textPage() [line 1441] -- calls Poppler::Page::textList()
  |-- abstractTextPage() [line 1835] -- iterates textBoxes char by char
  |   NO bidi reordering -- preserves Poppler's visual order
  v
Okular Core TextPage (core/textpage.cpp)
  |-- TextPage::text() [line 876] -- concatenates text for copy/paste
  |     RTL text comes out BACKWARDS
  |-- TextPage::findText() [line 549] -- searches text directly
  |     Searches VISUAL order text, user types LOGICAL order
  v
End User
  - Copy: gets backwards RTL text
  - Search: must type backwards to find text
```

### The key disconnect:
- **Poppler Core** HAS `reorderText()` for visual→logical reordering
- **Poppler Qt6 binding** does NOT use it (only used by CLI `pdftotext`)
- **Okular** receives visual-order text and stores/uses it as-is

### Poppler's reorderText() algorithm (TextOutputDev.cc:205):
1. Detects text direction with `unicodeTypeR()` and `unicodeTypeL()`
2. For primary-LTR documents: outputs LTR runs as-is, reverses RTL runs
3. For primary-RTL documents: reverses RTL runs, outputs LTR runs as-is
4. Inserts Unicode bidi control characters (LRE 0x202a, RLE 0x202b, PDF 0x202c)

---

## 3. Code Map (Key Files)

| File | Function | Role |
|------|----------|------|
| `generators/poppler/generator_pdf.cpp:1441` | `textPage()` | Entry: gets text from Poppler |
| `generators/poppler/generator_pdf.cpp:1835` | `abstractTextPage()` | Converts Poppler TextBoxes to Okular TextPage |
| `generators/poppler/generator_pdf.cpp:1801` | (export to text) | Also calls `pp->text()` which returns visual order |
| `core/textpage.cpp:876` | `TextPage::text()` | Text extraction for copy/paste |
| `core/textpage.cpp:549` | `TextPage::findText()` | Text search entry |
| `core/textpage.cpp:662` | `findTextInternalForward()` | Forward search implementation |
| `core/textpage.cpp:763` | `findTextInternalBackward()` | Backward search |
| `core/textpage.h` | `TextEntity`, `TextPage` | Core text data structures |

### Okular Build System
- Uses **CMake** + **KDE ECM** (Extra CMake Modules)
- Qt version: **Qt6** (KDE Frameworks 6)
- Poppler binding: **Poppler::Qt6** (CMakeLists.txt line 31)
- C++ standard: C++17

---

## 4. Solution Approaches

### Option A: Fix in Poppler Qt6 Binding (Upstream)
**What:** Add `reorderText()` call in Poppler's Qt6 `Page::textList()`.
**Pros:** Fixes all Qt-based PDF viewers, correct architecture
**Cons:** Requires upstream Poppler merge, long timeline, need to coordinate with Poppler maintainers

### Option B: Fix in Okular (text page creation) — RECOMMENDED
**What:** Add text reordering in `abstractTextPage()` when building the TextPage.
**Pros:** Immediate fix, full control, fixes both copy AND search
**Cons:** Duplicates some Poppler logic, needs careful handling

### Option C: Fix only Copy + Search (band-aid)
**What:** Fix `TextPage::text()` to reverse RTL runs, fix `findText()` to try reversed query.
**Pros:** Minimal changes
**Cons:** Doesn't fix root cause, fragile

### Option D: Use Qt's bidi support
Qt6 has `QString` with RTL/LTR detection and `QTextBoundaryFinder`. These could assist.

### Strategy Decision: **Option B** (Fix at text page creation level)
Rationale:
- Fixes both problems at once
- Changes are contained to one function
- Can be implemented with a standalone bidi reordering function
- Does NOT require new dependencies (Qt already provides needed utilities)
- Can be upstreamed to Poppler later if desired

---

## 5. Implementation Plan (High Level)

1. Add a `reorderBidiText()` helper function that:
   - Detects RTL/LTR character runs using `QChar::direction()`
   - Reverses RTL-only text runs
   - For mixed-direction text, applies proper bidi reordering
   - Preserves LTR text as-is

2. Modify `abstractTextPage()` in `generator_pdf.cpp`:
   - After extracting text from each TextBox, pass through `reorderBidiText()`
   - Reorder characters within each word before creating TextEntities

3. Alternative: Modify at `TextPage::append()` level to handle all generators

4. Add comprehensive tests with RTL, LTR, and mixed-direction text

---

## 6. Key Observations

1. **Poppler already has reorderText()** — we can port/adapt this logic
2. **Qt6's `QChar::direction()`** provides Unicode bidi character classification
3. **No Poppler changes needed** — fix can be entirely in Okular
4. **Export to text** (line 1801) uses `pp->text()` which ALSO returns visual order — this is a third bug location
5. **Other generators (DVI, XPS, etc.)** may have similar issues but Poppler is the primary one

---

## 7. Next Steps

1. Clone repo ✓ (done)
2. Analyze code ✓ (done)
3. Create detailed implementation plan (plan.md)
4. Implement the fix
5. Compile and test
