// Standalone bidi test - compile in container with Qt6
#include <QString>
#include <QVector>
#include <QChar>
#include <QDebug>

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
            while (i < len && !isRtlChar(visual.at(i))) {
                reordered += visual.at(i); mapping.append(i); i++;
            }
            int j = i;
            while (j < len && isRtlChar(visual.at(j))) j++;
            if (j > i) {
                for (int k = j - 1; k >= i; k--) {
                    reordered += visual.at(k); mapping.append(k);
                }
                i = j;
            }
        }
    } else {
        int i = len - 1;
        while (i >= 0) {
            while (i >= 0 && isRtlChar(visual.at(i))) {
                reordered += visual.at(i); mapping.append(i); i--;
            }
            int j = i;
            while (j >= 0 && !isRtlChar(visual.at(j))) j--;
            if (j < i) {
                for (int k = j + 1; k <= i; k++) {
                    reordered += visual.at(k); mapping.append(k);
                }
                i = j;
            }
        }
    }
    result.text = reordered;
    result.logicalToVisual = mapping;
    return result;
}

int main() {
    int passed = 0, failed = 0;

    auto test = [&](const QString &name, const QString &visual, const QString &expected) {
        BidiResult r = reorderVisualToLogical(visual);
        if (r.text == expected) {
            qDebug() << "PASS:" << name;
            passed++;
        } else {
            qCritical() << "FAIL:" << name;
            qCritical() << "  visual:" << visual;
            qCritical() << "  expected:" << expected;
            qCritical() << "  got:" << r.text;
            failed++;
        }
    };

    // Test 1: English unchanged
    test("English LTR", "Hello World", "Hello World");

    // Test 2: Persian word - visual order (left to right on page) -> logical order
    // "ketab" = کتاب, visual order: ب ا ت ک
    test("Persian word", QString::fromUtf8("\xD8\xA8\xD8\xA7\xD8\xAA\xDA\xA9"),
         QString::fromUtf8("\xDA\xA9\xD8\xAA\xD8\xA7\xD8\xA8")); // ک ت ا ب

    // Test 3: Persian "salam" = سلام
    test("Persian salam", QString::fromUtf8("\xD9\x85\xD8\xA7\xD9\x84\xD8\xB3"),
         QString::fromUtf8("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85")); // س ل ا م

    // Test 4: Mixed EN+FA
    test("Mixed EN+FA", "Hello " + QString::fromUtf8("\xD9\x85\xD8\xA7\xD9\x84\xD8\xB3"),
         "Hello " + QString::fromUtf8("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85")); // Hello سلام

    // Test 5: Hebrew shalom = שלום, visual: ם ו ל ש
    test("Hebrew", QString::fromUtf8("\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9"),
         QString::fromUtf8("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D")); // ש ל ו ם

    // Test 6: Single RTL char
    test("Single RTL", QString::fromUtf8("\xD8\xB3"), QString::fromUtf8("\xD8\xB3")); // س

    // Test 7: BBox mapping
    QVector<QString> bboxes = {"bb_0", "bb_1", "bb_2", "bb_3"};
    QString word = QString::fromUtf8("\xD8\xA8\xD8\xA7\xD8\xAA\xDA\xA9"); // باتک = visual
    BidiResult r = reorderVisualToLogical(word);
    qDebug() << "BBox mapping test:";
    for (int i = 0; i < 4; i++) {
        int vi = r.logicalToVisual[i];
        qDebug() << "  logical[" << i << "] uses visual bbox[" << vi << "] =" << bboxes[vi];
    }
    bool mappingOk = (r.logicalToVisual[0] == 3 && r.logicalToVisual[1] == 2 &&
                      r.logicalToVisual[2] == 1 && r.logicalToVisual[3] == 0);
    if (mappingOk) { qDebug() << "PASS: BBox mapping"; passed++; }
    else { qCritical() << "FAIL: BBox mapping"; failed++; }

    qDebug() << "\n========================================";
    qDebug() << passed << "/" << (passed+failed) << "tests passed";
    return failed > 0 ? 1 : 0;
}
