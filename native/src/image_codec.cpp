// Isolated translation unit for the vendored stb codecs (third_party/stb, MIT/public domain).
// Compiled without the project's -Werror warning set; see CMakeLists.txt.
#include "cat/scene.hpp"
#include "cat/utf8_path.hpp"
#include <climits>
#include <stdexcept>
#include <string>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#include <fstream>

namespace cat::scene {

Image decode_image(std::span<const std::uint8_t> bytes) {
    if(bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX)) throw std::runtime_error("image: invalid byte count");
    int w = 0, h = 0, channels = 0;
    // No premultiplication, no colour-space conversion, top row first: same bytes the legacy
    // uploads with UNPACK_PREMULTIPLY_ALPHA=false / UNPACK_COLORSPACE_CONVERSION=NONE.
    stbi_set_flip_vertically_on_load(0);
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if(!pixels) throw std::runtime_error(std::string("image: decode failed: ") + stbi_failure_reason());
    Image image;
    image.width = static_cast<std::uint32_t>(w);
    image.height = static_cast<std::uint32_t>(h);
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
    stbi_image_free(pixels);
    return image;
}

namespace {
void append(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}
}

void write_png(const std::filesystem::path& path, const Image& image) {
    if(!image.width || !image.height || image.rgba.size() != std::size_t(image.width) * image.height * 4U)
        throw std::runtime_error("image: cannot encode malformed image");
    std::vector<std::uint8_t> encoded;
    if(!stbi_write_png_to_func(append, &encoded, static_cast<int>(image.width), static_cast<int>(image.height), 4,
                               image.rgba.data(), static_cast<int>(image.width * 4U)))
        throw std::runtime_error("image: PNG encode failed");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if(!out) throw std::runtime_error("image: cannot write " + path_utf8(path));
}

}  // namespace cat::scene
