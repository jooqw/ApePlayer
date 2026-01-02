#define MINIAUDIO_IMPLEMENTATION
#include "../../libs/miniaudio.h"
#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

AudioEngine::AudioEngine() : m_device(new ma_device) {}

AudioEngine::~AudioEngine() {
    if (m_initialized) ma_device_uninit(m_device);
    delete m_device;
}

void AudioEngine::data_callback(ma_device* pDevice, void* pOutput, const void* pInput, unsigned int frameCount) {
    AudioEngine* engine = (AudioEngine*)pDevice->pUserData;
    if (!engine) return;

    std::lock_guard<std::mutex> lock(engine->m_mutex);
    int16_t* out = (int16_t*)pOutput;

    memset(out, 0, frameCount * 2 * sizeof(int16_t));

    if (!engine->m_mixer.isPlaying) return;

    for (int v = 0; v < 2; ++v) {
        auto& voice = engine->m_mixer.voices[v];
        if (!voice.active || voice.buffer.empty()) continue;

        for (unsigned int i = 0; i < frameCount; ++i) {
            // Loop Logic
            if (voice.cursor >= voice.buffer.size()) {
                if (voice.loop) voice.cursor = voice.loopStart;
                else { voice.active = false; break; }
            }

            size_t effectiveEnd = voice.loop ? voice.loopEnd : voice.buffer.size();
            if (voice.cursor >= effectiveEnd) {
                if (voice.loop) voice.cursor = voice.loopStart;
                else { voice.active = false; break; }
            }

            int16_t sample = voice.buffer[voice.cursor++];

            int32_t currentL = out[i * 2];
            int32_t currentR = out[i * 2 + 1];

            int32_t mixedL = currentL + (int32_t)(sample * voice.gainL);
            int32_t mixedR = currentR + (int32_t)(sample * voice.gainR);

            // Hard Clip
            if (mixedL > 32767) mixedL = 32767; else if (mixedL < -32768) mixedL = -32768;
            if (mixedR > 32767) mixedR = 32767; else if (mixedR < -32768) mixedR = -32768;

            out[i * 2]     = (int16_t)mixedL;
            out[i * 2 + 1] = (int16_t)mixedR;
        }
    }
}

bool AudioEngine::init() {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate        = 44100;
    config.dataCallback      = data_callback;
    config.pUserData         = this;

    if (ma_device_init(NULL, &config, m_device) != MA_SUCCESS) return false;
    if (ma_device_start(m_device) != MA_SUCCESS) {
        ma_device_uninit(m_device);
        return false;
    }
    m_initialized = true;
    return true;
}

void AudioEngine::play(const std::vector<VoiceRequest>& requests) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_mixer.isPlaying = false;
    for(int i=0; i<2; i++) m_mixer.voices[i].active = false;

    int limit = std::min((int)requests.size(), 2);
    for (int i = 0; i < limit; ++i) {
        const auto& req = requests[i];
        auto& v = m_mixer.voices[i];

        v.buffer = req.pcm;
        v.loop = req.loop;
        v.loopStart = req.loopStart;
        v.loopEnd = (req.loopEnd > 0 && req.loopEnd <= (int)req.pcm.size()) ? req.loopEnd : req.pcm.size();
        v.cursor = 0;

        float volNorm = (float)req.vol / 127.0f;
        float panNorm = (float)req.pan / 127.0f;
        if(panNorm < 0.0f) panNorm = 0.0f;
        if(panNorm > 1.0f) panNorm = 1.0f;

        v.gainL = (1.0f - panNorm) * volNorm;
        v.gainR = panNorm * volNorm;

        v.active = true;
    }
    m_mixer.isPlaying = true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mixer.isPlaying = false;
}

void AudioEngine::setLooping(bool loop) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for(int i=0; i<2; i++) m_mixer.voices[i].loop = loop;
}

// EngineUtils Implementation

static const double F0[] = {0.0, 0.9375, 1.796875, 1.53125, 1.90625};
static const double F1[] = {0.0, 0.0, -0.8125, -0.859375, -0.9375};

DecodedSample EngineUtils::decode_adpcm(const std::vector<u8>& adpcm_data) {
    DecodedSample result;
    std::vector<s16> samples;
    double s1 = 0, s2 = 0;
    int num_blocks = adpcm_data.size() / 16;
    samples.reserve(num_blocks * 28);
    for (int b = 0; b < num_blocks; b++) {
        int offset = b * 16;
        u8 shift_filter = adpcm_data[offset], flags = adpcm_data[offset+1];
        int shift = 12 - (shift_filter & 0x0F);
        int filter_idx = (shift_filter >> 4) & 0x07;
        if (filter_idx > 4) filter_idx = 0;
        if (flags & 4) result.loop_start = (int)samples.size();
        if (flags & 1) { if (flags & 2) result.looping = true; result.loop_end = (int)samples.size() + 28; }
        for (int i = 2; i < 16; i++) {
            u8 byte = adpcm_data[offset + i]; int nibbles[2] = { byte & 0x0F, (byte >> 4) & 0x0F };
            for (int nib : nibbles) {
                int val_s = (nib < 8) ? nib : nib - 16; double sample;
                if (shift >= 0) sample = (double)(val_s << shift); else sample = (double)(val_s >> (-shift));
                double val = sample + (s1 * F0[filter_idx]) + (s2 * F1[filter_idx]);
                s2 = s1; s1 = val;
                if (val > 32767.0) val = 32767.0; if (val < -32768.0) val = -32768.0;
                samples.push_back((s16)val);
            }
        }
    }
    if (result.loop_end == 0) result.loop_end = (int)samples.size();
    result.pcm = samples;
    return result;
}

int16_t EngineUtils::ps2_vol_to_cb(u8 vol) {
    if (vol == 0) return 1440;
    double ratio = vol / 127.0;
    if (ratio < 0.0001) ratio = 0.0001;
    return static_cast<int16_t>(-200.0 * std::log10(ratio));
}
