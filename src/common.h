#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <vector>

#pragma pack(push, 1)

struct ApeInstrumentPart {
    uint8_t key_min;        // 0x00
    uint8_t key_max;        // 0x01
    uint8_t key_root;       // 0x02
    int8_t  cents;          // 0x03
    uint16_t offset;        // 0x04-0x05
    uint8_t env_sustain_lvl;// 0x06
    uint8_t env_attack;     // 0x07

    uint16_t env_release_sustain;

    uint8_t  unk_0A;        // Padding?
    uint8_t vol;            // 0x0B
    uint8_t pan;            // 0x0C
    uint8_t unk_0D;         // Unknown
    uint8_t unk_0E;         // Unknown
    uint8_t reverb;         // 0x0F (0x80+ = On)
};

struct ApePatchHeader {
    uint8_t unk1;
    uint8_t vol;
    uint8_t header[4];
    uint8_t startkey;
    uint8_t unk2;
};
#pragma pack(pop)

struct VagResult {
    std::vector<int16_t> pcm;
    int loopStartSample = 0;
    int loopEndSample = 0;
    bool loopEnabled = false;
};

static const int GEN_FREQ = 43000;

#endif // COMMON_H
