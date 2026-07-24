# Okular RTL Fix Project — AGENTS.md

## Project Overview
This is a fork of Okular (KDE PDF viewer) for fixing long-standing RTL (Right-to-Left) text support bugs:
1. **Copy bug**: Persian/Arabic/Hebrew text is copied backwards (visual order instead of logical order)
2. **Search bug**: Searching RTL text requires typing letters backwards

## Quick Links
- **Research Report**: `RESEARCH_REPORT.md` — full analysis of bug history, root cause, architecture
- **Implementation Plan**: `plan.md` (to be created)
- **Upstream Repo**: https://invent.kde.org/graphics/okular (project #355)
- **Key Bugs**: KDE #184399 (copy), KDE #207748 (search)
- **Poppler Bug**: Freedesktop #55977 (ICU/fribidi patch for text reordering)

## Directory Map

| Directory | Role | AGENTS.md |
|-----------|------|-----------|
| `core/` | Text data model, search, extraction | `core/AGENTS.md` |
| `generators/poppler/` | PDF backend (Poppler Qt6) | `generators/poppler/AGENTS.md` |
| `part/` | UI components, page view, search UI | `part/AGENTS.md` |
| `gui/` | General UI widgets | — |
| `shell/` | Application entry point | — |
| `interfaces/` | Plugin interfaces | — |
| `conf/` | KConfig XT settings | — |
| `autotests/` | Test suite | — |
| `mobile/` | Mobile (Android) app | — |

## Build System
- **CMake** with KDE ECM (Extra CMake Modules)
- **Qt6** (required)
- **Poppler::Qt6** for PDF support
- **KDE Frameworks 6** (KF6) libraries

## Root Cause (TL;DR)
1. PDFs store glyphs in **visual (display) order**
2. Poppler's Qt6 binding `Page::textList()` returns text in **visual order** (no bidi reordering)
3. Okular's `abstractTextPage()` preserves this order character-by-character
4. Both copy (`TextPage::text()`) and search (`TextPage::findText()`) operate on visual-order text
5. For RTL scripts, visual order is reversed from logical (reading) order

## Fix Strategy
Add text reordering (visual → logical) in Okular's text page creation pipeline:
- Primary target: `generators/poppler/generator_pdf.cpp::abstractTextPage()`
- Alternatively: `core/textpage.cpp` when text enters the TextPage

## Key Files for Fix
1. `generators/poppler/generator_pdf.cpp:1835` — `abstractTextPage()` — THE FIX POINT
2. `generators/poppler/generator_pdf.cpp:1801` — text export (also broken)
3. `core/textpage.cpp:876` — `TextPage::text()` — copy/paste
4. `core/textpage.cpp:549` — `TextPage::findText()` — search
5. `core/textpage.cpp:1835` — `correctTextOrder()` — existing post-processing
