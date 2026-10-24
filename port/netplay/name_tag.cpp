/**
 * name_tag.cpp — the "1P".."4P" player tag redrawn with a player's name.
 *
 * In a netplay session the tag over each fighter (and on the results
 * screen) shows the name part of the player's connect code (ADO#1148 ->
 * ADO) instead of 1P..4P. The ROM only has the five tag pictures, so the
 * tag is rebuilt here in the same style: bold letters with a dark outline
 * and the pointer below, as an IA8 sprite whose intensity/alpha follow the
 * ROM tag's (full intensity inside, zero-intensity outline fading out).
 * The game tints it with the tag's own prim/env colours, so a name tag is
 * coloured exactly like the label it replaces. The letters are hand-drawn
 * (7 rows, 2-pixel strokes), nothing is taken from the ROM.
 *
 * One sprite per distinct name, built on first use and kept for the run
 * (a few hundred bytes each); buffers are never rewritten, so the
 * renderer's texture cache can't show a stale name.
 */

#ifdef PORT

#include "../bridge/lbreloc_byteswap.h"
#include "../resource/RelocPointerTable.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct sprite;
typedef struct sprite Sprite;

extern "C" void gfxRegisterTrustedLowVARange(const void *base, size_t size);

namespace {

constexpr int kGlyphRows = 7;
constexpr int kMargin = 2;      /* outline room around the art */
constexpr int kTextTop = 2;
constexpr int kArrowTop = 11;   /* two outline rows between text and pointer */
constexpr int kHeight = 18;
constexpr int kMaxGlyphs = 12;

constexpr uint8_t kFmtIA = 3;   /* G_IM_FMT_IA */
constexpr uint8_t kSiz8b = 1;   /* G_IM_SIZ_8b */

struct Glyph {
    char c;
    const char *rows[kGlyphRows];
};

const Glyph kGlyphs[] = {
    { 'A', { ".####.", "##..##", "##..##", "######", "##..##", "##..##", "##..##" } },
    { 'B', { "#####.", "##..##", "##..##", "#####.", "##..##", "##..##", "#####." } },
    { 'C', { ".####.", "##..##", "##....", "##....", "##....", "##..##", ".####." } },
    { 'D', { "####..", "##.##.", "##..##", "##..##", "##..##", "##.##.", "####.." } },
    { 'E', { "######", "##....", "##....", "#####.", "##....", "##....", "######" } },
    { 'F', { "######", "##....", "##....", "#####.", "##....", "##....", "##...." } },
    { 'G', { ".####.", "##..##", "##....", "##.###", "##..##", "##..##", ".#####" } },
    { 'H', { "##..##", "##..##", "##..##", "######", "##..##", "##..##", "##..##" } },
    { 'I', { "####", ".##.", ".##.", ".##.", ".##.", ".##.", "####" } },
    { 'J', { "...###", "....##", "....##", "....##", "##..##", "##..##", ".####." } },
    { 'K', { "##..##", "##.##.", "####..", "###...", "####..", "##.##.", "##..##" } },
    { 'L', { "##....", "##....", "##....", "##....", "##....", "##....", "######" } },
    { 'M', { "##...##", "###.###", "#######", "##.#.##", "##...##", "##...##", "##...##" } },
    { 'N', { "##..##", "###.##", "######", "##.###", "##..##", "##..##", "##..##" } },
    { 'O', { ".####.", "##..##", "##..##", "##..##", "##..##", "##..##", ".####." } },
    { 'P', { "#####.", "##..##", "##..##", "#####.", "##....", "##....", "##...." } },
    { 'Q', { ".####.", "##..##", "##..##", "##..##", "##.###", "##..##", ".###.#" } },
    { 'R', { "#####.", "##..##", "##..##", "#####.", "####..", "##.##.", "##..##" } },
    { 'S', { ".####.", "##..##", "##....", ".####.", "....##", "##..##", ".####." } },
    { 'T', { "######", "..##..", "..##..", "..##..", "..##..", "..##..", "..##.." } },
    { 'U', { "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", ".####." } },
    { 'V', { "##..##", "##..##", "##..##", "##..##", ".####.", ".####.", "..##.." } },
    { 'W', { "##...##", "##...##", "##...##", "##.#.##", "#######", "###.###", "##...##" } },
    { 'X', { "##..##", "##..##", ".####.", "..##..", ".####.", "##..##", "##..##" } },
    { 'Y', { "##..##", "##..##", ".####.", "..##..", "..##..", "..##..", "..##.." } },
    { 'Z', { "######", "....##", "...##.", "..##..", ".##...", "##....", "######" } },
    { '0', { ".###.", "##.##", "##.##", "##.##", "##.##", "##.##", ".###." } },
    { '1', { ".##.", "###.", ".##.", ".##.", ".##.", ".##.", "####" } },
    { '2', { ".####.", "##..##", "....##", "...##.", "..##..", ".##...", "######" } },
    { '3', { ".####.", "##..##", "....##", "..###.", "....##", "##..##", ".####." } },
    { '4', { "...##.", "..###.", ".####.", "##.##.", "######", "...##.", "...##." } },
    { '5', { "######", "##....", "#####.", "....##", "....##", "##..##", ".####." } },
    { '6', { ".####.", "##....", "##....", "#####.", "##..##", "##..##", ".####." } },
    { '7', { "######", "....##", "...##.", "...##.", "..##..", "..##..", "..##.." } },
    { '8', { ".####.", "##..##", "##..##", ".####.", "##..##", "##..##", ".####." } },
    { '9', { ".####.", "##..##", "##..##", ".#####", "....##", "....##", ".####." } },
};

/* Pointer under the name, widest row first (even widths keep it centred). */
const int kArrowRows[] = { 10, 8, 6, 4, 2 };

const Glyph *find_glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (const Glyph &g : kGlyphs)
        if (g.c == c) return &g;
    return nullptr;
}

inline void put_s16(uint8_t *b, unsigned off, int16_t v) {
    uint16_t u; std::memcpy(&u, &v, 2);
    b[off+0] = (uint8_t)(u & 0xFF); b[off+1] = (uint8_t)(u >> 8);
}
inline void put_u16(uint8_t *b, unsigned off, uint16_t v) {
    b[off+0] = (uint8_t)(v & 0xFF); b[off+1] = (uint8_t)(v >> 8);
}
inline void put_u32(uint8_t *b, unsigned off, uint32_t v) {
    b[off+0] = (uint8_t)(v & 0xFF);
    b[off+1] = (uint8_t)((v >> 8) & 0xFF);
    b[off+2] = (uint8_t)((v >> 16) & 0xFF);
    b[off+3] = (uint8_t)(v >> 24);
}
inline void put_f32(uint8_t *b, unsigned off, float v) {
    uint32_t bits; std::memcpy(&bits, &v, 4); put_u32(b, off, bits);
}

struct Entry {
    Sprite *sprite;
    void *bitmap;
    uint8_t *pixels;
    size_t size;
};

std::map<std::string, Entry> sTags;
std::mutex sTagsMutex;

/* The tag art as a fill mask, width rounded up to 8 (IA8 rows must be a
 * whole TMEM line). Returns the width, 0 if nothing in name is drawable. */
int draw_mask(const char *name, std::vector<uint8_t> *mask)
{
    std::vector<const Glyph *> glyphs;
    int text_w = 0;

    for (const char *p = name; *p != '\0' && (int)glyphs.size() < kMaxGlyphs; p++) {
        const Glyph *g = find_glyph(*p);
        if (g == nullptr) continue;
        if (!glyphs.empty()) text_w += 1;
        text_w += (int)std::strlen(g->rows[0]);
        glyphs.push_back(g);
    }
    if (glyphs.empty()) return 0;

    int w = (text_w + (kMargin * 2) + 7) & ~7;
    mask->assign((size_t)w * kHeight, 0);

    int x = (w - text_w) / 2;
    for (const Glyph *g : glyphs) {
        int gw = (int)std::strlen(g->rows[0]);
        for (int r = 0; r < kGlyphRows; r++)
            for (int c = 0; c < gw; c++)
                if (g->rows[r][c] == '#') (*mask)[(size_t)(kTextTop + r) * w + x + c] = 1;
        x += gw + 1;
    }
    for (int k = 0; k < (int)(sizeof(kArrowRows) / sizeof(kArrowRows[0])); k++)
        for (int c = (w / 2) - (kArrowRows[k] / 2); c < (w / 2) + (kArrowRows[k] / 2); c++)
            (*mask)[(size_t)(kArrowTop + k) * w + c] = 1;
    return w;
}

/* IA8 texel (intensity high nibble, alpha low): white inside; outside, a
 * black outline that is strongest next to the art and fades two pixels out,
 * as on the ROM tags. */
std::vector<uint8_t> shade(const std::vector<uint8_t> &mask, int w)
{
    auto at = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < kHeight && mask[(size_t)y * w + x] != 0;
    };
    std::vector<uint8_t> out(mask.size(), 0);

    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t alpha = 0;
            if (at(x, y)) {
                out[(size_t)y * w + x] = 0xFF;
                continue;
            }
            if (at(x + 1, y) || at(x - 1, y) || at(x, y + 1) || at(x, y - 1)) alpha = 0xD;
            else {
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++)
                        if (at(x + dx, y + dy)) {
                            uint8_t a = (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) ? 0xA : 0x5;
                            if (a > alpha) alpha = a;
                        }
            }
            out[(size_t)y * w + x] = alpha;
        }
    }
    return out;
}

Entry build(const char *name)
{
    Entry e = {};
    std::vector<uint8_t> mask;
    int w = draw_mask(name, &mask);
    if (w == 0) return e;

    std::vector<uint8_t> texels = shade(mask, w);
    e.size = texels.size();
    e.pixels = static_cast<uint8_t *>(std::malloc(e.size));
    uint8_t *sp_raw = static_cast<uint8_t *>(std::calloc(1, 68));
    uint8_t *bm_raw = static_cast<uint8_t *>(std::calloc(1, 16));
    if (!e.pixels || !sp_raw || !bm_raw) {
        std::free(e.pixels); std::free(sp_raw); std::free(bm_raw);
        return Entry{};
    }
    std::memcpy(e.pixels, texels.data(), e.size);

    /* Bitmap */
    put_s16(bm_raw, 0x00, (int16_t)w);
    put_s16(bm_raw, 0x02, (int16_t)w);
    put_u32(bm_raw, 0x08, portRelocRegisterPointer(e.pixels));
    put_s16(bm_raw, 0x0C, (int16_t)kHeight);

    /* Sprite, laid out like the ROM tags (one IA8 bitmap). */
    put_s16(sp_raw, 0x04, (int16_t)w);
    put_s16(sp_raw, 0x06, (int16_t)kHeight);
    put_f32(sp_raw, 0x08, 1.0f);
    put_f32(sp_raw, 0x0C, 1.0f);
    put_u16(sp_raw, 0x14, 0);
    sp_raw[0x18] = 0xFF; sp_raw[0x19] = 0xFF;
    sp_raw[0x1A] = 0xFF; sp_raw[0x1B] = 0xFF;
    put_u32(sp_raw, 0x20, portRelocRegisterPointer(nullptr));
    put_s16(sp_raw, 0x26, 1);
    put_s16(sp_raw, 0x28, 1);                  /* nbitmaps */
    put_s16(sp_raw, 0x2A, (int16_t)kHeight);   /* ndisplist */
    put_s16(sp_raw, 0x2C, (int16_t)kHeight);
    put_s16(sp_raw, 0x2E, (int16_t)kHeight);
    sp_raw[0x30] = kFmtIA; sp_raw[0x31] = kSiz8b;
    put_u32(sp_raw, 0x34, portRelocRegisterPointer(bm_raw));
    put_u32(sp_raw, 0x38, portRelocRegisterPointer(nullptr));
    put_u32(sp_raw, 0x3C, portRelocRegisterPointer(nullptr));

    e.sprite = reinterpret_cast<Sprite *>(sp_raw);
    e.bitmap = bm_raw;
    return e;
}

} // namespace

/* The name tag sprite for name, or NULL when name has nothing drawable
 * (A-Z, 0-9). Hand it to lbCommonMakeSObjForGObj like a ROM sprite. */
extern "C" Sprite *portNetplayNameTagSprite(const char *name)
{
    if (name == nullptr) return nullptr;

    std::lock_guard<std::mutex> guard(sTagsMutex);
    auto it = sTags.find(name);
    if (it == sTags.end()) {
        Entry e = build(name);
        if (e.sprite == nullptr) return nullptr;
        it = sTags.emplace(name, e).first;
    }
    Entry &e = it->second;

    /* Every time: scene changes clear the fixup tracking, and this keeps
     * the game's byte-swap/deswizzle passes off our native-order sprite. */
    void *bufs[1] = { e.pixels };
    portMarkSyntheticSprite(e.sprite, e.bitmap, 1, bufs);
    gfxRegisterTrustedLowVARange(e.pixels, e.size);
    return e.sprite;
}

#endif /* PORT */
