#include "presenter.h"

#include <SDL3/SDL.h>
#include <algorithm>

namespace {
constexpr usize gAudioFrameSize = sizeof(s16[2]);
constexpr f64 gAudioMaxRatio = 100.0;
} // namespace

void Presenter::Start_Audio() {
    SDL_AudioSpec spec = {};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<s32>(audioRate);
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio == nullptr) {
        Log_Warning("window", "no audio device: %s", SDL_GetError());
    }
    else {
        SDL_ResumeAudioStreamDevice(audio);
    }
}

void Presenter::Set_Audio_Speed_Limit(bool enabled) {
    if (enabled != audioLimited && audio != nullptr) {
        SDL_ClearAudioStream(audio);
    }
    audioLimited = enabled;
}

void Presenter::Push_Audio(const s16* samples, u32 frame_count, u32 sample_rate) {
    if (audio == nullptr || frame_count == 0 || sample_rate == 0 || frame_count > INT32_MAX / gAudioFrameSize) {
        return;
    }

    if (sample_rate != audioRate) {
        SDL_AudioSpec spec = {};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = static_cast<int>(sample_rate);
        SDL_SetAudioStreamFormat(audio, &spec, nullptr);
        audioRate = sample_rate;
    }
    
    SDL_AudioSpec device = {};
    s32 device_frames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio), &device, &device_frames)) {
        return;
    }

    f64 queued_frames = static_cast<f64>(SDL_GetAudioStreamQueued(audio)) / gAudioFrameSize;
    f64 native_frames = static_cast<f64>(device_frames) * sample_rate / device.freq;
    f64 ratio = queued_frames / native_frames;
    SDL_SetAudioStreamFrequencyRatio(audio, audioLimited ? 1.0f : static_cast<f32>(std::clamp(ratio, 1.0, gAudioMaxRatio)));
    if (ratio >= gAudioMaxRatio) {
        return;
    }

    SDL_PutAudioStreamData(audio, samples, static_cast<s32>(frame_count * gAudioFrameSize));
}

