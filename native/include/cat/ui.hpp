#pragma once
// N7 UI drawing: signed-distance-field text from the shipped fonts, flat and gradient shapes, the
// legacy's pointed "cut" panels and atlas images, emitted as one triangle list for the renderer's UI
// pass. GPU/SDL independent (tests build and inspect canvases without a device).
#include "cat/scene.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cat::ui {

// mode: 0 solid colour, 1 SDF glyph (atlas alpha = distance), 2 atlas image (RGBA * colour)
struct Vertex { float x, y, u, v, r, g, b, a, mode; };   // x, y in normalized device coordinates (y down)
static_assert(sizeof(Vertex) == 36);

struct Color {
    float r{1}, g{1}, b{1}, a{1};
    static Color hex(std::uint32_t rgb, float alpha = 1) { return {static_cast<float>((rgb >> 16) & 255) / 255.f, static_cast<float>((rgb >> 8) & 255) / 255.f, static_cast<float>(rgb & 255) / 255.f, alpha}; }
    Color with_alpha(float alpha) const { return {r, g, b, alpha}; }
    Color scaled(float k) const { return {r * k, g * k, b * k, a}; }
};
Color mix(Color a, Color b, float t);

struct Rect {
    double x{}, y{}, w{}, h{};
    double right() const { return x + w; }
    double bottom() const { return y + h; }
    bool contains(double px, double py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(double d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(double dx, double dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
};

// serif = Liberation Serif Bold (the legacy's Noto Serif / Georgia / Times New Roman at weight 900, HUD
// numbers), serif_regular = Liberation Serif (weight 400: menu buttons and titles), sans = Liberation Sans
// Bold (Arial 600 / 900), mono = Liberation Mono Bold (monospace 600 / 800).
enum class Face : std::uint8_t { serif, sans, mono, serif_regular };
inline constexpr std::size_t kFaces = 4;
enum class Align : std::uint8_t { left, center, right };

struct Glyph { float u0, v0, u1, v1; float x0, y0, x1, y1; float advance; };   // quad relative to the pen at the baseline, in em
struct Region { float u0{}, v0{}, u1{}, v1{}; int width{}, height{}; };

// Fonts and images packed in one RGBA atlas (glyphs: RGB white, A = distance field).
class Atlas {
public:
    // Builds the distance fields of the code points the UI uses for the four faces (in Face order).
    // `fallback` supplies code points a face lacks (symbols), scaled to the same em.
    static Atlas build(const std::array<std::filesystem::path, kFaces>& fonts, const std::filesystem::path& fallback = {}, int size = 2048);
    Region add_image(const scene::Image& image);   // packed below the glyphs; throws when full
    const scene::Image& image() const { return image_; }
    const Glyph* glyph(Face f, char32_t c) const;
    float kern(Face f, char32_t a, char32_t b) const;   // em
    float ascent(Face f) const { return ascent_[static_cast<std::size_t>(f)]; }   // em
    float descent(Face f) const { return descent_[static_cast<std::size_t>(f)]; }   // em (negative)
    float distance_range() const { return range_; }   // atlas pixels from edge to 0 / 1
    std::size_t glyph_count() const { return glyphs_.size(); }
    int missing() const { return missing_; }   // requested code points absent from the fonts
private:
    scene::Image image_;
    std::map<std::uint64_t, Glyph> glyphs_;
    std::map<std::uint64_t, float> kerning_;
    std::array<float, kFaces> ascent_{}, descent_{};
    float range_{6};
    int shelf_x_{}, shelf_y_{}, shelf_h_{}, missing_{};
    friend struct AtlasBuilder;
};

std::u32string utf8_decode(std::string_view s);

struct TextStyle {
    Face face{Face::serif};
    double size{16};           // px (CSS font-size)
    Color color{};
    double spacing{0};         // letter-spacing in em (CSS)
    Align align{Align::left};
    double weight{0};          // 0 normal; > 0 thickens the SDF edge (bold look)
    std::optional<Color> shadow;   // drop shadow 1 px down
};

class Canvas {   // pixel coordinates, origin top-left
public:
    Canvas(const Atlas* atlas, double width, double height) : atlas_(atlas), width_(width), height_(height) {}
    double width() const { return width_; }
    double height() const { return height_; }
    const std::vector<Vertex>& vertices() const { return vertices_; }
    void clear() { vertices_.clear(); }
    const Atlas* atlas() const { return atlas_; }
    // Clipping (rectangles only, intersected): geometry outside is cut, text/images are cut per quad.
    void push_clip(const Rect& r);
    void pop_clip();
    void rect(const Rect& r, Color c);
    void gradient(const Rect& r, Color top, Color bottom);
    void gradient_h(const Rect& r, Color left, Color right);
    void frame(const Rect& r, double thickness, Color c);
    void line(double x0, double y0, double x1, double y1, double thickness, Color c);
    void polygon(const std::vector<std::array<double, 2>>& convex, Color c);
    void polyline(const std::vector<std::array<double, 2>>& points, double thickness, Color c, bool closed);
    // The legacy --feral-cut shape: corners cut at 45 degrees by `cut` px; fill (top/bottom gradient) and outline.
    static std::vector<std::array<double, 2>> cut_shape(const Rect& r, double cut);
    // Pointed button (.cat-nav button clip-path): chevron ends of depth `point` px.
    static std::vector<std::array<double, 2>> pointed_shape(const Rect& r, double point);
    void shape(const std::vector<std::array<double, 2>>& convex, Color top, Color bottom, double outline, Color outline_color);
    void circle(double cx, double cy, double radius, Color c, int segments = 28);
    void ring(double cx, double cy, double radius, double thickness, Color c, int segments = 32);
    void image(const Region& region, const Rect& r, Color tint = {}, std::optional<Rect> crop = std::nullopt);   // crop: sub-rectangle in region pixels
    double text(double x, double baseline, std::string_view utf8, const TextStyle& s);   // returns the width
    double measure(std::string_view utf8, const TextStyle& s) const;
    // Word-wrapped paragraph; returns the height used.
    double paragraph(const Rect& r, std::string_view utf8, const TextStyle& s, double line_height);
    std::size_t triangles() const { return vertices_.size() / 3; }
private:
    void tri(const std::array<double, 2>& a, const std::array<double, 2>& b, const std::array<double, 2>& c, Color ca, Color cb, Color cc);
    void quad_uv(double x0, double y0, double x1, double y1, float u0, float v0, float u1, float v1, Color c, float mode);
    void emit(double x, double y, float u, float v, Color c, float mode);
    const Atlas* atlas_;
    double width_, height_;
    std::vector<Vertex> vertices_;
    std::vector<Rect> clips_;
};

}  // namespace cat::ui
