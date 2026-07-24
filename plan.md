# Okular RTL Text Fix — Implementation Plan
**Status:** In Progress
**Last Updated:** 2026-07-24

---

## Problem Summary
PDF stores text in **visual (display) order**. For RTL scripts (Arabic, Persian, Hebrew), this means characters are stored left-to-right instead of right-to-left. Poppler's Qt6 binding returns this visual-order text. Okular stores and uses it as-is, causing:
1. **Copy bug**: Copied RTL text is backwards
2. **Search bug**: Must type RTL text backwards to find it

---

## Fix Strategy

### Chosen Approach: Fix at the character level in `abstractTextPage()`

**Rationale:**
- Poppler gives us words (`TextBox`) as units — we can reorder within each word
- Only one function to modify
- Fixes both copy and search simultaneously
- No new dependencies needed (uses Qt6's `QChar::direction()`)

### What We Do NOT Do:
- Do NOT modify Poppler upstream (too slow, out of scope)
- Do NOT add new dependencies (ICU, fribidi)
- Do NOT change the search algorithm
- Do NOT modify the copy/paste code directly

**Instead: fix the text representation at the source, and all downstream operations just work.**

---

## Implementation Steps

### Phase 1: Add Bidi Reordering Utility

Create a utility function that reorders characters within a word from visual to logical order.

**File:** `core/textpage.cpp` (add as static function near the top)

**Algorithm:** (based on Poppler's `reorderText()` in `TextOutputDev.cc:205`)

```cpp
// Determines if a QChar has RTL direction (Arabic, Hebrew, etc.)
static bool isRtlChar(QChar c) {
    switch (c.direction()) {
    case QChar::DirR:      // Right-to-Left (Hebrew, etc.)
    case QChar::DirAL:     // Right-to-Left Arabic
        return true;
    default:
        return false;
    }
}

// Determines if a QChar has LTR direction
static bool isLtrChar(QChar c) {
    switch (c.direction()) {
    case QChar::DirL:      // Left-to-Right
        return true;
    default:
        return false;
    }
}

// Reorder a word from visual to logical order using Unicode Bidi Algorithm
// Returns the reordered string
static QString reorderVisualToLogical(const QString &visual) {
    const int len = visual.length();
    if (len <= 1) return visual;
    
    // Check if string contains any RTL characters
    bool hasRtl = false;
    for (int i = 0; i < len; i++) {
        if (isRtlChar(visual.at(i))) {
            hasRtl = true;
            break;
        }
    }
    if (!hasRtl) return visual;  // LTR-only: no change needed
    
    // Determine primary direction: if first strong char is RTL, primary is RTL
    bool primaryRtl = false;
    for (int i = 0; i < len; i++) {
        QChar::Direction d = visual.at(i).direction();
        if (d == QChar::DirR || d == QChar::DirAL) {
            primaryRtl = true;
            break;
        }
        if (d == QChar::DirL) {
            break;
        }
    }
    
    QString result;
    result.reserve(len);
    
    int i = 0;
    if (!primaryRtl) {
        // Primary LTR: scan forward, output LTR as-is, reverse RTL runs
        while (i < len) {
            // Output LTR run as-is
            while (i < len && !isRtlChar(visual.at(i))) {
                result += visual.at(i);
                i++;
            }
            // Reverse RTL run
            int j = i;
            while (j < len && isRtlChar(visual.at(j))) {
                j++;
            }
            if (j > i) {
                for (int k = j - 1; k >= i; k--) {
                    result += visual.at(k);
                }
                i = j;
            }
        }
    } else {
        // Primary RTL: scan backward, output RTL runs as-is (reversed),
        // reverse LTR runs
        i = len - 1;
        while (i >= 0) {
            // Output RTL run: already in correct relative order
            // (last character in visual = first in logical)
            while (i >= 0 && isRtlChar(visual.at(i))) {
                result += visual.at(i);
                i--;
            }
            // Reverse LTR run
            int j = i;
            while (j >= 0 && !isRtlChar(visual.at(j))) {
                j--;
            }
            if (j < i) {
                for (int k = j + 1; k <= i; k++) {
                    result += visual.at(k);
                }
                i = j;
            }
        }
    }
    
    // Guard: apply Bidi control characters for proper rendering
    // Use U+202B (RLE) for RTL segments, U+202A (LRE) for LTR segments
    // This ensures the text renders correctly when pasted
    
    return result;
}
```

### Phase 2: Integrate into `abstractTextPage()`

**File:** `generators/poppler/generator_pdf.cpp`

**Function:** `PDFGenerator::abstractTextPage()` (line 1835)

**Change:** Before appending each word's characters, pass the word text through `reorderVisualToLogical()`.

Current code (pseudocode):
```cpp
for (const auto &word : text) {
    for each char in word->text():
        append(char, bbox);
}
```

New code:
```cpp
auto reorderVisualToLogical = ...; // or include from core/textpage.cpp

for (const auto &word : text) {
    QString reorderedText = reorderVisualToLogical(word->text());
    // iterate reorderedText instead of word->text()
    // use the charBoundingBox indices mapped to reordered positions
    for each char in reorderedText:
        append(char, mappedBBox);
}
```

**Important detail**: The character bounding boxes from Poppler (`charBoundingBox()`) are indexed by visual position. When we reorder characters, we need to map the visual index to the logical index so each character still gets the correct bounding box for selection highlighting.

### Phase 3: Fix Text Export

**File:** `generators/poppler/generator_pdf.cpp`

**Function:** `exportTo()` (around line 1801)

Also apply reordering to the text export path which uses `pp->text()`.

### Phase 4: Add Proper Unicode Bidi Markers

For the final output strings (returned from `TextPage::text()`), add Unicode bidi control characters:
- U+202B (RLE) before RTL segments
- U+202C (PDF) to pop direction
- This ensures pasted text renders correctly in other applications

---

## Files to Modify

| File | Function | Change |
|------|----------|--------|
| `core/textpage.cpp` | (new static function) | Add `reorderVisualToLogical()` |
| `generators/poppler/generator_pdf.cpp` | `abstractTextPage()` | Apply reordering per word |
| `generators/poppler/generator_pdf.cpp` | `exportTo()` | Apply reordering to text export |

---

## Detailed Change Plan (generator_pdf.cpp:1835)

### Before:
```cpp
for (const auto &word : text) {
    const int qstringCharCount = word->text().length();
    next = word->nextWord();
    int textBoxChar = 0;
    for (int j = 0; j < qstringCharCount; j++) {
        const QChar c = word->text().at(j);
        // ... surrogate handling ...
        if (addChar) {
            QRectF charBBox = word->charBoundingBox(textBoxChar);
            append(ktp, ..., charBBox ...);
            textBoxChar++;
        }
    }
}
```

### After:
```cpp
// Forward declare or include the reorder function
static QString reorderVisualToLogical(const QString &visual);

for (const auto &word : text) {
    // Reorder word text from visual to logical order
    const QString wordText = word->text();
    const QString reorderedText = reorderVisualToLogical(wordText);
    const int qstringCharCount = reorderedText.length();
    
    // Build visual->logical index mapping for bounding boxes
    // The charBoundingBox() uses visual indices
    // We need to map each logical position to its visual index
    QVector<int> logicalToVisual;
    // (build mapping based on how reorderVisualToLogical works)
    
    next = word->nextWord();
    for (int j = 0; j < qstringCharCount; j++) {
        const QChar c = reorderedText.at(j);
        // ... surrogate handling (same as before) ...
        if (addChar) {
            int visualIndex = logicalToVisual.value(j, j);
            QRectF charBBox = word->charBoundingBox(visualIndex);
            append(ktp, ..., charBBox ...);
        }
    }
}
```

### Index Mapping Strategy:
Rather than building a complex mapping, we can use a simpler approach:
- Pass the word through `reorderVisualToLogical()` to get the final text
- The `reorderVisualToLogical()` function returns BOTH:
  - The reordered string
  - An array mapping logical→visual index positions
- This way character bounding boxes maintain their correct spatial positions

---

## Risk Analysis

| Risk | Mitigation |
|------|------------|
| Breaking LTR text handling | Always check `hasRtl` flag; LTR-only text passes through unchanged |
| Wrong char bounding box mapping | Build explicit visual→logical index map |
| Performance regression | Only trigger reordering when RTL chars detected; O(n) algorithm |
| Breaking existing search | Search works on reordered text; should be MORE correct now |
| Mixed RTL/LTR text corruption | Use Unicode bidi algorithm (handles mixed runs correctly) |

---

## Test Plan

### Test Cases
1. **Pure English text**: Copy and search should work unchanged
2. **Pure Persian text**: Copy should produce correct right-to-left string
3. **Mixed Persian+English**: "Hello سلام World" should read correctly
4. **Search Persian**: Typing "سلام" should find it (without reversing)
5. **Search English in Persian doc**: Should still work
6. **Multi-page selection**: Copy across pages with RTL text
7. **Hebrew text**: Same as Persian (different Unicode block)
8. **Arabic text**: Same as Persian with additional shaping considerations

### Regression Tests
- Run existing test suite: `ctest` in build directory
- Manual copy/paste from Okular to text editor
- Search functionality for LTR text

---

## Build & Test Commands

```bash
mkdir -p ~/playground/gits/okular-rtl/build
cd ~/playground/gits/okular-rtl/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc) okularGenerator_poppler  # build just the PDF generator
# OR
make -j$(nproc)  # full build
```

---

## Iteration Loop

1. Make a change
2. Build
3. Test with RTL PDF documents
4. If issue found → update this plan → go to step 1
5. Commit working change
