// N2 scene preparation. Every rule below cites the legacy/index.html source it reproduces.
#include "cat/scene.hpp"
#include "cat/utf8_path.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <unordered_map>

namespace cat::scene {
namespace {
[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("scene: " + text); }
void need(bool ok, const std::string& text) { if(!ok) fail(text); }
const assets::Value* find(const assets::Value& object, std::string_view key) {
    const auto& o = object.object();
    auto it = o.find(key);
    return it == o.end() ? nullptr : &it->second;
}
bool is_null(const assets::Value* v) { return v == nullptr || std::holds_alternative<std::nullptr_t>(v->data); }
double number_or(const assets::Value& object, std::string_view key, double fallback) {
    const auto* v = find(object, key);
    return is_null(v) ? fallback : v->number();
}
bool bool_or(const assets::Value& object, std::string_view key, bool fallback) {
    const auto* v = find(object, key);
    return is_null(v) ? fallback : v->boolean();
}
std::string string_or(const assets::Value& object, std::string_view key, std::string fallback) {
    const auto* v = find(object, key);
    return is_null(v) ? fallback : v->string();
}
std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    need(bool(f), "cannot open " + path_utf8(path));
    const auto size = f.tellg();
    need(size > 0 && size < (std::streamoff(1) << 30), "invalid file size " + path_utf8(path));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    need(bool(f), "read failed " + path_utf8(path));
    return bytes;
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

// ---------------------------------------------------------------- images
Image load_image(const std::filesystem::path& path) {
    try { return decode_image(read_bytes(path)); }
    catch(const std::exception& e) { fail(path_utf8(path) + ": " + e.what()); }
}

Image load_raw_rgba(const std::filesystem::path& path) {
    auto bytes = read_bytes(path);
    need(bytes.size() % 4 == 0, "raw RGBA size not a multiple of 4: " + path_utf8(path));
    const auto pixels = bytes.size() / 4;
    auto side = static_cast<std::uint32_t>(std::llround(std::sqrt(static_cast<double>(pixels))));
    need(std::size_t(side) * side == pixels && side >= 1 && (side & (side - 1)) == 0,
         "raw RGBA is not a square power-of-two texture: " + path_utf8(path));
    return Image{side, side, std::move(bytes)};
}

void flip_rows(Image& image) {
    const std::size_t row = std::size_t(image.width) * 4U;
    for(std::uint32_t y = 0; y < image.height / 2; ++y)
        std::swap_ranges(image.rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                         image.rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                         image.rgba.begin() + static_cast<std::ptrdiff_t>((image.height - 1 - y) * row));
}

std::vector<Image> mip_chain(const Image& base) {
    need(base.width && base.height && base.rgba.size() == std::size_t(base.width) * base.height * 4U, "mip_chain: malformed image");
    std::vector<Image> levels{base};
    while(levels.back().width > 1 || levels.back().height > 1) {
        const Image& src = levels.back();
        Image dst{std::max(1U, src.width / 2), std::max(1U, src.height / 2), {}};
        dst.rgba.resize(std::size_t(dst.width) * dst.height * 4U);
        for(std::uint32_t y = 0; y < dst.height; ++y)
            for(std::uint32_t x = 0; x < dst.width; ++x)
                for(std::uint32_t c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for(std::uint32_t dy = 0; dy < 2; ++dy)
                        for(std::uint32_t dx = 0; dx < 2; ++dx) {
                            const auto sx = std::min(src.width - 1, x * 2 + dx), sy = std::min(src.height - 1, y * 2 + dy);
                            sum += src.rgba[(std::size_t(sy) * src.width + sx) * 4U + c];
                        }
                    dst.rgba[(std::size_t(y) * dst.width + x) * 4U + c] = static_cast<std::uint8_t>((sum + 2U) / 4U);
                }
        levels.push_back(std::move(dst));
    }
    return levels;
}

std::vector<Image> world_atlas_layers(const Image& atlas) {
    // Q3R.rebuildWorld (legacy 2814-2829): T=128, layer m = tile (m%4, floor(m/4)), and
    // layers.set(px row T-1-y -> row y): every layer is vertically flipped before upload.
    need(atlas.width == 512 && atlas.height == 512, "world atlas must be 512x512");
    std::vector<Image> layers;
    for(std::uint32_t m = 0; m < 16; ++m) {
        Image layer{128, 128, std::vector<std::uint8_t>(128U * 128U * 4U)};
        const std::uint32_t ox = (m % 4) * 128, oy = (m / 4) * 128;
        for(std::uint32_t y = 0; y < 128; ++y) {
            const auto src = ((std::size_t(oy) + 127 - y) * 512 + ox) * 4U;
            std::copy_n(atlas.rgba.begin() + static_cast<std::ptrdiff_t>(src), 128 * 4,
                        layer.rgba.begin() + static_cast<std::ptrdiff_t>(std::size_t(y) * 128 * 4));
        }
        layers.push_back(std::move(layer));
    }
    return layers;
}

std::filesystem::path default_runtime_world_atlas(const std::filesystem::path& asset_root) {
    auto candidate = asset_root.parent_path() / "n2" / "world-atlas-runtime.png";
    if(asset_root.filename().empty()) candidate = asset_root.parent_path().parent_path() / "n2" / "world-atlas-runtime.png";
    return std::filesystem::exists(candidate) ? candidate : std::filesystem::path{};
}

std::filesystem::path default_sky_box_dir(const std::filesystem::path& asset_root) {
    auto candidate = asset_root.parent_path() / "n4";
    if(asset_root.filename().empty()) candidate = asset_root.parent_path().parent_path() / "n4";
    return std::filesystem::exists(candidate) ? candidate : std::filesystem::path{};
}

// ---------------------------------------------------------------- view
Camera legacy_camera(double x, double y, double feet_z, double yaw, double pitch, double hfov_deg, double aspect) {
    need(std::isfinite(x) && std::isfinite(y) && std::isfinite(feet_z) && std::isfinite(yaw) && std::isfinite(pitch), "camera: nonfinite pose");
    need(hfov_deg > 1 && hfov_deg < 179 && aspect > 0, "camera: invalid fov/aspect");
    Camera c;
    const double ca = std::cos(yaw), sa = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    c.position = {x, y, feet_z + kEyeHeight};   // q3ShotOrigin: p.z + V.eye
    c.forward = {ca * cp, sa * cp, -sp};          // q3ViewCamera (legacy 10537)
    c.right = {-sa, ca, 0};
    c.up = {ca * sp, sa * sp, cp};
    c.yaw = yaw; c.pitch = pitch; c.fov_deg = hfov_deg; c.aspect = aspect;
    // render(): tanHalf = tan(fov/2)/aspect is the vertical term; x divides by tanHalf*aspect.
    c.tan_half_x = std::tan(hfov_deg * std::numbers::pi / 360.0);
    c.tan_half_y = c.tan_half_x / aspect;
    return c;
}

std::array<double, 4> clip_position(const Camera& c, const V3& p) {
    const V3 r{p[0] - c.position[0], p[1] - c.position[1], p[2] - c.position[2]};
    auto dot = [&](const V3& v) { return r[0] * v[0] + r[1] * v[1] + r[2] * v[2]; };
    const double depth = dot(c.forward);
    // GL z = depth*1.00025003-0.1000125 maps [n,f] to [-1,1]; Vulkan z = (z_gl + w)/2 gives the
    // identical window depth in [0,1]. Y is negated because Vulkan framebuffer Y points down.
    return {dot(c.right) / c.tan_half_x, -dot(c.up) / c.tan_half_y, depth * 1.000125015 - 0.05000625, depth};
}

bool in_view(const Camera& c, double x, double y, double z, double r) {
    // Legacy inView (5302-5317), using the eye position.
    const double ca = std::cos(c.yaw), sa = std::sin(c.yaw), cp = std::cos(c.pitch), sp = std::sin(c.pitch);
    const double th = std::tan(std::min(88.0, c.fov_deg * .5 + 3) * std::numbers::pi / 180), tv = th / c.aspect;
    const double dx = x - c.position[0], dy = y - c.position[1], dz = z - c.position[2];
    const double depth = dx * ca * cp + dy * sa * cp - dz * sp;
    if(depth < -r - 1.25) return false;
    const double right = -dx * sa + dy * ca, up = dx * ca * sp + dy * sa * sp + dz * cp;
    return std::abs(right) <= std::max(0.0, depth) * th + r + 2.0 && std::abs(up) <= std::max(0.0, depth) * tv + r + 2.0;
}

// ---------------------------------------------------------------- waves (q3-shader 2151-2171)
namespace {
const std::array<float, 1024>& noise_table() {
    static const auto table = [] {
        std::array<float, 1024> t{};
        std::uint32_t s = 0x51ed2701U;
        for(auto& v : t) {
            s = s * 1664525U + 1013904223U;   // Math.imul + >>>0
            v = static_cast<float>(static_cast<double>(s) / 4294967296.0 * 2 - 1);   // Float32Array
        }
        return t;
    }();
    return table;
}
}

double noise1(double t) {
    constexpr long long N = 256;
    const auto& table = noise_table();
    const double i = std::floor(t), f = t - i;
    const auto wrap = [](long long k) { return static_cast<std::size_t>(((k % N) + N) % N); };
    const auto ii = static_cast<long long>(i);
    const double a = table[wrap(ii)], b = table[wrap(ii + 1)], u = f * f * (3 - 2 * f);
    return a + (b - a) * u;
}

double wave_table(WaveFunc func, double x) {
    x -= std::floor(x);
    switch(func) {
        case WaveFunc::sin: return std::sin(x * 2 * std::numbers::pi);
        case WaveFunc::triangle: return x < .25 ? x * 4 : x < .75 ? 2 - x * 4 : x * 4 - 4;
        case WaveFunc::square: return x < .5 ? 1 : -1;
        case WaveFunc::sawtooth: return x;
        case WaveFunc::inverse_sawtooth: return 1 - x;
        case WaveFunc::noise: return 0;
    }
    return 0;
}

double eval_wave(const Wave& w, double time, double extra) {
    if(w.func == WaveFunc::noise) return w.base + w.amp * noise1((time + w.phase + extra) * w.freq);
    return w.base + w.amp * wave_table(w.func, w.phase + extra + time * w.freq);
}

// ---------------------------------------------------------------- tcMod (q3-shader 2181-2221)
TexMatrix compose(const TexMatrix& M, const TexMatrix& N) {
    return {M.a * N.a + M.b * N.c, M.a * N.b + M.b * N.d, M.c * N.a + M.d * N.c, M.c * N.b + M.d * N.d,
            M.a * N.e + M.b * N.f + M.e, M.c * N.e + M.d * N.f + M.f};
}

TexMatrix tcmod_matrix(const TcMod& mod, double time) {
    switch(mod.type) {
        case TcModType::scroll: {
            double s = mod.s * time, t = mod.t * time;
            s -= std::floor(s); t -= std::floor(t);
            return {1, 0, 0, 1, s, t};
        }
        case TcModType::scale: return {mod.s, 0, 0, mod.t, 0, 0};
        case TcModType::rotate: {
            const double r = -mod.speed * time * std::numbers::pi / 180, c = std::cos(r), s = std::sin(r);
            return {c, -s, s, c, .5 - .5 * c + .5 * s, .5 - .5 * s - .5 * c};
        }
        case TcModType::stretch: {
            const double w = eval_wave(mod.wave, time), p = w == 0 ? 1 : 1 / w;
            return {p, 0, 0, p, .5 - .5 * p, .5 - .5 * p};
        }
        case TcModType::transform: return {mod.m[0], mod.m[2], mod.m[1], mod.m[3], mod.translate[0], mod.translate[1]};
        case TcModType::entity_translate: return {};   // no entity scroll on static surfaces
        case TcModType::turb: return {};
    }
    return {};
}

StageTexture stage_texture(const Stage& stage, double time) {
    StageTexture out;
    for(const auto& mod : stage.tcmods) {
        if(mod.type == TcModType::turb) {
            if(!out.turb) { out.turb = true; out.turb_amp = mod.amp; out.turb_phase = mod.phase; out.turb_freq = mod.freq; }
            continue;
        }
        const auto M = tcmod_matrix(mod, time);
        if(out.turb) out.post = compose(M, out.post); else out.pre = compose(M, out.pre);
    }
    return out;
}

std::array<double, 4> stage_color(const Stage& s, double time) {
    // Q3MAT.writeStage passes overbright=1 and no entity colour for static surfaces.
    double r = 1, g = 1, b = 1, a = 1;
    auto clamp01 = [](double v) { return v < 0 ? 0 : v > 1 ? 1 : v; };
    switch(s.rgb) {
        case RgbGen::identity_lighting: r = g = b = 1; break;
        case RgbGen::wave: r = g = b = clamp01(eval_wave(s.rgb_wave, time)); break;
        case RgbGen::constant: r = s.rgb_const[0]; g = s.rgb_const[1]; b = s.rgb_const[2]; break;
        case RgbGen::one_minus_entity: r = g = b = 0; break;
        default: break;
    }
    switch(s.alpha) {
        case AlphaGen::wave: a = clamp01(eval_wave(s.alpha_wave, time)); break;
        case AlphaGen::constant: a = s.alpha_const; break;
        case AlphaGen::one_minus_entity: a = 0; break;
        default: break;
    }
    return {r, g, b, a};
}

int rgb_code(RgbGen g) {
    switch(g) {
        case RgbGen::vertex: return 1;
        case RgbGen::exact_vertex: return 2;
        case RgbGen::one_minus_vertex: return 3;
        case RgbGen::lighting_diffuse: return 4;
        default: return 0;
    }
}
int alpha_code(AlphaGen g) {
    switch(g) {
        case AlphaGen::vertex: return 1;
        case AlphaGen::one_minus_vertex: return 2;
        case AlphaGen::lighting_specular: return 3;
        case AlphaGen::portal: return 4;
        default: return 0;
    }
}

// ---------------------------------------------------------------- material parsing
namespace {
WaveFunc parse_func(const std::string& f) {
    static const std::map<std::string, WaveFunc, std::less<>> table{
        {"sin", WaveFunc::sin}, {"triangle", WaveFunc::triangle}, {"square", WaveFunc::square},
        {"sawtooth", WaveFunc::sawtooth}, {"inversesawtooth", WaveFunc::inverse_sawtooth}, {"noise", WaveFunc::noise}};
    auto it = table.find(lower(f));
    need(it != table.end(), "unknown wave function " + f);
    return it->second;
}
Wave parse_wave(const assets::Value& w) {
    return {parse_func(w.at("func").string()), w.at("base").number(), w.at("amp").number(), w.at("phase").number(), w.at("freq").number()};
}
std::array<double, 3> vec3(const assets::Value& v) {
    const auto& a = v.array();
    need(a.size() == 3, "expected 3-vector");
    return {a[0].number(), a[1].number(), a[2].number()};
}
TcMod parse_tcmod(const assets::Value& v) {
    TcMod m;
    const auto type = v.at("type").string();
    if(type == "scroll") { m.type = TcModType::scroll; m.s = v.at("s").number(); m.t = v.at("t").number(); }
    else if(type == "scale") { m.type = TcModType::scale; m.s = v.at("s").number(); m.t = v.at("t").number(); }
    else if(type == "rotate") { m.type = TcModType::rotate; m.speed = v.at("speed").number(); }
    else if(type == "stretch") { m.type = TcModType::stretch; m.wave = parse_wave(v.at("wave")); }
    else if(type == "transform") {
        m.type = TcModType::transform;
        const auto& a = v.at("m").array(); const auto& t = v.at("t").array();
        need(a.size() == 4 && t.size() == 2, "tcMod transform shape");
        m.m = {a[0].number(), a[1].number(), a[2].number(), a[3].number()};
        m.translate = {t[0].number(), t[1].number()};
    }
    else if(type == "turb") { m.type = TcModType::turb; m.amp = v.at("amp").number(); m.phase = v.at("phase").number(); m.freq = v.at("freq").number(); }
    else if(type == "entityTranslate") m.type = TcModType::entity_translate;
    else fail("unsupported tcMod " + type);
    return m;
}
Stage parse_stage(const assets::Value& v) {
    Stage s;
    s.map = string_or(v, "map", "");
    if(const auto* maps = find(v, "maps"); !is_null(maps))
        for(const auto& m : maps->array()) s.maps.push_back(m.string());
    s.anim_freq = number_or(v, "animFreq", 0);
    if(const auto* blend = find(v, "blend"); !is_null(blend)) {
        const auto& b = blend->array();
        need(b.size() == 2, "blend shape");
        s.has_blend = true;
        s.blend_src = static_cast<int>(b[0].number());
        s.blend_dst = static_cast<int>(b[1].number());
        need(s.blend_src >= 0 && s.blend_src <= 10 && s.blend_dst >= 0 && s.blend_dst <= 10, "blend code out of range");
    }
    const auto af = string_or(v, "alphaFunc", "");
    s.alpha_func = af.empty() ? AlphaFunc::none : af == "GT0" ? AlphaFunc::gt0 : af == "LT128" ? AlphaFunc::lt128
                 : af == "GE128" ? AlphaFunc::ge128 : (fail("unknown alphaFunc " + af), AlphaFunc::none);
    s.depth_write = bool_or(v, "depthWrite", true);
    const auto df = string_or(v, "depthFunc", "lequal");
    need(df == "lequal" || df == "equal", "unknown depthFunc " + df);
    s.depth_equal = df == "equal";
    s.clamp = bool_or(v, "clamp", false);
    s.detail = bool_or(v, "detail", false);

    const auto& rgb = v.at("rgbGen");
    const auto rt = lower(rgb.at("type").string());
    if(rt == "identity") s.rgb = RgbGen::identity;
    else if(rt == "identitylighting") s.rgb = RgbGen::identity_lighting;
    else if(rt == "const") { s.rgb = RgbGen::constant; s.rgb_const = vec3(rgb.at("color")); }
    else if(rt == "wave") { s.rgb = RgbGen::wave; s.rgb_wave = parse_wave(rgb.at("wave")); }
    else if(rt == "entity") s.rgb = RgbGen::entity;
    else if(rt == "oneminusentity") s.rgb = RgbGen::one_minus_entity;
    else if(rt == "vertex") s.rgb = RgbGen::vertex;
    else if(rt == "exactvertex") s.rgb = RgbGen::exact_vertex;
    else if(rt == "oneminusvertex") s.rgb = RgbGen::one_minus_vertex;
    else if(rt == "lightingdiffuse") s.rgb = RgbGen::lighting_diffuse;
    else fail("unknown rgbGen " + rt);

    const auto& alpha = v.at("alphaGen");
    const auto at = lower(alpha.at("type").string());
    if(at == "identity") s.alpha = AlphaGen::identity;
    else if(at == "const") { s.alpha = AlphaGen::constant; s.alpha_const = alpha.at("value").number(); }
    else if(at == "wave") { s.alpha = AlphaGen::wave; s.alpha_wave = parse_wave(alpha.at("wave")); }
    else if(at == "entity") s.alpha = AlphaGen::entity;
    else if(at == "oneminusentity") s.alpha = AlphaGen::one_minus_entity;
    else if(at == "vertex") s.alpha = AlphaGen::vertex;
    else if(at == "oneminusvertex") s.alpha = AlphaGen::one_minus_vertex;
    else if(at == "lightingspecular") s.alpha = AlphaGen::lighting_specular;
    else if(at == "portal") { s.alpha = AlphaGen::portal; s.portal_range = alpha.at("range").number(); }
    else fail("unknown alphaGen " + at);

    const auto& tc = v.at("tcGen");
    const auto tt = tc.at("type").string();
    if(tt == "base") s.tcgen = TcGen::base;
    else if(tt == "lightmap") s.tcgen = TcGen::lightmap;
    else if(tt == "environment") s.tcgen = TcGen::environment;
    else if(tt == "vector") { s.tcgen = TcGen::vector; s.tc_s = vec3(tc.at("s")); s.tc_t = vec3(tc.at("t")); }
    else fail("unknown tcGen " + tt);
    for(const auto& m : v.at("tcMods").array()) s.tcmods.push_back(parse_tcmod(m));

    // Q3MAT.resolve (3769-3779): texture source from the map key.
    const auto& key = s.map;
    if(key == "$lightmap") s.source = Source::lightmap;
    else if(key == "$whiteimage" || key == "*white") s.source = Source::white;
    else if(key == "$mirror" || key == "$portal") s.source = Source::mirror;
    else if(key == "$portalfeed") s.source = Source::portal_feed;
    else if(key.starts_with("$world:")) {
        s.source = Source::world;
        const auto layer = std::stoi(key.substr(7));
        need(layer >= 0 && layer < 16, "world atlas layer out of range " + key);
        s.world_layer = static_cast<std::uint32_t>(layer);
    }
    else if(key == "$vertexlight") s.source = Source::vertex_light;
    else { need(!key.empty() && key[0] != '$', "unsupported special map " + key); s.source = Source::material; }
    return s;
}
Deform parse_deform(const assets::Value& v) {
    Deform d;
    const auto type = v.at("type").string();
    if(type == "wave") { d.type = DeformType::wave; d.spread = v.at("spread").number(); d.wave = parse_wave(v.at("wave")); }
    else if(type == "bulge") { d.type = DeformType::bulge; d.width = v.at("width").number(); d.height = v.at("height").number(); d.speed = v.at("speed").number(); }
    else if(type == "move") { d.type = DeformType::move; d.vec = vec3(v.at("vec")); d.wave = parse_wave(v.at("wave")); }
    else if(type == "normal") { d.type = DeformType::normal; d.amp = v.at("amp").number(); d.freq = v.at("freq").number(); }
    else if(type == "autosprite") d.type = DeformType::autosprite;
    else if(type == "autosprite2") d.type = DeformType::autosprite2;
    else fail("unsupported deform " + type);
    return d;
}
}  // namespace

Material parse_material(const assets::Value& v) {
    Material m;
    m.name = v.at("name").string();
    m.id = static_cast<int>(number_or(v, "id", -1));
    m.sort = static_cast<int>(v.at("sort").number());
    const auto cull = v.at("cull").string();
    m.cull = cull == "back" ? Cull::back : cull == "front" ? Cull::front : cull == "none" ? Cull::none : (fail("unknown cull " + cull), Cull::back);
    m.translucent = v.at("translucent").boolean();
    m.portal = bool_or(v, "portal", false);
    m.mirror = bool_or(v, "mirror", false);
    m.polygon_offset = bool_or(v, "polygonOffset", false);
    const auto sprite = string_or(v, "autosprite", "");
    m.autosprite = sprite.empty() ? 0 : sprite == "autosprite" ? 1 : sprite == "autosprite2" ? 2 : (fail("unknown autosprite " + sprite), 0);
    for(const auto& d : v.at("deforms").array()) m.deforms.push_back(parse_deform(d));
    for(const auto& s : v.at("stages").array()) m.stages.push_back(parse_stage(s));
    need(m.stages.size() <= 8, "material has more than 8 stages: " + m.name);
    if(const auto* sky = find(v, "sky"); !is_null(sky)) {
        m.sky.present = true;
        m.sky.cloud_height = number_or(*sky, "cloudHeight", 128);
        m.sky.farbox = string_or(*sky, "farbox", "");
        m.sky.nearbox = string_or(*sky, "nearbox", "");
    }
    m.sky_rotate = number_or(v, "skyRotate", 0);
    // Q3MAT.resolve (3783-3787) and q3-shader (2144-2146)
    if(m.autosprite) m.cull = Cull::none;
    m.lightmapped = std::any_of(m.stages.begin(), m.stages.end(), [](const Stage& s) { return s.map == "$lightmap"; });
    m.vertex_lit = std::any_of(m.stages.begin(), m.stages.end(), [](const Stage& s) { return s.source == Source::vertex_light; });
    if(m.vertex_lit) m.lightmapped = true;
    m.all_add = !m.stages.empty() && std::all_of(m.stages.begin(), m.stages.end(), [](const Stage& s) { return s.has_blend && s.blend_src == 1 && s.blend_dst == 1; });
    m.has_detail = std::any_of(m.stages.begin(), m.stages.end(), [](const Stage& s) { return s.detail; });
    return m;
}

std::vector<const Stage*> active_stages(const Material& m, bool detail) {
    // Q3MAT.activeStages (4116); r_detailtextures default on in the normal preset.
    std::vector<const Stage*> out;
    if(!m.has_detail || detail) { for(const auto& s : m.stages) out.push_back(&s); return out; }
    for(const auto& s : m.stages) if(!s.detail) out.push_back(&s);
    if(out.empty() && !m.stages.empty()) out.push_back(&m.stages.front());
    return out;
}

bool can_collapse(const Material& m, std::size_t stage_count) {
    return (!m.translucent || m.all_add) && stage_count > 1;   // S.collapse && isolateStage<0 by default
}

int fog_mode(bool has_blend, int src, int dst, bool collapsed_opaque) {
    if(collapsed_opaque || !has_blend) return 0;
    if(src == 1 && dst == 1) return 1;
    if(src == 4 || dst == 2) return 4;
    if(src == 6) return 2;
    return 3;
}

// ---------------------------------------------------------------- legacy vertex lighting
namespace {
bool inside(double x, double y, const assets::Value& poly) {   // levelInside (8225-8232)
    const auto& p = poly.array();
    bool hit = false;
    for(std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        const double ax = p[i].array()[0].number(), ay = p[i].array()[1].number();
        const double cx = p[j].array()[0].number(), cy = p[j].array()[1].number();
        if((ay > y) != (cy > y) && x < (cx - ax) * (y - ay) / (cy - ay) + ax) hit = !hit;
    }
    return hit;
}
}

WalkGrid::WalkGrid(const assets::Value& compiled) : compiled_(&compiled), blocked_(std::size_t(size) * size) {
    // createMap (11349): map[y][x] = levelWalkable(x+.5, y+.5) ? 0 : 1; buildCollisionCache copies it.
    for(int y = 0; y < size; ++y)
        for(int x = 0; x < size; ++x)
            blocked_[std::size_t(y) * size + std::size_t(x)] = walkable(x + .5, y + .5) ? 0 : 1;
}

bool WalkGrid::walkable(double x, double y) const {
    // levelWalkable (8304-8306) = worldInFootprint && !worldInMassive
    const auto& c = *compiled_;
    bool foot = false;
    for(const auto& f : c.at("footprint").array()) if(inside(x, y, f)) { foot = true; break; }
    if(!foot) return false;
    const double cell = c.at("cell").number();
    const auto gw = static_cast<long long>(c.at("gw").number());
    const auto ix = static_cast<long long>(std::floor(x / cell)), iy = static_cast<long long>(std::floor(y / cell));
    if(ix < 0 || iy < 0 || ix >= gw || iy >= gw) return true;
    const auto& solids = c.at("solids").array();
    for(const auto& id : c.at("solidGrid").array()[static_cast<std::size_t>(iy * gw + ix)].array()) {
        const auto& s = solids.at(static_cast<std::size_t>(id.number()));
        if(bool_or(s, "massive", false) && inside(x, y, s.at("poly"))) return false;
    }
    return true;
}

bool WalkGrid::map_cell(double x, double y) const {
    const auto ix = static_cast<long long>(std::floor(x)), iy = static_cast<long long>(std::floor(y));
    if(ix < 0 || ix >= size || iy < 0 || iy >= size) return true;
    return blocked_[static_cast<std::size_t>(iy * size + ix)] != 0;
}

double legacy_vertex_light(std::span<const float> v, const WalkGrid& grid) {
    // lightVertexColor (5226-5262) with buildings=[] (createMap resets it; WorldKit adds none).
    need(v.size() >= 12, "vertex too short");
    const double x = v[0], y = v[1], z = v[2], nx = v[3], ny = v[4], nz = v[5];
    const float mat = v[11];
    if(mat == 4.0F || mat == 5.0F) return 1;   // LAMP, YELLOW
    const double lambert = std::max(0.0, nx * -.30 + ny * -.60 + nz * .63);
    double lit = .79 + lambert * .13, occlusion = 1;
    if(mat == 1.0F || mat == 2.0F || mat == 9.0F || mat == 6.0F || mat == 14.0F) {   // WALL FLOOR RUST TRIM GRATE
        if(nz > .58 && z < 5.7) {
            int solid = 0;
            if(grid.map_cell(std::floor(x + 0.61), std::floor(y))) ++solid;
            if(grid.map_cell(std::floor(x - 0.61), std::floor(y))) ++solid;
            if(grid.map_cell(std::floor(x), std::floor(y + 0.61))) ++solid;
            if(grid.map_cell(std::floor(x), std::floor(y - 0.61))) ++solid;
            occlusion -= std::min(.22, solid * .062);
        }
        if(nz < -.48) occlusion *= .69;
        if(std::abs(nz) < .36 && z < 1.25) occlusion *= .79;
    }
    return std::max(.48, std::min(1.12, lit * occlusion));
}

// ---------------------------------------------------------------- sky dome (Q3MAT.skyGeometry 4312-4337, q3InitSky 15094-15101)
assets::Mesh sky_dome(std::string_view sky_material, double sun_az) {
    constexpr int rings = 12, segs = 24;
    assets::Mesh mesh;
    mesh.stride = 18;
    auto V = [](double az, double el) { return V3{std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)}; };
    for(int j = 0; j < rings; ++j)
        for(int i = 0; i < segs; ++i) {
            const double half = std::numbers::pi / 2 + .12;
            const double e0 = -.12 + (double(j) / rings) * half, e1 = -.12 + (double(j + 1) / rings) * half;
            const double a0 = double(i) / segs * std::numbers::pi * 2, a1 = double(i + 1) / segs * std::numbers::pi * 2;
            for(const auto& [az, el] : {std::pair{a0, e0}, {a0, e1}, {a1, e1}, {a0, e0}, {a1, e1}, {a1, e0}}) {
                const auto p = V(az, el);
                std::array<float, 18> vtx{};
                for(int k = 0; k < 3; ++k) vtx[std::size_t(k)] = static_cast<float>(p[std::size_t(k)]);   // Float32Array
                std::array<double, 4> color{1, 1, 1, 1};
                if(sky_material == "sky/reactor_bastion") {
                    const double px = vtx[0], py = vtx[1], pz = vtx[2];
                    const double elev = std::asin(std::max(-1.0, std::min(1.0, pz))), azim = std::atan2(py, px);
                    const double sun = std::max(0.0, std::cos(azim - sun_az)) * std::max(0.0, 1 - elev * 1.4);
                    const double a = std::min(1.0, std::max(0.0, (elev - .035) / .2)) * .55;
                    color = {.62 + .42 * sun, .36 + .3 * sun, .3 + .12 * sun, a};
                }
                for(int k = 0; k < 4; ++k) vtx[std::size_t(6 + k)] = static_cast<float>(color[std::size_t(k)]);
                mesh.vertices.insert(mesh.vertices.end(), vtx.begin(), vtx.end());
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.indices.size()));
            }
        }
    return mesh;
}

// ---------------------------------------------------------------- map scene
MapScene load_map_scene(const std::filesystem::path& root, std::string_view map_id, const SceneOptions& options) {
    auto world = assets::load_world(root / "maps" / std::string(map_id) / "map.cadata");
    assets::validate_render_assets(world, root);
    const auto& doc = world.document;
    const auto& render = doc.at("render");
    MapScene scene;
    scene.id = world.id;

    auto mesh = [&](std::string_view key, std::uint32_t stride) {
        auto m = assets::load_mesh(assets::content_path(root, render.at(key).at("file").string()));
        need(m.stride == stride, std::string(key) + " mesh stride");
        return m;
    };
    scene.world = mesh("world", 14);
    scene.sky = mesh("sky", 12);
    auto materials = mesh("materialMesh", 18);

    // Lightmap atlas: shipped PNG, uploaded without flip (Q3LM.upload 4917-4924).
    const auto& lm = doc.at("lightmap");
    need(lm.at("compatible").boolean(), "lightmap layout does not match the shipped atlas");
    scene.lightmap = load_image(assets::content_path(root, lm.at("png").string()));
    need(scene.lightmap.width == static_cast<std::uint32_t>(lm.at("w").number()) &&
         scene.lightmap.height == static_cast<std::uint32_t>(lm.at("h").number()), "lightmap PNG dimensions");

    // Texture catalog: shader map name -> file.
    const auto catalog = assets::load_data(root / "catalog.cadata");
    std::unordered_map<std::string, std::string> files;
    for(const auto& t : catalog.at("textures").array()) {
        const auto* meta = find(t, "metadata");
        const auto name = (!is_null(meta) && find(*meta, "name") && !is_null(find(*meta, "name"))) ? meta->at("name").string() : t.at("id").string();
        files[name] = t.at("file").string();
    }
    need(files.contains("$world"), "catalog has no $world atlas");
    if(!options.runtime_world_atlas.empty()) {
        scene.world_layers = world_atlas_layers(load_image(options.runtime_world_atlas));
        scene.stats.world_atlas_source = "runtime capture " + path_utf8(options.runtime_world_atlas.filename());
    } else {
        scene.world_layers = world_atlas_layers(load_image(assets::content_path(root, files.at("$world"))));
        scene.stats.world_atlas_source = "N1 $world (repainted canvas; does NOT match the legacy runtime world array)";
    }

    // Legacy lightVertexColor on the world stream.
    {
        const WalkGrid grid(doc.at("compiled"));
        const auto n = scene.world.vertex_count();
        for(std::size_t i = 0; i < n; ++i) {
            auto vtx = std::span<float>(scene.world.vertices).subspan(i * 14, 14);
            const double f = legacy_vertex_light(vtx, grid);
            if(f != 1.0) ++scene.stats.lit_vertices_changed;
            scene.stats.vertex_light_min = std::min(scene.stats.vertex_light_min, f);
            scene.stats.vertex_light_max = std::max(scene.stats.vertex_light_max, f);
            for(int c = 6; c < 9; ++c) vtx[std::size_t(c)] = static_cast<float>(vtx[std::size_t(c)] * f);
        }
    }

    // Material definitions used by this map, in first-use order.
    std::unordered_map<std::string, const assets::Value*> definitions;
    for(const auto& d : doc.at("materialDefinitions").array()) definitions[d.at("name").string()] = &d;
    std::unordered_map<std::string, std::uint32_t> material_index;
    auto add_material = [&](const std::string& name) -> std::uint32_t {
        if(auto it = material_index.find(name); it != material_index.end()) return it->second;
        need(definitions.contains(name), "missing material definition " + name);
        scene.material_defs.push_back(parse_material(*definitions.at(name)));
        const auto index = static_cast<std::uint32_t>(scene.material_defs.size() - 1);
        material_index[name] = index;
        return index;
    };

    // Static clusters (Q3MAT.buildStatic 4216-4227): key = material + (portal origin | 10x10x6 m cell
    // of the triangle centroid); groups keep insertion order, then stable sort by (sort, id).
    struct Group { std::uint32_t material{}; std::vector<std::uint32_t> indices; };
    std::vector<Group> groups;
    std::unordered_map<std::string, std::size_t> group_of;
    const auto& mv = materials.vertices;
    for(const auto& g : render.at("materials").array()) {
        const auto name = g.at("material").string();
        const auto mi = add_material(name);
        const auto* origin = find(g, "origin");
        std::string portal_key;
        if(!is_null(origin) && !is_null(find(*origin, "portal"))) portal_key = "portal:" + origin->at("id").string();
        const auto first = static_cast<std::size_t>(g.at("firstIndex").number());
        const auto count = static_cast<std::size_t>(g.at("indexCount").number());
        for(std::size_t t = first; t < first + count; t += 3) {
            double c[3] = {0, 0, 0};
            for(std::size_t k = 0; k < 3; ++k)
                for(std::size_t a = 0; a < 3; ++a) c[a] += mv[std::size_t(materials.indices[t + k]) * 18 + a];
            for(double& x : c) x /= 3;
            std::string key = name + "|";
            if(!portal_key.empty()) key += portal_key;
            else key += std::to_string(static_cast<long long>(std::floor(c[0] / 10))) + "," +
                        std::to_string(static_cast<long long>(std::floor(c[1] / 10))) + "," +
                        std::to_string(static_cast<long long>(std::floor(c[2] / 6)));
            auto [it, inserted] = group_of.try_emplace(key, groups.size());
            if(inserted) groups.push_back({mi, {}});
            auto& indices = groups[it->second].indices;
            indices.insert(indices.end(), materials.indices.begin() + static_cast<std::ptrdiff_t>(t),
                           materials.indices.begin() + static_cast<std::ptrdiff_t>(t + 3));
        }
    }
    for(const auto& m : scene.material_defs) need(m.id >= 0, "used material without resolved legacy id: " + m.name);
    std::stable_sort(groups.begin(), groups.end(), [&](const Group& a, const Group& b) {
        const auto& ma = scene.material_defs[a.material]; const auto& mb = scene.material_defs[b.material];
        return ma.sort != mb.sort ? ma.sort < mb.sort : ma.id < mb.id;
    });
    scene.materials.stride = 18;
    scene.materials.vertices = std::move(materials.vertices);
    for(const auto& g : groups) {
        Cluster c;
        c.material = g.material;
        c.first = static_cast<std::uint32_t>(scene.materials.indices.size());
        c.count = static_cast<std::uint32_t>(g.indices.size());
        V3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
        for(auto i : g.indices)
            for(std::size_t a = 0; a < 3; ++a) {
                const double p = scene.materials.vertices[std::size_t(i) * 18 + a];
                lo[a] = std::min(lo[a], p); hi[a] = std::max(hi[a], p);
            }
        for(std::size_t a = 0; a < 3; ++a) c.center[a] = (lo[a] + hi[a]) / 2;
        const auto& m = scene.material_defs[g.material];
        c.radius = std::hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2 + (m.autosprite || !m.deforms.empty() ? 2 : 0);
        scene.materials.indices.insert(scene.materials.indices.end(), g.indices.begin(), g.indices.end());
        scene.clusters.push_back(c);
    }
    need(scene.materials.indices.size() == materials.indices.size(), "cluster rebuild lost triangles");

    // Sky (q3InitSky 15079-15101): WORLD.meta.sky.shader or sky/reactor_bastion.
    const auto& meta = doc.at("authored").at("meta");
    const auto* skymeta = find(meta, "sky");
    scene.sky_material = (!is_null(skymeta)) ? string_or(*skymeta, "shader", "sky/reactor_bastion") : "sky/reactor_bastion";
    scene.sun_azimuth = (!is_null(skymeta)) ? number_or(*skymeta, "sunAzimuth", -.62) : -.62;
    if(const auto* center = find(meta, "center"); !is_null(center)) {
        const auto& a = center->array();
        need(a.size() == 2, "meta.center shape");
        scene.sky_center = {a[0].number(), a[1].number()};
    }
    scene.sky_material_index = add_material(scene.sky_material);
    need(scene.material_defs[scene.sky_material_index].sky.present, "sky material has no skyParms");
    // q3InitSky: skyFog = meta.sky.horizonFog ?? 0. A named far box (not "$baked") is an image box:
    // the authored sky geometry is then not used for the backdrop (Q3MAT.prepareSky, legacy 4380).
    scene.sky_fog = (!is_null(skymeta)) ? number_or(*skymeta, "horizonFog", 0) : 0;
    if(const auto& farbox = scene.material_defs[scene.sky_material_index].sky.farbox; !farbox.empty() && farbox != "-" && farbox != "$baked") {
        scene.sky_box_name = farbox;
        std::string file = "skybox-" + farbox + ".png";
        std::replace(file.begin(), file.end(), '/', '-');
        const auto path = options.sky_box_dir.empty() ? std::filesystem::path{} : options.sky_box_dir / file;
        if(!path.empty() && std::filesystem::exists(path)) {
            scene.sky_box = load_image(path);
            need(scene.sky_box.width > 0 && scene.sky_box.height == scene.sky_box.width * 6, "sky box image must be six square faces stacked vertically");
        } else scene.sky_box_missing = true;
    }
    scene.clouds = sky_dome(scene.sky_material, scene.sun_azimuth);

    // Texture arrays: 256 px -> material array, 512 px -> kit array (registerKitImage 3680-3684).
    std::unordered_map<std::string, std::pair<bool, std::uint32_t>> loaded;   // name -> (is512, layer)
    auto texture = [&](const std::string& name) -> std::pair<bool, std::uint32_t> {
        if(auto it = loaded.find(name); it != loaded.end()) return it->second;
        need(files.contains(name), "texture referenced by material is missing from the catalog: " + name);
        const auto path = assets::content_path(root, files.at(name));
        Image img = path.extension() == ".rgba" ? load_raw_rgba(path) : load_image(path);
        need(img.width == img.height && (img.width == 256 || img.width == 512), "material texture must be 256 or 512 px: " + name);
        const bool big = img.width == 512;
        auto& layers = big ? scene.layers512 : scene.layers256;
        auto& names = big ? scene.names512 : scene.names256;
        layers.push_back(std::move(img));
        names.push_back(name);
        const auto result = std::pair{big, static_cast<std::uint32_t>(layers.size() - 1)};
        loaded[name] = result;
        return result;
    };
    for(auto& m : scene.material_defs) {
        MaterialBinding binding;
        for(auto& s : m.stages) {
            std::vector<std::uint32_t> frames;
            if(s.source == Source::material) {
                const auto& names = s.maps.empty() ? std::vector<std::string>{s.map} : s.maps;
                bool any_big = false, any_small = false;
                for(const auto& n : names) {
                    auto [big, layer] = texture(n);
                    (big ? any_big : any_small) = true;
                    frames.push_back(layer);
                }
                need(!(any_big && any_small), "animMap mixes 256 and 512 textures: " + m.name);
                if(any_big) s.source = Source::kit;
            } else if(s.source == Source::world) frames.push_back(s.world_layer);
            else frames.push_back(0);
            binding.stage_layers.push_back(std::move(frames));
        }
        scene.bindings.push_back(std::move(binding));
    }
    if(scene.layers256.empty()) { scene.layers256.push_back(Image{256, 256, std::vector<std::uint8_t>(256U * 256U * 4U, 255)}); scene.names256.push_back("$placeholder"); }
    if(scene.layers512.empty()) { scene.layers512.push_back(Image{512, 512, std::vector<std::uint8_t>(512U * 512U * 4U, 255)}); scene.names512.push_back("$placeholder"); }

    for(const auto& s : doc.at("compiled").at("spawns").array())
        scene.spawns.push_back({s.at("x").number(), s.at("y").number(), s.at("z").number(), s.at("a").number()});

    scene.stats.world_triangles = scene.world.indices.size() / 3;
    scene.stats.sky_triangles = scene.sky.indices.size() / 3;
    scene.stats.material_triangles = scene.materials.indices.size() / 3;
    scene.stats.clusters = scene.clusters.size();
    scene.stats.materials = scene.material_defs.size();
    scene.stats.textures256 = scene.names256.size();
    scene.stats.textures512 = scene.names512.size();
    return scene;
}

}  // namespace cat::scene
