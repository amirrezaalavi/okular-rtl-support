# Working Notes — RTL Selection Fix
**Date:** 2026-07-24
**Status:** Planning complete, implementation starting

---

## Session Log

### Step 0 — Confirmed the bug exists
**Date:** 2026-07-24
**Action:** Dispatched subagent to research. Manually tested with real Persian PDF.

**Findings:**
- Bug 345512 already exists as duplicate of 184399
- Real test: dragging over Persian word `شبیه‌سازی` produces `یزاسهیبش` (visual order)
- Both drag directions produce the same backwards result
- Even with Phase 1 fix, multi-word selections are still wrong

**Decision:** Go with **Option 4** — make `m_words` truly in reading order, by modifying `correctTextOrder()` to sort lines based on their detected direction.

### Step 1 — Identified the fix point
**File:** `core/textpage.cpp::makeAndSortLines()` line 1115-1117

Current code:
```cpp
for (QPair<WordsWithCharacters, QRect> &line : lines) {
    WordsWithCharacters &list = line.first;
    std::sort(list.begin(), list.end(), compareTinyTextEntityX);
}
```

This sorts every line left-to-right regardless of direction. Need to add a per-line direction check.

### Step 2 — Designed the algorithm
**Per-line direction detection:**
- Scan all characters in the line
- Count strong RTL (`DirR`, `DirAL`) and strong LTR (`DirL`) characters
- If RTL count dominates → RTL line → sort in reverse
- If LTR count dominates or tie → LTR line → sort as today
- Mixed case → use LTR (simplification for v1)

**Per-line comparator:**
- LTR: `compareTinyTextEntityX` (ascending left)
- RTL: NEW `compareTinyTextEntityXReverse` (descending left)

**Why per-line, not per-page:**
- Mixed-direction documents (Persian with English numbers/code) are common
- A page can have RTL header, LTR body, RTL sidebar
- Each line has its own direction

### Step 3 — Predicted bugs (10 of them, with mitigations)
See `plan-selection-detail.md` section "Predicted Future Bugs and Mitigations" for full list.

Top 3 to watch:
1. **Mixed-line bidi reordering is incomplete** (Bug 7) — acceptable for v1
2. **Page rotation flips LTR/RTL axis** (Bug 1) — punt to separate fix
3. **Generator other than Poppler** (Bug 6) — should be fine because the sort is format-agnostic

### Step 4 — Files to modify
- `core/textpage.cpp` — add helpers, modify `makeAndSortLines`
- `core/textpage.h` — no change
- `core/textpage_p.h` — no change
- `generators/poppler/generator_pdf.cpp` — no change (Phase 1 fix stays)
- `part/pageview.cpp` — no change

### Step 5 — Implementation plan
1. Add `LineDirection` enum and `detectLineDirection()` helper
2. Add `compareTinyTextEntityXReverse()` helper
3. Modify `makeAndSortLines` to use direction-aware sort
4. Build, test
5. Run regression tests
6. Commit

### Step 6 — Cross-checked with industry
**Date:** 2026-07-24
**Action:** Dispatched subagent to research other PDF readers.

**Findings:**
- **PDF.js** uses per-text-item `dir` field + browser native bidi. Full UBA in `bidi.js` (454 lines).
- **Poppler** uses per-page `primaryLR` flag + `reorderText()` (only on CLI/findText). Qt6 binding is the broken path.
- **MuPDF** has full UBA in C (~2000 lines of bidi code).
- **Sumatra PDF** uses MuPDF + proper anchor/focus model (wordStart/wordEnd + PosBefore()).
- **Evince** inherits Poppler behavior.

**Key insight**: No major reader does what I was planning (reordering at storage layer). They either:
1. Store per-item direction metadata (PDF.js)
2. Branch the selection algorithm on per-page direction (Poppler)
3. Reorder at read time (MuPDF)

**Annotation impact**: ZERO. Annotations use plain `QString` + Qt's text engine. No interaction with `m_words`.

**Revised recommendation**:
- **v1**: per-line sort inversion (Option 4) — same plan, faster to ship
- **v2**: per-TextEntity direction field — matches PDF.js, most correct

The original plan still stands for v1. Industry research validated the approach direction.
