// N7: distance-field glyph atlas built at start from the shipped TrueType fonts (stb_truetype).
// Compiled without the project's strict warning flags because it includes stb_truetype.
#include "cat/ui.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb/stb_truetype.h"

namespace cat::ui {
namespace {
constexpr int kEm = 48, kPad = 6;   // glyphs are rasterized at 48 px per em with 6 px of distance range
constexpr float kOnEdge = 128, kDistScale = 128.f / kPad;
std::uint64_t key(Face f, char32_t c) { return (std::uint64_t(f) << 32) | c; }
std::uint64_t pair_key(Face f, char32_t a, char32_t b) { return (std::uint64_t(f) << 48) | (std::uint64_t(a & 0xFFFFFF) << 24) | (b & 0xFFFFFF); }
// The code points the UI draws: ASCII plus the typography of the legacy menus.
std::vector<char32_t> code_points() {
    std::vector<char32_t> cps;
    for(char32_t c = 32; c < 127; ++c) cps.push_back(c);
    for(char32_t c : {U'·', U'—', U'–', U'←', U'→', U'↑', U'↓', U'°', U'×', U'∞', U'•', U'…', U'‘', U'’', U'“', U'”', U'▶', U'✓', U'✕', U'⚙', U'✥', U'▣',
                      U'⚠', U'✦', U'Ⅱ', U'é', U'ë', U'ö', U'ü', U'©', U'■', U'▲', U'▼', U'◀', U'¼', U'½', U'%', U'±', U'↻', U'●', U'○', U'⟳'})
        cps.push_back(c);
    return cps;
}
}  // namespace

struct AtlasBuilder {
    static void place(Atlas& a, int w, int h, int& x, int& y) {
        const int size = a.image_.width;
        if(a.shelf_x_ + w > size) { a.shelf_x_ = 0; a.shelf_y_ += a.shelf_h_ + 1; a.shelf_h_ = 0; }
        if(a.shelf_y_ + h > a.image_.height) throw std::runtime_error("UI atlas is full");
        x = a.shelf_x_; y = a.shelf_y_;
        a.shelf_x_ += w + 1; a.shelf_h_ = std::max(a.shelf_h_, h);
    }
};

Atlas Atlas::build(const std::array<std::filesystem::path, kFaces>& fonts, const std::filesystem::path& fallback, int size) {
    Atlas a;
    a.image_.width = a.image_.height = static_cast<std::uint32_t>(size);
    a.image_.rgba.assign(static_cast<std::size_t>(size) * size * 4, 0);
    for(std::size_t i = 0; i < a.image_.rgba.size(); i += 4) a.image_.rgba[i] = a.image_.rgba[i + 1] = a.image_.rgba[i + 2] = 255;   // white glyph colour, alpha = distance
    a.range_ = kPad;
    // 2 x 2 opaque white texels at (0, 0): solid fills may sample it
    for(int y = 0; y < 2; ++y) for(int x = 0; x < 2; ++x) a.image_.rgba[(static_cast<std::size_t>(y) * size + x) * 4 + 3] = 255;
    a.shelf_x_ = 3;
    const auto cps = code_points();
    auto read = [](const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        if(!in) throw std::runtime_error("Cannot open font " + p.string());
        return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), {});
    };
    std::vector<unsigned char> fallback_data;
    stbtt_fontinfo fallback_info{};
    const bool has_fallback = !fallback.empty();
    if(has_fallback) {
        fallback_data = read(fallback);
        if(!stbtt_InitFont(&fallback_info, fallback_data.data(), stbtt_GetFontOffsetForIndex(fallback_data.data(), 0))) throw std::runtime_error("Not a TrueType font: " + fallback.string());
    }
    for(std::size_t f = 0; f < kFaces; ++f) {
        const std::vector<unsigned char> data = read(fonts[f]);
        stbtt_fontinfo info;
        if(!stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) throw std::runtime_error("Not a TrueType font: " + fonts[f].string());
        const float scale = stbtt_ScaleForMappingEmToPixels(&info, kEm);
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        a.ascent_[f] = asc * scale / kEm; a.descent_[f] = desc * scale / kEm;
        const Face face = static_cast<Face>(f);
        for(char32_t c : cps) {
            const stbtt_fontinfo* font = &info;
            float font_scale = scale;
            int index = stbtt_FindGlyphIndex(&info, static_cast<int>(c));
            if(index == 0 && c != U' ' && has_fallback) {
                const int alt = stbtt_FindGlyphIndex(&fallback_info, static_cast<int>(c));
                if(alt != 0) { font = &fallback_info; font_scale = stbtt_ScaleForMappingEmToPixels(&fallback_info, kEm); index = alt; }
            }
            if(index == 0 && c != U' ') { ++a.missing_; continue; }
            int advance = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(font, index, &advance, &lsb);
            Glyph g{};
            g.advance = advance * font_scale / kEm;
            int w = 0, h = 0, xoff = 0, yoff = 0;
            unsigned char* sdf = c == U' ' ? nullptr : stbtt_GetGlyphSDF(font, font_scale, index, kPad, static_cast<unsigned char>(kOnEdge), kDistScale, &w, &h, &xoff, &yoff);
            if(sdf && w > 0 && h > 0) {
                int x = 0, y = 0;
                AtlasBuilder::place(a, w, h, x, y);
                for(int row = 0; row < h; ++row) for(int col = 0; col < w; ++col)
                    a.image_.rgba[(static_cast<std::size_t>(y + row) * size + (x + col)) * 4 + 3] = sdf[row * w + col];
                g.u0 = float(x) / size; g.v0 = float(y) / size; g.u1 = float(x + w) / size; g.v1 = float(y + h) / size;
                g.x0 = float(xoff) / kEm; g.y0 = float(yoff) / kEm; g.x1 = float(xoff + w) / kEm; g.y1 = float(yoff + h) / kEm;
            }
            if(sdf) stbtt_FreeSDF(sdf, nullptr);
            a.glyphs_[key(face, c)] = g;
        }
        for(char32_t l : cps) for(char32_t r : cps) {
            if(l > 126 || r > 126) continue;   // kerning for ASCII pairs
            const int k = stbtt_GetCodepointKernAdvance(&info, static_cast<int>(l), static_cast<int>(r));
            if(k) a.kerning_[pair_key(face, l, r)] = k * scale / kEm;
        }
    }
    a.shelf_x_ = 0; a.shelf_y_ += a.shelf_h_ + 2; a.shelf_h_ = 0;   // images start on a new shelf
    return a;
}

Region Atlas::add_image(const scene::Image& img) {
    int x = 0, y = 0;
    const int w = static_cast<int>(img.width), h = static_cast<int>(img.height), size = static_cast<int>(image_.width);
    AtlasBuilder::place(*this, w + 2, h + 2, x, y);
    for(int row = -1; row <= h; ++row) for(int col = -1; col <= w; ++col) {   // 1 px clamped border against bleeding
        const int sr = std::clamp(row, 0, h - 1), sc = std::clamp(col, 0, w - 1);
        std::copy_n(&img.rgba[(static_cast<std::size_t>(sr) * w + sc) * 4], 4, &image_.rgba[(static_cast<std::size_t>(y + 1 + row) * size + (x + 1 + col)) * 4]);
    }
    return {float(x + 1) / size, float(y + 1) / size, float(x + 1 + w) / size, float(y + 1 + h) / size, w, h};
}

const Glyph* Atlas::glyph(Face f, char32_t c) const {
    const auto it = glyphs_.find(key(f, c));
    return it == glyphs_.end() ? nullptr : &it->second;
}
float Atlas::kern(Face f, char32_t a, char32_t b) const {
    const auto it = kerning_.find(pair_key(f, a, b));
    return it == kerning_.end() ? 0 : it->second;
}

}  // namespace cat::ui
