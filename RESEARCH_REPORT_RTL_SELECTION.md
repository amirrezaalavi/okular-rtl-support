# Okular RTL Text Selection Direction — Research Report
**Date:** 2026-07-24
**Scope:** Does Okular correctly handle click-and-drag text selection direction for RTL scripts (Persian/Arabic/Hebrew)?
**TL;DR:** **YES, this is a real bug.** RTL drag selection is treated identically to LTR. Root cause: `TextPage::textArea()` in `core/textpage.cpp` swaps the two cursor points whenever `start.x > end.x`, assuming "leftmost cursor = selection start". `m_words` itself is also sorted left-to-right by `correctTextOrder()` (sorting characters and lines by `left` coordinate), so iteration is always LTR. No per-character/per-line direction metadata exists. KDE Bug 345512 already documents this exact issue as a duplicate of the RTL copy bug (#184399). Existing Phase 1 fix in `plan.md` (reordering text in `abstractTextPage()`) does NOT fix selection drag direction because `correctTextOrder()` re-sorts spatially.

---

## 1. Where Does Selection Direction Get Decided?

Selection direction is decided in **two** places, both in `core/textpage.cpp`:

### A. `TextPage::textArea(const TextSelection &sel)` — line 277
This is the core function that takes a drag rectangle and returns the set of glyph rectangles to highlight. The selection direction is decided by:

**Line 308-310** (the most LTR-hardcoded line in the entire file):
```cpp
// if startPoint is right to endPoint swap them
if (startC.x > endC.x) {
    std::swap(startC, endC);
}
```
This **unconditionally swaps** the cursors so that `startC.x <= endC.x`. It assumes the user always wants the leftmost cursor to be the "start" of the selection. For RTL reading, the start of a selection in reading order can be the RIGHTMOST cursor.

**Line 425-471 (case 3.a)** — finds a starting entity:
- Type 01 (drag top→bottom): searches forward through `m_words` for the first entity that is "right of" or "bottom of" the start cursor. Uses `rect.isRight(startC)` and `rect.isBottom(startC)` — pure LTR geometry.
- Type 02 (drag bottom→top): searches forward for the closest entity to start cursor using Manhattan distance.

**Line 473-518 (case 3.b)** — finds the ending entity:
- Iterates `m_words` **backwards** (`itEnd--`) looking for entities "left of" the end cursor.
- Type 01: finds first entity with `rect.isLeft(endC)` and `!rect.isTop(endC)`.
- Type 02: closest entity using Manhattan distance.

**Line 530-535** — final swap:
```cpp
// if start is less than end swap them
if (start > end) {
    it = start;
    start = end;
    end = it;
}
```
Again LTR-only: assumes the start of selection corresponds to a smaller iterator index in `m_words`, which only works when `m_words` is in LTR order.

**Line 542-544** — final highlight loop:
```cpp
for (; start <= end; start++) {
    ret->appendShape(start->transformedArea(matrix), side);
}
```
This iterates `m_words` in order, appending rectangles for each entity between the resolved start and end iterators. The order of iteration in `m_words` is the order of highlight.

### B. `TextPagePrivate::correctTextOrder()` — line 1623
This reorders `m_words` so iteration order matches the visual reading order. **It is purely LTR**:
- `compareTinyTextEntityX` (line 905-911): sorts by `firstArea.left() < secondArea.left()` — strict ascending X.
- `compareTinyTextEntityY` (line 913-919): sorts by `firstArea.top() < secondArea.top()` — strict ascending Y (top→bottom).
- `makeAndSortLines` (line 1043) line 1115-1117: within each line, sorts by `compareTinyTextEntityX` — strict left-to-right.
- `XYCutForBoundingBoxes` (line 1307): recursively cuts regions left-to-right, top-to-bottom.

**No function in this file ever sorts right-to-left** or detects text direction.

---

## 2. Does TextPage Have Per-Word/Per-Character Direction Metadata?

**No.** Verified by reading the relevant headers.

### `core/textpage.h` — `TextEntity` class (line 52-86):
```cpp
class OKULARCORE_EXPORT TextEntity
{
public:
    typedef QList<TextEntity> List;
    TextEntity(const QString &text, const NormalizedRect &area);
    ~TextEntity();
    QString text() const;
    NormalizedRect area() const;
    NormalizedRect transformedArea(const QTransform &matrix) const;

private:
    QString m_text;
    NormalizedRect m_area;
};
```
**Only two fields**: text string and bounding rectangle. No `direction`, no `isRtl`, no `logicalIndex`, no `visualIndex`.

### `core/textpage_p.h`:
- `TextEntity::List m_words` — the canonical ordered list.
- `QMap<int, SearchPoint*> m_searchPoints` — per-searchID state.
- `Page *m_page`.
No direction field on the page level either.

### `core/area.h` — `NormalizedRect`:
- `isLeft(const NormalizedPoint &pt)` (line 371-374): `return left < pt.x;` — pure LTR spatial test.
- `isRight(const NormalizedPoint &pt)` (line 380-383): `return right > pt.x;` — pure LTR spatial test.
- `isTop` / `isBottom` / `isTopOrLevel` / `isBottomOrLevel` (line 335-368): pure vertical spatial tests.

These four helpers are the only `is*()` predicates, and they encode LTR geometry into the API.

### `core/area.h` — `TextSelection` (in `misc.h`):
```cpp
class TextSelection
{
    NormalizedPoint start;
    NormalizedPoint end;
    // ...
};
```
Just two named points. Nothing indicates reading direction.

### `core/global.h`:
- `SearchDirection` enum: `FromTop`, `FromBottom`, `NextResult`, `PreviousResult` — these refer to search iteration, not text reading direction.

**Conclusion**: The data model has no concept of per-glyph, per-word, or per-line text direction. All code below the data model assumes LTR.

---

## 3. The Selection Algorithm — End-to-End Walkthrough

What happens when the user click-drags from pixel A to pixel B in a PDF:

### Step 1 — UI layer (`part/pageview.cpp`)
- `mouseMoveEvent` (line 2394-2402) in `TextSelect` mode: if `mouseTextSelecting` is false and movement > 5px from `mouseSelectPos`, set `mouseTextSelecting = true`. Then call `updateSelection(pos)`.
- `updateSelection` (line 3900-3930) calls `textSelections(pos, d->mouseSelectPos.toPoint(), first)`.

### Step 2 — `textSelections` (line 3525-3594)
- Builds a `selectionRect = QRect::span(start, end)`.
- Finds all `PageViewItem`s whose geometry intersects the selection rect.
- `direction_ne_sw` = is the drag along the NE-SW diagonal (line 3542). If yes, `start` is the top-right corner; if no, `start` is the top-left corner. **This is already an LTR assumption** — for RTL, "start" of drag should map to a different corner.
- For each affected page, calls `textSelectionForItem(item, startPoint, endPoint)` with corner-corrected coordinates.

### Step 3 — `textSelectionForItem` (line 3954-3978)
- Converts pixel points to normalized page coordinates via `rotateInNormRect` (handles page rotation 0/90/180/270).
- Calls `okularPage->textArea(mouseTextSelectionInfo)`.

### Step 4 — `Page::textArea` (delegates to `TextPage::textArea`)
Same function described in §1.A above.

### Step 5 — `Page::text()` for copy (called from `selectedText()` line 1005-1034)
- Iterates `m_words` in order, includes any entity whose center pixel is inside the selection rect.
- Concatenates included entity text in `m_words` order.

### Order of rectangles in highlight:
The order of `RegularAreaRect` produced by `textArea()` is `m_words` order. `RegularAreaRect` doesn't itself care about order — it's a set of rectangles used for hit-testing (which is what highlight uses). So the **highlight rendering** doesn't care about order, but the **text extraction for copy** does.

### Where it would be wrong for RTL:
- `textArea()` (line 308) swaps so `startC.x <= endC.x`. For an RTL page where the user clicks at a point and drags LEFT (which is the natural "select forward" direction in RTL reading), the click point is right of the drag point. After the swap, `startC` is now the **drag endpoint**, and `endC` is the **click start**. This puts the selection iteration in reverse reading order, causing copy to come out backwards even if `m_words` is in logical order.
- For LTR pages this works because "drag right" = "select forward".

---

## 4. How Chromium / Firefox Handle This

I cannot browse the web here, but the general approach in mature viewers is:
1. **Snapshot the cursor position at mouse-down**, and at mouse-up. Both are recorded verbatim — the "start" of the selection is whichever button was pressed, and the "end" is wherever the user dragged to.
2. **Find the first character at the click point and detect its direction** (e.g., via Unicode bidi class — `QChar::direction()` in Qt).
3. **For each character hit-test, append to a selection list in REVERSE if direction is RTL.** The iteration order is governed by the actual character direction, not by cursor X coordinates.
4. **Selection anchor/focus model** (the W3C Selection API): the user can extend a selection in either direction; the "anchor" is the fixed point and the "focus" moves. The selection range is `[anchor, focus]` regardless of order, and the rendering of the caret/handle swaps based on the actual direction of the character at each end.

Okular's `TextSelection` only has `start` and `end` (no separate anchor/focus) and never considers character direction, so the "reverse iteration for RTL" trick is not implemented.

---

## 5. Specific Code Locations That Would Need to Change

To fix RTL drag selection direction, the changes would be in `core/textpage.cpp`:

### Fix Location 1: `textArea()` line 308-310 (the unconditional X-swap)
**Current:**
```cpp
if (startC.x > endC.x) {
    std::swap(startC, endC);
}
```
**Should be:** Keep both points. Use the *first character hit at startC* to detect direction; if the start cursor is to the right of the end cursor AND the first character is RTL, treat `startC` as the "logical start" anyway (no swap, but reverse the subsequent iteration).

### Fix Location 2: `textArea()` case 3.a (line 426-471) — start entity search
When direction is RTL, the search predicate should be `rect.isLeft(startC)` instead of `rect.isRight(startC)` — i.e., find the first character to the LEFT of (and on same line as) the start cursor, not to the right.

### Fix Location 3: `textArea()` case 3.b (line 473-518) — end entity search
Conversely, when direction is RTL, the end cursor is to the right of the start cursor. The end search should use `rect.isRight(endC)` instead of `rect.isLeft(endC)`.

### Fix Location 4: `textArea()` final swap (line 530-535)
The `if (start > end) swap` is correct if `m_words` is in logical order — but in the existing fix, characters within a word are in logical order while `m_words` overall is in visual (LTR-spatial) order. So this swap is doing the wrong thing for RTL: it normalizes iteration to LTR. The fix needs to also detect that the *start* of the selection in reading order corresponds to a HIGHER iterator index in `m_words` when the start character is RTL.

### Fix Location 5: `textArea()` final highlight loop (line 542-544)
The loop iterates `start++` (forward). For RTL, it may need to iterate `end--` (backward), or the `RegularAreaRect` needs to know that the order matters for copy.

### Fix Location 6: `Page::text()` and `TextPage::text()` in `core/textpage.cpp` ~line 876
This iterates `m_words` to extract text for copy. If `m_words` is in visual order (current state) and selection is RTL, copy order is wrong. If the existing Phase 1 fix in `plan.md` is applied AND selection direction is also fixed, this would work — but the two must be coordinated.

### Fix Location 7: `correctTextOrder()` sort by X (line 905-911, 1117)
For an RTL document, the X-sort comparator should reverse (`>` instead of `<`). However, this would break mixed-direction pages and require per-region direction detection (the XYCut algorithm would need a direction tag on each region).

### Fix Location 8: `compareTinyTextEntityX` (line 905-911)
This comparator is passed to `std::sort`. It would need to be direction-aware or replaced by a per-region decision in `makeAndSortLines`.

---

## 6. Existing Bug Reports

**Yes, this is already reported as KDE Bug 345512** ("Hebrew text selection wrong direction"), and it's marked as a DUPLICATE of KDE 184399 ("Copy/paste of Hebrew text is backwards"). See `RESEARCH_REPORT.md` line 18.

The previous research report (`RESEARCH_REPORT.md`) and `plan.md` focus on the **copy and search** aspects of the visual-vs-logical bug. **They do NOT address the selection drag direction specifically.** The `plan.md` Phase 1 (reordering in `abstractTextPage()`) is necessary but not sufficient — it fixes what text comes out, but the spatial layout and the selection direction logic in `textArea()` still operate in LTR.

The current `plan.md` line 27 says "Do NOT change the search algorithm" — and by extension, the selection algorithm. But that constraint is what blocks the fix. The selection algorithm needs to be aware of direction.

---

## 7. Assessment: Is This a Real Bug?

**Yes, this is a real bug.** Specifically:

1. **For visual feedback (highlight)**: The highlighted rectangle is the same regardless of selection direction — `RegularAreaRect` is an unordered set, and `m_words` is iterated both ways to produce the same set of rects. So the **highlighted area looks the same** for an RTL drag as for an LTR drag. The user sees correct highlighting.

2. **For text content (copy)**: After the existing Phase 1 fix is applied (reordering text in `abstractTextPage()` to logical order), copy will be correct *if* the selection iterates `m_words` in the correct order. Currently `textArea()` does `if (start > end) swap`, which forces the iteration to go in `m_words` spatial order. If the user drags from right→left on RTL text, the "natural reading start" is at the right, and the "natural reading end" is at the left. `m_words` is sorted spatially (LTR), so the rightmost character has a higher index. The current code's swap would put the higher index as `start` and lower index as `end`, and iterate forward — yielding left-to-right (visual) order. **This means even with the Phase 1 fix, an RTL drag would yield visually-ordered text, not logically-ordered text.**

3. **For the user's mental model**: When a user clicks at the right side of a Hebrew word and drags left, they expect the selection to be "from this letter to that letter" in reading order (right-to-left). The current Okular behavior produces "from this letter (the leftmost in screen) to that letter" — the opposite.

---

## 8. Suggested Fix Approach (No Code Changes Made)

### Option A: Minimal fix — detect direction at the start cursor, iterate in reverse
- In `textArea()` line 308, instead of swapping, leave the cursors in their natural order.
- Find the entity at the start cursor and inspect its first character's direction via `QChar::direction()` (returns `QChar::DirR` or `QChar::DirAL` for RTL).
- If the start cursor is RIGHT of the end cursor AND the start character is RTL, treat the start cursor as the "end" of the logical selection.
- Adjust case 3.a and 3.b predicates accordingly.
- The final highlight loop's order can stay as-is because highlight is unordered; only the copy iteration matters, and copy iterates `m_words` which (with the Phase 1 fix) is in logical order.

### Option B: Full fix — per-region/per-line direction metadata
- Add a `direction` field to `TextEntity` or to a wrapper struct in `correctTextOrder()`.
- When `makeAndSortLines` builds a line, detect the primary direction of the line by inspecting character bidi classes.
- For RTL lines, sort `compareTinyTextEntityX` in REVERSE order.
- In `textArea()`, no per-cursor direction detection is needed — the spatial order of `m_words` already matches reading order.

### Option C: Hybrid — page-level direction detected once
- Add a per-TextPage direction flag, set lazily on first access.
- Detection: scan the first N characters of `m_words` and decide based on bidi class majority.
- Use this flag in `textArea()` to choose the swap/no-swap branch.
- This is simplest but doesn't handle mixed-direction documents well.

### Recommendation
**Option B** is the correct long-term solution. **Option A** is the minimal patch and can be done quickly (~50-100 lines of code, localized to `textArea()`). The pre-existing Phase 1 fix in `plan.md` does the heavy lifting for copy/search; **adding Option A to `textArea()` makes drag selection direction correct for RTL pages**.

---

## 9. Estimated Effort

| Option | Effort | Risk | Files Touched |
|--------|--------|------|---------------|
| **A: Minimal cursor-aware fix** | Trivial (~2-4 hours) | Low — only affects selection direction, not copy/search/extraction | `core/textpage.cpp` `textArea()` line 308, 425-471, 473-518, 530-535, 542-544 |
| **B: Full per-region direction** | Major (~3-5 days) | Medium — touches `correctTextOrder()` and downstream consumers, requires new tests for mixed-direction | `core/textpage.cpp`, `core/textpage.h` (add direction field), possibly `core/page.cpp` |
| **C: Page-level direction flag** | Moderate (~1 day) | Medium — wrong for mixed documents | `core/textpage.cpp`, `core/textpage.h` |

### Recommended starting point
Start with **Option A** because:
- It is local to one function.
- It only requires `QChar::direction()` (already in Qt6).
- It does not conflict with the Phase 1 fix in `plan.md`.
- It is the most direct response to the user-reported behavior.
- If a different test case reveals a deeper problem, **Option B** can be done later without revisiting the Phase 1 fix.

### Critical caveat for Option A
The fix relies on knowing the direction of the character at the start cursor. If the start cursor lands between two characters of mixed direction (e.g., a Persian word adjacent to an English word), the choice of "direction" is ambiguous. Option A would need to inspect both neighboring characters and pick the one with the most overlapping bounding box, OR fall back to the predominant direction of the first 5-10 characters of the surrounding line.

---

## 10. Summary of Findings

1. **Selection direction is decided in `core/textpage.cpp::TextPage::textArea()` at line 277**, specifically at line 308 (the X-swap) and lines 530-535 (the iterator-order swap).
2. **No per-glyph/per-line direction metadata exists** on `TextEntity` or `TextPage`. The data model is LTR-only.
3. **The selection algorithm**: cursor pixels → normalized coords → `textArea()` swaps so leftmost = start, finds start entity and end entity in `m_words`, iterates between them. `m_words` is built by `correctTextOrder()` which sorts by left coordinate (LTR).
4. **No Chromium/Firefox-style anchor/focus or character-direction-awareness** in Okular.
5. **Fix locations**: primarily `textArea()` line 308-310 and the case 3.a/3.b predicates, with the character-direction detection happening at the start cursor.
6. **Bug already reported**: KDE #345512 (duplicate of #184399). The current `plan.md` does not address it.
7. **Real bug**: yes, particularly when combined with the existing copy bug. Visual highlight may look right, but the iterator order in `textArea()` makes the copy direction dependent on spatial drag direction, not reading direction.
8. **Effort**: trivial (Option A, ~2-4 hours) to major (Option B, 3-5 days).

---

## Appendix A — Key Code Snippets (Reference)

### A.1 `core/textpage.cpp` line 277-547 (full `textArea()`)
```cpp
std::unique_ptr<RegularAreaRect> TextPage::textArea(const TextSelection &sel) const
{
    if (d->m_words.isEmpty()) {
        return std::make_unique<RegularAreaRect>();
    }
    // [comments explaining the 3-rectangle decomposition]
    auto ret = std::make_unique<RegularAreaRect>();
    const PagePrivate *pagePrivate = PagePrivate::get(d->m_page);
    const QTransform matrix = pagePrivate ? pagePrivate->rotationMatrix() : QTransform();
    const double scaleX = d->m_page->width();
    const double scaleY = d->m_page->height();

    NormalizedPoint startC = sel.start();
    NormalizedPoint endC = sel.end();

    // BUG: unconditionally swaps so leftmost = start (LTR-only)
    if (startC.x > endC.x) {
        std::swap(startC, endC);
    }

    // ... bounding rect clamping ...

    TextEntity::List::ConstIterator it = d->m_words.constBegin(), itEnd = d->m_words.constEnd();
    TextEntity::List::ConstIterator start = it, end = itEnd, tmpIt = it;
    const MergeSide side = d->m_page ? (MergeSide)d->m_page->totalOrientation() : MergeRight;

    NormalizedRect tmp;
    // case 2(a) — find entity containing startC/endC
    for (; it != itEnd; ++it) {
        tmp = it->area();
        if (tmp.contains(startC.x, startC.y)) {
            start = it;
        }
        if (tmp.contains(endC.x, endC.y)) {
            end = it;
        }
    }

    // case 2(b) — find any entity inside start_end rect
    it = tmpIt;
    if (start == it && end == itEnd) {
        for (; it != itEnd; ++it) {
            tmp = it->area();
            if (start_end.intersects(tmp)) {
                break;
            }
        }
        if (it == itEnd) {
            return ret;
        }
    }
    it = tmpIt;
    bool selection_two_start = false;

    // case 3.a — find start entity (search forward through m_words)
    if (start == it) {
        NormalizedRect rect;
        if (startC.y <= endC.y) {
            for (; it != itEnd; ++it) {
                rect = it->area();
                bool flagV = !rect.isBottom(startC);
                // BUG: 'isRight' is LTR-only — for RTL should be 'isLeft'
                if (flagV && rect.isRight(startC)) {
                    start = it;
                    break;
                }
            }
        } else {
            selection_two_start = true;
            int distance = scaleX + scaleY + 100;
            for (; it != itEnd; ++it) {
                rect = it->area();
                if (rect.isBottomOrLevel(startC) && rect.isRight(startC)) {
                    // Manhattan distance
                }
            }
        }
    }

    // case 3.b — find end entity (search BACKWARD through m_words)
    if (end == itEnd) {
        it = tmpIt;
        itEnd = itEnd - 1;
        NormalizedRect rect;
        if (startC.y <= endC.y) {
            for (; itEnd >= it; itEnd--) {
                rect = itEnd->area();
                bool flagV = !rect.isTop(endC);
                // BUG: 'isLeft' is LTR-only — for RTL should be 'isRight'
                if (flagV && rect.isLeft(endC)) {
                    end = itEnd;
                    break;
                }
            }
        } else {
            int distance = scaleX + scaleY + 100;
            for (; itEnd >= it; itEnd--) {
                rect = itEnd->area();
                if (rect.isTopOrLevel(endC) && rect.isLeft(endC)) {
                    // Manhattan distance
                }
            }
        }
    }

    if (selection_two_start) {
        if (start > end) {
            start = start - 1;
        }
    }

    // BUG: this swap assumes start < end in m_words, which only works for LTR
    if (start > end) {
        it = start;
        start = end;
        end = it;
    }

    if (end == d->m_words.constEnd()) {
        end--;
    }

    for (; start <= end; start++) {
        ret->appendShape(start->transformedArea(matrix), side);
    }

    return ret;
}
```

### A.2 `core/textpage.cpp` line 905-919 (X and Y comparators)
```cpp
static bool compareTinyTextEntityX(const WordWithCharacters &first, const WordWithCharacters &second)
{
    QRect firstArea = first.area().roundedGeometry(1000, 1000);
    QRect secondArea = second.area().roundedGeometry(1000, 1000);

    return firstArea.left() < secondArea.left();  // BUG: always LTR
}

static bool compareTinyTextEntityY(const WordWithCharacters &first, const WordWithCharacters &second)
{
    const QRect firstArea = first.area().roundedGeometry(1000, 1000);
    const QRect secondArea = second.area().roundedGeometry(1000, 1000);

    return firstArea.top() < secondArea.top();
}
```

### A.3 `core/textpage.cpp` line 1115-1117 (within-line sort)
```cpp
// Step 3
for (QPair<WordsWithCharacters, QRect> &line : lines) {
    WordsWithCharacters &list = line.first;
    std::sort(list.begin(), list.end(), compareTinyTextEntityX);  // BUG: LTR-only
}
```

### A.4 `part/pageview.cpp` line 3542-3543 (drag corner detection in `textSelections`)
```cpp
// is the mouse drag line the ne-sw diagonal of the selection rect?
bool direction_ne_sw = start == selectionRect.topRight() || start == selectionRect.bottomLeft();
```
This decides whether `start` is the top-left or top-right corner of the drag rect. Used in lines 3572, 3577, 3583 to swap which corner is sent as the "start cursor" to `textSelectionForItem`. **LTR assumption**: a top-left start is the "beginning". For RTL, a top-right start would be the "beginning" if dragging right-to-left, but the algorithm doesn't know that.

### A.5 `part/pageview.cpp` line 3954-3978 (`textSelectionForItem`)
```cpp
std::unique_ptr<Okular::RegularAreaRect> PageView::textSelectionForItem(const PageViewItem *item, const QPoint startPoint, const QPoint endPoint)
{
    const QRect &geometry = item->uncroppedGeometry();
    Okular::NormalizedPoint startCursor(0.0, 0.0);
    if (!startPoint.isNull()) {
        startCursor = rotateInNormRect(startPoint, geometry, item->page()->rotation());
    }
    Okular::NormalizedPoint endCursor(1.0, 1.0);
    if (!endPoint.isNull()) {
        endCursor = rotateInNormRect(endPoint, geometry, item->page()->rotation());
    }
    Okular::TextSelection mouseTextSelectionInfo(startCursor, endCursor);
    // ...
    std::unique_ptr<Okular::RegularAreaRect> selectionArea = okularPage->textArea(mouseTextSelectionInfo);
    return selectionArea;
}
```
Just converts pixels → normalized coords and forwards to `textArea()`. No direction logic.

---

## Appendix B — Files Reviewed

- `core/textpage.cpp` (1968 lines) — primary file
- `core/textpage.h` (210 lines) — class definitions
- `core/textpage_p.h` — private state
- `core/area.h` — geometry predicates
- `core/misc.h` — `TextSelection` class
- `core/global.h` — enums
- `part/pageview.cpp` lines 1005-1034, 2394-2402, 2597-2608, 3120-3124, 3525-3594, 3827-3838, 3900-3930, 3932-3952, 3954-3978
- `RESEARCH_REPORT.md` — prior research
- `plan.md` — prior implementation plan
- `core/AGENTS.md` — core architecture
- `part/AGENTS.md` — part/ architecture
- `AGENTS.md` — project root context
