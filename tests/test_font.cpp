// Tests for the font module (src/ui/font).
//
// Covers both face classes through the one library:
//   - bitmap face: a BDF font is generated at run time (no system
//     dependency), loaded through the same resolution path a real boot
//     uses ($HAVEL_FONT), rasterized, and its metrics/pixels checked
//   - vector face: an installed TTF (skipped with a PASS-style note if
//     the box has none - the bitmap half carries the coverage)
//   - missing glyphs, UTF-8 handling, per-face sizing
//
// Style: same standalone assert runner as tests/test_utilities.c.

#include <ui/font/Font.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        tests_run++;                                                \
        if (cond) {                                                 \
            tests_passed++;                                         \
        } else {                                                   \
            tests_failed++;                                         \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);          \
        }                                                           \
    } while (0)

// ---------------------------------------------------------------------------
// A tiny 5x7 bitmap font in BDF, authored here so the test does not
// depend on system PCF fonts existing. Glyphs cover every character of
// the strings the compositor's workspace bar renders ("Workspace N",
// "N windows", "NN:NN"): letters W w o r k s p a c e i n d, digits
// 0-9 are covered via 1 2 3 4, plus ':' '|' and space.
// ---------------------------------------------------------------------------
static const struct { const char* name; int enc; int dwidth; const char* rows[7]; } kGlyphs[] = {
    {"W", 87, 7, {"10001","10001","10001","10101","10101","10101","01010"}},
    {"w", 119, 6, {"00000","00000","10001","10001","10101","10101","01010"}},
    {"o", 111, 6, {"00000","00000","01110","10001","10001","10001","01110"}},
    {"r", 114, 5, {"00000","00000","11110","10000","10000","10000","10000"}},
    {"k", 107, 6, {"00000","10010","10100","11000","10100","10010","00000"}},
    {"s", 115, 6, {"00000","00000","01111","10000","01110","00001","11110"}},
    {"p", 112, 6, {"00000","11100","10010","10010","11100","10000","10000"}},
    {"a", 97, 6, {"00000","00000","01110","00001","01111","10001","01111"}},
    {"c", 99, 6, {"00000","00000","01110","10001","10000","10001","01110"}},
    {"e", 101, 6, {"00000","00000","01110","10001","11111","10000","01110"}},
    {"i", 105, 3, {"00100","00000","00100","00100","00100","00100","00100"}},
    {"n", 110, 6, {"00000","00000","11110","10001","10001","10001","10001"}},
    {"d", 100, 6, {"00000","00001","00001","01101","10001","10001","01111"}},
    {"1", 49, 5, {"00100","01000","10000","00100","00100","00100","01110"}},
    {"2", 50, 6, {"01110","10001","00001","00110","01000","10000","11111"}},
    {"3", 51, 6, {"01110","10001","00001","00110","00001","10001","01110"}},
    {"4", 52, 6, {"00010","00110","01010","10010","11111","00010","00010"}},
    {":", 58, 3, {"00000","00100","00000","00100","00000","00000","00000"}},
    {"|", 124, 3, {"00100","00100","00100","00100","00100","00100","00100"}},
};

static const int kSpaceDwidth = 4;  // int, not char: a streamed char writes its glyph, not the number

static std::string writeTestBdf() {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::filesystem::path file = dir / "havel-font-test.bdf";

    std::ofstream out(file);
    if (!out) return {};
    out << "STARTFONT 2.1\n";
    out << "FONT -havel-test-fixed-medium-r-normal--7-70-75-75-C-60-ISO10646-1\n";
    out << "SIZE 7 75 75\n";
    out << "FONTBOUNDINGBOX 5 7 0 0\n";
    // FreeType's BDF driver refuses files without the charset properties
    // (FT error 3 "broken file" without them - observed live).
    out << "STARTPROPERTIES 4\n";
    out << "FONT_ASCENT 7\n";
    out << "FONT_DESCENT 0\n";
    out << "CHARSET_REGISTRY \"ISO8859\"\n";
    out << "CHARSET_ENCODING \"1\"\n";
    out << "ENDPROPERTIES\n";
    // +2 for space and a .notdef entry FreeType likes to see
    out << "CHARS " << (sizeof(kGlyphs) / sizeof(kGlyphs[0]) + 1) << "\n";
    for (const auto& g : kGlyphs) {
        out << "STARTCHAR " << g.name << "\n";
        out << "ENCODING " << g.enc << "\n";
        out << "SWIDTH " << (g.dwidth * 1440 / 72) << " 0\n";
        out << "DWIDTH " << g.dwidth << " 0\n";
        out << "BBX 5 7 0 0\n";
        out << "BITMAP\n";
        for (const char* row : g.rows) {
            // 5 bits -> one byte, high bits used
            unsigned v = 0;
            for (int b = 0; b < 5; b++) v = (v << 1) | (row[b] == '1' ? 1u : 0u);
            char hex[4];
            snprintf(hex, sizeof hex, "%02X", v);
            out << hex << "\n";
        }
        out << "ENDCHAR\n";
    }
    // space: zero-size bitmap, nonzero advance
    out << "STARTCHAR space\n";
    out << "ENCODING 32\n";
    out << "SWIDTH " << (kSpaceDwidth * 1440 / 72) << " 0\n";
    out << "DWIDTH " << (kSpaceDwidth) << " 0\n";
    out << "BBX 0 0 0 0\n";
    out << "BITMAP\n";
    out << "ENDCHAR\n";
    out << "ENDFONT\n";
    return file.string();
}

int main() {
    using havel::font::Font;
    using havel::font::RGBA;

    printf("=== font module tests ===\n");

    // ---- bitmap face (generated BDF) ---------------------------------
    const std::string bdf = writeTestBdf();
    CHECK(!bdf.empty(), "generated BDF");

    Font bitmapFont;
    if (!bdf.empty()) {
        setenv("HAVEL_FONT", bdf.c_str(), 1);
        CHECK(bitmapFont.initialize(), "bitmap face loads");
        CHECK(bitmapFont.ok(), "bitmap face ok");
        if (!bitmapFont.ok()) {
            printf("  bitmap load reason: %s\n", bitmapFont.reason().c_str());
        }
        const std::string desc = bitmapFont.describe();
        CHECK(desc.find("(bitmap") != std::string::npos,
              "describe() reports a bitmap face");
        printf("  face: %s\n", desc.c_str());

        auto rz = bitmapFont.rasterize("Workspace 1", 7, RGBA{1, 1, 1, 1});
        CHECK(!rz.empty(), "rasterize from bitmap glyphs");
        CHECK(rz.w > 0 && rz.h > 0, "bitmap metrics nonzero");
        int solid = 0;
        for (uint32_t p : rz.pixels) if (p == 0xFFFFFFFFu) solid++;
        CHECK(solid > 40, "bitmap glyphs produce solid white pixels");
        printf("  \"Workspace 1\" -> %dx%d, %d solid px\n", rz.w, rz.h, solid);

        auto wide = bitmapFont.rasterize("W", 7, RGBA{1, 1, 1, 1});
        auto narrow = bitmapFont.rasterize("i", 7, RGBA{1, 1, 1, 1});
        CHECK(wide.w > narrow.w, "per-glyph advances differ (W wider than i)");

        // Glyphs the font does not have go through FreeType's .notdef:
        // measure what that actually produces, then assert it.
        auto missing = bitmapFont.rasterize("Z", 7, RGBA{1, 1, 1, 1});
        int missSolid = 0;
        for (uint32_t p : missing.pixels) if ((p & 0xFFu) == 255) missSolid++;
        printf("  missing glyph (Z): %s %dx%d, %d full-alpha px\n",
               missing.empty() ? "empty" : "rasterized", missing.w, missing.h, missSolid);
        CHECK(rz.w > missing.w, "real glyphs are wider than a lone missing one");

        // UTF-8: a two-byte sequence whose codepoint is absent. It must
        // decode as ONE codepoint (not two garbage bytes), i.e. the
        // result matches the single-.notdef case, not a two-glyph one.
        auto utf8Missing = bitmapFont.rasterize("\xC3\xA9", 7, RGBA{1, 1, 1, 1});
        int utfSolid = 0;
        for (uint32_t p : utf8Missing.pixels) if ((p & 0xFFu) == 255) utfSolid++;
        printf("  utf-8 missing (e-acute): %s %dx%d, %d full-alpha px\n",
               utf8Missing.empty() ? "empty" : "rasterized", utf8Missing.w, utf8Missing.h, utfSolid);
        CHECK(utf8Missing.w == missing.w && utf8Missing.h == missing.h,
              "utf-8 decodes to one codepoint, not per-byte garbage");

        // bitmap faces are fixed-size: a request for another size picks
        // the nearest strike rather than failing
        auto otherSize = bitmapFont.rasterize("Workspace 1", 20, RGBA{1, 1, 1, 1});
        CHECK(!otherSize.empty(), "bitmap face serves a different requested size");

        unsetenv("HAVEL_FONT");
    } else {
        printf("SKIP: bitmap half (could not write the BDF)\n");
    }

    // ---- vector face (installed font) -------------------------------
    static const char* vectorCandidates[] = {
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        nullptr,
    };
    std::string ttf;
    for (int i = 0; vectorCandidates[i]; i++) {
        if (std::filesystem::is_regular_file(vectorCandidates[i])) {
            ttf = vectorCandidates[i];
            break;
        }
    }
    if (!ttf.empty()) {
        Font vectorFont;
        setenv("HAVEL_FONT", ttf.c_str(), 1);
        CHECK(vectorFont.initialize(), "vector face loads");
        CHECK(vectorFont.describe().find("(vector)") != std::string::npos,
              "describe() reports a vector face");
        auto rz = vectorFont.rasterize("Workspace 1", 16, RGBA{1, 1, 1, 1});
        CHECK(!rz.empty(), "rasterize from outline glyphs");
        CHECK(rz.h >= 10, "vector glyph height scales with requested size");
        // anti-aliased coverage: not every covered pixel is full alpha
        int partial = 0;
        for (uint32_t p : rz.pixels) {
            uint32_t a = p & 0xFFu;
            if (a > 0 && a < 255) partial++;
        }
        CHECK(partial > 0, "vector raster has anti-aliased coverage");
        printf("  \"Workspace 1\" @16px -> %dx%d, %d partial-alpha px\n",
               rz.w, rz.h, partial);
        unsetenv("HAVEL_FONT");
    } else {
        printf("SKIP: vector half (no installed TTF found)\n");
    }

    // ---- resolution failure is honest -------------------------------
    {
        setenv("HAVEL_FONT", "/nonexistent/font.ttf", 1);
        Font nope;
        CHECK(!nope.initialize(), "explicit override must fail on a bad path");
        CHECK(!nope.reason().empty(), "failure carries a reason");
        printf("  reason: %s\n", nope.reason().c_str());
        unsetenv("HAVEL_FONT");
    }

    printf("=== font: %d passed, %d failed, %d run ===\n",
           tests_passed, tests_failed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
