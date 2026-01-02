#ifndef ADSR_H
#define ADSR_H

#include "../common.h"
#include <algorithm>

class VolumeEnvelope {
public:
    u32 counter = 0; u16 counter_increment = 0; s16 step = 0; u8 rate = 0;
    bool decreasing = false; bool exponential = false; bool phase_invert = false;
    void Reset(u8 rate_, u8 rate_mask_, bool decreasing_, bool exponential_, bool phase_invert_);
    bool Tick(s16& current_level);
};

class HardwareADSR {
public:
    enum class Phase { Off, Attack, Decay, Sustain, Release };
    Phase phase = Phase::Off;
    VolumeEnvelope envelope;
    s16 current_volume = 0; s16 target_volume = 0; u32 reg_val = 0;

    HardwareADSR(u32 registers);
    void KeyOn();
    void KeyOff();
    void UpdateEnvelope();
    s16 Tick();

    static s16 calculate_timecents(u32 reg, Phase phase);

private:
    u32 get_bits(u32 val, int bit, int count) const { return (val >> bit) & ((1 << count) - 1); }
};

#endif
