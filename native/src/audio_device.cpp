// N7: SDL3 audio output for the Mixer (see include/cat/audio_device.hpp).
#include "cat/audio_device.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace cat::audio {

Device::Device(Mixer& mixer) : mixer_(&mixer) {
    if(!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) { status_ = std::string("no audio: ") + SDL_GetError(); return; }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32; spec.channels = 2; spec.freq = mixer.rate();
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Device::callback, this);
    if(!stream_) { status_ = std::string("no audio device: ") + SDL_GetError(); return; }
    const char* driver = SDL_GetCurrentAudioDriver();
    SDL_AudioSpec device{}; int frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &device, &frames);
    status_ = std::string(driver ? driver : "audio") + ", " + std::to_string(device.freq) + " Hz device, " + std::to_string(frames) + "-frame buffer, mixer " + std::to_string(mixer.rate()) + " Hz stereo float";
    if(!SDL_ResumeAudioStreamDevice(stream_)) { status_ = std::string("audio device did not start: ") + SDL_GetError(); SDL_DestroyAudioStream(stream_); stream_ = nullptr; }
}

bool Device::owns_device(std::uint32_t id) const {
    return stream_ && SDL_GetAudioStreamDevice(stream_) == id;
}

Device::~Device() {
    if(stream_) SDL_DestroyAudioStream(stream_);   // stops the callback before the mixer goes away
}

void Device::callback(void* self, SDL_AudioStream* stream, int additional, int) {
    auto* d = static_cast<Device*>(self);
    std::size_t frames = static_cast<std::size_t>(std::max(additional, 0)) / (2 * sizeof(float));
    while(frames > 0) {
        const std::size_t n = std::min<std::size_t>(frames, 1024);
        d->buffer_.resize(n * 2);
        d->mixer_->render(d->buffer_.data(), n);
        SDL_PutAudioStreamData(stream, d->buffer_.data(), static_cast<int>(n * 2 * sizeof(float)));
        frames -= n;
    }
}

}  // namespace cat::audio
