# Okular RTL Selection Fix — Full Implementation Plan
**Status:** Planning
**Date:** 2026-07-24
**See also:** `RESEARCH_REPORT_RTL_SELECTION.md`, `SELECTION_OPTIONS.md`

---

## Goal

Make text selection (drag-to-select) of Persian/Arabic/Hebrew text produce correct **logical (reading) order** in the copied clipboard, regardless of drag direction. Multi-line and mixed-direction documents must work correctly.

## Chosen Strategy: **Option 4 — Make `m_words` Truly Reading-Order**

The fundamental change: `TextPagePrivate::m_words` will be in **logical (reading) order**, not LTR-spatial order. This is achieved by modifying `correctTextOrder()` to:
1. Group words into lines (already done)
2. Detect each line's direction (new)
3. Sort words within each line in **reading order** (LTR or RTL per line)
4. Concatenate lines top-to-bottom (already done)

Once `m_words` is in reading order, **everything downstream works correctly**:
- `text()` iterates in reading order ✓
- `textArea()` iterator order matches reading order ✓
- `findText()` works (already fixed in Phase 1 for within-word) ✓
- `selectedText()` concatenation is correct ✓
- `wordAt()`, `lineAt()` work in reading order ✓

---

## Files Modified

| File | Function | Change |
|------|----------|--------|
| `core/textpage.cpp` | `correctTextOrder()` | Per-line direction detection + per-line sort inversion |
| `core/textpage.cpp` | `textArea()` | (Optional cleanup) — remove the X-swap that was a workaround |
| `core/textpage.cpp` | `text()` | (No change — works once `m_words` is in order) |
| `core/textpage.cpp` | `addNecessarySpace()` | (Must preserve line boundaries) |

The Phase 1 fix in `generators/poppler/generator_pdf.cpp` should be **kept** — it correctly reorders characters within each word, which is necessary for Phase 1 + Phase 2 to compose properly.

---

## Detailed Implementation

### Step 1: Add `lineDirection()` helper

```cpp
// In textpage.cpp, near compareTinyTextEntityX/Y
enum class LineDirection { LTR, RTL, Mixed };

// Count strong RTL vs strong LTR characters in a line
static LineDirection detectLineDirection(const WordsWithCharacters &line) {
    int rtlCount = 0, ltrCount = 0;
    for (const auto &wwc : line) {
        for (QChar c : wwc.word().text()) {
            const QChar::Direction d = c.direction();
            if (d == QChar::DirR || d == QChar::DirAL) ++rtlCount;
            else if (d == QChar::DirL) ++ltrCount;
        }
    }
    if (rtlCount == 0) return LineDirection::LTR;
    if (ltrCount == 0) return LineDirection::RTL;
    return LineDirection::Mixed;
}
```

### Step 2: Invert the comparator for RTL lines

The current code in `makeAndSortLines` (line 1115-1117) does:
```cpp
for (auto &line : lines) {
    auto &list = line.first;
    std::sort(list.begin(), list.end(), compareTinyTextEntityX);
}
```

New code:
```cpp
for (auto &line : lines) {
    auto &list = line.first;
    const LineDirection dir = detectLineDirection(list);
    if (dir == LineDirection::RTL) {
        std::sort(list.begin(), list.end(), compareTinyTextEntityXReverse);
    } else {
        std::sort(list.begin(), list.end(), compareTinyTextEntityX);
    }
}
```

And add a reverse comparator:
```cpp
static bool compareTinyTextEntityXReverse(const WordWithCharacters &first, const WordWithCharacters &second) {
    QRect firstArea = first.area().roundedGeometry(1000, 1000);
    QRect secondArea = second.area().roundedGeometry(1000, 1000);
    return firstArea.left() > secondArea.left();  // REVERSED
}
```

### Step 3: Handle mixed-direction lines

Mixed lines contain both LTR and RTL runs (e.g., "Hello سلام World"). For these:
- The Unicode Bidi Algorithm reorders them into logical order
- Our existing Phase 1 fix already handles within-word reordering
- For within-line, we need to apply the **full Unicode Bidi Algorithm** per line, not just count direction

**This is the hard part.** A proper implementation requires:
1. Splitting each line into "runs" of consistent direction
2. Reordering runs based on line's primary direction
3. Within each run, ordering words by their X position (LTR or RTL as appropriate)

**Simplification for v1**: Treat Mixed as LTR (use the existing comparator). This works correctly for:
- Pure LTR lines ✓
- Pure RTL lines ✓ (now fixed)
- Lines with LTR-dominant content (most of the line is English with a few Persian words) ✓
- Lines with RTL-dominant content (mostly Persian with embedded English numbers/latin) — **BROKEN** in v1

**For v2**: Add proper Unicode Bidi reordering per line.

### Step 4: Add `correctTextOrder()` boundary check

When the Phase 1 fix in `abstractTextPage()` is applied, characters within each word are already in logical order. We must ensure `correctTextOrder()` doesn't re-sort them based on bounding box (which would put them back in visual order).

Looking at `makeWordFromCharacters()` (line 945) — it builds words from individual characters, preserving the order they came in. ✓ This is fine.

Looking at `makeAndSortLines()` step 3 (line 1115-1117) — it sorts `WordWithCharacters` (which are words, not characters). ✓ Words are not reordered within themselves.

But wait: `addNecessarySpace()` (line 1558) might re-order. Let me check.

Actually, looking at the data flow: `correctTextOrder()` calls `makeWordFromCharacters` → `XYCutForBoundingBoxes` → `addNecessarySpace`. The XYCut analysis treats each character as a separate text entity, and re-groups them into words. **This is where our Phase 1 fix's per-word reordering could be lost** if the character bounding boxes from Phase 1 are in visual order (rightmost character has the leftmost x, etc.).

Wait, no. Our Phase 1 fix already reorders characters within each word. The bounding boxes are assigned based on visual position. So when `makeWordFromCharacters` groups characters into words, the characters are already in logical order in the input, and the output is also in logical order. ✓

But: `addNecessarySpace` (line 1558) re-builds the final list by appending words. The order of words in the output is what we control in Step 2.

### Step 5: Update `textArea()` to handle the new m_words order

The current `textArea()` does:
```cpp
if (startC.x > endC.x) std::swap(startC, endC);
```

This is LTR-only. With `m_words` now in reading order, this is **wrong** for RTL selections. We need to NOT swap — let the user's natural drag direction dictate the order.

But: `textArea()` only returns a `RegularAreaRect` (a set of rectangles for highlighting). The order of rectangles in `RegularAreaRect` is not significant — the highlight is just drawn. So the swap doesn't actually affect the visual highlight.

The order DOES matter for `text()` and `selectedText()`. But those iterate `m_words` directly, not the selection rects. So as long as `m_words` is in reading order, the extracted text is in reading order regardless of which rectangles the user selected.

**Therefore**: the X-swap in `textArea()` is now cosmetic. The "start" and "end" iterators in `textArea()` might be swapped relative to what the user expects, but the highlight rects are still correct, and the extracted text is in `m_words` order which is reading order.

**However**: if the user drags right-to-left across an RTL line, the anchor and focus are reversed. In a proper selection model, the anchor and focus should be preserved. We may need to keep the swap logic but use the **line direction** to decide which way to swap.

Let me think: in `textArea()`, we resolve `start` and `end` iterators. We iterate from `start` to `end` to build the highlight rects. If the user drags LTR on an LTR line, `start` is the left iterator and `end` is the right one — natural order. If the user drags RTL on an LTR line (right to left), the original `start` is on the right and `end` is on the left — but after our swap (`startC.x > endC.x`), they're swapped. This is correct behavior — we always want the highlight to be a continuous span.

For an RTL line with `m_words` in reading order: the rightmost word on screen is `m_words[k]` (first in reading). If the user drags from screen-left to screen-right, the anchor is on the left (m_words[k+1]) and focus is on the right (m_words[k]). The current swap would make `start` = right (m_words[k]) and `end` = left (m_words[k+1]), iterating backward — which is the **wrong** direction for an RTL line.

**Fix**: in the swap, check if the line direction is RTL, and if so, swap the opposite way. But `textArea()` doesn't know about line direction yet.

**Simplification**: remove the swap entirely. Let `start` and `end` be the iterators at the actual mouse positions. For highlight, the order doesn't matter. For `text()`, `m_words` is in reading order so the text is always correct.

Actually wait — there's a subtle issue. If the user drags from middle of line 1 to middle of line 2, the selection might be "backward" (in the user's intent). But Okular doesn't have a notion of "forward" vs "backward" selection — it just highlights the rect and extracts the text in `m_words` order. So we're fine.

**Decision**: 
- Keep the X-swap in `textArea()` (don't change it for now)
- The swap is fine because highlight is unordered
- `text()` and `selectedText()` work correctly because `m_words` is in reading order

### Step 6: Verify the test passes

After implementation, test:
1. Single Persian word, drag LTR: should copy word correctly
2. Single Persian word, drag RTL: should copy word correctly (m_words is in reading order, both drags select same m_words span)
3. Multi-word Persian line, drag LTR: should copy in reading order
4. Multi-line selection (LTR + RTL lines): each line in correct direction
5. Mixed LTR+RTL line: best-effort (LTR for now)

---

## Predicted Future Bugs and Mitigations

### Bug 1: Rotation 90/180/270 flips LTR/RTL axis
**Symptom**: After page rotation, the "LTR" line sort becomes the "vertical" sort.
**Detection**: A Persian line rotated 90° would be sorted incorrectly.
**Mitigation**: Apply rotation inverse before sorting. Or: detect rotation and swap the comparator. (Could be in scope if user reports, or punt to a separate fix.)
**Likelihood**: Medium. PDFs with rotated pages exist but are rare.

### Bug 2: Search direction interaction
**Symptom**: Search "find next" from a search hit might not work in expected reading order on RTL pages.
**Detection**: After Phase 1+2, search works for single words. Multi-line search direction (top-to-bottom) is still correct because lines are sorted by Y in `correctTextOrder()`.
**Mitigation**: Verify the search code uses the iterator order from `findTextInternalForward/Backward`. The `m_words` order is now reading order, so iterating forward is reading direction. ✓ Should be fine.
**Likelihood**: Low.

### Bug 3: Text selection rectangle merge side
**Symptom**: The `MergeSide` in `textArea()` is computed from `page->totalOrientation()`. For RTL selections, the merge direction might be wrong.
**Detection**: After Phase 1+2, the `RegularAreaRect` should still produce visually correct highlight (because rects are drawn in their original screen positions).
**Mitigation**: No change needed. The merge side is about how to combine adjacent rectangles, not their order. ✓
**Likelihood**: Low.

### Bug 4: Word boundary detection breaks for RTL
**Symptom**: `wordAt(point)` might return the wrong word for RTL text.
**Detection**: `wordAt()` iterates `m_words` to find the entity containing the point. With `m_words` in reading order, the point test `it->area().contains(p.x, p.y)` is purely spatial — works regardless of order. ✓
**Mitigation**: No change needed.
**Likelihood**: None.

### Bug 5: Highlight color broken for RTL
**Symptom**: The selection highlight color is wrong on RTL.
**Detection**: Highlight is drawn by `setHighlight` which uses the `RegularAreaRect`. Each rect is drawn at its original screen position. ✓
**Mitigation**: No change needed.
**Likelihood**: None.

### Bug 6: Generator other than Poppler breaks
**Symptom**: DjVu/EPUB/etc. generators might produce text in different orders, and our sort change might break them.
**Detection**: If those generators already produce LTR text (as our Phase 1 fix assumes), they'll continue to work with the LTR comparator. If they produce visual-order text, our LTR comparator will put it in spatial order — same as before. ✓
**Mitigation**: The change is in `correctTextOrder()` which is format-agnostic. The new RTL sort only kicks in when the line is detected as RTL.
**Likelihood**: Low.

### Bug 7: Mixed-line bidi reordering is incomplete
**Symptom**: A line like "Hello سلام World" might have its English words reversed.
**Detection**: With our v1 simplification, mixed lines are treated as LTR. So "Hello" and "World" stay in their spatial order (correct), but the Persian "سلام" word is internally reordered (correct). The result would be: "Hello سلام World" ✓ — but this is by accident. If a mixed line has more complex bidi (e.g., "X text Y متن Z"), the result would be wrong.
**Mitigation**: For v1, this is acceptable. For v2, implement proper per-run bidi reordering per line.
**Likelihood**: Medium. Real-world mixed documents exist.

### Bug 8: Same-line multiple words in different scripts
**Symptom**: "ABCD word1 word2 EFGH" where word1/word2 are Persian and the rest is English.
**Detection**: After correctTextOrder sorts this line as LTR (because of the English at edges), the words appear in m_words as: A, B, C, D, word1, word2, E, F, G, H. But word1 and word2 are internally in logical order (Phase 1). The user might select the whole line and expect: "ABCD word1 word2 EFGH" but the Persian words are internally reversed... wait no, our Phase 1 fix already reorders them internally. ✓
**Mitigation**: This should actually work correctly because Phase 1 handles within-word reordering, and Phase 2 handles per-line direction.
**Likelihood**: Low.

### Bug 9: Lines with same Y (multi-column layouts)
**Symptom**: A two-column document has left column ending and right column starting at the same Y. The XY-Cut algorithm should separate them, but the order might be wrong.
**Detection**: `XYCutForBoundingBoxes` uses spatial cuts. For a two-column layout, it should cut the page into two regions. Our sort change is per-line, so each line is sorted within itself. ✓
**Mitigation**: The XYCut is format-agnostic and our change is in the line-sort step. Should be fine.
**Likelihood**: Low.

### Bug 10: `addNecessarySpace` puts spaces between words
**Symptom**: After Phase 1+2, spaces between words in a multi-word selection might be in wrong direction.
**Detection**: `addNecessarySpace` appends a " " between words and a "\n" at the end. The order of words in the output is determined by `makeAndSortLines` (now correct) and the order of line emission from `XYCutForBoundingBoxes` (top-to-bottom, correct).
**Mitigation**: Verify the output of `addNecessarySpace` preserves the new word order. Look at the function: it iterates `RegionTextList tree` in tree order. Tree order is the XYCut order (top-to-bottom, left-to-right for each cut). For RTL lines, our words are now in reading order within the line, so iterating the line emits words in reading order. ✓
**Likelihood**: Low.

---

## Implementation Order

1. **Backup**: Create a feature branch in git
2. **Add helpers**: `LineDirection` enum, `detectLineDirection()`, `compareTinyTextEntityXReverse()`
3. **Modify `makeAndSortLines`**: Add per-line direction check, sort LTR or RTL accordingly
4. **Test single-word**: Build, test on Persian PDF
5. **Test multi-word**: Build, test multi-word selection
6. **Test multi-line**: Build, test selection across multiple lines
7. **Run existing tests**: `searchtest`, `calculatetexttest`
8. **Update README**: Document the new behavior
9. **Commit and push**

---

## Files to Create/Update

- `plan-selection.md` (already created) — this is the high-level plan
- `plan-selection-detail.md` (this file) — detailed implementation
- `NOTES.md` — running log of what I did, decisions made, problems encountered
- `core/textpage.cpp` — the code changes
- `README.md` — update with new behavior

---

## Estimated Effort

~6-10 hours of coding + testing. The plan is clear, the risk areas are identified, the code change is localized to one function in one file.
