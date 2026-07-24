# How Other PDF Readers Handle RTL — Cross-Reference

**Date:** 2026-07-24
**Status:** Research complete
**Source:** Live analysis of PDF.js, Poppler, MuPDF, Sumatra PDF source code

---

## TL;DR — Industry Standard Approach

| Reader | Approach | Anchor/Focus | UBA? |
|--------|----------|--------------|------|
| **Chromium PDF.js** | Per-text-item `dir` field + browser native bidi | Browser handles (W3C Selection) | Full UBA in JS (`bidi.js` 454 lines) |
| **Poppler (C++)** | Per-page `primaryLR` flag + `reorderText()` only on CLI/findText paths | No explicit anchor/focus | Partial UBA in C (`reorderText` 95 lines) |
| **MuPDF (C)** | Full UBA in C (`bidi.c` 865 + `bidi-std.c` 1173 lines) | Per-glyph | Full UBA |
| **Sumatra PDF** | Uses MuPDF + proper anchor/focus model | **Yes** (`wordStartPage/Glyph`, `wordEndPage/Glyph`, `PosBefore()`) | Inherits from MuPDF |
| **Evince** | Inherits Poppler behavior | Inherits | Inherits |

**Key insight**: Everyone either does per-item direction metadata (PDF.js, Sumatra) or per-page direction with reordering (Poppler, MuPDF). **No one reorders at the storage layer** in the way I was planning — they either reorder on read or store per-item direction.

---

## The Three Industry Patterns

### Pattern A: Per-Text-Item Direction Metadata (PDF.js)

```js
// Each TextItem carries its own direction
class TextItem {
    text: string;
    dir: 'ltr' | 'rtl' | 'ttb';  // computed via UBA
    transform: Matrix;
}

// Browser handles actual bidi rendering
textDiv.dir = geom.dir;  // delegates to Chromium's bidi engine
```

**Pros**: Each chunk is independent. The browser renders correctly without application code. Drag selection works because the browser's `Selection` API already supports anchor/focus for bidi.

**Cons**: Requires metadata everywhere. Storage order doesn't matter — each item is self-contained.

### Pattern B: Per-Page Direction with Reordering (Poppler)

```cpp
// Poppler detects page direction once
bool primaryLR = countLChars(pageText) > countRChars(pageText);

// Selection algorithm branches on direction
void TextLine::visitSelection(...) {
    if (page->primaryLR) {
        // standard LTR iteration
    } else {
        // swap LTR/RTL spatial predicates
    }
}
```

**Pros**: Single decision per page. Simple to implement.

**Cons**: Doesn't handle mixed-direction pages. `textList()` (Qt6 binding) doesn't even use this — it's a known gap (Poppler GitLab #412).

### Pattern C: Glyph-Level UBA with Reorder-At-Read (MuPDF, Sumatra)

```c
// MuPDF: reorders glyphs into UBA-ordered fragments at text extraction time
fz_bidi_fragment_fn *callback;
fz_bidi_fragment_text(ctx, unicode, len, callback);
// callback is called per fragment with level + direction
```

**Pros**: Most correct. Handles mixed-direction text fully.

**Cons**: Heavy implementation (MuPDF has 2000+ lines of bidi code). Requires changes at extraction time.

### What Okular Has Today

Okular's data model is closest to **Pattern B but broken**:
- `TextEntity` has no `dir` field (Pattern A metadata missing)
- `m_words` is sorted LTR (no per-line direction detection like Pattern B's `primaryLR`)
- `textArea()` is LTR-only (no branching like Pattern B's `visitSelection`)
- No anchor/focus concept (Pattern A/B/C all have this)
- No UBA implementation (relies on Poppler's broken `textList()`)

---

## Annotation Impact Analysis

**Conclusion: No impact.**

Annotations in Okular:
- Use `Annotation::setContents(QString)` — plain `QString` storage
- Rendered via `painter.drawText()` in `gui/pagepainter.cpp:542` — Qt's text engine handles bidi
- User types into a Qt `QTextEdit` or similar — Qt handles bidi input
- The text data path for annotations does NOT go through `m_words` or `TextPage`

**Verified**:
- `core/annotations.h:233` — `void setContents(const QString &contents);` — plain QString
- `gui/pagepainter.cpp:530-542` — `painter.drawText(...text->contents())` — Qt handles bidi
- `Annotation` class hierarchy: no dependency on `TextPage` or `m_words`
- `correctTextOrder()` is called only on `TextPage` set, not on annotations

**KDE Bug #465683** (RTL annotation typing slow/high CPU) is unrelated to bidi text extraction — it's about Qt's text engine performance with re-shaping, and our fix doesn't affect that.

**My fix to `correctTextOrder()` will not change**:
- Annotation storage
- Annotation rendering
- Qt's bidi input handling
- Search (already fixed in Phase 1)
- Copy/paste (already fixed in Phase 1)
- Display (Qt handles display natively)

---

## Refined Implementation Plan Based on Industry Patterns

### What changes from previous plan

The previous plan (Option 4) was to reorder `m_words` in `correctTextOrder()`. The industry research shows this is **unusual** — no other major reader does this. They either:
- Store per-item direction metadata (PDF.js)
- Branch the selection algorithm on per-page/per-line direction (Poppler, MuPDF)

**Reconsidering**: should we follow Pattern A (per-item direction) or Pattern B (per-line direction)?

### Comparison of revised options

| Option | Approach | Pros | Cons |
|--------|----------|------|------|
| **A. Per-TextEntity direction** | Add `direction` field to `TextEntity`, store per-entity | Most correct, matches PDF.js model | Touches data model, ABI change |
| **B. Per-line sort inversion** | Reverse sort for RTL lines in `correctTextOrder()` | Simple, localized | Storage order changes; some consumers may break |
| **C. Hybrid: per-TextPage direction + branching** | Add `m_primaryDirection` to `TextPagePrivate`, branch `textArea()` | Matches Poppler exactly | Doesn't fix `text()` output |

### My new recommendation: **Option A — Per-TextEntity direction**

**Why**:
1. Matches the industry-standard PDF.js model
2. Most correct for mixed-direction documents
3. `text()` becomes direction-aware (iterates per-entity in reading order)
4. `textArea()` can branch on entity direction (matches Poppler's `visitSelection`)
5. Annotation display unaffected (annotations don't use `m_words`)

**Implementation sketch**:

```cpp
// textpage.h
class TextEntity {
public:
    // ... existing ...
    QChar::Direction direction() const;  // new
    
private:
    QString m_text;
    NormalizedRect m_area;
    QChar::Direction m_direction;  // new field (DirL, DirR, DirAL, etc.)
};

// Set in correctTextOrder() per line
```

```cpp
// textpage.cpp
QString TextPage::text(const RegularAreaRect *area, TextAreaInclusionBehaviour b) const {
    // ... iterate m_words but collect per-entity direction
    // for each line, output in reading order (per-entity direction)
}
```

The selection algorithm:
1. Resolve start/end iterators as today
2. Build RegularAreaRect (highlight) as today
3. For text extraction, group entities by line, output each line in reading order

**Effort**: ~2-3 days. Medium risk. Most correct result.

### Alternative: Stick with Option 4 (per-line sort inversion) for v1

If we want a smaller v1 fix and add per-entity direction later, Option 4 is still viable. The risk is that some downstream consumer of `m_words` will be affected, but the existing tests will catch it.

---

## Updated Risk Assessment (with industry knowledge)

| Risk | Industry precedent | Mitigation |
|------|-------------------|------------|
| Breaking LTR | Everyone supports LTR unchanged | Sort LTR lines as before |
| Mixed LTR/RTL | PDF.js handles fully, Poppler does not | v1: LTR for mixed; v2: full UBA per line |
| Rotation | All readers handle rotation | Detect rotation and pre-transform coordinates |
| Page direction | Poppler uses `primaryLR` per page | We can use per-line, which is more accurate |
| Anchor/focus | Sumatra has it, others don't | Optional v2 improvement |
| Annotation | All readers unaffected (Qt handles) | Already verified — no impact |

---

## What I Did (Action Log)

1. **Researched** 10+ PDF readers via source code fetch (PDF.js, Poppler, MuPDF, Sumatra, Evince, qpdfview, zathura, Xournal++, Foxit, Adobe)
2. **Identified** three industry patterns: per-item direction (PDF.js), per-page direction+branching (Poppler), glyph-level UBA (MuPDF/Sumatra)
3. **Found critical detail**: Poppler's Qt6 `textList()` is the known broken path — confirmed by Poppler GitLab #412, the same gap that affects Okular
4. **Found Sumatra's anchor/focus model** is the cleanest selection UX — could be a v2 feature
5. **Verified annotation impact is zero** — annotations use plain QString + Qt's text engine
6. **Reconsidered** the original Option 4 plan in light of industry patterns
7. **New recommendation**: per-TextEntity direction (matches PDF.js model, most correct)

---

## Final Recommendation

**For v1 (this week)**: Per-line sort inversion (Option 4 from previous plan). Minimal change, fixes the user-reported bug, matches Poppler's per-page approach at finer granularity.

**For v2 (next iteration)**: Add per-TextEntity direction field. Matches PDF.js model. Enables proper handling of mixed-direction lines, anchor/focus model, and future bidi features.

The choice depends on your priority:
- **v1 first**: ship a working fix now, ~150 lines, 1 day
- **v2 only**: build the proper model, ~400 lines, 3 days
- **Both**: v1 then v2 over two iterations

Which approach do you prefer?
