# Okular RTL Selection Direction — Implementation Plan
**Status:** Confirmed bug, plan drafted
**Last Updated:** 2026-07-24
**See also:** `RESEARCH_REPORT_RTL_SELECTION.md`

---

## Problem Summary

When a user click-and-drags to select text in a PDF with Persian/Arabic/Hebrew content, the **copied text is in visual (screen) order, not reading (logical) order** — even after the existing Phase 1 fix.

### Concrete test result (verified)

Drag from x=779 to x=835 (left→right in screen) over the Persian word "شبیه‌سازی" (at screen x:779–835):

- **Expected output**: `شبیه‌سازی` (logical, reading order)
- **Actual output**: `یزاسهیبش` (visual, screen order)

The same backwards result occurs for **both drag directions** (left→right AND right→left), because the selection always iterates `m_words` in spatial (LTR) order.

### Already reported

[KDE Bug #345512](https://bugs.kde.org/345512) "Hebrew text selection wrong direction" — duplicate of #184399.

---

## Root Cause

The selection algorithm in `core/textpage.cpp::TextPage::textArea()`:

1. **Line 308–310**: Unconditionally swaps the two cursor points so the leftmost becomes `startC`. This discards the drag direction (anchor vs focus).
2. **Line 530–535**: If `start > end` iterator-wise in `m_words`, swap them. This forces iteration in `m_words` spatial order.
3. **Line 542–544**: Iterates `m_words[start..end]` and appends each `TextEntity` to the `RegularAreaRect`.

Then in `PageView::selectedText()`, the entities are concatenated **in `m_words` order** (not the user's natural drag direction, not the reading order).

Even though our Phase 1 fix in `generators/poppler/generator_pdf.cpp` stores characters in **logical** order **within each word**, the cross-word selection ordering is still driven by spatial position, which produces visual order for spans that span multiple RTL words.

---

## Fix Strategy

### Chosen approach: **Option A (minimal cursor-aware fix)** from the research report

1. **Remove the unconditional LTR X-swap** in `textArea()` line 308. Pass the natural cursor positions through.
2. **Detect direction of the start character** using `QChar::direction()` (returns `DirR` or `DirAL` for RTL).
3. **Adjust the case 3.a / case 3.b predicates** to use `isLeft`/`isRight` based on the determined direction.
4. **Adjust the final iterator swap** (line 530) to use the directional logic.

### What we do NOT do:
- Do NOT add a per-`TextEntity` direction field (Option B) — too invasive, requires touching the data model.
- Do NOT change `correctTextOrder()` (we leave its X-sorted behavior alone).
- Do NOT add a per-page direction flag (Option C) — doesn't handle mixed-direction documents.
- Do NOT change `TextSelection` — its `direction` field is unused by `textArea()`.

### Rationale

This is a **localized fix in one function**:
- It only affects how the selection iterates (does NOT change what is selected or how text is stored).
- It uses Qt6's built-in `QChar::direction()` — no new dependencies.
- It works on the user's existing mental model: drag direction + text direction.
- It does not conflict with the Phase 1 fix.

---

## Files to Modify

| File | Function | Change |
|------|----------|--------|
| `core/textpage.cpp` | `TextPage::textArea()` | Lines 308-310, 425-471, 473-518, 530-535 |

That's it. **One function, one file.**

---

## Detailed Implementation

### Step 1: Track drag direction (anchor vs focus)

`TextSelection::start()` and `TextSelection::end()` already swap based on a `direction` field (set in `TextSelection` constructor at `core/misc.cpp:23-30`). However, `textArea()` doesn't use these — it re-swaps at line 308 based on `startC.x > endC.x`.

**New approach:** Use `sel.start()` and `sel.end()` (which give the **original** mouse press / mouse release points) to determine the actual drag direction.

```cpp
// Use original mouse points (before any swap in TextSelection)
const NormalizedPoint anchor = sel.start();  // mouse press
const NormalizedPoint focus  = sel.end();    // mouse release
```

### Step 2: Detect text direction at anchor

Find the `TextEntity` at the anchor point. If the entity's first character has a strong RTL direction (`QChar::DirR` or `QChar::DirAL`), the text is RTL.

```cpp
// Helper: detect if string contains any RTL characters
static bool hasRtlChars(const QString &str) {
    for (int i = 0; i < str.length(); i++) {
        const QChar::Direction d = str.at(i).direction();
        if (d == QChar::DirR || d == QChar::DirAL) return true;
    }
    return false;
}
```

In `textArea()`, after finding the start entity:
```cpp
bool startIsRtl = false;
if (start != it) {  // we have a resolved start entity
    const QString &text = start->text();
    if (!text.isEmpty()) {
        const QChar::Direction d = text.at(0).direction();
        startIsRtl = (d == QChar::DirR || d == QChar::DirAL);
    }
}
```

### Step 3: Adjust the X-swap and iterator swap

```cpp
// Original (LTR-only):
if (startC.x > endC.x) std::swap(startC, endC);

// New: only swap X if anchor is LTR (or text is mixed LTR at the start cursor)
// For RTL, the rightmost point is the "start" in reading order.
if (startIsRtl) {
    if (startC.x < endC.x) std::swap(startC, endC);
} else {
    if (startC.x > endC.x) std::swap(startC, endC);
}
```

### Step 4: Adjust case 3.a / case 3.b predicates

When `startIsRtl`:
- The "right of" predicate becomes "left of"
- The "left of" predicate becomes "right of"

```cpp
// Original case 3.a (LTR, find start entity to the right of startC):
if (flagV && rect.isRight(startC)) { ... }

// New (when startIsRtl):
if (flagV && rect.isLeft(startC)) { ... }
```

### Step 5: Adjust the final iterator swap

The final `if (start > end) swap` swap needs to be made conditional on the line direction. For RTL lines, the "reading start" is at the rightmost X position, which corresponds to a **higher** iterator index in `m_words` (which is sorted LTR).

```cpp
// Original:
if (start > end) { start ↔ end }

// New: only swap if start and end are in unexpected order
// For LTR text: start < end (in m_words order)
// For RTL text: start > end (in m_words order, since rightmost = start of reading)
if (startIsRtl) {
    if (start < end) { start ↔ end }
} else {
    if (start > end) { start ↔ end }
}
```

### Step 6: Ensure copy iteration uses the resolved order

In `PageView::selectedText()` (line 1005-1094), the code iterates `m_pagesVector` in **spatial (page) order**, then calls `pg->text(...)` which uses `textPage->text(area)`. The `text(area)` function in `core/textpage.cpp:876-903` iterates `m_words` in storage order.

**This is the critical part**: the iteration order of `m_words` is what determines the order of the copied text. Even if `textArea()` correctly identifies the start/end iterators, the text extraction happens in `m_words` storage order.

**Solution:** Add a per-page "primary direction" flag, OR make `text()` use a parameter to control iteration direction, OR — simplest — sort the selection's `RegularAreaRect` sub-rects by the resolved reading order of their corresponding entities.

For this fix, the simplest correct approach is:

1. In `textArea()`, after resolving `start` and `end` iterators, **store them as the selection geometry** (the existing code already does this via `ret->appendShape(start->transformedArea(matrix), side)`).
2. In `text(area)`, **iterate the entities that contributed to `area` in the resolved order**, not in `m_words` order.

This is more invasive than the report suggests. For minimal changes, we can leverage an alternative:

**Alternative — use anchor focus direction from TextSelection:**

Since `TextSelection` already tracks `direction` (1 = drag up/left, 0 = drag down/right), we can pass this through to `text(area, direction)` and have it iterate `m_words` either forward or backward accordingly.

### Final plan (revised)

**Add a new parameter to `text()` for iteration direction:**

```cpp
// In core/textpage.h
QString text(const RegularAreaRect *area,
             TextAreaInclusionBehaviour b,
             bool reverse = false) const;  // new
```

In `text()`:
```cpp
if (reverse) {
    // iterate m_words backward
    auto it = m_words.rbegin();
    // ... but we need to filter by area ...
}
```

**Simpler approach — modify only `selectedText()` in pageview.cpp:**

Pass the `TextSelection`'s `direction` flag through to `text()`, and if `direction == 1` (drag was up/left), iterate in reverse.

Actually the cleanest approach is:

1. In `textArea()`, do NOT swap `startC/endC` (preserve the user's drag direction).
2. Find `start` and `end` iterators based on **the cursor's spatial position**, not its logical position.
3. In the final swap, use the **detected line direction** to determine if `start > end` should be swapped.
4. In `selectedText()`, append text in the **resolved order** of `start` and `end` (not in `m_words` order).

For a minimal first cut:

**The simplest fix that works for 90% of cases:**

1. In `textArea()` line 308, don't swap X coordinates if the anchor char is RTL.
2. In `textArea()` line 530, swap the start/end iterators if RTL.
3. In `textPage::text(area)`, when computing the result, iterate entities in the order they appear in `area`'s sub-rects (which preserves the resolved reading order).

This is what I'll implement. Let me code it up.

---

## Risk Analysis

| Risk | Mitigation |
|------|------------|
| Breaking LTR selection | Only modify behavior when `startIsRtl == true` |
| Mixed LTR/RTL text on same page | Use per-line direction detection; fall back to LTR if line is mixed |
| Iterator swap could select wrong range | Test with the known Persian PDF before committing |
| Affects the search algorithm | The fix is in `textArea()`, not `findText()`. Search is unaffected. |

---

## Test Plan

### Test cases

1. **Pure English line, drag LTR**: Should still work as before (no change)
2. **Pure Persian line, drag LTR (left→right)**: Should produce correct logical-order text
3. **Pure Persian line, drag RTL (right→left)**: Should produce correct logical-order text
4. **Mixed Persian+English, drag from Persian side**: Should produce correct mixed-direction text
5. **Persian word, double-click**: Should select the word correctly
6. **Multi-line Persian selection**: Should select from top-line to bottom-line in reading order

### Regression tests

Run existing `searchtest` and `calculatetexttest` to ensure no regression.

---

## Implementation Order

1. Add the `hasRtlChars` and `isRtlString` helpers in `textpage.cpp`
2. Modify `TextPage::textArea()` to track `startIsRtl`
3. Modify the X-swap logic
4. Modify the case 3.a / case 3.b predicates (invert `isLeft`/`isRight`)
5. Modify the final iterator swap
6. Modify `TextPage::text(area, b)` to iterate in the resolved order
7. Test with real Persian PDF
8. Iterate as needed

---

## Estimated Effort

~4-8 hours (one function, one file, well-scoped, plus testing).
