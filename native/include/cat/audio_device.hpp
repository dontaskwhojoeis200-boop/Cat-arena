#pragma once
// N7: the platform audio output. SDL3 pulls stereo float frames from the Mixer on its own thread
// (WASAPI on Windows, AAudio / OpenSL ES on Android, PipeWire / Pulse / ALSA on Linux). Failure to
// open a device is not fatal: the game runs silent and says so in the log and the audio tab.
#include "cat/audio.hpp"

#include <string>

struct SDL_AudioStream;

namespace cat::audio {

class Device {
public:
    explicit Device(Mixer& mixer);   // opens the default playback device at the mixer's rate
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    bool ok() const { return stream_ != nullptr; }
    const std::string& status() const { return status_; }   // device name / driver, or the error
private:
    static void callback(void* self, SDL_AudioStream* stream, int additional, int total);
    Mixer* mixer_;
    SDL_AudioStream* stream_{};
    std::string status_;
    std::vector<float> buffer_;
};

}  // namespace cat::audio
