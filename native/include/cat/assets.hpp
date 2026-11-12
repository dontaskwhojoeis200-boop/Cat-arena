#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>
namespace cat::assets {
struct Value {
    using Array=std::vector<Value>;
    using Object=std::map<std::string,Value,std::less<>>;
    std::variant<std::nullptr_t,bool,double,std::string,Array,Object> data{nullptr};
    Value()=default;
    template<class T> explicit Value(T value):data(std::move(value)){}
    const Value& at(std::string_view key) const;
    const Array& array() const;
    const Object& object() const;
    const std::string& string() const;
    double number() const;
    bool boolean() const;
    bool operator==(const Value&) const = default;
};
struct Mesh {
    std::uint32_t stride{};
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t vertex_count() const {return stride?vertices.size()/stride:0;}
};
// Portable little-endian versioned containers, bounded readers, CRC32 integrity.
Value decode_data(std::span<const std::uint8_t>);
std::vector<std::uint8_t> encode_data(const Value&);
Mesh decode_mesh(std::span<const std::uint8_t>);
std::vector<std::uint8_t> encode_mesh(const Mesh&);
Value load_data(const std::filesystem::path&);
Mesh load_mesh(const std::filesystem::path&);
std::uint32_t crc32(std::span<const std::uint8_t>);
// Resolves only relative content paths inside root, including symlink containment.
std::filesystem::path content_path(const std::filesystem::path& root,std::string_view relative);
struct World {
    Value document;
    std::string id;
    std::size_t slabs{},walls{},spawns{},ramps{},kits{};
    bool lightmap_compatible{};
};
World validate_world(Value document);
World load_world(const std::filesystem::path&);
void validate_render_assets(const World&,const std::filesystem::path& asset_root);
}
