#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <string>
#include <memory>
#include <utility>

using u8 = uint8_t;
using s8 = int8_t;
using u16 = uint16_t;
using s16 = int16_t;
using u32 = uint32_t;
using s32 = int32_t;

struct DecodedSample {
    std::vector<s16> pcm;
    int loop_start = 0;
    int loop_end = 0;
    bool looping = false;
};

struct Tone {
    u8 min_note;
    u8 max_note;
    u8 root_key;
    s8 pitch_fine;
    u32 bd_offset;
    u16 adsr1;
    u16 adsr2;
    u8 vol;
    u8 pan;
    u8 pitch_mult;
    u8 breath_idx;
    u8 flags;

    bool is_high_priority() const { return (flags & 0x01) != 0; }
    bool is_noise() const { return (flags & 0x02) != 0; }
    bool use_prog_pitch() const { return (flags & 0x10) != 0; }
    bool use_modulation() const { return (flags & 0x20) != 0; }
    bool use_prog_breath() const { return (flags & 0x40) != 0; }
    bool is_reverb() const { return (flags & 0x80) != 0; }
};

struct Program {
    int id;
    u8 type;
    u8 master_vol;
    u8 master_pan;
    u8 pitch_mult;
    u8 breath_idx;

    bool is_sfx = false;
    bool is_layered = false;  // (type & 0x80) != 0

    std::vector<Tone> tones;
};

struct SQChannelInit {
    u8 prog_idx;
    u8 vol;
    u8 pan;
    u8 modulation;
    u8 pitch_bend;
    u8 vibrato;
};

namespace Util {
    inline s16 clamp16(int val) {
        if (val < -32768) return -32768;
        if (val > 32767) return 32767;
        return (s16)val;
    }
    inline int clamp_pan(int val) {
        if (val < 0) return 0;
        if (val > 127) return 127;
        return val;
    }
    inline u16 readU16(const std::vector<u8>& data, size_t offset) {
        if (offset + 2 > data.size()) return 0;
        return data[offset] | (data[offset + 1] << 8);
    }
    inline u32 readU32(const std::vector<u8>& data, size_t offset) {
        if (offset + 4 > data.size()) return 0;
        return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24);
    }
    inline u32 readU32BE(const std::vector<u8>& data, size_t offset) {
        if (offset + 4 > data.size()) return 0;
        return (data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) | data[offset + 3];
    }
    inline u16 readU16BE(const std::vector<u8>& data, size_t offset) {
        if (offset + 2 > data.size()) return 0;
        return (data[offset] << 8) | data[offset + 1];
    }
    inline s8 readS8(const std::vector<u8>& data, size_t offset) {
        if (offset + 1 > data.size()) return 0;
        return (s8)data[offset];
    }
    
    inline std::pair<int, size_t> read_varlen(const std::vector<u8>& data, size_t cursor) {
        int value = 0;
        while (cursor < data.size()) {
            u8 byte = data[cursor++];
            value = (value << 7) | (byte & 0x7F);
            if (!(byte & 0x80)) break;
        }
        return {value, cursor};
    }
}

#endif // COMMON_H
