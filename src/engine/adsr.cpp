#include "adsr.h"
#include <cmath>
#include <algorithm>

void VolumeEnvelope::Reset(u8 rate_, u8 rate_mask_, bool decreasing_, bool exponential_, bool phase_invert_) {
    rate = rate_; decreasing = decreasing_; exponential = exponential_;
    phase_invert = phase_invert_ && !(decreasing_ && exponential_);
    counter = 0; counter_increment = 0x8000;
    const s16 base_step = 7 - (rate & 3);
    step = ((decreasing_ ^ phase_invert_) | (decreasing_ & exponential_)) ? ~base_step : base_step;
    if (rate < 44) { step <<= (11 - (rate >> 2)); }
    else if (rate >= 48) {
        counter_increment >>= ((rate >> 2) - 11);
        if ((rate & rate_mask_) != rate_mask_) counter_increment = std::max<u16>(counter_increment, 1u);
    }
}

bool VolumeEnvelope::Tick(s16& current_level) {
    u32 this_increment = counter_increment; s32 this_step = step;
    if (exponential) {
        if (decreasing) this_step = (this_step * current_level) >> 15;
        else {
            if (current_level >= 0x6000) {
                if (rate < 40) this_step >>= 2;
                else if (rate >= 44) this_increment >>= 2;
                else { this_step >>= 1; this_increment >>= 1; }
            }
        }
    }
    counter += this_increment;
    if (!(counter & 0x8000)) return true;
    counter = 0;
    s32 new_level = current_level + this_step;
    if (!decreasing) {
        if (new_level < -32768) new_level = -32768; if (new_level > 32767) new_level = 32767;
        current_level = (s16)new_level;
        return (new_level != ((this_step < 0) ? -32768 : 32767));
    } else {
        if (phase_invert) { if (new_level < -32768) new_level = -32768; if (new_level > 0) new_level = 0; }
        else { if (new_level < 0) new_level = 0; }
        current_level = (s16)new_level;
        return (new_level == 0);
    }
}

HardwareADSR::HardwareADSR(u32 registers) { reg_val = registers; }

void HardwareADSR::KeyOn() { current_volume = 0; phase = Phase::Attack; UpdateEnvelope(); }

void HardwareADSR::KeyOff() { if (phase == Phase::Off || phase == Phase::Release) return; phase = Phase::Release; UpdateEnvelope(); }

void HardwareADSR::UpdateEnvelope() {
    u32 sustain_level = get_bits(reg_val, 0, 4); u32 decay_shift = get_bits(reg_val, 4, 4);
    u32 attack_step = get_bits(reg_val, 8, 2); u32 attack_shift = get_bits(reg_val, 10, 5);
    bool attack_exp = get_bits(reg_val, 15, 1); u32 release_shift = get_bits(reg_val, 16, 5);
    bool release_exp = get_bits(reg_val, 21, 1); u32 sustain_step = get_bits(reg_val, 22, 2);
    u32 sustain_shift = get_bits(reg_val, 24, 5); bool sustain_dec = get_bits(reg_val, 30, 1);
    bool sustain_exp = get_bits(reg_val, 31, 1);
    u8 attack_rate = (attack_shift << 2) | attack_step; u8 decay_rate = (decay_shift << 2);
    u8 sustain_rate = (sustain_shift << 2) | sustain_step; u8 release_rate = (release_shift << 2);
    switch(phase) {
        case Phase::Off: target_volume = 0; envelope.Reset(0, 0, false, false, false); break;
        case Phase::Attack: target_volume = 32767; envelope.Reset(attack_rate, 0x7F, false, attack_exp, false); break;
        case Phase::Decay: target_volume = (s16)std::min<s32>((sustain_level + 1) * 0x800, 32767); envelope.Reset(decay_rate, 0x1F << 2, true, true, false); break;
        case Phase::Sustain: target_volume = 0; envelope.Reset(sustain_rate, 0x7F, sustain_dec, sustain_exp, false); break;
        case Phase::Release: target_volume = 0; envelope.Reset(release_rate, 0x1F << 2, true, release_exp, false); break;
    }
}

s16 HardwareADSR::Tick() {
    if (phase == Phase::Off) return 0;
    if (envelope.counter_increment > 0) envelope.Tick(current_volume);
    if (phase != Phase::Sustain) {
        bool reached = envelope.decreasing ? (current_volume <= target_volume) : (current_volume >= target_volume);
        if (reached) {
            if (phase == Phase::Attack) phase = Phase::Decay;
            else if (phase == Phase::Decay) phase = Phase::Sustain;
            else if (phase == Phase::Release) phase = Phase::Off;
            UpdateEnvelope();
        }
    }
    return current_volume;
}

s16 HardwareADSR::calculate_timecents(u32 reg, Phase phase) {
    u32 decay_shift = (reg >> 4) & 0xF;
    u32 attack_step = (reg >> 8) & 0x3;
    u32 attack_shift = (reg >> 10) & 0x1F;
    u32 release_shift = (reg >> 16) & 0x1F;
    u32 sustain_step = (reg >> 22) & 0x3;
    u32 sustain_shift = (reg >> 24) & 0x1F;

    u8 rate = 0;
    switch(phase) {
        case Phase::Attack: rate = (attack_shift << 2) | attack_step; break;
        case Phase::Decay: rate = (decay_shift << 2); break;
        case Phase::Sustain: rate = (sustain_shift << 2) | sustain_step; break;
        case Phase::Release: rate = (release_shift << 2); break;
        default: return -32768;
    }

    if (rate == 0) return -32768; 
    if (rate >= 0x7F) return -32768; 

    double seconds = 0.001 * std::pow(2.0, (127.0 - rate) / 12.0);
    if (seconds <= 0.0001) return -32768;
    
    return (s16)(1200.0 * std::log2(seconds));
}
