# AGENTS.md — Okular Core Directory: Text Handling, Search, and Extraction

This document explains the key files, classes, and code paths in `/core/` related to text representation, layout ordering, search, and copy/paste text extraction in the Okular PDF viewer codebase. It is written for another developer or agent working on RTL text fixes.

---

## Directory Overview

The `core/` directory contains the Okular document model — the format-agnostic representation of pages, text, links, annotations, and the abstract generator interface. Only text-related files are detailed below.

## Key Files and Their Roles

### 1. `textpage.h` / `textpage.cpp` / `textpage_p.h`

**This is the most important file for text handling.** It contains:

- **`TextEntity`** — The fundamental text atom. Stores:
  - `QString m_text` — the string content (may be a character, word, or entire line depending on generator)
  - `NormalizedRect m_area` — the bounding box in normalized [0,1] coordinates

- **`TextPage`** — Holds all text entities for one page. Key public API:
  - `append(text, area)` — adds a TextEntity (used by generators)
  - `findText(searchID, query, direction, caseSensitivity, area)` — searches for text
  - `text(area, behaviour)` — extracts text from a region (copy/paste)
  - `words(area, behaviour)` — returns TextEntity list for a region
  - `wordAt(point)` — finds the word at a normalized point
  - `lineAt(point)` — finds the line at a normalized point
  - `textArea(selection)` — returns the geometry of a text selection
  - **`TextAreaInclusionBehaviour`** enum:
    - `AnyPixelTextAreaInclusionBehaviour` — include entity if *any* pixel of its bbox is in area
    - `CentralPixelTextAreaInclusionBehaviour` — include only if *center* pixel is in area

- **`TextPagePrivate`** (in `textpage_p.h`) — Internal implementation:
  - `TextEntity::List m_words` — **the canonical ordered text list**. After `correctTextOrder()`, this contains reordered text entities (characters) with spaces inserted.
  - `QMap<int, SearchPoint*> m_searchPoints` — per-searchID state for "find next/previous"
  - `Page *m_page` — back-pointer to owning Page
  - `TextComparisonFunction` typedef — function pointer `bool (*)(QStringView, QStringView)` for case-sensitive/insensitive matching

### 2. `generator.h` / `generator.cpp` / `generator_p.h`

**Abstract base class for all document format backends** (PDF, DjVu, etc.).

- **`Generator`** — Abstract base. Key text-related parts:
  - `GeneratorFeature::TextExtraction` — flag indicating text support
  - `virtual TextPage *textPage(TextRequest *request)` — **the method generators override to produce a TextPage**. Runs in its own thread when `Threaded` feature is set.
  - `generateTextPage(Page *page)` — public wrapper that calls `textPage()` and `page->setTextPage()`
  - `signalTextGenerationDone(Page*, TextPage*)` — notifies Document that text generation finished

- **`TextRequest`** — Simple request object wrapping a `Page*` and an abort flag.

- **`TextPageGenerationThread`** — QThread subclass that runs `textPage()` in a background thread. Used when generator supports Threaded feature.

### 3. `document.h` / `document.cpp` / `document_p.h`

**The central orchestrator.** Manages the document lifecycle, pages, search, and observer notifications.

- **`Document`** — Key text-related API:
  - `supportsSearching()` — checks if generator has `TextExtraction` feature
  - `searchText(searchID, text, fromStart, caseSensitivity, type, moveViewport, color)` — **entry point for all search**
  - `continueSearch(searchID)` / `resetSearch(searchID)` — continue/reset an existing search
  - `requestTextPage(pageNumber)` — triggers text page generation for a page
  - `setPageTextSelection(page, rect, color)` — sets text selection highlight

- **`DocumentPrivate`** (in `document_p.h`) — Internal state:
  - `QMap<int, RunningSearch*> m_searches` — per-searchID state
  - `QList<Page*> m_pagesVector` — all pages
  - `Generator *m_generator` — the active backend
  - `m_searchCancelled` — flag to abort search
  - Helper methods: `doContinueDirectionMatchSearch()`, `doContinueAllDocumentSearch()`, `doProcessSearchMatch()`

### 4. `page.h` / `page.cpp` / `page_p.h`

**Represents a single document page.** Delegates text operations to `TextPage`.

- **`Page`** — Text-related API:
  - `findText(id, text, direction, caseSensitivity, lastRect)` → delegates to `TextPage::findText()`
  - `text(area, behaviour)` → delegates to `TextPage::text()`
  - `words(area, behaviour)` → delegates to `TextPage::words()`
  - `wordAt(point)` / `lineAt(point)` / `textArea(selection)` → delegates to TextPage
  - `setTextPage(TextPage*)` — stores/wraps the text page (called by Generator)

- **`PagePrivate`** (in `page_p.h`) — Internal state:
  - `TextPage *m_text` — **the text page** (null if not generated yet)
  - `m_rotation`, `m_orientation` — page rotation state
  - `rotationMatrix()` — returns QTransform for rotation
  - `setTextSelections(rect, color)` — highlights text selection on page
  - `m_boundingBox` — the bounding box of page content

### 5. `area.h` / `area.cpp`

**Geometry primitives.** Everything is in normalized [0,1] coordinates where (0,0) is top-left and (1,1) is bottom-right of the page.

- **`NormalizedPoint`** — (x, y) pair. Used for cursor positions in selections.
- **`NormalizedRect`** — (left, top, right, bottom). Used for bounding boxes of TextEntity.
  - `contains(x, y)`, `intersects(rect)`, `center()`, `transform(matrix)`
  - `width()`, `height()`, `isNull()`
  - `isTop()`, `isBottom()`, `isLeft()`, `isRight()` — spatial relationship to a point
  - `geometry(xScale, yScale)` — convert to absolute QRect
- **`RegularAreaRect`** = `RegularArea<NormalizedRect, QRect>` — a list of NormalizedRect objects forming a multi-rectangle area.
  - `appendShape(rect, mergeSide)` — adds a rect, optionally merging with existing rects
  - `simplify()` — merges intersecting sub-rects
  - `intersects(rect)` — checks if any sub-rect intersects
  - `transform(matrix)` — transforms all sub-rects
- **`HighlightAreaRect`** — RegularAreaRect with color and search ID. Used for search result highlighting.
- **`ObjectRect`** — area with a pointer to a document object (Action, Image, Annotation).
- **`MergeSide` enum** (in `global.h`) — MergeRight(0), MergeBottom(1), MergeLeft(2), MergeTop(3), MergeAll(4). Controls how shapes are merged in `appendShape()`. The merge side used for text selections depends on page `totalOrientation()`.

### 6. `misc.h`

- **`TextSelection`** — Simple class holding `start` and `end` NormalizedPoints for text selection.
- **`removeLineBreaks(text)`** utility function.

### 7. `global.h`

- **`SearchDirection` enum** — FromTop, FromBottom, NextResult, PreviousResult
- **`Rotation` enum** — Rotation0, Rotation90, Rotation180, Rotation270
- **`MergeSide` enum** — controls direction of area merging (used in textArea)

---

## Text Handling Data Flow

### Phase 1: Text Generation (Generator → TextPage)

```
Generator::generateTextPage(Page *page)
  ├─ TextRequest treq(page);
  ├─ TextPage *tp = textPage(&treq);     // virtual — backend implements
  └─ page->setTextPage(tp);              // stores tp into PagePrivate::m_text
    
Generator::generatePixmap(PixmapRequest*)  // asynchronous path
  ├─ if TextExtraction && !page->hasTextPage():
  │    textPageGenerationThread->setPage(page);
  │    // thread runs Generator::textPage() in background
  └─ On thread finish → GeneratorPrivate::textpageGenerationFinished()
       ├─ page->setTextPage(tp)  // stores in PagePrivate::m_text
       └─ signalTextGenerationDone(page, tp)
```

**Each backend generator** (e.g. PDF) overrides `textPage()` to:
1. Extract character/glyph positions and strings from the document format
2. Create `TextEntity` objects: `TextEntity(QString text, NormalizedRect area)`
3. Append them to a `TextPage` via `TextPage::append()`
4. Return the TextPage

The `append()` method has special combining logic: when a new character combines with the previous one (e.g. 'A' + combining ring above = 'Å'), as detected by NFC normalization, the two TextEntity objects are merged into one with a union area.

### Phase 2: Text Reordering (TextPagePrivate::correctTextOrder)

**This is where the raw generator output gets ordered for proper reading.** The algorithm is critical for RTL text correctness.

```
TextPagePrivate::correctTextOrder()
  ├─ Scale page dimensions to sum=2000 for DPI-independent processing
  ├─ characters = m_words (copy)
  ├─ removeSpace(characters)                         // strip " " entities
  ├─ wordsWithCharacters = makeWordFromCharacters(characters, pageW, pageH)
  │    ├─ Groups characters into words by:
  │    │   ├─ Y-overlap (same line) with 60% threshold
  │    │   └─ Zero horizontal gap between consecutive chars (space=0)
  │    └─ Each word gets a union bounding rect
  ├─ tree = XYCutForBoundingBoxes(wordsWithCharacters, pageW, pageH)
  │    ├─ Recursive X-Y cut page segmentation
  │    ├─ Statistical analysis of word/line/column spacing
  │    ├─ Finds widest gaps in projection profiles
  │    └─ Cuts into regions (horizontal/vertical)
  ├─ listOfCharacters = addNecessarySpace(tree, pageW, pageH)
  │    ├─ makeAndSortLines() — per region: sort by Y, group into lines by overlap, sort lines by X
  │    ├─ Inserts space " " TextEntity between words with gaps
  │    └─ Returns flat list of characters (including space entities)
  └─ setWordList(listOfCharacters) // replaces m_words
```

**Key insight for RTL**: `correctTextOrder()` sorts purely by geometric position:
- `compareTinyTextEntityY` — sorts by `top` coordinate
- `compareTinyTextEntityX` — sorts by `left` coordinate
- `makeAndSortLines` — groups by Y overlap, then sorts each line left-to-right
- This always assumes **left-to-right** reading order!

For RTL text, characters would need to be sorted **right-to-left** within lines. The XYCut algorithm and the `compareTinyTextEntityX` comparator are where changes would be needed.

### Phase 3: Text Extraction for Copy/Paste

```
Page::text(area, behaviour)
  ├─ If area is given: rotate area by inverse rotation matrix
  └─ TextPage::text(area, behaviour)
       ├─ For each TextEntity in m_words:
       │    ├─ If behaviour == AnyPixel:  include if area intersects entity bounding rect
       │    └─ If behaviour == CentralPixel: include if area contains entity center
       └─ Concatenate all matching entity texts into one QString
```

**Key insight for RTL**: The text is extracted in `m_words` order (which is the post-`correctTextOrder` order). If `correctTextOrder` sorts left-to-right, RTL text will be extracted character-by-character in LTR order. The fix must ensure `m_words` has characters in visual RTL order (right-to-left) for RTL text regions.

### Phase 4: Text Selection (Mouse Drag)

```
Page::textArea(TextSelection selection)
  └─ TextPage::textArea(selection)  // complex geometry
       ├─ Normalize start/end points
       ├─ Handle edge cases (outside page, swapped cursors)
       ├─ Find start/end TextEntity positions
       ├─ For selection type 01 (start above end): 
       │    search right-and-down from start, left-and-up from end
       ├─ For selection type 02 (start below end):
       │    find nearest entity to each cursor
       └─ Build RegularAreaRect by appending entity areas with rotation matrix applied
```

---

## How Search Works

### Entry Point: `Document::searchText()`

```
Document::searchText(searchID, text, fromStart, caseSensitive, type, moveViewport, color)
  ├─ Validate: generator has TextExtraction, pages exist
  ├─ Get/create RunningSearch for searchID
  ├─ Clear old highlights
  ├─ Branch by SearchType:
  │    ├─ AllDocument → doContinueAllDocumentSearch (iterates all pages)
  │    ├─ NextMatch/PreviousMatch → doContinueDirectionMatchSearch
  │    └─ GoogleAll/GoogleAny → doContinueGooglesDocumentSearch (per-word coloring)
  └─ All branches use QTimer::singleShot(0, ...) for non-blocking search
```

### Per-Page Search: `Page::findText()` → `TextPage::findText()`

```
TextPage::findText(searchID, query, direction, caseSensitivity, lastArea)
  ├─ Resolve direction:
  │    FromTop → start at begin, forward
  │    FromBottom → start at end, backward
  │    NextResult → resume from last match end, forward
  │    PreviousResult → resume from last match begin, backward
  ├─ Pick comparison function (case sensitive/insensitive)
  ├─ Forward:  findTextInternalForward()
  └─ Backward: findTextInternalBackward()
```

### Core Search Algorithm: `findTextInternalForward()`

```
findTextInternalForward(searchID, _query, comparer, start, start_offset, end)
  ├─ Normalize query with NFKC (compatibility decomposition)
  ├─ For each TextEntity from start to end:
  │    ├─ Normalize entity text with NFKC
  │    ├─ Adjust length for hyphenation: 
  │    │    stringLengthAdaptedWithHyphen(str, it, end)
  │    │    ├─ If str ends with '-' and next entity is '\n': skip the hyphen
  │    │    ├─ If str ends with '-' and next entity is in different Y region: skip hyphen
  │    │    └─ If str ends with "-\n": skip both chars
  │    ├─ Try matching from full string down to adjusted length
  │    ├─ On match: advance query position by matchedLen
  │    ├─ On full match (queryLeft==0):
  │    │    ├─ Save SearchPoint (it_begin, it_end, offset_begin, offset_end)
  │    │    └─ Return searchPointToArea(sp) → RegularAreaRect
  │    └─ On no match: reset query to start, back up to it_begin+1
  └─ If no match found: delete SearchPoint for this searchID, return nullptr
```

**Key insight for RTL search**: The search algorithm walks entities in `m_words` order (forward or backward). If `correctTextOrder` produces LTR ordering for RTL text, the search will work but highlights may appear in unexpected visual positions since the match spans a different set of bounding rectangles than the user expects visually.

---

## How Copy/Paste Text Extraction Works

### `TextPage::text(area, behaviour)`

Very simple: iterates all TextEntity in `m_words`, concatenating text where the entity's bounding box satisfies the inclusion criteria:

- **AnyPixel**: `area->intersects(entity.area())` — any pixel overlap
- **CentralPixel**: `area->contains(entity.area().center())` — center must be inside

Returns a single `QString`. The text order is exactly `m_words` order.

### `TextPage::words(area, behaviour)`

Same as `text()` but returns `TextEntity::List` instead of concatenated string. Used when the caller needs per-character bounding boxes (e.g. for rendering or annotation).

### `Page::text(area, behaviour)`

Wrapper that applies inverse rotation to the input area before delegating to `TextPage::text()`. This way, the TextPage always receives coordinates in the page's unrotated frame.

---

## Key Classes and Relationships

```
Document
  ├── Generator *m_generator             (abstract backend)
  │     └── virtual TextPage *textPage(TextRequest*)
  ├── QList<Page*> m_pagesVector
  │     ├── PagePrivate *d
  │     │     ├── TextPage *m_text       (generated on-demand)
  │     │     │     └── TextEntity::List m_words  (THE canonical text list)
  │     │     ├── Rotation m_rotation
  │     │     └── NormalizedRect m_boundingBox
  │     └── Highlights / ObjectRects / Annotations
  └── QMap<int, RunningSearch*> m_searches
```

**TextEntity** is the atom. It stores text and a NormalizedRect bounding box.

**TextPage::m_words** is the authoritative text list. Everything — search, extraction, selection — reads from this list in order. The order is set by `correctTextOrder()`.

**SearchPoint** remembers where a search result was found (start/end iterators and offsets into `m_words`). Used by NextResult/PreviousResult to resume searching from the last match.

---

## Important Implementation Details

### 1. Unicode Normalization

- **Search** uses **NFKC** (Compatibility Composition) — normalizes both the query and entity text before comparison. This ensures "ﬁ" (ligature fi) matches "fi".
- **Copy/Export** uses **NFC** (Canonical Composition) — preserves character identity better for copy-paste. See comments in `append()` and `makeWordFromCharacters`.

### 2. Hyphenation Handling in Search

`stringLengthAdaptedWithHyphen()` allows searching for "hyphen" to match "hy-\nphen" across lines. It detects:
- `"-"` at end of entity + `"\n"` at start of next entity
- `"-"` at end of entity + next entity in different Y region (different line)
- `"-\n"` at end of entity

The search tries matching without the hyphen first, then with it.

### 3. String Pooling

A `thread_local unordered_set<QString>` pool in `fromPool()` reuses short strings (≤2 chars) via QString implicit sharing. This optimizes memory since many TextEntity objects have 1-2 character strings (individual glyphs).

### 4. Page Rotation

- `PagePrivate::rotationMatrix()` returns a QTransform for the current rotation.
- When extracting text via `Page::text(area)`, the area is inverse-rotated before passing to `TextPage::text()`.
- When getting selection geometry via `TextPage::textArea()`, each entity's area is transformed by the rotation matrix before appending to the result.
- `MergeSide` used in `appendShape()` depends on `page->totalOrientation()` — this ensures the "merge adjacent rectangles" logic works correctly for rotated pages.

### 5. DPI-Independent Text Layout

`correctTextOrder()` scales page dimensions so their sum equals 2000:
```cpp
const double scalingFactor = 2000.0 / (m_page->width() + m_page->height());
```
This avoids layout recognition failures on low-DPI displays.

### 6. The XYCut Algorithm

`XYCutForBoundingBoxes()` is the page layout segmentation algorithm. It:
1. Computes horizontal and vertical projection profiles
2. Cleans up boundary whitespace
3. Finds widest gaps in both directions
4. Compares gaps against statistically-computed thresholds (word_spacing×2, line_spacing×2)
5. Cuts the region recursively (horizontal or vertical)
6. Always processes regions left-to-right, top-to-bottom

**For RTL support**: This algorithm would need to be modified to optionally process right-to-left for regions containing RTL text.

---

## Critical Code Locations for RTL Fix

| Issue | File | Key Function/Line |
|-------|------|-------------------|
| Text ordering (characters within words) | `textpage.cpp` | `correctTextOrder()` ~L1623 |
| X-sorting within lines | `textpage.cpp` | `compareTinyTextEntityX()` ~L906 |
| Line sorting | `textpage.cpp` | `makeAndSortLines()` ~L1043 |
| Word building from chars | `textpage.cpp` | `makeWordFromCharacters()` ~L945 |
| XYCut segmentation | `textpage.cpp` | `XYCutForBoundingBoxes()` ~L1307 |
| Text extraction (copy/paste order) | `textpage.cpp` | `TextPage::text()` ~L876 |
| Text selection geometry | `textpage.cpp` | `TextPage::textArea()` ~L277 |
| Search character comparison | `textpage.cpp` | `findTextInternalForward()` ~L662, `findTextInternalBackward()` ~L763 |
