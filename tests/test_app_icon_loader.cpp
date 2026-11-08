// Tests for the app icon loader (src/wm/render/AppIconLoader.cpp).
//
// The loader decides whether real app icons ever appear (launcher, panel
// window buttons, alt-tab). Three properties matter, and each one has
// burned a consumer:
//   1. theme-dir resolution: foot ships only as
//      /usr/share/icons/hicolor/48x48/apps/foot.png - no flat file under
//      /usr/share/icons/ or /usr/share/pixmaps/ - so a flat-only search
//      returned a placeholder for an installed icon
//   2. size normalization: consumers copy getIconSize()^2 pixels straight
//      out of the handle; kitty.png is 256x256 on this box and reading
//      it as 32x32 rendered a sheared crop
//   3. placeholder packing: 0xAARRGGBB little-endian ([B][G][R][A] in
//      memory), fully opaque - the old packing put 0xFF in the low byte
//      and blue in the alpha byte, so placeholders were semi-transparent
//      with swapped channels
//
// Real-icon halves are conditioned on the PNGs existing (they do on this
// box; a missing file prints a note and skips that half). The
// placeholder half always runs.
//
// Style: same standalone assert runner as tests/test_font.cpp.

#include <wm/render/AppIconLoader.hpp>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        tests_run++;                                                \
        if (cond) {                                                 \
            tests_passed++;                                         \
        } else {                                                    \
            tests_failed++;                                         \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);          \
        }                                                           \
    } while (0)

static bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Mean premultiplied colour of an ARGB8888 image: the same measure on
// both sides of the loader proves the handle holds *that* file's
// (downscaled) content and not a flat placeholder.
static void meanPremul(const uint32_t* px, size_t n,
                       double* r, double* g, double* b) {
    double accA = 0, accR = 0, accG = 0, accB = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t p = px[i];
        uint32_t a = (p >> 24) & 0xFF;
        accA += a;
        accR += ((p >> 16) & 0xFF) * a;
        accG += ((p >> 8) & 0xFF) * a;
        accB += (p & 0xFF) * a;
    }
    *r = accA ? accR / accA : 0;
    *g = accA ? accG / accA : 0;
    *b = accA ? accB / accA : 0;
}

static int distinctColours(const uint32_t* px, size_t n) {
    std::set<uint32_t> s;
    for (size_t i = 0; i < n; i++) s.insert(px[i]);
    return (int)s.size();
}

// The loader's theme rule: hicolor, sizes descending from 512. Mirror it
// so the test compares against the file the loader should have picked.
static std::string expectedHicolorFile(const std::string& name) {
    static const int sizes[] = {512, 256, 192, 128, 96, 64, 48, 36, 32, 24, 22, 16};
    for (int s : sizes) {
        char buf[64];
        snprintf(buf, sizeof buf, "%dx%d/apps/", s, s);
        std::string p = "/usr/share/icons/hicolor/" + std::string(buf) + name + ".png";
        if (fileExists(p)) return p;
    }
    return "";
}

// Shared real-icon assertions: resolved, normalized to the display size,
// not a flat placeholder, and carrying the source file's mean colour.
static void checkRealIcon(const char* appId, const std::string& srcPath) {
    if (srcPath.empty() || !fileExists(srcPath)) {
        printf("note: %s missing, skipping %s real-icon half\n",
               srcPath.empty() ? "(no hicolor size)" : srcPath.c_str(), appId);
        return;
    }
    havel::AppIconLoader loader;
    uint32_t h = loader.loadIcon(appId);
    CHECK(h != 0, "real icon handle is nonzero");

    int w = 0, hh = 0;
    loader.getIconDimensions(h, &w, &hh);
    CHECK(w == 32 && hh == 32, "icon normalized to the 32x32 display size");

    const uint32_t* px = loader.getIconPixels(h);
    CHECK(px != nullptr, "icon pixels reachable through the handle");
    if (!px) return;

    int distinct = distinctColours(px, (size_t)w * hh);
    CHECK(distinct >= 8, "real icon is not a flat placeholder");
    printf("  %s <- %s: %d distinct colours\n", appId, srcPath.c_str(), distinct);

    std::vector<uint32_t> raw;
    int rw = 0, rh = 0;
    if (havel::loadPngPixels(srcPath, raw, &rw, &rh)) {
        double sR, sG, sB, lR, lG, lB;
        meanPremul(raw.data(), raw.size(), &sR, &sG, &sB);
        meanPremul(px, (size_t)w * hh, &lR, &lG, &lB);
        bool near = (sR - lR) < 16 && (lR - sR) < 16 &&
                    (sG - lG) < 16 && (lG - sG) < 16 &&
                    (sB - lB) < 16 && (lB - sB) < 16;
        CHECK(near, "loaded pixels match a direct decode of the source PNG");
    } else {
        CHECK(false, "source PNG decodes for comparison");
    }
}

int main() {
    // ---- placeholder packing is exact and opaque ---------------------
    {
        havel::AppIconLoader loader;
        const char* name = "zzz-no-such-app-000";
        uint32_t h = loader.loadIcon(name);
        CHECK(h != 0, "placeholder handle");

        int w = 0, hh = 0;
        loader.getIconDimensions(h, &w, &hh);
        CHECK(w == 32 && hh == 32, "placeholder is 32x32");

        const uint32_t* px = loader.getIconPixels(h);
        CHECK(px != nullptr, "placeholder pixels reachable");

        // Same hash the loader uses; the exact colour pins the packing.
        unsigned hash = 0;
        for (const char* c = name; *c; c++) hash = hash * 31 + (unsigned char)*c;
        uint32_t r = (hash >> 16) & 0xFF, g = (hash >> 8) & 0xFF, b = hash & 0xFF;
        uint32_t want = 0xFF000000u | (r << 16) | (g << 8) | b;
        if (px) {
            int bad = 0, opaque = 1;
            for (int i = 0; i < w * hh; i++) {
                if (px[i] != want) bad++;
                if (((px[i] >> 24) & 0xFF) != 0xFF) opaque = 0;
            }
            CHECK(bad == 0, "placeholder pixels use 0xAARRGGBB packing (hash colour)");
            CHECK(opaque == 1, "placeholder is fully opaque");
        }

        // A miss is cached: asking twice must not rescan and must return
        // the same handle.
        CHECK(loader.loadIcon(name) == h, "miss entry is cached");
    }

    // ---- flat pixmaps path still works (regression guard) ------------
    checkRealIcon("kitty", "/usr/share/pixmaps/kitty.png");

    // ---- theme-dir path: foot/firefox exist only under hicolor -------
    checkRealIcon("foot", expectedHicolorFile("foot"));
    checkRealIcon("firefox", expectedHicolorFile("firefox"));

    printf("=== app-icons: %d passed, %d failed, %d run ===\n",
           tests_passed, tests_failed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
