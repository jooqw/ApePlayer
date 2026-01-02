#ifndef AUDIO_H
#define AUDIO_H

#include "../common.h"
#include <vector>
#include <mutex>
#include <cstdint>

// Forward declare miniaudio stuff
struct ma_device;

struct VoiceRequest {
    std::vector<int16_t> pcm;
    bool loop = false;
    int loopStart = 0;
    int loopEnd = 0;
    int vol = 127;
    int pan = 64;
};

struct MixerVoice {
    bool active = false;
    std::vector<int16_t> buffer;
    size_t cursor = 0;
    bool loop = false;
    size_t loopStart = 0;
    size_t loopEnd = 0;
    float gainL = 0.5f;
    float gainR = 0.5f;
};

struct MixerState {
    bool isPlaying = false;
    MixerVoice voices[2];
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    void play(const std::vector<VoiceRequest>& requests);
    void stop();
    void setLooping(bool loop);

private:
    ma_device* m_device = nullptr;
    bool m_initialized = false;
    std::mutex m_mutex;
    MixerState m_mixer;

    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, unsigned int frameCount);
};

class EngineUtils {
public:
    static DecodedSample decode_adpcm(const std::vector<u8>& adpcm_data);
    static int16_t ps2_vol_to_cb(u8 vol);
};

#endif // AUDIO_H
