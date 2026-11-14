#include "cat/scene.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace cat::scene {
namespace {
void require(bool valid, std::string_view reason) {
    if(!valid) throw std::runtime_error("MD3 props: " + std::string(reason));
}

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::span<const std::uint8_t> range(std::size_t offset, std::size_t size) const {
        require(offset <= bytes.size() && size <= bytes.size() - offset, "truncated file or invalid offset");
        return bytes.subspan(offset, size);
    }
    std::uint16_t u16(std::size_t offset) const {
        const auto b = range(offset, 2);
        return static_cast<std::uint16_t>(b[0] | (static_cast<unsigned>(b[1]) << 8));
    }
    std::uint32_t u32(std::size_t offset) const {
        const auto b = range(offset, 4);
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
    }
    float f32(std::size_t offset) const {
        const auto value = std::bit_cast<float>(u32(offset));
        require(std::isfinite(value), "non-finite value");
        return value;
    }
    std::string string(std::size_t offset, std::size_t size) const {
        const auto b = range(offset, size);
        const auto end = std::find(b.begin(), b.end(), std::uint8_t{0});
        return {b.begin(), end};
    }
};

constexpr std::uint32_t kIdp3 = 0x33504449;
constexpr double kWorldUnits = 32;
constexpr double kTau = 2 * std::numbers::pi;
V3 add(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 mul(const V3& a, double k) { return {a[0] * k, a[1] * k, a[2] * k}; }
V3 mix(const V3& a, const V3& b, double t) { return add(mul(a, 1 - t), mul(b, t)); }
double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 unit(const V3& v) {
    const double length = std::hypot(v[0], v[1], v[2]);
    return length > 0 ? mul(v, 1 / length) : v;
}
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
V3 vector(const Reader& r, std::size_t offset) { return {r.f32(offset), r.f32(offset + 4), r.f32(offset + 8)}; }
V3 position(const assets::Value& v) { return {v.at("x").number(), v.at("y").number(), v.at("z").number()}; }
std::vector<std::uint8_t> read_model(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    require(size > 0 && size <= 16 * 1024 * 1024, "missing model or invalid model size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    require(bool(file), "could not read model");
    return bytes;
}
void prop_light(MapProp& prop, const Image& grid, const assets::Value::Array& lights) {
    prop.ambient = {.45, .45, .48};
    if(grid.width == 624 && grid.height == 234) {
        // The legacy prop path samples the nearest voxel, including the atlas's one-pixel borders.
        const auto xy = [](double x) { return static_cast<std::size_t>(std::clamp(std::floor(x / 117 * 76), 0.0, 75.0)); };
        const auto layer = static_cast<std::size_t>(std::clamp(std::round((prop.position[2] + .8 + 6) / 30 * 23), 0.0, 23.0));
        const auto x = layer % 8 * 78 + 1 + xy(prop.position[0]);
        const auto y = layer / 8 * 78 + 1 + xy(prop.position[1]);
        for(std::size_t c = 0; c < 3; ++c) prop.ambient[c] = grid.rgba[(y * grid.width + x) * 4 + c] / 255.0 * 1.9 * .62;
    }
    prop.directed = {.25, .25, .25};
    for(const auto& light : lights) {
        const auto delta = sub(position(light), prop.position);
        const double distance = std::hypot(delta[0], delta[1], delta[2]), range = light.at("range").number();
        if(distance < .001 || distance >= range) continue;
        const double falloff = 1 - distance / range, strength = light.at("power").number();
        const double weight = falloff * falloff * (strength == 0 ? 1 : strength);
        prop.direction = add(prop.direction, mul(delta, weight / distance));
        const auto& authored_color = light.at("color");
        V3 color;
        if(!std::holds_alternative<std::nullptr_t>(authored_color.data)) {
            const auto& rgb = authored_color.array();
            require(rgb.size() == 3, "invalid map light color");
            color = {rgb[0].number(), rgb[1].number(), rgb[2].number()};
        } else {
            const auto* kind = std::get_if<std::string>(&light.at("kind").data);
            color = kind && *kind == "reactor" ? V3{1.42, .57, .2} : kind && *kind == "service" ? V3{.35, .88, .76}
                  : light.at("warm").boolean() ? V3{1.22, .73, .37} : V3{.6, .78, 1.06};
        }
        prop.directed = add(prop.directed, mul(color, weight * .5));
    }
    for(auto& c : prop.directed) c = std::min(1.2, c);
    if(dot(prop.direction, prop.direction) < 1e-8) prop.direction = {-.3, -.6, .75};
    prop.direction = unit(prop.direction);
}

struct Transform {
    V3 origin;
    std::array<V3, 3> axes;
    V3 direction(const V3& v) const { return add(add(mul(axes[0], v[0]), mul(axes[1], v[1])), mul(axes[2], v[2])); }
    V3 point(const V3& v) const { return add(origin, direction(v)); }
};
struct FramePair { std::size_t first, second; double fraction; };
FramePair sample(std::size_t count, double fps, double time) {
    const double frame = std::fmod(std::max(0.0, time) * fps, static_cast<double>(count));
    const auto first = static_cast<std::size_t>(frame);
    return {first, (first + 1) % count, frame - static_cast<double>(first)};
}
Transform attachment(const Transform& parent, const Md3Model& model, const FramePair& frame, std::size_t tag, double yaw) {
    const auto& a = model.tags[frame.first][tag];
    const auto& b = model.tags[frame.second][tag];
    const auto x = unit(mix(a.axes[0], b.axes[0], frame.fraction));
    const auto y = unit(mix(a.axes[1], b.axes[1], frame.fraction));
    const auto z = unit(mix(a.axes[2], b.axes[2], frame.fraction));
    const double c = std::cos(yaw), s = std::sin(yaw);
    return {parent.point(mix(a.origin, b.origin, frame.fraction)), {parent.direction(add(mul(x, c), mul(y, s))),
            parent.direction(add(mul(x, -s), mul(y, c))), parent.direction(z)}};
}
} // namespace

Md3Model decode_md3(std::span<const std::uint8_t> bytes) {
    Reader r{bytes};
    require(r.u32(0) == kIdp3 && r.u32(4) == 15, "expected IDP3 version 15");
    const std::size_t frames = r.u32(76), tags = r.u32(80), surfaces = r.u32(84);
    const std::size_t frame_offset = r.u32(92), tag_offset = r.u32(96), surface_offset = r.u32(100), end = r.u32(104);
    require(frames > 0 && frames <= 1024 && tags <= 16 && surfaces > 0 && surfaces <= 32, "invalid header counts");
    require(frame_offset >= 108 && frame_offset + frames * 56 <= tag_offset &&
            tag_offset + frames * tags * 112 <= surface_offset && surface_offset <= end && end == bytes.size(), "invalid header layout");
    Md3Model model;
    model.frame_count = frames;
    model.tags.resize(frames);
    for(std::size_t f = 0; f < frames; ++f) for(std::size_t t = 0; t < tags; ++t) {
        const auto o = tag_offset + (f * tags + t) * 112;
        auto name = r.string(o, 64);
        require(!name.empty() && (f == 0 || model.tags.front()[t].name == name), "invalid tag name across frames");
        model.tags[f].push_back({std::move(name), vector(r, o + 64), {vector(r, o + 76), vector(r, o + 88), vector(r, o + 100)}});
    }
    std::size_t offset = surface_offset;
    for(std::size_t i = 0; i < surfaces; ++i) {
        Reader s{r.bytes.subspan(offset)};
        require(s.u32(0) == kIdp3 && s.u32(72) == frames, "invalid surface header");
        const std::size_t shaders = s.u32(76), vertices = s.u32(80), triangles = s.u32(84);
        const std::size_t tri = s.u32(88), shader = s.u32(92), uv = s.u32(96), xyz = s.u32(100), length = s.u32(104);
        require(shaders > 0 && shaders <= 256 && vertices > 0 && vertices <= 4096 && triangles > 0 && triangles <= 8192, "invalid surface counts");
        s.bytes = s.range(0, length);
        require(tri >= 108 && shader >= 108 && uv >= 108 && xyz >= 108, "surface array overlaps its header");
        (void)s.range(tri, triangles * 12);
        (void)s.range(shader, shaders * 68);
        (void)s.range(uv, vertices * 8);
        (void)s.range(xyz, frames * vertices * 8);
        Md3Surface surface;
        surface.material = s.string(shader, 64);
        require(!surface.material.empty(), "surface has no material");
        surface.indices.reserve(triangles * 3);
        for(std::size_t j = 0; j < triangles * 3; ++j) {
            const auto index = s.u32(tri + j * 4);
            require(index < vertices, "triangle references an invalid vertex");
            surface.indices.push_back(index);
        }
        surface.uv.reserve(vertices);
        for(std::size_t j = 0; j < vertices; ++j) surface.uv.push_back({s.f32(uv + j * 8), s.f32(uv + j * 8 + 4)});
        surface.vertices.reserve(frames * vertices);
        for(std::size_t j = 0; j < frames * vertices; ++j) {
            const auto o = xyz + j * 8;
            const auto decode = [&](std::size_t k) { return std::bit_cast<std::int16_t>(s.u16(o + k)) / 64.0; };
            const auto packed = s.u16(o + 6);
            const double az = (packed >> 8) * kTau / 256, el = (packed & 255) * kTau / 256;
            surface.vertices.push_back({{decode(0), decode(2), decode(4)}, {std::cos(az) * std::sin(el), std::sin(az) * std::sin(el), std::cos(el)}});
        }
        for(std::size_t j = 0; j < surface.indices.size(); j += 3) {
            const auto& a = surface.vertices[surface.indices[j]];
            const auto& b = surface.vertices[surface.indices[j + 1]];
            const auto& c = surface.vertices[surface.indices[j + 2]];
            if(dot(cross(sub(b.position, a.position), sub(c.position, a.position)), add(add(a.normal, b.normal), c.normal)) < 0)
                std::swap(surface.indices[j + 1], surface.indices[j + 2]);
        }
        model.surfaces.push_back(std::move(surface));
        offset += length;
    }
    require(offset == end, "invalid surface table size");
    return model;
}

void load_props(PropScene& scene, const assets::Value& document, const std::filesystem::path& root,
                const std::function<std::uint32_t(const std::string&)>& material_index) {
    const auto& authored = document.at("authored").object();
    const auto items = authored.find("models");
    if(items == authored.end() || items->second.array().empty()) return;
    const auto grid_path = root.parent_path() / "n6" / "light-volume" / (document.at("id").string() + ".png");
    const auto grid = std::filesystem::exists(grid_path) ? load_image(grid_path) : Image{};
    const auto& lights = document.at("compiled").at("lights").array();
    std::unordered_map<std::string, std::size_t> models;
    for(const auto& item : items->second.array()) {
        MapProp p{};
        p.position = position(item); p.yaw = item.at("yaw").number(); p.scale = item.at("scale").number();
        const auto& name = item.at("model").string();
        require(name == "turret" || name == "flag", "unknown prop rig " + name);
        require(p.scale > 0, "invalid placement scale");
        p.turret = name == "turret"; p.radius = (p.turret ? 2.2 : 2.5) * p.scale;
        p.phase = std::fmod(p.position[0] * .37 + p.position[1] * .29, 10.0);
        prop_light(p, grid, lights);
        auto add_part = [&](const std::string& file, double fps, std::size_t parent, std::string_view tag) {
            const auto [it, inserted] = models.try_emplace(file, scene.models.size());
            if(inserted) scene.models.push_back(decode_md3(read_model(assets::content_path(root, "models/md3/" + file + ".md3"))));
            PropPart part{};
            part.model = it->second; part.first_vertex = scene.vertex_count; part.fps = fps; part.parent = parent;
            if(!tag.empty()) {
                const auto& tags = scene.models[p.parts[parent].model].tags.front();
                const auto found = std::find_if(tags.begin(), tags.end(), [&](const Md3Tag& t) { return t.name == tag; });
                require(found != tags.end(), "missing attachment tag");
                part.tag = static_cast<std::size_t>(found - tags.begin());
            }
            for(const auto& surface : scene.models[part.model].surfaces) {
                const auto base = static_cast<std::uint32_t>(scene.vertex_count);
                part.draws.push_back({material_index(surface.material), static_cast<std::uint32_t>(scene.indices.size()), static_cast<std::uint32_t>(surface.indices.size())});
                for(const auto i : surface.indices) scene.indices.push_back(base + i);
                scene.vertex_count += surface.uv.size();
            }
            p.parts.push_back(std::move(part));
        };
        if(p.turret) {
            add_part("turret_base", .6, 0, "");
            add_part("turret_head", 10, 0, "tag_head");
            add_part("turret_barrel", 12, 1, "tag_barrel");
        } else add_part("flag", 12, 0, "");
        scene.placements.push_back(std::move(p));
    }
}

void animate_props(const PropScene& scene, std::span<PropMotion> motion, std::span<float> output, double time, const V3* player) {
    for(std::size_t i = 0; i < scene.placements.size(); ++i) {
        const auto& p = scene.placements[i];
        auto& state = motion[i];
        bool firing = false;
        V3 color{1, 1, 1};
        if(p.turret) {
            double aim = std::sin(time * .4 + p.phase) * 1.1;
            if(player) {
                const auto delta = sub(*player, p.position);
                const auto distance = std::hypot(delta[0], delta[1]);
                if(distance < 22) aim = std::atan2(delta[1], delta[0]) - p.yaw;
                firing = distance < 16;
            }
            if(!state.initialized || time < state.last_time) state = {time, 0, true};
            const double step = 2.5 * std::clamp(time - state.last_time, 0.0, .1);
            state.yaw += std::clamp(std::remainder(aim - state.yaw, kTau), -step, step);
            state.last_time = time;
            const double glow = firing ? .55 + .45 * std::max(0.0, std::sin(time * 9)) : .25 + .1 * std::sin(time * 2);
            color = {glow * 1.1, glow * .35, glow * .2};
        }
        const double scale = p.scale / kWorldUnits, c = std::cos(p.yaw) * scale, s = std::sin(p.yaw) * scale;
        std::array<Transform, 3> poses{};
        poses[0] = {p.position, {{{c, s, 0}, {-s, c, 0}, {0, 0, scale}}}};
        std::array<FramePair, 3> frames{};
        for(std::size_t j = 0; j < p.parts.size(); ++j) {
            const auto& part = p.parts[j];
            const auto& model = scene.models[part.model];
            frames[j] = sample(model.frame_count, part.fps, p.turret && j == 2 && !firing ? 0 : time);
            if(j > 0) poses[j] = attachment(poses[part.parent], scene.models[p.parts[part.parent].model],
                                          frames[part.parent], part.tag, j == 1 ? state.yaw : 0);
            const auto frame = frames[j];
            auto vertex = part.first_vertex;
            for(const auto& surface : model.surfaces) {
                const auto count = surface.uv.size();
                for(std::size_t k = 0; k < count; ++k, ++vertex) {
                    const auto& a = surface.vertices[frame.first * count + k];
                    const auto& b = surface.vertices[frame.second * count + k];
                    const auto point = poses[j].point(mix(a.position, b.position, frame.fraction));
                    const auto normal = unit(poses[j].direction(mix(a.normal, b.normal, frame.fraction)));
                    const auto diffuse = add(p.ambient, mul(p.directed, std::max(0.0, dot(normal, p.direction))));
                    auto dst = output.subspan(vertex * kMaterialVertexStride, kMaterialVertexStride);
                    for(std::size_t l = 0; l < 3; ++l) {
                        dst[l] = static_cast<float>(point[l]); dst[l + 3] = static_cast<float>(normal[l]); dst[l + 6] = static_cast<float>(color[l]);
                    }
                    dst[9] = 1; dst[10] = surface.uv[k][0]; dst[11] = surface.uv[k][1];
                    dst[12] = dst[13] = dst[17] = 0;
                    for(std::size_t l = 0; l < 3; ++l) dst[l + 14] = static_cast<float>(diffuse[l]);
                }
            }
        }
    }
}
} // namespace cat::scene
