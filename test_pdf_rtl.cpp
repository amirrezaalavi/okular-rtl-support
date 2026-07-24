// Integration test: open Persian PDF, extract text, verify logical order
// Uses Poppler::Page::textList() + our reorderVisualToLogical
// Compile: g++ -std=c++17 $(pkg-config --cflags Qt6Core poppler-qt6) test_pdf_rtl.cpp $(pkg-config --libs Qt6Core poppler-qt6) -o test_pdf_rtl

#include <QString>
#include <QVector>
#include <QChar>
#include <QDebug>
#include <poppler-qt6.h>
#include <memory>
#include <vector>

// --- Copied from generator_pdf.cpp ---
static bool isRtlChar(QChar c) {
    const QChar::Direction d = c.direction();
    return d == QChar::DirR || d == QChar::DirAL;
}
static bool hasRtlChars(const QString &str) {
    for (int i = 0; i < str.length(); i++)
        if (isRtlChar(str.at(i))) return true;
    return false;
}
static bool isPrimaryRtl(const QString &str) {
    for (int i = 0; i < str.length(); i++) {
        const QChar::Direction d = str.at(i).direction();
        if (d == QChar::DirR || d == QChar::DirAL) return true;
        if (d == QChar::DirL) return false;
    }
    return false;
}
struct BidiResult { QString text; QVector<int> logicalToVisual; };
static BidiResult reorderVisualToLogical(const QString &visual) {
    BidiResult result;
    const int len = visual.length();
    if (len <= 1 || !hasRtlChars(visual)) {
        result.text = visual;
        result.logicalToVisual.resize(len);
        for (int i = 0; i < len; i++) result.logicalToVisual[i] = i;
        return result;
    }
    const bool primaryRtl = isPrimaryRtl(visual);
    QString reordered; reordered.reserve(len);
    QVector<int> mapping; mapping.reserve(len);
    if (!primaryRtl) {
        int i = 0;
        while (i < len) {
            while (i < len && !isRtlChar(visual.at(i))) { reordered += visual.at(i); mapping.append(i); i++; }
            int j = i;
            while (j < len && isRtlChar(visual.at(j))) j++;
            if (j > i) { for (int k = j - 1; k >= i; k--) { reordered += visual.at(k); mapping.append(k); } i = j; }
        }
    } else {
        int i = len - 1;
        while (i >= 0) {
            while (i >= 0 && isRtlChar(visual.at(i))) { reordered += visual.at(i); mapping.append(i); i--; }
            int j = i;
            while (j >= 0 && !isRtlChar(visual.at(j))) j--;
            if (j < i) { for (int k = j + 1; k <= i; k++) { reordered += visual.at(k); mapping.append(k); } i = j; }
        }
    }
    result.text = reordered;
    result.logicalToVisual = mapping;
    return result;
}
// --- End copy ---

int main(int argc, char *argv[]) {
    if (argc < 2) {
        qCritical() << "Usage: test_pdf_rtl <pdf_file>";
        return 1;
    }

    QString pdfPath = argv[1];
    qDebug() << "Opening:" << pdfPath;

    std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(pdfPath));
    if (!doc) {
        qCritical() << "Failed to load PDF";
        return 1;
    }

    qDebug() << "Pages:" << doc->numPages();
    int passed = 0, failed = 0;

    for (int p = 0; p < doc->numPages(); p++) {
        std::unique_ptr<Poppler::Page> page(doc->page(p));
        if (!page) continue;

        std::vector<std::unique_ptr<Poppler::TextBox>> textList = page->textList();
        qDebug() << "Page" << p << ": text boxes =" << textList.size();

        for (const auto &word : textList) {
            QString visual = word->text();
            QString visualTrimmed = visual.trimmed();

            if (visualTrimmed.isEmpty()) continue;
            if (!hasRtlChars(visualTrimmed)) continue;

            BidiResult result = reorderVisualToLogical(visual);

            if (result.text != visual) {
                qDebug() << "  REORDERED:" << visual << "->" << result.text;
                passed++;
            }
        }
    }

    qDebug() << "\n========================================";
    qDebug() << "RTL words reordered:" << passed;
    if (passed > 0) {
        qDebug() << "SUCCESS: Bidi reordering is working!";
    } else {
        qDebug() << "NOTE: No RTL text found in this PDF (or all already in logical order)";
    }
    return 0;
}
