// N7: UI canvas (triangles with per-vertex colour, atlas UV and a mode; rectangle clipping).
#include "cat/ui.hpp"

#include <algorithm>
#include <cmath>

namespace cat::ui {
namespace {
constexpr double kPi = 3.141592653589793;
struct V { double x, y; float u, v; Color c; float mode; };
V lerp(const V& a, const V& b, double t) {
    const auto f = static_cast<float>(t);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.u + (b.u - a.u) * f, a.v + (b.v - a.v) * f, mix(a.c, b.c, f), a.mode};
}
// Sutherland-Hodgman against one axis-aligned edge.
template <class Inside, class Cross>
std::vector<V> clip_edge(const std::vector<V>& in, Inside inside, Cross cross) {
    std::vector<V> out;
    for(std::size_t i = 0; i < in.size(); ++i) {
        const V& a = in[i];
        const V& b = in[(i + 1) % in.size()];
        const bool ia = inside(a), ib = inside(b);
        if(ia) out.push_back(a);
        if(ia != ib) out.push_back(lerp(a, b, cross(a, b)));
    }
    return out;
}
}  // namespace

Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }

std::u32string utf8_decode(std::string_view s) {
    std::u32string out;
    for(std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        int extra = 0;
        if(c >= 0xF0) { cp = c & 7; extra = 3; } else if(c >= 0xE0) { cp = c & 15; extra = 2; } else if(c >= 0xC0) { cp = c & 31; extra = 1; }
        ++i;
        for(int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 63);
        out.push_back(cp);
    }
    return out;
}

void Canvas::push_clip(const Rect& r) {
    Rect c = r;
    if(!clips_.empty()) {
        const Rect& p = clips_.back();
        const double x0 = std::max(c.x, p.x), y0 = std::max(c.y, p.y), x1 = std::min(c.right(), p.right()), y1 = std::min(c.bottom(), p.bottom());
        c = {x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
    }
    clips_.push_back(c);
}
void Canvas::pop_clip() { if(!clips_.empty()) clips_.pop_back(); }

void Canvas::emit(double x, double y, float u, float v, Color c, float mode) {
    vertices_.push_back({static_cast<float>(x / width_ * 2 - 1), static_cast<float>(y / height_ * 2 - 1), u, v, c.r, c.g, c.b, c.a, mode});
}

void Canvas::tri(const std::array<double, 2>& a, const std::array<double, 2>& b, const std::array<double, 2>& c, Color ca, Color cb, Color cc) {
    std::vector<V> poly{{a[0], a[1], 0, 0, ca, 0}, {b[0], b[1], 0, 0, cb, 0}, {c[0], c[1], 0, 0, cc, 0}};
    if(!clips_.empty()) {
        const Rect& r = clips_.back();
        poly = clip_edge(poly, [&](const V& p) { return p.x >= r.x; }, [&](const V& p, const V& q) { return (r.x - p.x) / (q.x - p.x); });
        poly = clip_edge(poly, [&](const V& p) { return p.x <= r.right(); }, [&](const V& p, const V& q) { return (r.right() - p.x) / (q.x - p.x); });
        poly = clip_edge(poly, [&](const V& p) { return p.y >= r.y; }, [&](const V& p, const V& q) { return (r.y - p.y) / (q.y - p.y); });
        poly = clip_edge(poly, [&](const V& p) { return p.y <= r.bottom(); }, [&](const V& p, const V& q) { return (r.bottom() - p.y) / (q.y - p.y); });
    }
    for(std::size_t i = 1; i + 1 < poly.size(); ++i)
        for(const V* v : {&poly[0], &poly[i], &poly[i + 1]}) emit(v->x, v->y, v->u, v->v, v->c, v->mode);
}

void Canvas::quad_uv(double x0, double y0, double x1, double y1, float u0, float v0, float u1, float v1, Color c, float mode) {
    if(!clips_.empty()) {   // axis-aligned: clip by adjusting the texture coordinates
        const Rect& r = clips_.back();
        const double cx0 = std::max(x0, r.x), cy0 = std::max(y0, r.y), cx1 = std::min(x1, r.right()), cy1 = std::min(y1, r.bottom());
        if(cx0 >= cx1 || cy0 >= cy1) return;
        const auto fu = [&](double x) { return u0 + static_cast<float>((x - x0) / (x1 - x0)) * (u1 - u0); };
        const auto fv = [&](double y) { return v0 + static_cast<float>((y - y0) / (y1 - y0)) * (v1 - v0); };
        const float nu0 = fu(cx0), nu1 = fu(cx1), nv0 = fv(cy0), nv1 = fv(cy1);
        x0 = cx0; x1 = cx1; y0 = cy0; y1 = cy1; u0 = nu0; u1 = nu1; v0 = nv0; v1 = nv1;
    }
    emit(x0, y0, u0, v0, c, mode); emit(x1, y0, u1, v0, c, mode); emit(x1, y1, u1, v1, c, mode);
    emit(x0, y0, u0, v0, c, mode); emit(x1, y1, u1, v1, c, mode); emit(x0, y1, u0, v1, c, mode);
}

void Canvas::rect(const Rect& r, Color c) { gradient(r, c, c); }
void Canvas::gradient(const Rect& r, Color top, Color bottom) {
    if(r.w <= 0 || r.h <= 0) return;
    tri({r.x, r.y}, {r.right(), r.y}, {r.right(), r.bottom()}, top, top, bottom);
    tri({r.x, r.y}, {r.right(), r.bottom()}, {r.x, r.bottom()}, top, bottom, bottom);
}
void Canvas::gradient_h(const Rect& r, Color left, Color right) {
    if(r.w <= 0 || r.h <= 0) return;
    tri({r.x, r.y}, {r.right(), r.y}, {r.right(), r.bottom()}, left, right, right);
    tri({r.x, r.y}, {r.right(), r.bottom()}, {r.x, r.bottom()}, left, right, left);
}
void Canvas::frame(const Rect& r, double t, Color c) {
    rect({r.x, r.y, r.w, t}, c); rect({r.x, r.bottom() - t, r.w, t}, c);
    rect({r.x, r.y + t, t, r.h - 2 * t}, c); rect({r.right() - t, r.y + t, t, r.h - 2 * t}, c);
}
void Canvas::line(double x0, double y0, double x1, double y1, double t, Color c) {
    const double dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
    if(len <= 0) return;
    const double nx = -dy / len * t / 2, ny = dx / len * t / 2;
    tri({x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, c, c, c);
    tri({x0 + nx, y0 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}, c, c, c);
}
void Canvas::polygon(const std::vector<std::array<double, 2>>& p, Color c) {
    for(std::size_t i = 1; i + 1 < p.size(); ++i) tri(p[0], p[i], p[i + 1], c, c, c);
}
void Canvas::polyline(const std::vector<std::array<double, 2>>& p, double t, Color c, bool closed) {
    for(std::size_t i = 0; i + 1 < p.size(); ++i) line(p[i][0], p[i][1], p[i + 1][0], p[i + 1][1], t, c);
    if(closed && p.size() > 2) line(p.back()[0], p.back()[1], p[0][0], p[0][1], t, c);
}
std::vector<std::array<double, 2>> Canvas::cut_shape(const Rect& r, double k) {
    k = std::min({k, r.w / 2, r.h / 2});
    return {{r.x, r.y + k}, {r.x + k, r.y}, {r.right() - k, r.y}, {r.right(), r.y + k}, {r.right(), r.bottom() - k}, {r.right() - k, r.bottom()}, {r.x + k, r.bottom()}, {r.x, r.bottom() - k}};
}
std::vector<std::array<double, 2>> Canvas::pointed_shape(const Rect& r, double p) {
    p = std::min(p, r.w / 4);
    const double my = r.y + r.h / 2;
    return {{r.x, my}, {r.x + p, r.y}, {r.right() - p, r.y}, {r.right(), my}, {r.right() - p, r.bottom()}, {r.x + p, r.bottom()}};
}
void Canvas::shape(const std::vector<std::array<double, 2>>& p, Color top, Color bottom, double outline, Color oc) {
    if(p.size() < 3) return;
    double y0 = p[0][1], y1 = p[0][1];
    for(const auto& q : p) { y0 = std::min(y0, q[1]); y1 = std::max(y1, q[1]); }
    auto col = [&](const std::array<double, 2>& q) { return y1 > y0 ? mix(top, bottom, static_cast<float>((q[1] - y0) / (y1 - y0))) : top; };
    for(std::size_t i = 1; i + 1 < p.size(); ++i) tri(p[0], p[i], p[i + 1], col(p[0]), col(p[i]), col(p[i + 1]));
    if(outline > 0) polyline(p, outline, oc, true);
}
void Canvas::circle(double cx, double cy, double radius, Color c, int segments) {
    for(int i = 0; i < segments; ++i) {
        const double a0 = 2 * kPi * i / segments, a1 = 2 * kPi * (i + 1) / segments;
        tri({cx, cy}, {cx + std::cos(a0) * radius, cy + std::sin(a0) * radius}, {cx + std::cos(a1) * radius, cy + std::sin(a1) * radius}, c, c, c);
    }
}
void Canvas::ring(double cx, double cy, double radius, double t, Color c, int segments) {
    for(int i = 0; i < segments; ++i) {
        const double a0 = 2 * kPi * i / segments, a1 = 2 * kPi * (i + 1) / segments;
        const double r0 = radius - t / 2, r1 = radius + t / 2;
        const std::array<double, 2> p0{cx + std::cos(a0) * r0, cy + std::sin(a0) * r0}, p1{cx + std::cos(a0) * r1, cy + std::sin(a0) * r1};
        const std::array<double, 2> p2{cx + std::cos(a1) * r1, cy + std::sin(a1) * r1}, p3{cx + std::cos(a1) * r0, cy + std::sin(a1) * r0};
        tri(p0, p1, p2, c, c, c); tri(p0, p2, p3, c, c, c);
    }
}
void Canvas::image(const Region& g, const Rect& r, Color tint, std::optional<Rect> crop) {
    float u0 = g.u0, v0 = g.v0, u1 = g.u1, v1 = g.v1;
    if(crop && g.width > 0 && g.height > 0) {
        const float du = (g.u1 - g.u0) / static_cast<float>(g.width), dv = (g.v1 - g.v0) / static_cast<float>(g.height);
        u0 = g.u0 + du * static_cast<float>(crop->x); u1 = g.u0 + du * static_cast<float>(crop->right());
        v0 = g.v0 + dv * static_cast<float>(crop->y); v1 = g.v0 + dv * static_cast<float>(crop->bottom());
    }
    quad_uv(r.x, r.y, r.right(), r.bottom(), u0, v0, u1, v1, tint, 2);
}

double Canvas::measure(std::string_view utf8, const TextStyle& s) const {
    if(!atlas_) return 0;
    const auto text = utf8_decode(utf8);
    double w = 0;
    for(std::size_t i = 0; i < text.size(); ++i) {
        const Glyph* g = atlas_->glyph(s.face, text[i]);
        if(!g) g = atlas_->glyph(s.face, U'?');
        if(!g) continue;
        w += g->advance * s.size + s.spacing * s.size;
        if(i + 1 < text.size()) w += atlas_->kern(s.face, text[i], text[i + 1]) * s.size;
    }
    return w;
}

double Canvas::text(double x, double baseline, std::string_view utf8, const TextStyle& s) {
    if(!atlas_) return 0;
    const double width = measure(utf8, s);
    if(s.align == Align::center) x -= width / 2; else if(s.align == Align::right) x -= width;
    if(s.shadow) { TextStyle sh = s; sh.shadow.reset(); sh.color = *s.shadow; sh.align = Align::left; text(x, baseline + std::max(1.0, s.size / 18), utf8, sh); }
    const auto text = utf8_decode(utf8);
    const float mode = 1 + static_cast<float>(std::clamp(s.weight, 0.0, .9));
    double pen = x;
    for(std::size_t i = 0; i < text.size(); ++i) {
        const Glyph* g = atlas_->glyph(s.face, text[i]);
        if(!g) g = atlas_->glyph(s.face, U'?');
        if(!g) continue;
        if(g->u1 > g->u0) quad_uv(pen + g->x0 * s.size, baseline + g->y0 * s.size, pen + g->x1 * s.size, baseline + g->y1 * s.size, g->u0, g->v0, g->u1, g->v1, s.color, mode);
        pen += g->advance * s.size + s.spacing * s.size;
        if(i + 1 < text.size()) pen += atlas_->kern(s.face, text[i], text[i + 1]) * s.size;
    }
    return width;
}

double Canvas::paragraph(const Rect& r, std::string_view utf8, const TextStyle& s, double line_height) {
    std::vector<std::string> words;
    std::string word;
    for(char c : utf8) { if(c == ' ') { if(!word.empty()) words.push_back(word); word.clear(); } else word += c; }
    if(!word.empty()) words.push_back(word);
    std::string line;
    double y = r.y + line_height * .78;
    const double first = y;
    auto flush = [&] {
        if(line.empty()) return;
        const double x = s.align == Align::center ? r.x + r.w / 2 : s.align == Align::right ? r.right() : r.x;
        text(x, y, line, s);
        y += line_height; line.clear();
    };
    for(const auto& w : words) {
        const std::string candidate = line.empty() ? w : line + " " + w;
        if(!line.empty() && measure(candidate, s) > r.w) { flush(); line = w; } else line = candidate;
    }
    flush();
    return y - first + line_height * .22;
}

}  // namespace cat::ui
