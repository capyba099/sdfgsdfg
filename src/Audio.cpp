#include "Audio.h"
#include <cmath>
#include <cstdlib>

namespace {
constexpr float TAU = 6.28318530718f;

float frand01() { return (float)std::rand() / (float)RAND_MAX; }

// Append a tone with an exponential decay envelope.
void addTone(std::vector<int16_t>& b, int freq, float dur, float startFreq,
             float endFreq, float amp, float decay) {
    int n = (int)(freq * dur);
    int base = (int)b.size();
    b.resize(base + n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / n;
        float f = startFreq + (endFreq - startFreq) * t;
        phase += TAU * f / freq;
        float env = std::exp(-decay * t);
        float s = std::sin((float)phase) * amp * env;
        int v = (int)(b[base + i]) + (int)(s * 32767.0f);
        v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
        b[base + i] = (int16_t)v;
    }
}

// Append a filtered-noise burst (gunshot-like).
void addNoise(std::vector<int16_t>& b, int freq, float dur, float amp,
              float decay, float lowpass) {
    int n = (int)(freq * dur);
    int base = (int)b.size();
    b.resize(base + n);
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / n;
        float white = frand01() * 2.0f - 1.0f;
        prev = prev + lowpass * (white - prev);  // simple one-pole low-pass
        float env = std::exp(-decay * t);
        float s = prev * amp * env;
        int v = (int)(b[base + i]) + (int)(s * 32767.0f);
        v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
        b[base + i] = (int16_t)v;
    }
}

void silence(std::vector<int16_t>& b, float freq, float dur) {
    b.resize(b.size() + (int)(freq * dur), 0);
}
}  // namespace

bool Audio::init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = freq_;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = &Audio::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                               SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (dev_ == 0) return false;
    freq_ = have.freq;
    buildSfx();
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void Audio::shutdown() {
    if (dev_) {
        SDL_CloseAudioDevice(dev_);
        dev_ = 0;
    }
}

void Audio::buildSfx() {
    int f = freq_;
    // Gunshots: a punchy low thump + a noise crack, varying by weapon.
    addNoise(buffers_[ShotPistol], f, 0.13f, 0.55f, 7.0f, 0.55f);
    addTone(buffers_[ShotPistol], f, 0.10f, 180, 70, 0.5f, 7.0f);

    addNoise(buffers_[ShotSmg], f, 0.09f, 0.5f, 9.0f, 0.6f);
    addTone(buffers_[ShotSmg], f, 0.07f, 220, 90, 0.4f, 9.0f);

    addNoise(buffers_[ShotRifle], f, 0.16f, 0.7f, 6.0f, 0.5f);
    addTone(buffers_[ShotRifle], f, 0.12f, 150, 60, 0.6f, 6.0f);

    addNoise(buffers_[ShotSniper], f, 0.30f, 0.85f, 4.0f, 0.4f);
    addTone(buffers_[ShotSniper], f, 0.22f, 120, 45, 0.7f, 4.5f);

    addNoise(buffers_[Knife], f, 0.06f, 0.4f, 16.0f, 0.85f);

    // Reload: two mechanical clicks.
    addNoise(buffers_[Reload], f, 0.04f, 0.5f, 24.0f, 0.95f);
    silence(buffers_[Reload], f, 0.10f);
    addNoise(buffers_[Reload], f, 0.05f, 0.6f, 20.0f, 0.95f);

    addTone(buffers_[Hit], f, 0.05f, 1500, 1100, 0.45f, 12.0f);
    addTone(buffers_[Death], f, 0.45f, 420, 70, 0.5f, 3.0f);

    addTone(buffers_[Plant], f, 0.10f, 880, 880, 0.5f, 4.0f);
    addTone(buffers_[Defuse], f, 0.12f, 520, 520, 0.45f, 4.0f);
    addTone(buffers_[Beep], f, 0.06f, 1200, 1200, 0.4f, 6.0f);
    addNoise(buffers_[Footstep], f, 0.05f, 0.25f, 14.0f, 0.5f);

    // Jingles.
    addTone(buffers_[Win], f, 0.12f, 523, 523, 0.4f, 2.5f);
    addTone(buffers_[Win], f, 0.12f, 659, 659, 0.4f, 2.5f);
    addTone(buffers_[Win], f, 0.18f, 784, 784, 0.4f, 2.0f);
    addTone(buffers_[Lose], f, 0.12f, 392, 392, 0.4f, 2.5f);
    addTone(buffers_[Lose], f, 0.12f, 330, 330, 0.4f, 2.5f);
    addTone(buffers_[Lose], f, 0.20f, 247, 247, 0.4f, 2.0f);

    addTone(buffers_[UiMove], f, 0.04f, 660, 660, 0.3f, 6.0f);
    addTone(buffers_[UiSelect], f, 0.07f, 880, 990, 0.35f, 5.0f);
}

void Audio::play(Sfx s, float volume, float pan) {
    if (!dev_ || s < 0 || s >= COUNT || buffers_[s].empty()) return;
    if (volume <= 0.001f) return;
    if (volume > 1.0f) volume = 1.0f;
    pan = pan < -1 ? -1 : (pan > 1 ? 1 : pan);
    float l = volume * (pan > 0 ? (1.0f - pan * 0.85f) : 1.0f);
    float r = volume * (pan < 0 ? (1.0f + pan * 0.85f) : 1.0f);

    SDL_LockAudioDevice(dev_);
    for (auto& v : voices_) {
        if (!v.active) {
            v.buf = &buffers_[s];
            v.pos = 0;
            v.volL = l;
            v.volR = r;
            v.active = true;
            break;
        }
    }
    SDL_UnlockAudioDevice(dev_);
}

void Audio::callback(void* userdata, Uint8* stream, int len) {
    Audio* self = static_cast<Audio*>(userdata);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int frames = len / (int)(sizeof(int16_t) * 2);
    for (int i = 0; i < frames; ++i) {
        float mixL = 0, mixR = 0;
        for (auto& v : self->voices_) {
            if (!v.active) continue;
            if (v.pos >= v.buf->size()) { v.active = false; continue; }
            float s = (*v.buf)[v.pos++] / 32768.0f;
            mixL += s * v.volL;
            mixR += s * v.volR;
        }
        // Soft clip.
        mixL = mixL > 1 ? 1 : (mixL < -1 ? -1 : mixL);
        mixR = mixR > 1 ? 1 : (mixR < -1 ? -1 : mixR);
        out[i * 2] = (int16_t)(mixL * 32767.0f);
        out[i * 2 + 1] = (int16_t)(mixR * 32767.0f);
    }
}
