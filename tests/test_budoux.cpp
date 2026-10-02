// Self-checking test for the BudouX port.
//
// The expected chunks are the output of the original Python implementation
// (`budoux.load_default_japanese_parser().parse(...)`), not of this port:
// a port that only agrees with itself proves nothing.

#include "maxlabel/budoux.h"

#include <iostream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string & what) {
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

static std::string join(const std::vector<std::string> & chunks) {
    std::string out;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (i != 0) out += " | ";
        out += chunks[i];
    }
    return out;
}

static void expect(const maxlabel::BudouX & parser, const std::string & text,
                   const std::string & expected) {
    const std::vector<std::string> chunks = parser.parse(text);
    const std::string got = join(chunks);

    // The invariant first: whatever the split, it must reconstruct the input.
    std::string rejoined;
    for (const std::string & chunk : chunks) rejoined += chunk;
    if (rejoined != text) {
        check(false, "chunks reconstruct the input for '" + text + "'");
        return;
    }

    if (got == expected) {
        check(true, "'" + text + "' -> " + got);
    } else {
        check(false, "'" + text + "' -> " + got + "   (expected " + expected + ")");
    }
}

int main() {
    const std::string directory = BUDOUX_MODEL_DIR;

    maxlabel::BudouX japanese;
    std::string error;
    if (!japanese.load(directory + "/ja.json", &error)) {
        std::cout << "FAIL: cannot load ja.json: " << error << "\n";
        return 1;
    }
    check(true, "ja model loads");

    maxlabel::BudouX chinese;
    if (!chinese.load(directory + "/zh-hans.json", &error)) {
        std::cout << "FAIL: cannot load zh-hans.json: " << error << "\n";
        return 1;
    }
    check(true, "zh-hans model loads");

    // Reference output from the Python original.
    expect(japanese, "今日はいい天気ですね", "今日は | いい天気ですね");
    expect(japanese, "東京へ行く", "東京へ | 行く");
    expect(japanese, "私は学生です", "私は | 学生です");
    expect(japanese, "こんにちは世界", "こんにちは | 世界");

    expect(chinese, "今天天气不错", "今天 | 天气 | 不 | 错");
    expect(chinese, "衬衫的价格是9.15便士", "衬衫 | 的 | 价格 | 是 | 9. | 15 | 便士");
    expect(chinese, "我是学生", "我 | 是 | 学生");
    expect(chinese, "你好世界", "你 | 好 | 世界");

    // Degenerate inputs.
    check(japanese.parse("").empty(), "empty input -> no chunks");
    check(join(japanese.parse("あ")) == "あ", "one character -> one chunk");
    check(join(chinese.parse("a b c")) == "a b c",
          "latin passes through as the model decides");

    // A model that is not loaded must not lose the text: callers use this to
    // sub-split before detection, and dropping it there drops it everywhere.
    {
        const maxlabel::BudouX unloaded;
        check(!unloaded.ready(), "an unloaded parser reports not ready");
        const std::vector<std::string> chunks = unloaded.parse("今日は");
        check(chunks.size() == 1 && chunks.front() == "今日は",
              "an unloaded parser returns the text unchanged");
    }

    // A file that is not a model is rejected, not half-loaded.
    {
        maxlabel::BudouX broken;
        std::string why;
        check(!broken.load(directory + "/ja.json", &why) || broken.ready(),
              "loading a real model succeeds");
        check(!broken.load("no/such/model.json", &why), "a missing model fails");
        check(!why.empty(), "and says why");
        check(!broken.ready(), "a failed load leaves the parser unusable");
    }

    std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
    return failures == 0 ? 0 : 1;
}
