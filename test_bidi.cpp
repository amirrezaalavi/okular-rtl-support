// Standalone test for the bidi reordering function
// Compile with: g++ -std=c++17 -I/usr/include/x86_64-linux-gnu/qt6 -I/usr/include/qt6 -fPIC test_bidi.cpp -lQt6Core -o test_bidi
#include <QString>
#include <QVector>
#include <QChar>
#include <iostream>

// --- Copy of the bidi reordering functions from generator_pdf.cpp ---

static bool isRtlChar(QChar c)
{
    const QChar::Direction d = c.direction();
    return d == QChar::DirR || d == QChar::DirAL;
}

static bool hasRtlChars(const QString &str)
{
    for (int i = 0; i < str.length(); i++) {
        if (isRtlChar(str.at(i))) {
            return true;
        }
    }
    return false;
}

static bool isPrimaryRtl(const QString &str)
{
    for (int i = 0; i < str.length(); i++) {
        const QChar::Direction d = str.at(i).direction();
        if (d == QChar::DirR || d == QChar::DirAL) {
            return true;
        }
        if (d == QChar::DirL) {
            return false;
        }
    }
    return false;
}

struct BidiResult {
    QString text;
    QVector<int> logicalToVisual;
};

static BidiResult reorderVisualToLogical(const QString &visual)
{
    BidiResult result;
    const int len = visual.length();

    if (len <= 1 || !hasRtlChars(visual)) {
        result.text = visual;
        result.logicalToVisual.resize(len);
        for (int i = 0; i < len; i++) {
            result.logicalToVisual[i] = i;
        }
        return result;
    }

    const bool primaryRtl = isPrimaryRtl(visual);
    QString reordered;
    reordered.reserve(len);
    QVector<int> mapping;
    mapping.reserve(len);

    if (!primaryRtl) {
        int i = 0;
        while (i < len) {
            while (i < len && !isRtlChar(visual.at(i))) {
                reordered += visual.at(i);
                mapping.append(i);
                i++;
            }
            int j = i;
            while (j < len && isRtlChar(visual.at(j))) {
                j++;
            }
            if (j > i) {
                for (int k = j - 1; k >= i; k--) {
                    reordered += visual.at(k);
                    mapping.append(k);
                }
                i = j;
            }
        }
    } else {
        int i = len - 1;
        while (i >= 0) {
            while (i >= 0 && isRtlChar(visual.at(i))) {
                reordered += visual.at(i);
                mapping.append(i);
                i--;
            }
            int j = i;
            while (j >= 0 && !isRtlChar(visual.at(j))) {
                j--;
            }
            if (j < i) {
                for (int k = j + 1; k <= i; k++) {
                    reordered += visual.at(k);
                    mapping.append(k);
                }
                i = j;
            }
        }
    }

    result.text = reordered;
    result.logicalToVisual = mapping;
    return result;
}

// --- Test runner ---

struct TestCase {
    QString name;
    QString input;       // visual order
    QString expected;    // expected logical order
};

int main() {
    QVector<TestCase> tests = {
        // Test 1: Pure English - no change
        {"English LTR", "Hello World", "Hello World"},

        // Test 2: Pure Persian (Persian word "ketab" = book: ک ت ا ب)
        // Visual order: leftmost=ب, rightmost=ک
        // Expected logical: ک ت ا ب (right-to-left)
        {"Persian word",
         QString::fromUtf8("\xD8\xA8\xD8\xAA\xD8\xA7\xDA\xA9"),  // visual: ب ت ا ک
         QString::fromUtf8("\xDA\xA9\xD8\xA7\xD8\xAA\xD8\xA8")}, // logical: ک ا ت ب

        // Test 3: Mixed Persian + English ("Hello سلام")
        // Visual order: H e l l o   س ل ا م
        // Expected: Hello م ا ل س (LTR then RTL)
        {"Mixed Persian+English",
         QString::fromUtf8("Hello ") + QString::fromUtf8("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"),
         QString::fromUtf8("Hello ") + QString::fromUtf8("\xD9\x85\xD8\xA7\xD9\x84\xD8\xB3")},

        // Test 4: Single RTL char - no change needed
        {"Single RTL char",
         QString::fromUtf8("\xD8\xB3"),  // س
         QString::fromUtf8("\xD8\xB3")},

        // Test 5: Hebrew word שלום (shalom)
        {"Hebrew word",
         QString::fromUtf8("\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9"),  // visual: ם ו ל ש
         QString::fromUtf8("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D")}, // logical: ש ל ו ם

        // Test 6: Empty string
        {"Empty string", "", ""},
    };

    int passed = 0;
    int failed = 0;

    for (const auto &test : tests) {
        BidiResult result = reorderVisualToLogical(test.input);

        bool ok = (result.text == test.expected);
        if (ok) {
            passed++;
            std::cout << "PASS: " << test.name.toStdString() << std::endl;
        } else {
            failed++;
            std::cout << "FAIL: " << test.name.toStdString() << std::endl;
            std::cout << "  Input:    " << std::hex;
            for (int i = 0; i < test.input.length(); i++) {
                std::cout << "U+" << test.input.at(i).unicode() << " ";
            }
            std::cout << std::dec << std::endl;
            std::cout << "  Expected: ";
            for (int i = 0; i < test.expected.length(); i++) {
                std::cout << "U+" << std::hex << test.expected.at(i).unicode() << " ";
            }
            std::cout << std::dec << std::endl;
            std::cout << "  Got:      ";
            for (int i = 0; i < result.text.length(); i++) {
                std::cout << "U+" << std::hex << result.text.at(i).unicode() << " ";
            }
            std::cout << std::dec << std::endl;

            // Show mapping
            std::cout << "  Mapping:  ";
            for (int i = 0; i < result.logicalToVisual.size(); i++) {
                std::cout << result.logicalToVisual[i] << " ";
            }
            std::cout << std::endl;
        }
    }

    std::cout << "\n---" << std::endl;
    std::cout << "Results: " << passed << "/" << (passed + failed) << " passed" << std::endl;

    return failed > 0 ? 1 : 0;
}
