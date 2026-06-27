#pragma once
#include <SDL.h>
#include <array>
#include <cstdint>
#include <vector>

// A tiny self-contained audio engine: it synthesizes all sound effects
// procedurally at startup (no audio files needed) and mixes a small pool of
// voices in the SDL audio callback. Stereo, with simple volume + panning.
class Audio {
public:
    enum Sfx {
        ShotPistol, ShotSmg, ShotRifle, ShotSniper, Knife, Reload, Hit,
        Death, Plant, Defuse, Beep, Footstep, Win, Lose, UiMove, UiSelect,
        COUNT
    };

    bool init();
    void shutdown();

    // volume 0..1, pan -1 (left) .. +1 (right).
    void play(Sfx s, float volume = 1.0f, float pan = 0.0f);

    bool ready() const { return dev_ != 0; }

private:
    static void callback(void* userdata, Uint8* stream, int len);

    struct Voice {
        const std::vector<int16_t>* buf = nullptr;
        size_t pos = 0;
        float volL = 0, volR = 0;
        bool active = false;
    };

    void buildSfx();

    SDL_AudioDeviceID dev_ = 0;
    int freq_ = 44100;
    std::array<std::vector<int16_t>, COUNT> buffers_;
    static constexpr int MAX_VOICES = 48;
    std::array<Voice, MAX_VOICES> voices_;
};
