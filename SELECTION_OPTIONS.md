# RTL Selection Direction — Fix Options
**Date:** 2026-07-24
**Status:** Bug confirmed via real test. See `RESEARCH_REPORT_RTL_SELECTION.md`.

---

## The Bug in One Paragraph

When the user selects Persian/Arabic/Hebrew text in a PDF (with our Phase 1 fix already applied), the **copied text comes out in visual (screen) order, not reading (logical) order**. The drag direction (left→right or right→left) has no effect — both produce the same backwards result. The selection algorithm in `core/textpage.cpp::TextPage::textArea()` unconditionally swaps cursor X-coordinates so the leftmost point becomes "start", and iterates `m_words` in spatial (LTR) order, which is the wrong order for RTL spans.

---

## The Two Distinct Sub-Problems

The bug has **two** independent issues that combine to produce the wrong result:

| # | Where | Problem |
|---|-------|---------|
| **A** | `textArea()` lines 308, 425-518 | The two cursor points get swapped/dropped so anchor vs focus is lost. The "start" of selection is always the leftmost pixel. |
| **B** | `TextPage::text()` line 882 + `PageView::selectedText()` line 1005 | After resolving which entities to copy, they're concatenated in `m_words` storage order (LTR), not in the user's drag direction or the reading order. |

Both fixes are needed. Even if we fix A perfectly, B will still produce wrong-order text because `m_words` is sorted LTR.

---

## Option 1 — Minimal Cursor-Aware Fix (the current plan-selection.md)

### What
Touch only `core/textpage.cpp::TextPage::textArea()`. Detect the direction of the character at the start cursor with `QChar::direction()`. Invert the X-swap, the `isLeft`/`isRight` predicates, and the final iterator swap when the start char is RTL.

### Pros
- Smallest possible diff (~30-60 lines)
- Localized to one function
- No new data model fields
- No new APIs

### Cons
- **Only fixes sub-problem A**, not B
- Even after the fix, the highlight geometry returned by `textArea()` is still built in `m_words` spatial order. When `PageView::selectedText()` calls `pg->text(pg->textSelection(), ...)`, it gets entities in `m_words` order.
- **Result: still wrong for multi-word RTL selections.** The single-word Persian test passes, but "select two words" still comes out in screen order.

### Effort
~3-4 hours. High risk of "looks right but isn't" — passes the test case but fails in real use.

---

## Option 2 — Per-TextEntity Direction + Per-Line Sort Inversion

### What
Add a `direction` field (or wrapper struct) to `TextEntity` in `textpage.h`. In `correctTextOrder()`:
- Detect each line's primary direction by scanning its characters
- For RTL lines, sort `compareTinyTextEntityX` in REVERSE
- Store the direction on each `TextEntity` (or on the line)

Then in `textArea()`, **the spatial order of `m_words` already matches reading order**, so no per-cursor detection is needed. The current X-swap and `isLeft`/`isRight` predicates "just work" — they're relative to the now-reading-aligned spatial order.

### Pros
- Solves **both A and B** at once
- `m_words` becomes the canonical reading order for all consumers (search, copy, text extraction)
- Aligns with how Chromium/Firefox/Word do it
- The downstream `text()` and `selectedText()` are unchanged

### Cons
- Touches the data model (`textpage.h` ABI) — but we're already changing the file
- The reverse-sorted `m_words` could surprise other consumers (search, wordAt) — need to test
- Rotation handling in `textArea()` becomes subtle (rotation 90° flips LTR/RTL spatial axis)

### Effort
~2-3 days. Medium risk.

---

## Option 3 — Reverse the Storage Order of `m_words` for RTL (Hybrid)

### What
The Phase 1 fix currently keeps `m_words` in LTR spatial order (because `correctTextOrder()` sorts by `left`). Instead:
- Keep `m_words` in LTR spatial order (for the highlight geometry to work)
- In `TextPage::text(area)`, when extracting text for copy, **iterate `m_words` in reverse order if the selection's primary line is RTL**

Add a per-page `primaryDirection` flag, set in `correctTextOrder()` by scanning all characters.

### Pros
- `m_words` spatial order preserved (no breaking the highlight geometry)
- Text extraction correctly iterates backwards for RTL
- Solves both A and B
- No changes to `textArea()` selection algorithm — it just produces the same highlight rect either way

### Cons
- Adding a per-page field requires API consideration
- The reverse iteration needs to handle multi-line selections (each line might have its own direction)
- The page text export (non-selection) would also need to respect direction

### Effort
~2-3 days. Medium risk.

---

## Option 4 — Full Bidi Reorder of `m_words` Itself (in `correctTextOrder`)

### What
Move the bidi reordering from the generator (`abstractTextPage()`) into `TextPagePrivate::correctTextOrder()`. Have `correctTextOrder()` actually reorder **characters within each word** based on visual order, and **sort words within each line in reading order** (LTR or RTL based on line direction).

This would be the architecturally cleanest fix: `m_words` is always in **logical reading order**, regardless of source format.

### Pros
- Single, principled fix that handles all RTL scenarios
- `m_words` becomes truly order-independent of source format
- Future generators (DjVu, EPUB, etc.) automatically benefit
- No per-TextPage direction state needed

### Cons
- Most invasive — touches the core ordering pipeline
- The generator-side fix in `abstractTextPage()` would need to be reverted (or kept; doesn't matter)
- `correctTextOrder()` is called every time a page is set — must be fast
- More regression risk

### Effort
~1 week. Higher risk, but cleanest result.

---

## Option 5 — Bypass: Don't Use the Selection API for Copy

### What
In `PageView::copyTextSelection()` and `selectedText()`, instead of using `pg->text(pg->textSelection(), ...)`, get the **list of `TextEntity` objects** via `pg->words(...)` and re-order them ourselves based on the actual mouse drag direction (which we know from the `TextSelection`).

### Pros
- Most surgical change — only in `part/pageview.cpp`
- Doesn't touch core code at all
- We have all the information we need: the drag direction is known, the entities are available

### Cons
- Doesn't help `text()` calls from other code paths (none currently, but future risk)
- Duplicates the iteration logic
- Doesn't fix the underlying selection geometry algorithm
- Other code that reads the selection (`setPageTextSelection`) still works in screen order — which is actually correct for highlight purposes

### Effort
~2-3 hours. Low risk, but limited scope.

---

## Comparison Table

| Option | Fixes A | Fixes B | Files Touched | LOC | Effort | Risk | Quality |
|--------|---------|---------|---------------|-----|--------|------|---------|
| 1. Minimal cursor | ✅ | ❌ | 1 | ~50 | 3-4h | Medium | Partial (single word only) |
| 2. Per-entity direction | ✅ | ✅ | 2 | ~200 | 2-3d | Medium | Good |
| 3. Per-page direction | ✅ | ✅ | 2 | ~150 | 2-3d | Medium | Good |
| 4. Full bidi in `correctTextOrder` | ✅ | ✅ | 1 | ~300 | 1w | Higher | Best |
| 5. Bypass in `copyTextSelection` | ❌ | ✅ | 1 | ~80 | 2-3h | Low | Partial |

---

## My Recommendation

**Option 2 or 3** is the right balance. They both fix the root cause without being as invasive as Option 4.

Of the two:
- **Option 2** is more architecturally pure — adds the metadata to the data model where it belongs
- **Option 3** is less invasive — keeps `m_words` in its current spatial order and adds a single per-page flag

If you want to keep the diff small and easy to review, **Option 3** is probably the better choice. If you want to set up Okular for the long term to handle more bidi scenarios (mixed-script documents, vertical text, etc.), **Option 2** is the foundation.

**If you want a "right now" fix that works**: combine **Option 1 + Option 5**. Fix the swap logic in `textArea()` so highlight matches reading order, and rewrite `selectedText()` to use `pg->words()` and re-order based on drag direction. ~150 lines, ~6 hours, low risk.

Which direction would you like to go?
