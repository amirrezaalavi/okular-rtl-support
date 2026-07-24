# Okular RTL Text Support

A fork of [Okular](https://invent.kde.org/graphics/okular) (KDE document viewer) that fixes long-standing **Right-to-Left** (RTL) text support bugs:

1. **Copy bug** — Persian/Arabic/Hebrew text is copied backwards
2. **Search bug** — Searching RTL text requires typing letters backwards

These bugs have been open since **2009** (KDE bugs [#184399](https://bugs.kde.org/184399) and [#207748](https://bugs.kde.org/207748), 17 years).

---

## What This Fixes

For PDFs containing **Persian, Arabic, or Hebrew** text, this fix ensures:

- ✅ Copy/paste produces correctly-ordered logical text
- ✅ Search works with normal (un-reversed) input
- ✅ Mixed Persian + English text is rendered/copied correctly
- ✅ LTR (English, etc.) text is completely unchanged
- ✅ Text selection highlighting still works (bounding boxes correctly mapped)

### Example

**Before this fix**, copying the Persian word "کتاب" (ketab = book) gave: `باتک`

**After this fix**, copying the same word gives: `کتاب` ✓

---

## How It Works

PDFs store glyphs in **visual (display) order**, while text editing and search work with **logical (reading) order**. For RTL scripts, the visual order is the reverse of the logical order.

**Root cause:** Poppler's Qt6 binding `Page::textList()` returns text in visual order. Okular's `abstractTextPage()` preserved this visual order character-by-character, and the resulting `TextPage` was used for both copy/paste and search.

**The fix** adds a Unicode Bidi Algorithm implementation (`reorderVisualToLogical()`) in `generators/poppler/generator_pdf.cpp` that reorders RTL text from visual to logical order at the point where text enters the `TextPage`. Character bounding boxes are remapped so text selection highlighting still works correctly.

```
                       BEFORE                          AFTER
                       ──────                          ─────
PDF  ──► Poppler  ──►  [visual order]                 [visual order]
                              │                               │
                              ▼                               ▼
                      abstractTextPage()             abstractTextPage()
                              │                               │
                              ▼                               ▼
                      [unchanged: backwards]         reorderVisualToLogical()
                      (copy & search broken)                │
                                                           ▼
                                                    [logical order]
                                                           │
                                                           ▼
                                                    (copy & search work)
```

### Files Changed

Only **one file** in Okular's source tree is modified:

- `generators/poppler/generator_pdf.cpp` (+/- ~250 lines, no breaking changes)

### Why Fix It Here (and Not in Poppler)?

- ✅ Self-contained — no Poppler upstream coordination needed
- ✅ No new dependencies (uses Qt6's built-in `QChar::direction()`)
- ✅ Fixes both copy and search simultaneously
- ✅ Poppler has its own `reorderText()` function (used by CLI `pdftotext`) — we're applying the same logic to the Qt6 binding output at the Okular level
- ✅ Easy to upstream as a single focused patch

---

## Build & Test

### Prerequisites

- Qt 6 (≥ 6.6)
- KDE Frameworks 6
- Poppler with Qt6 bindings (≥ 24.08)
- CMake + ECM (Extra CMake Modules)

On Ubuntu 26.04+:

```bash
sudo apt install build-essential cmake extra-cmake-modules \
  libpoppler-qt6-dev qt6-base-dev libqt6svg6-dev \
  libkf6i18n-dev libkf6kio-dev libkf6xmlgui-dev libkf6coreaddons-dev \
  libkf6widgetsaddons-dev libkf6parts-dev libkf6config-dev libkf6crash-dev \
  libkf6colorscheme-dev libkf6archive-dev libkf6textwidgets-dev \
  libkf6purpose-dev libkf6doctools-dev libkf6iconthemes-dev \
  libkf6threadweaver-dev qt6-speech-dev
```

### Build

```bash
git clone https://github.com/amirrezaalavi/okular-rtl-support.git
cd okular-rtl-support
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc) okularcore okularGenerator_poppler
```

### Install & Test

```bash
# Build the full app
make -j$(nproc)

# Install the PDF generator plugin only (overrides system okular)
sudo cp bin/okular_generators/okularGenerator_poppler.so \
     /usr/lib/x86_64-linux-gnu/qt6/plugins/okular_generators/okularGenerator_poppler.so

# Open any PDF with Persian/Arabic/Hebrew text and test
okular /path/to/your-persian.pdf
```

To revert: `sudo cp /usr/lib/.../okularGenerator_poppler.so{.bak,}` or just reinstall the system package.

---

## Verification

| Test | Result |
|------|--------|
| Python bidi algorithm tests | 7/7 pass |
| C++ bidi tests (Qt6) | 7/7 pass |
| PDF integration test (37 RTL words) | All correctly reordered |
| `calculatetexttest` (existing) | 3/3 pass — no regression |
| `searchtest` (existing, 13 cases) | 13/13 pass — no regression |
| End-to-end GUI test (real Persian PDF) | Copy & search both work |

### Test with a Real PDF

The fix was verified end-to-end on a 14 MB Persian university course PDF (`mabahes-jozveh-v3.pdf`):

- **Copy**: Persian text copied in correct logical order
- **Search**: Typing `شبیه سازی` finds the term without reversing letters

---

## Project Structure

```
okular-rtl-support/
├── README.md                          ← This file
├── RESEARCH_REPORT.md                 ← Full research & root cause analysis
├── plan.md                            ← Implementation plan
├── poppler-research-report.md         ← Poppler-side research (subagent)
├── AGENTS.md                          ← Project-level agent notes
├── core/AGENTS.md                     ← core/ directory notes
├── generators/poppler/AGENTS.md       ← PDF backend notes
├── part/AGENTS.md                     ← UI notes
├── generators/poppler/generator_pdf.cpp  ← THE FIX
├── test_bidi*.py / test_bidi*.cpp     ← Bidi algorithm tests
├── test_pdf_rtl.cpp                   ← PDF integration test
├── Containerfile                      ← Build the PDF generator in a container
├── entrypoint.sh                      ← VNC + LXQt test environment
└── okularGenerator_poppler.so         ← Pre-built binary (8.8 MB) — see Releases
```

---

## Related

- **KDE Bug #184399** — Copy/paste of Hebrew text is backwards
- **KDE Bug #207748** — Searching Hebrew text requires typing backwards
- **Poppler Bug #55977** — Patch for text reordering (never merged)
- **Poppler GitLab #412** — `textList()` reverses characters for Arabic
- **Review Board r/125442** — Quick patch for Okular search (never merged)

---

## License

GPL-2.0-or-later (same as upstream Okular)

## Credits

- Original Okular: KDE contributors
- Research and implementation: [@amirrezaalavi](https://github.com/amirrezaalavi)
- Algorithm: Adapted from Poppler's own `reorderText()` function
