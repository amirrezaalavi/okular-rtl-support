# Poppler RTL Text Handling: Research Report

## Executive Summary

Poppler has **NO ICU/fribidi integration** for proper visual-to-logical text reordering. The 2012 patch proposing ICU-based reordering (freedesktop bug #55977) was never merged in its ICU form. Instead, a simpler home-grown `reorderText()` function was implemented that uses Unicode Bidi Control characters (LRE/RLE/POPDF) and reverses RTL segments. **Critically, this reordering only runs in the `getText()`/`findText()` code path — NOT in the `textList()` path that Okular uses.** Okular receives text in visual order from Poppler and stores it as-is, with its own search implementation operating on visual-order text. This is the root cause of both the copy/paste reversal and the search-backwards bugs.

---

## 1. Bug History

### Freedesktop Bug 55977 — "handling of rtl text inversion is too naive"
- **URL:** https://bugs.freedesktop.org/show_bug.cgi?id=55977
- **Status:** RESOLVED FIXED
- **Product:** poppler / general
- **Timeline:** 2012-10-14 to ~2016 (20+ comments)
- **Key contributor:** Alex (proposed ICU/fribidi patch)
- **Poppler maintainer:** Albert Astals Cid

**What happened:**
1. Alex submitted patches adding ICU-based visual-to-logical reordering with fribidi fallback
2. Extensive discussion about API design (ReorderingMode enum, where to store it, cmake integration)
3. Alex couldn't complete cmake integration; maintainers pushed back on the enum complexity
4. ICU/fribidi approach was **abandoned**
5. Eventually, a simpler solution was merged: the `reorderText()` function + `primaryLR` auto-detection
6. The bug was marked RESOLVED FIXED, but the actual fix is the naive reorderText, NOT ICU

**Result:** The fix only applies to the `TextPage::dumpFragment` → `TextPage::getText()` path. The `textList()` path used by Okular was never fixed.

### Freedesktop Bug 2981 — "RTL select, copy/paste and search support for Arabic and Hebrew scripts are missing"
- **Status:** RESOLVED MOVED (migrated to GitLab)
- Was the original bug tracking RTL support

### Freedesktop Bug 105015 — "textList() reverse characters order for Arabic"
- **URL:** https://bugs.freedesktop.org/show_bug.cgi?id=105015
- **Status:** RESOLVED (migrated to GitLab)
- **GitLab:** https://gitlab.freedesktop.org/poppler/poppler/-/issues/412
- **GitLab status:** **STILL OPENED** (as of July 2026)
- **Title:** "textList() reverse characters order for Arabic"
- **Reporter:** Fahad Al-Saidi (2018-02-08)
- **Description:** `textList()` returns Arabic text like "ةبرجت" instead of "تجربة"
- **Comment 4:** "I think if we can run reorderText() through it, the problem will be fixed."

### KDE Bug 184399 — "copy and paste of hebrew text inverse the text flow of the selection"
- **URL:** https://bugs.kde.org/show_bug.cgi?id=184399
- **Status:** CONFIRMED (since 2009!)
- **Last modified:** 2025-02-10
- **Product:** okular / general
- **Duplicates:** 282850, 345512, 396757, 412487
- **Key comment #3:** "export to text file is generating a simple text file with the correct order of characters" — this is because pdftotext uses `getText()` which goes through `reorderText`

### KDE Bug 207748 — "LTR languages searches text backwards"
- **URL:** https://bugs.kde.org/show_bug.cgi?id=207748
- **Status:** CONFIRMED (since 2009!)
- **Last modified:** 2026-07-14 (comment #27: "I have the same issue, it still exists")
- **Product:** okular / general
- **Duplicates:** 282849, 331785, 386468, 429869, 442046, 457448, 504881
- **Key insight from comment #5 (Matitiahu Allouche):** "PDF stores glyphs in visual order. If your PDF viewer accepts user input in logical order, it should transform search arguments from logical to visual order before performing the search."
- **Key comment #10 (Fahad Al-Saidi):** "This bug needs retest against Poppler >= 0.40 because of this: https://bugs.freedesktop.org/show_bug.cgi?id=55977"
- **Comment #11:** Confirmed still broken with Poppler 0.42.0
- **Comment #15:** "The similar bug in Evince, Atril is fixed as of Ubuntu 16.04" — this is because Evince uses `getText()` path
- **Comment #20 (Fahad Al-Saidi):** "It is from TextPagePrivate::correctTextOrder(), it sorts words & characters to be LTR using compareTinyTextEntityY & compareTinyTextEntityX. This approach doesn't fit with RTL text."
- **Comment #21:** Patch proposed at https://phabricator.kde.org/D10455

---

## 2. Poppler's Current Text Reordering Implementation

### File: `poppler/TextOutputDev.cc`, `poppler/TextOutputDev.h`

### NO ICU/FriBiDi Integration
- **CMakeLists.txt:** No `find_package(ICU)` or `find_package(FriBiDi)` anywhere
- **No `#include <unicode/ubidi.h>`** or similar
- **No `HAVE_ICU` or `HAVE_FRIBIDI` defines**
- The only bidi-related functionality is a self-contained C++ function

### `primaryLR` — Auto-Detected Text Direction
**File:** `poppler/TextOutputDev.h` line 694
```cpp
bool primaryLR; // primary direction (true means L-to-R, false means R-to-L)
```

**Detection logic** (`TextOutputDev.cc` line 3300-3311):
```cpp
// Count LTR vs RTL characters across all words
for (word0 = line->words; word0; word0 = word0->next) {
    for (size_t i = 0; i < word0->len(); ++i) {
        if (unicodeTypeL(word0->chars[i].text)) {
            ++lrCount;
        } else if (unicodeTypeR(word0->chars[i].text)) {
            --lrCount;
        }
    }
}
primaryLR = lrCount >= 0;
```

This is set during `TextPage::coalesce()` (line 2839). If `rawOrder` is true, `primaryLR` is forced to `true` (LTR).

### `reorderText()` — The Core Reordering Function
**File:** `poppler/TextOutputDev.cc` line 205

```cpp
static int reorderText(const Unicode *text, int len, const UnicodeMap *uMap, 
                        bool primaryLR, GooString *s, Unicode *u)
```

**Algorithm:**
- Uses Unicode Bidi Control characters:
  - LRE (U+202A) — Left-to-Right Embedding
  - RLE (U+202B) — Right-to-Left Embedding
  - POPDF (U+202C) — Pop Directional Formatting

**When `primaryLR = true` (LTR document):**
1. Output LTR sections forward (no change)
2. Output RTL sections **reversed**, wrapped in RLE...POPDF

**When `primaryLR = false` (RTL document):**
1. Wrap everything in RLE...POPDF
2. Output RTL sections forward
3. Output LTR sections **reversed**, wrapped in LRE...POPDF

**Note:** "This code treats numeric characters (European and Arabic/Indic) as left-to-right, which isn't strictly correct (incurs extra LRE/POPDF pairs), but does produce correct visual formatting."

### `TextPage::dumpFragment()` — Where Reordering Happens
**File:** `poppler/TextOutputDev.cc` line 5462

```cpp
int TextPage::dumpFragment(const Unicode *text, int len, 
                           const UnicodeMap *uMap, GooString *s) const
{
    if (uMap->isUnicode()) {
        return reorderText(text, len, uMap, primaryLR, s, nullptr);
    }
    // Non-unicode: just output characters as-is
    int nCols = 0;
    char buf[8];
    int buflen = 0;
    for (int i = 0; i < len; ++i) {
        buflen = uMap->mapUnicode(text[i], buf, sizeof(buf));
        s->append(buf, buflen);
        nCols += buflen;
    }
    return nCols;
}
```

### `TextPage::findText()` — Search Also Uses ReorderText
**File:** `poppler/TextOutputDev.cc` line 3876

```cpp
bool TextPage::findText(...) {
    // ...
    reordered = static_cast<Unicode *>(gmallocn(len, sizeof(Unicode)));
    reorderText(s, len, nullptr, primaryLR, nullptr, reordered);
    s2 = unicodeNormalizeNFKC(reordered, len, &len, nullptr);
    // Then does the actual search using s2 (reordered + normalized)
}
```
This means **Poppler's own search reorders the query** to match visual-order text. Okular doesn't use this.

### `TextWord::getText()` — NO REORDERING!
**File:** `poppler/TextOutputDev.cc` line 814

```cpp
std::unique_ptr<std::string> TextWord::getText() const
{
    const UnicodeMap *uMap;
    char buf[8];
    auto s = std::make_unique<std::string>();
    if (!(uMap = globalParams->getTextEncoding())) {
        return s;
    }
    for (size_t i = 0; i < len(); ++i) {
        auto n = uMap->mapUnicode(chars[i].text, buf, sizeof(buf));
        s->append(buf, n);
    }
    return s;
}
```
**This is the critical function!** It outputs characters in the order they were stored in the PDF (visual order), with NO reordering at all. This is what feeds into the `textList()` API.

---

## 3. Poppler Version & Okular Integration

### Poppler Version
- **Current Poppler:** 26.07.90 (development)
- **File:** `CMakeLists.txt` lines 43-49
- **SO version:** 162

### Okular's Poppler Requirement
- **File:** `/home/amiralavi/okular-src/CMakeLists.txt` line 220
```cmake
find_package(Poppler "24.08.0" COMPONENTS Qt6)
```
- Okular targets **Poppler >= 24.08.0** with **Qt6 bindings**
- Uses `POPPLER_VERSION_MACRO` extensively for conditional compilation

---

## 4. How Okular Gets Text from Poppler

### Okular's Text Extraction Path
**File:** `generators/poppler/generator_pdf.cpp` line 1441

```cpp
Okular::TextPage *PDFGenerator::textPage(Okular::TextRequest *request)
{
    // ...
    std::vector<std::unique_ptr<Poppler::TextBox>> textList;
    textList = pp->textList(Poppler::Page::Rotate0, ...);
    // ...
    Okular::TextPage *tp = abstractTextPage(textList, pageHeight, pageWidth, ...);
    return tp;
}
```

### Poppler Qt6 `Page::textList()` Implementation
**File:** `qt6/src/poppler-page.cc` line 748

```cpp
std::vector<std::unique_ptr<TextBox>> Page::textList(...) const
{
    TextOutputDev output_dev(nullptr, false, 0, false, false);
    m_page->parentDoc->doc->displayPageSlice(&output_dev, ...);
    std::unique_ptr<TextWordList> word_list = output_dev.makeWordList();
    
    for (const TextWord *word : words) {
        const std::unique_ptr<std::string> wordText = word->getText();  // ← NO REORDERING
        const QString string = QString::fromUtf8(wordText->c_str());
        // ... creates TextBox with string + bounding boxes
    }
}
```

This calls `TextWord::getText()` — which does **NO reordering** at all.

### Poppler Qt6 `Page::search()` — Uses getText() Path (CORRECT)
**File:** `qt6/src/poppler-page.cc` line 713

```cpp
bool Page::search(const QString &text, ...) const
{
    QVector<Unicode> u;
    std::unique_ptr<TextPage> textPage = m_page->prepareTextSearch(text, rotate, &u);
    // This creates a TextOutputDev with rawOrder=true and calls displayPage()
    // Then uses textPage->findText() which DOES call reorderText()
    const bool found = PageData::performSingleTextSearch(textPage.get(), u, ...);
    return found;
}
```

Poppler's own `search()` works correctly because it uses the `TextPage::findText()` path that calls `reorderText()` on the query string.

**But Okular does NOT use `Poppler::Page::search()`!** It uses its own search.

### Okular's `abstractTextPage()` — Creates Okular TextPage
**File:** `generators/poppler/generator_pdf.cpp` line 1835

```cpp
Okular::TextPage *PDFGenerator::abstractTextPage(
    const std::vector<std::unique_ptr<Poppler::TextBox>> &text, 
    double height, double width, int rot)
{
    Okular::TextPage *ktp = new Okular::TextPage;
    for (const auto &word : text) {
        const int qstringCharCount = word->text().length();
        for (int j = 0; j < qstringCharCount; j++) {
            const QChar c = word->text().at(j);
            // ... surrogate pair handling ...
            QRectF charBBox = word->charBoundingBox(textBoxChar);
            append(ktp, s, charBBox.left()/width, charBBox.bottom()/height, ...);
        }
    }
    return ktp;
}
```

Characters are stored **in the order they arrive from `textList()`** — which is visual order. The append helper stores them in `Okular::TextPage`'s internal `m_words` list.

### Okular's Search — Own Implementation on Visual-Order Text
**File:** `core/textpage.cpp` line 549

```cpp
RegularAreaRect *TextPage::findText(int searchID, const QString &query, 
    SearchDirection direct, Qt::CaseSensitivity caseSensitivity, 
    const RegularAreaRect *area)
{
    // Searches through d->m_words list (TextEntity::List)
    // Uses findTextInternalForward() / findTextInternalBackward()
    // Compares query against text entities as they are stored
}
```

This search operates on text in **visual order** (as stored from Poppler's `textList()`). The query string from the user is in **logical order** (as typed). This mismatch is why RTL search requires typing backwards.

---

## 5. Summary of the Problem

### Two Separate Code Paths in Poppler

| Path | Used By | Reordering? | Result |
|------|---------|-------------|--------|
| `TextWord::getText()` → `textList()` | Okular (text extraction) | **NO** | Text in visual order |
| `TextPage::dumpFragment()` → `getText()` | pdftotext, Poppler::Page::text() | **Yes** (reorderText) | Text in logical order |
| `TextPage::findText()` | Poppler::Page::search() | **Yes** (reorderText) | Query reordered to match visual text |

### Okular's Problems
1. **Copy/paste reversed:** Okular extracts text via `textList()` which returns visual order. No reordering happens.
2. **Search broken:** Okular's own search compares logical-order query against visual-order stored text. No match unless user types backwards.
3. **Poppler's own search works correctly** but Okular doesn't use it.

### Root Cause
Okular chose to implement its own text page storage and search infrastructure (in `core/textpage.cpp`) rather than using Poppler's built-in search/text APIs. This was necessary for Okular to support multiple document formats uniformly, but it means Okular bypasses Poppler's `reorderText()` function.

---

## 6. Potential Fix Strategies

### Option A: Fix in Poppler's `textList()` Path
Modify `TextWord::getText()` or the Qt6 `Page::textList()` to apply `reorderText()` before returning text. This would fix the problem at the source for all Poppler consumers.
- **File to modify:** `poppler/TextOutputDev.cc` (TextWord::getText) or `qt6/src/poppler-page.cc` (Page::textList)
- **GitLab issue:** https://gitlab.freedesktop.org/poppler/poppler/-/issues/412 (still open)
- **Pros:** Fixes all Poppler consumers at once
- **Cons:** Requires changes to Poppler; wait for distro adoption

### Option B: Fix in Okular's `abstractTextPage()`
Apply bidi reordering (via Qt's built-in bidi support, ICU, or fribidi) in Okular's `abstractTextPage()` before storing text in the Okular TextPage.
- **File to modify:** `generators/poppler/generator_pdf.cpp` (abstractTextPage)
- **Pros:** Can be fixed purely in Okular; can use Qt's QString bidi reordering
- **Cons:** Only fixes Okular; need to handle word/character boundary correctly

### Option C: Use Poppler's `Page::text()` Instead of `textList()`
Switch Okular to use `Poppler::Page::text()` which goes through `TextOutputDev::getText()` → `dumpFragment()` → `reorderText()`. But this loses per-character bounding box information needed for selection.
- **Not viable** — Okular needs per-char bboxes for text selection

### Option D: Use Poppler's `Page::search()` for Search
For the search problem specifically, Okular could delegate search to `Poppler::Page::search()`. But this only fixes search, not copy/paste.
- **File to modify:** `core/document.cpp` (search flow)

### Recommendation
**Option B (fix in Okular's abstractTextPage)** is the most practical approach:
1. After getting text from Poppler's `textList()`, apply bidi reordering per-word using Qt's built-in bidi capabilities or a simple algorithm that reverses RTL segments within words
2. Map the reordered characters back to their original bounding boxes
3. Store reordered text in the Okular TextPage
4. This fixes both copy/paste AND search in one change

Alternatively, a simpler first step: in `abstractTextPage()`, for each word that is detected as RTL (using the `primaryLR` flag from Poppler or Qt's `QString::isRightToLeft()`), reverse the character order before storage. For mixed LTR/RTL words, proper bidi reordering is needed.

---

## Files of Interest

### Poppler Source (cloned at `/home/amiralavi/poppler-src/`)
| File | Lines | Purpose |
|------|-------|---------|
| `poppler/TextOutputDev.cc` | 5857 | Core text extraction: reorderText(), TextWord::getText(), TextPage::dumpFragment(), TextPage::findText(), TextPage::getText() |
| `poppler/TextOutputDev.h` | ~900 | TextPage class definition (primaryLR member at line 694) |
| `qt6/src/poppler-page.cc` | ~910 | Qt6 wrapper: Page::textList(), Page::search(), Page::text() |
| `qt6/src/poppler-qt6.h` | ~900 | Qt6 public API: TextBox, Page::textList declaration |
| `CMakeLists.txt` | ~1000 | Build system; NO ICU/fribidi integration |

### Okular Source (cloned at `/home/amiralavi/okular-src/`)
| File | Lines | Purpose |
|------|-------|---------|
| `generators/poppler/generator_pdf.cpp` | ~2300 | PDFGenerator::textPage(), abstractTextPage() — where Okular extracts text from Poppler |
| `core/textpage.cpp` | ~700 | Okular::TextPage::findText() — Okular's own search implementation |
| `core/textpage.h` | ~200 | Okular::TextPage, TextEntity class definitions |
| `core/textpage_p.h` | ~200 | TextPagePrivate with m_words list |
| `core/document.cpp` | ~2000 | Document::searchText(), doContinueDirectionMatchSearch() |
| `CMakeLists.txt` line 220 | | `find_package(Poppler "24.08.0" COMPONENTS Qt6")` |

### Key Function Call Chain
```
Okular PDFGenerator::textPage()
  → Poppler::Page::textList()
    → TextOutputDev (displayPageSlice)
    → TextOutputDev::makeWordList()
      → TextWord::getText()  ← NO REORDERING! Characters in visual order
  → PDFGenerator::abstractTextPage()
    → Okular::TextPage::append()  ← Stores in visual order
      → Okular TextPage::findText()  ← Searches in visual order
```

```
Poppler::Page::search()  ← NOT used by Okular
  → TextOutputDev (displayPage)
  → TextPage::findText()
    → reorderText()  ← Reorders query to match visual-order text
```
