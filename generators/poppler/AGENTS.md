# PDF Generator (Poppler) — Architecture & Text Extraction Pipeline

## Overview

The `generators/poppler/` directory contains the **Okular PDF backend**, which bridges the [Poppler](https://poppler.freedesktop.org/) PDF rendering library (Qt6 bindings) to the Okular document framework. It implements `Okular::Generator` and several interfaces (`ConfigInterface`, `PrintInterface`, `SaveInterface`) to provide full PDF support: rendering, text extraction, annotations, forms, signatures, printing, and saving.

### Key Files

| File | Purpose |
|------|---------|
| `generator_pdf.h` / `.cpp` | Main generator: document loading, text extraction, rendering, export, printing |
| `annots.h` / `.cpp` | Conversion of Poppler annotations → Okular annotations |
| `formfields.h` / `.cpp` | Conversion of Poppler form fields → Okular form fields |
| `pdfsignatureutils.h` / `.cpp` | Digital signature support |
| `imagescaling.h` / `.cpp` | Image downscaling/pixelation logic |
| `CMakeLists.txt` | Build: links against `Poppler::Qt6`, `okularcore`, Qt6, KF6 |

### Dependencies

```
Poppler::Qt6          — PDF parsing/rendering (the Poppler Qt6 C++ wrapper)
okularcore            — Okular's core data types (TextPage, Page, Generator)
Qt6::Core, Qt6::Gui   — Qt fundamentals
KF6::I18n, KF6::KIOWidgets, KF6::Completion — KDE Frameworks
Qt6::Xml              — For metadata parsing
```

---

## Document Loading Pipeline

```
PDF File / Byte Array
        │
        ▼
loadDocumentWithPassword() / loadDocumentFromDataWithPassword()
        │
        ├─► Poppler::Document::load(filePath)     ← creates Poppler doc
        ├─► Poppler::Document::loadFromData(data)  ← alt path from memory
        │
        ▼
PDFGenerator::init(pagesVector, password)
        │
        ├─► pdfdoc->unlock(password)               ← handle encrypted PDFs
        ├─► Parse XMP metadata (HasVisibleOverprint)
        ├─► pdfdoc->numPages()                     ← page count
        │
        ▼
PDFGenerator::loadPages(pagesVector)
        │
        ├─► For each page:
        │     pdfdoc->page(i)                      ← Poppler::Page
        │     p->pageSizeF()                       ← dimensions in points
        │     p->orientation()                     ← Portrait/Landscape/etc.
        │     new Okular::Page(i, w, h, rot)       ← Okular page object
        │     addTransition(), addAnnotations()    ← metadata
        │     p->action(Opening/Closing)           ← page actions
        │
        ▼
    Okular::Document with populated Page vector
```

**Key detail**: Page dimensions from Poppler are in **points** (1/72 inch). They are converted to pixels using `dpi().width()` / `dpi().height()` in `loadPages()`.

---

## Text Extraction Pipeline (THE CRITICAL PATH)

This section covers how text flows from a PDF page through Poppler's text extraction API into Okular's `TextPage` data structure. **This is the pipeline with the RTL/visual-order bug.**

### Step 1: Okular requests a TextPage

```cpp
// generator_pdf.cpp:1441
Okular::TextPage *PDFGenerator::textPage(Okular::TextRequest *request)
```

This is the protected override of `Generator::textPage()`. Okular's framework calls this when it needs text for a page (for search, selection, or copy).

### Step 2: Call Poppler to extract text boxes

```cpp
// generator_pdf.cpp:1448-1458
std::vector<std::unique_ptr<Poppler::TextBox>> textList;
std::unique_ptr<Poppler::Page> pp = pdfdoc->page(page->number());
textList = pp->textList(Poppler::Page::Rotate0, callback, payload);
```

`Poppler::Page::textList()` returns a **vector of `Poppler::TextBox` objects**. Each `Poppler::TextBox` represents a **word** rendered on the PDF page, with:

| Poppler::TextBox Member | Type | Description |
|--------------------------|------|-------------|
| `text()` | `QString` | The word's text content |
| `boundingBox()` | `QRectF` | The word's bounding box (in points) |
| `charBoundingBox(i)` | `QRectF` | Individual character bounding box (in points) |
| `nextWord()` | `Poppler::TextBox*` | Pointer to the next word (linked list traversal) |
| `hasSpaceAfter()` | `bool` | Whether there's a space after this word |

**⚠️ CRITICAL: Poppler returns words in VISUAL ORDER (left-to-right on the page).** For RTL scripts (Arabic, Hebrew, Persian, etc.), the words appear in the order they are rendered on the page, NOT in logical reading order. This is the root cause of the RTL text bug.

### Step 3: Convert Poppler TextBoxes → Okular TextEntities

```cpp
// generator_pdf.cpp:1835
Okular::TextPage *PDFGenerator::abstractTextPage(
    const std::vector<std::unique_ptr<Poppler::TextBox>> &text,
    double height, double width, int rot)
```

This function builds an `Okular::TextPage` by iterating over every `Poppler::TextBox`. **The rotation parameter `rot` is accepted but UNUSED** (`Q_UNUSED(rot)`), meaning text is never reoriented based on page rotation.

#### Character-by-character iteration:

```cpp
for (const auto &word : text) {
    const int qstringCharCount = word->text().length();
    next = word->nextWord();
    int textBoxChar = 0;

    for (int j = 0; j < qstringCharCount; j++) {
        const QChar c = word->text().at(j);
        // Handle surrogate pairs (emoji, some CJK)
        if (c.isHighSurrogate()) { s = c; addChar = false; }
        else if (c.isLowSurrogate()) { s += c; addChar = true; }
        else { s = c; addChar = true; }

        if (addChar) {
            QRectF charBBox = word->charBoundingBox(textBoxChar);
            append(ktp,
                (j == qstringCharCount - 1 && !next)
                    ? (s + QLatin1Char('\n'))   // last char of last word: +newline
                    : s,
                charBBox.left() / width,    // normalized coords (0.0–1.0)
                charBBox.bottom() / height,
                charBBox.right() / width,
                charBBox.top() / height);
            textBoxChar++;
        }
    }

    // Add space between words if hasSpaceAfter() and there IS a next word
    if (word->hasSpaceAfter() && next) {
        QRectF wordBBox = word->boundingBox();
        QRectF nextWordBBox = next->boundingBox();
        append(ktp, QStringLiteral(" "),
            wordBBox.right() / width,
            wordBBox.bottom() / height,
            nextWordBBox.left() / width,
            wordBBox.top() / height);
    }
}
```

#### What each iteration produces:

For each character in each word, the function:
1. Extracts the character's text (`word->text().at(j)`)
2. Gets its **individual bounding box** via `word->charBoundingBox(textBoxChar)`
3. **Normalizes** the coordinates from points to 0.0–1.0 range (dividing by `width` and `height`)
4. Appends an `Okular::TextEntity(text, NormalizedRect)` to the `TextPage`
5. If it's the **last character of the last word**, appends a `"\n"` character
6. Between words with `hasSpaceAfter()`, inserts a `" "` character with an interpolated bounding box

#### The `append()` helper:

```cpp
inline void append(Okular::TextPage *ktp, const QString &s,
                   double l, double b, double r, double t) {
    ktp->append(s, Okular::NormalizedRect(l, t, r, b));
}
```

Note: `NormalizedRect` is constructed with (left, top, right, bottom) — but since PDF coordinates have Y=0 at the bottom, `bottom` in PDF maps to `top` in screen coordinates, and vice versa. The `append()` function does a flip: `(l, b, r, t)` → `NormalizedRect(l, t, r, b)`.

### Step 4: TextPage is installed on the Page

```cpp
// core/page.cpp:612
void Page::setTextPage(TextPage *textPage) {
    d->m_text = textPage;
    d->m_text->d->m_page = this;
    d->m_text->d->correctTextOrder();  // ← layout reordering
}
```

When the `TextPage` is attached to an `Okular::Page`, `correctTextOrder()` is automatically called. This function performs **geometric layout analysis** — NOT bidi reordering.

### Step 5: correctTextOrder() — Geometric Layout Analysis

```cpp
// core/textpage.cpp:1623
void TextPagePrivate::correctTextOrder()
```

This function reorganizes the flat list of character `TextEntity` objects into a proper reading order using the **XY-Cut algorithm**:

```
Input: Flat list of per-character TextEntity objects (m_words)
       │
       ▼
1. removeSpace(characters)
   — Removes standalone space TextEntities (they will be re-added later)
       │
       ▼
2. makeWordFromCharacters(characters, pageWidth, pageHeight)
   — Groups consecutive characters into words using horizontal adjacency
   — Returns QList<WordWithCharacters> (word + its constituent characters)
       │
       ▼
3. XYCutForBoundingBoxes(wordsWithCharacters, pageWidth, pageHeight)
   — Recursive XY-Cut tree building using projection profiles
   — Splits regions horizontally (lines) and vertically (columns)
   — Returns RegionTextList (hierarchical regions of words)
       │
       ▼
4. addNecessarySpace(tree, pageWidth, pageHeight)
   — Sorts words within each line by X position (makeAndSortLines)
   — Inserts space characters between non-adjacent words
   — Flattens back to a single TextEntity::List
       │
       ▼
5. setWordList(listOfCharacters)
   — Stores the reordered list as m_words (the canonical word list)
```

**⚠️ IMPORTANT: `correctTextOrder()` does NOT perform bidirectional (bidi) text reordering.** It is a purely geometric algorithm that:
- Groups characters by horizontal proximity (left → right)
- Sorts lines by vertical position (top → bottom)
- Sorts words within a line by X position (left → right)

For RTL scripts stored in visual order (left-to-right), this means the logical reading order is **never recovered**. Characters within an RTL word, and RTL words within a line, remain in visual (reversed) order.

### Step 6: Text Selection and Search

After `correctTextOrder()`, the `m_words` list is used for:
- **Text search** (`findText()`): scans `m_words` sequentially
- **Text selection** (`textArea()`, `text()`): extracts text from `m_words` within a region
- **Word at point** (`wordAt()`): looks up by bounding box

All of these operate on the **already-ordered `m_words` list** — if the list is in visual order for RTL text, search and selection will be broken.

---

## The RTL / Visual-Order Bug

### Root Cause

```
PDF Content (logical order)
        │
        ▼
Poppler::Page::textList()
  Returns Poppler::TextBox objects in VISUAL ORDER (L→R on page)
  For RTL text: words appear right-to-left on page, but are listed left-to-right
        │
        ▼
abstractTextPage()
  Iterates Poppler::TextBox vector in the order Poppler returns them
  Preserves VISUAL ORDER — NO bidi reordering applied
  Creates Okular::TextEntity objects in visual order
        │
        ▼
correctTextOrder()
  Geometric XY-Cut analysis — NO bidi reordering
  Only reorders for columns/paragraphs/lines, preserves within-line order
        │
        ▼
Result: RTL text in Okular is stored in VISUAL (reversed) order
        → Text selection highlights wrong characters
        → Copy-paste produces reversed text
        → Text search fails for RTL text
```

### Where the Fix Should Go

There are two possible fix points:

1. **In `abstractTextPage()`** (generator_pdf.cpp:1835): After receiving `Poppler::TextBox` objects from Poppler, perform bidi reordering on the word sequence before converting to `Okular::TextEntity` objects. This is the most natural place because it's the boundary between Poppler's visual-order output and Okular's logical-order expectation.

2. **In `correctTextOrder()`** (core/textpage.cpp:1623): Add bidi reordering as part of the layout analysis. This would benefit all generators, not just PDF. However, it's harder because `correctTextOrder` operates on per-character entities without script direction information.

### What Poppler Provides

Poppler's `textList()` returns visual order because:
- Poppler uses the PDF's content stream directly
- PDF stores text in the order it's drawn (visual order)
- RTL text in PDFs is stored as individual glyphs positioned right-to-left
- Poppler preserves this rendering order

Poppler does not have built-in bidi reordering for `textList()`. The `Poppler::Page::text()` method (used by `exportTo()`) also returns visual-order text.

---

## Text Export Pipeline

The text export path is separate from the TextPage pipeline:

```cpp
// generator_pdf.cpp:1799
bool PDFGenerator::exportTo(const QString &fileName, const Okular::ExportFormat &format)
```

For plain text export:
1. Opens a `QFile` for writing
2. For each page, calls `pp->text(QRect()).normalized(NFC)` — Poppler's built-in `text()` method
3. Writes the raw text to file
4. **No bidi reordering** — same visual-order bug applies

`exportFormats()` only advertises `PlainText` format.

---

## Rendering Pipeline (Brief)

```
PDFGenerator::image(request)
        │
        ├─► pdfdoc->page(request->pageNumber())
        ├─► pp->renderToImage(dpiX, dpiY, x, y, w, h, rotation)
        │     └─► Returns QImage rendered at requested resolution
        │
        ├─► ImageScaling::scaleImage() — optional downscaling for large images
        │
        └─► Returns QImage to Okular for display
```

The `image()` method supports:
- Partial page rendering (via `NormalizedRect` in the request)
- Arbitrary DPI
- Rotation
- Abort-on-demand via `shouldAbortRenderCallback`

---

## Other Features

### Annotations
- `addAnnotations()` fetches Poppler annotations and converts to Okular via `createAnnotationFromPopplerAnnotation()` in `annots.cpp`
- Supports: Text, Line, Geom, Highlight, Ink, Stamp, Caret, FileAttachment, Sound, Movie, Widget (forms), Screen
- Annotations are stored in `annotationsOnOpenHash` for deduplication

### Form Fields
- `getFormFields()` in `formfields.cpp` converts `Poppler::FormField` → `Okular::FormField`
- Supports: Text, Choice (combo/list), Button (push/check/radio), Signature

### Signatures
- `sign()` method handles document signing via `Poppler::PDFConverter`
- `canSign()` checks if the document supports signing
- `okularToPoppler()` converts Okular signature data to Poppler format

### Print
- `print()` renders each page through `QPrinter`
- Supports annotation printing toggle, force rasterization, and scale modes (FitToPrintableArea, FitToPage, None)

### Fonts
- `fontsForPage()` iterates Poppler font iterators
- `requestFontData()` retrieves embedded font binary data

---

## Key Data Flow Summary

```
┌─────────────────────────────────────────────────────────────────┐
│ PDF FILE                                                        │
└───────────────┬─────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────┐
│ Poppler::Document                                               │
│   ├─► Poppler::Page (per page)                                  │
│   │     ├─► pageSizeF(), orientation()                          │
│   │     ├─► textList() → vector<Poppler::TextBox>               │
│   │     │       └─► text(), charBoundingBox(), nextWord(), ...  │
│   │     ├─► text() → QString (raw extraction)                   │
│   │     ├─► renderToImage() → QImage                            │
│   │     ├─► annotations() → vector<Poppler::Annotation>         │
│   │     └─► formFields() → Poppler::FormField list              │
│   ├─► numPages(), metadata(), fonts(), ...                      │
│   └─► PDFConverter (signing)                                    │
└───────────────┬─────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────┐
│ PDFGenerator (generator_pdf.cpp)                                │
│   ├─► abstractTextPage()                                        │
│   │     Poppler::TextBox → Okular::TextEntity (per-char)        │
│   │     Preserves visual order ⚠                                │
│   ├─► exportTo() → Poppler::Page::text()                        │
│   ├─► image() → Poppler::Page::renderToImage()                  │
│   └─► loadPages() → Okular::Page creation                       │
└───────────────┬─────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────┐
│ Okular Core                                                     │
│   ├─► Page::setTextPage()                                       │
│   │     └─► TextPagePrivate::correctTextOrder()                 │
│   │           XY-Cut geometric layout (NO bidi) ⚠               │
│   ├─► TextPage::findText() → search in m_words                  │
│   ├─► TextPage::text() → extract text from area                 │
│   └─► TextSelection → highlight using m_words areas             │
└─────────────────────────────────────────────────────────────────┘
```

---

## Build System

The CMakeLists.txt uses `okular_add_generator()` to register the backend and links against:

```cmake
target_link_libraries(okularGenerator_poppler
    okularcore
    KF6::I18n
    KF6::Completion
    KF6::KIOWidgets
    Poppler::Qt6      # ← THE critical dependency
    Qt6::Xml
)
```

The minimum required Poppler version is defined in `popplerversion.h` and checked at build time.
