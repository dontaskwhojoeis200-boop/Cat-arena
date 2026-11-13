// N7: Ogg Vorbis (stb_vorbis) and MP3 (dr_mp3) decoding, and windowed-sinc resampling.
// Compiled without the project's strict warning flags because it includes the third-party decoders.
#include "cat/audio.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../third_party/stb/stb_vorbis.c"
#undef L
#undef C
#undef R

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../third_party/dr_libs/dr_mp3.h"

namespace cat::audio {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open audio file " + path.string());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

Buffer decode_ogg(const std::vector<std::uint8_t>& bytes) {
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
    if(!v) throw std::runtime_error("Ogg Vorbis decode failed (stb_vorbis error " + std::to_string(error) + ")");
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    Buffer b;
    b.rate = static_cast<int>(info.sample_rate);
    b.channels = info.channels;
    std::vector<float> chunk(4096 * static_cast<std::size_t>(info.channels));
    for(;;) {
        const int n = stb_vorbis_get_samples_float_interleaved(v, info.channels, chunk.data(), static_cast<int>(chunk.size()));
        if(n <= 0) break;
        b.samples.insert(b.samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n) * info.channels);
    }
    stb_vorbis_close(v);
    if(b.samples.empty()) throw std::runtime_error("Ogg Vorbis stream has no samples");
    return b;
}

Buffer decode_mp3(const std::vector<std::uint8_t>& bytes) {
    drmp3_config config{};
    drmp3_uint64 frames = 0;
    float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &config, &frames, nullptr);
    if(!pcm) throw std::runtime_error("MP3 decode failed");
    Buffer b;
    b.rate = static_cast<int>(config.sampleRate);
    b.channels = static_cast<int>(config.channels);
    b.samples.assign(pcm, pcm + frames * config.channels);
    drmp3_free(pcm, nullptr);
    return b;
}

namespace {
constexpr double kPi = 3.141592653589793;
// Blackman-windowed sinc, 16 zero crossings each side; cutoff below the lower Nyquist frequency.
constexpr int kHalfTaps = 16;
double sinc_tap(double x, double cutoff) {   // x in input samples from the centre
    if(std::fabs(x) >= kHalfTaps / cutoff) return 0;
    const double t = x * cutoff;
    const double s = t == 0 ? 1 : std::sin(kPi * t) / (kPi * t);
    const double w = x / (kHalfTaps / cutoff);   // -1..1
    const double window = .42 + .5 * std::cos(kPi * w) + .08 * std::cos(2 * kPi * w);
    return s * window * cutoff;
}
}  // namespace

Buffer resample(const Buffer& in, int rate) {
    if(in.rate == rate || in.frames() == 0) { Buffer copy = in; copy.rate = rate; return copy; }
    const double ratio = double(rate) / in.rate;   // output / input
    const double cutoff = std::min(1.0, ratio) * .97;
    const std::size_t in_frames = in.frames(), channels = static_cast<std::size_t>(in.channels);
    const auto out_frames = static_cast<std::size_t>(std::llround(double(in_frames) * ratio));
    Buffer out;
    out.rate = rate;
    out.channels = in.channels;
    out.samples.assign(out_frames * channels, 0.f);
    const double reach = kHalfTaps / cutoff;
    for(std::size_t o = 0; o < out_frames; ++o) {
        const double centre = double(o) / ratio;
        const auto first = static_cast<long long>(std::ceil(centre - reach)), last = static_cast<long long>(std::floor(centre + reach));
        for(std::size_t c = 0; c < channels; ++c) {
            double acc = 0;
            for(long long i = std::max(0LL, first); i <= std::min(static_cast<long long>(in_frames) - 1, last); ++i)
                acc += in.samples[static_cast<std::size_t>(i) * channels + c] * sinc_tap(centre - double(i), cutoff);
            out.samples[o * channels + c] = static_cast<float>(acc);
        }
    }
    return out;
}

namespace {
class Mp3Stream final : public Stream {
public:
    explicit Mp3Stream(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
        if(!drmp3_init_memory(&mp3_, bytes_.data(), bytes_.size(), nullptr)) throw std::runtime_error("MP3 stream open failed");
        open_ = true;
    }
    ~Mp3Stream() override { if(open_) drmp3_uninit(&mp3_); }
    int rate() const override { return static_cast<int>(mp3_.sampleRate); }
    std::size_t read(float* stereo, std::size_t frames) override {
        const std::size_t channels = mp3_.channels;
        if(channels == 2) return static_cast<std::size_t>(drmp3_read_pcm_frames_f32(&mp3_, frames, stereo));
        temp_.resize(frames * channels);
        const auto got = static_cast<std::size_t>(drmp3_read_pcm_frames_f32(&mp3_, frames, temp_.data()));
        for(std::size_t i = 0; i < got; ++i) { stereo[2 * i] = temp_[i * channels]; stereo[2 * i + 1] = channels > 1 ? temp_[i * channels + 1] : temp_[i * channels]; }
        return got;
    }
    void rewind() override { drmp3_seek_to_pcm_frame(&mp3_, 0); }
private:
    std::vector<std::uint8_t> bytes_;
    drmp3 mp3_{};
    bool open_{};
    std::vector<float> temp_;
};
}  // namespace

std::unique_ptr<Stream> open_mp3(std::vector<std::uint8_t> bytes) { return std::make_unique<Mp3Stream>(std::move(bytes)); }

}  // namespace cat::audio
