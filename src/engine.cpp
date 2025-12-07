#include "engine.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <iomanip>

// =============================================================
// INTERNAL HELPERS
// =============================================================

class FastNoise {
    uint32_t state = 0xA491;
public:
    inline int16_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return (int16_t)(x & 0xFFFF);
    }
};

static const double F0[] = {0.0, 0.9375, 1.796875, 1.53125, 1.90625};
static const double F1[] = {0.0, 0.0, -0.8125, -0.859375, -0.9375};

// =============================================================
// BD PARSER
// =============================================================

bool BDParser::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    data.resize(file.tellg());
    file.seekg(0); file.read((char*)data.data(), data.size());
    return true;
}

std::vector<u8> BDParser::get_adpcm_block(u32 start_offset) {
    std::vector<u8> raw_blocks;
    size_t cursor = start_offset;
    if (cursor >= data.size()) return {};
    while (true) {
        if (cursor + 16 > data.size()) break;
        size_t current_size = raw_blocks.size();
        raw_blocks.resize(current_size + 16);
        std::memcpy(raw_blocks.data() + current_size, data.data() + cursor, 16);
        u8 flags = data[cursor + 1];
        cursor += 16;
        if (flags & 1) break;
        if (raw_blocks.size() > 1024 * 1024) break;
    }
    return raw_blocks;
}

// =============================================================
// HD PARSER
// =============================================================

void HDParser::clear() { programs.clear(); breath_scripts.clear(); data.clear(); }

bool HDParser::load(const std::string& filename) {
    clear();
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    data.resize(file.tellg()); file.seekg(0); file.read((char*)data.data(), data.size());
    if (data.size() < 16 || std::memcmp(data.data() + 0x0C, "SShd", 4) != 0) return false;
    parse();
    return true;
}

void HDParser::parse() {
    u32 prog_offset = Util::readU32(data, 0x10);
    u32 breath_offset = Util::readU32(data, 0x18);
    if (prog_offset < data.size()) parse_programs(prog_offset);
    if (breath_offset < data.size()) parse_breath_waves(breath_offset);
}

void HDParser::parse_programs(u32 base_offset) {
    u16 count = Util::readU16(data, base_offset) + 1;
    u32 ptr_table = base_offset + 2;
    for (int i = 0; i < count; i++) {
        if (ptr_table + (i * 2) + 2 > data.size()) break;
        u16 rel_offset = Util::readU16(data, ptr_table + (i * 2));
        if (rel_offset == 0xFFFF) { programs.push_back(nullptr); continue; }

        u32 abs_offset = base_offset + rel_offset;
        auto prog = std::make_shared<Program>();
        prog->id = i;
        prog->type = data[abs_offset];
        prog->master_vol = data[abs_offset + 1];
        prog->master_pan = data[abs_offset + 2];
        prog->pitch_mult = data[abs_offset + 4];
        prog->breath_idx = data[abs_offset + 5];

        prog->is_sfx = (prog->type == 0xFF);
        if (prog->is_sfx) prog->is_layered = false;
        else prog->is_layered = (prog->type & 0x80) != 0;

        int tone_count = prog->is_sfx ? data[abs_offset + 7] : (prog->type & 0x7F) + 1;
        u32 current_tone_offset = abs_offset + 8;

        for (int t = 0; t < tone_count; t++) {
            if (current_tone_offset + 16 > data.size()) break;
            Tone tone;
            tone.min_note = data[current_tone_offset + 0]; tone.max_note = data[current_tone_offset + 1];
            tone.root_key = data[current_tone_offset + 2]; tone.pitch_fine = (s8)data[current_tone_offset + 3];
            tone.bd_offset = Util::readU16(data, current_tone_offset + 4) * 8;
            tone.adsr1 = Util::readU16(data, current_tone_offset + 6);
            u16 raw_adsr2 = Util::readU16(data, current_tone_offset + 8);
            tone.vol = data[current_tone_offset + 11]; tone.pan = data[current_tone_offset + 12];
            tone.pitch_mult = data[current_tone_offset + 13]; tone.breath_idx = data[current_tone_offset + 14];
            tone.flags = data[current_tone_offset + 15];
            tone.adsr2 = raw_adsr2 ^ (u16)data[current_tone_offset + 10];
            prog->tones.push_back(tone);
            current_tone_offset += 16;
        }
        programs.push_back(prog);
    }
}

void HDParser::parse_breath_waves(u32 base_offset) {
    u16 count = Util::readU16(data, base_offset) + 1;
    u32 ptr_table = base_offset + 2;
    std::vector<u32> offsets;
    for (int i = 0; i < count; i++) offsets.push_back(base_offset + Util::readU16(data, ptr_table + (i * 2)));
    for (size_t i = 0; i < offsets.size(); i++) {
        u32 start = offsets[i];
        u32 end = (i + 1 < offsets.size()) ? offsets[i+1] : (u32)data.size();
        if (end > data.size()) end = (u32)data.size();
        if (start >= end) { breath_scripts.push_back({}); continue; }
        std::vector<u8> script(end - start);
        std::memcpy(script.data(), data.data() + start, end - start);
        breath_scripts.push_back(script);
    }
}

void HDParser::print_debug_info() const { /* ... */ }

// =============================================================
// ENGINE UTILS
// =============================================================

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

int16_t EngineUtils::calculate_adsr_timecents(u32 reg_val, HardwareADSR::Phase phase) {
    return HardwareADSR::simulate_timecents(reg_val, phase);
}

// =============================================================
// HARDWARE ADSR
// =============================================================

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

int16_t HardwareADSR::simulate_timecents(u32 reg_val, Phase target_phase) {
    HardwareADSR sim(reg_val);
    sim.phase = target_phase;
    sim.current_volume = (target_phase == Phase::Attack) ? 0 : 32767;
    sim.UpdateEnvelope();
    if (sim.envelope.counter_increment == 0) return 8000;
    int samples = 0; int limit = 44100 * 15;
    while (samples < limit) {
        sim.envelope.Tick(sim.current_volume);
        bool finished = (target_phase == Phase::Attack) ? (sim.current_volume >= 32767) : (sim.current_volume <= 0);
        if (finished) break;
        samples++;
    }
    if (samples <= 1) return -32768;
    double seconds = (double)samples / 44100.0;
    if (seconds < 0.001) return -32768;
    return static_cast<int16_t>(1200.0 * std::log2(seconds));
}

// =============================================================
// REVERB ENGINE
// =============================================================

ReverbEngine::ReverbEngine() {
    ram.resize(256 * 1024, 0); std::memset(&regs, 0, sizeof(ReverbRegs));
}

void ReverbEngine::init_studio_large() {
    regs.mBASE = 0; base_addr = 0; current_addr = 0;
    regs.dAPF1 = 0x00E3; regs.dAPF2 = 0x00A9; regs.vIIR = 0x6F60;
    regs.vCOMB1 = 0x4FA8; regs.vCOMB2 = 0xBCE0; regs.vCOMB3 = 0x4510; regs.vCOMB4 = 0xBEF0;
    regs.vWALL = 0xA680; regs.vAPF1 = 0x5680; regs.vAPF2 = 0x52C0;
    regs.mLSAME = 0x0DFB; regs.mRSAME = 0x0B58; regs.mLCOMB1 = 0x0D09; regs.mRCOMB1 = 0x0A3C;
    regs.mLCOMB2 = 0x0BD9; regs.mRCOMB2 = 0x0973; regs.dLSAME = 0x0B59; regs.dRSAME = 0x08DA;
    regs.mLDIFF = 0x08D9; regs.mRDIFF = 0x05E9; regs.mLCOMB3 = 0x07EC; regs.mRCOMB3 = 0x04B0;
    regs.mLCOMB4 = 0x06EF; regs.mRCOMB4 = 0x03D2; regs.dLDIFF = 0x05EA; regs.dRDIFF = 0x031D;
    regs.mLAPF1 = 0x031C; regs.mRAPF1 = 0x0238; regs.mLAPF2 = 0x0154; regs.mRAPF2 = 0x00AA;
    regs.vLIN = 0x4000; regs.vRIN = 0x4000; regs.vLOUT = 0x4000; regs.vROUT = 0x4000;
}

static const int RAM_SIZE = 512 * 1024;
static const int RAM_MASK = (RAM_SIZE - 1) / 2;
inline s16 rev_read(const std::vector<s16>& ram, u32 current, u32 base, u32 rel_addr) {
    u32 offset = current + (rel_addr & RAM_MASK);
    if (offset >= RAM_MASK) offset -= (RAM_MASK - base);
    return ram[offset % (RAM_SIZE / 2)];
}

void ReverbEngine::process(const std::vector<float>& in_l, const std::vector<float>& in_r, std::vector<float>& out_l, std::vector<float>& out_r) {
    out_l.resize(in_l.size()); out_r.resize(in_r.size());
    for (size_t i = 0; i < in_l.size(); i++) {
        s16 lin = Util::clamp16((int)(in_l[i] * 32767.0f)); s16 rin = Util::clamp16((int)(in_r[i] * 32767.0f));
        s32 Lin = (lin * regs.vLIN) >> 15; s32 Rin = (rin * regs.vRIN) >> 15;

        auto write_ram = [&](u32 rel, s16 v) { ram[(current_addr + (rel&RAM_MASK)) % (RAM_SIZE/2)] = v; };

        s16 dLSAME_val = rev_read(ram, current_addr, base_addr, regs.dLSAME);
        s16 mLSAME_2_val = rev_read(ram, current_addr, base_addr, regs.mLSAME - 2);
        s32 l_same = ((Lin + ((dLSAME_val * regs.vWALL) >> 15) - mLSAME_2_val) * regs.vIIR) >> 15;
        l_same += mLSAME_2_val; write_ram(regs.mLSAME, Util::clamp16(l_same));

        s16 dRSAME_val = rev_read(ram, current_addr, base_addr, regs.dRSAME);
        s16 mRSAME_2_val = rev_read(ram, current_addr, base_addr, regs.mRSAME - 2);
        s32 r_same = ((Rin + ((dRSAME_val * regs.vWALL) >> 15) - mRSAME_2_val) * regs.vIIR) >> 15;
        r_same += mRSAME_2_val; write_ram(regs.mRSAME, Util::clamp16(r_same));

        s16 dRDIFF_val = rev_read(ram, current_addr, base_addr, regs.dRDIFF);
        s16 mLDIFF_2_val = rev_read(ram, current_addr, base_addr, regs.mLDIFF - 2);
        s32 l_diff = ((Lin + ((dRDIFF_val * regs.vWALL) >> 15) - mLDIFF_2_val) * regs.vIIR) >> 15;
        l_diff += mLDIFF_2_val; write_ram(regs.mLDIFF, Util::clamp16(l_diff));

        s16 dLDIFF_val = rev_read(ram, current_addr, base_addr, regs.dLDIFF);
        s16 mRDIFF_2_val = rev_read(ram, current_addr, base_addr, regs.mRDIFF - 2);
        s32 r_diff = ((Rin + ((dLDIFF_val * regs.vWALL) >> 15) - mRDIFF_2_val) * regs.vIIR) >> 15;
        r_diff += mRDIFF_2_val; write_ram(regs.mRDIFF, Util::clamp16(r_diff));

        s32 Lout = ((regs.vCOMB1 * rev_read(ram, current_addr, base_addr, regs.mLCOMB1)) + (regs.vCOMB2 * rev_read(ram, current_addr, base_addr, regs.mLCOMB2)) + (regs.vCOMB3 * rev_read(ram, current_addr, base_addr, regs.mLCOMB3)) + (regs.vCOMB4 * rev_read(ram, current_addr, base_addr, regs.mLCOMB4))) >> 15;
        s32 Rout = ((regs.vCOMB1 * rev_read(ram, current_addr, base_addr, regs.mRCOMB1)) + (regs.vCOMB2 * rev_read(ram, current_addr, base_addr, regs.mRCOMB2)) + (regs.vCOMB3 * rev_read(ram, current_addr, base_addr, regs.mRCOMB3)) + (regs.vCOMB4 * rev_read(ram, current_addr, base_addr, regs.mRCOMB4))) >> 15;

        s16 mLAPF1_d_val = rev_read(ram, current_addr, base_addr, regs.mLAPF1 - regs.dAPF1); Lout = Lout - ((regs.vAPF1 * mLAPF1_d_val) >> 15); write_ram(regs.mLAPF1, Util::clamp16(Lout)); Lout = ((Lout * regs.vAPF1) >> 15) + mLAPF1_d_val;
        s16 mRAPF1_d_val = rev_read(ram, current_addr, base_addr, regs.mRAPF1 - regs.dAPF1); Rout = Rout - ((regs.vAPF1 * mRAPF1_d_val) >> 15); write_ram(regs.mRAPF1, Util::clamp16(Rout)); Rout = ((Rout * regs.vAPF1) >> 15) + mRAPF1_d_val;
        s16 mLAPF2_d_val = rev_read(ram, current_addr, base_addr, regs.mLAPF2 - regs.dAPF2); Lout = Lout - ((regs.vAPF2 * mLAPF2_d_val) >> 15); write_ram(regs.mLAPF2, Util::clamp16(Lout)); Lout = ((Lout * regs.vAPF2) >> 15) + mLAPF2_d_val;
        s16 mRAPF2_d_val = rev_read(ram, current_addr, base_addr, regs.mRAPF2 - regs.dAPF2); Rout = Rout - ((regs.vAPF2 * mRAPF2_d_val) >> 15); write_ram(regs.mRAPF2, Util::clamp16(Rout)); Rout = ((Rout * regs.vAPF2) >> 15) + mRAPF2_d_val;

        out_l[i] = (float)((Lout * regs.vLOUT) >> 15) / 32767.0f; out_r[i] = (float)((Rout * regs.vROUT) >> 15) / 32767.0f;
        current_addr = (current_addr + 1); if (current_addr > 0x3FFFE) current_addr = base_addr;
    }
}

// =============================================================
// VIBRATO ENGINE
// =============================================================

void VibratoEngine::init(const std::vector<u8>& data) {
    lfo_table = data; active = !lfo_table.empty();
    if (active) {
        double nearest_dist = 256.0;
        for (size_t i = 0; i < lfo_table.size(); i++) {
            double dist = std::abs((double)lfo_table[i] - 127.0);
            if (dist < nearest_dist) { nearest_dist = dist; phase = (double)i; }
        }
    } else phase = 0.0;
}

void VibratoEngine::tick(double rate_step) {
    if (!active || lfo_table.empty()) return;
    phase += rate_step;
    double max_sz = (double)lfo_table.size();
    if (phase >= max_sz) phase = std::fmod(phase, max_sz);
    if (phase < 0.0) { phase = std::fmod(phase, max_sz); if (phase < 0.0) phase += max_sz; }
}

float VibratoEngine::get_pitch_offset() const {
    if (!active || lfo_table.empty()) return 0.0f;
    size_t sz = lfo_table.size();
    int idx0 = (int)phase;
    double frac = phase - idx0;
    idx0 = idx0 % sz; if (idx0 < 0) idx0 += sz;
    int idx1 = (idx0 + 1) % sz;
    double val0 = (double)lfo_table[idx0]; double val1 = (double)lfo_table[idx1];
    double interp = val0 + (val1 - val0) * frac;
    uint32_t lfo_val = (uint32_t)std::round(interp); lfo_val = std::min(lfo_val, 255U);
    double center_offset = (lfo_val / 255.0) - 0.5;
    return (float)(center_offset * 2.0 * depth);
}

// =============================================================
// SPU CORE
// =============================================================

void SPU::ChannelState::reset_controllers() {
    vol = 127; expr = 127; pan = 64;
    pitch_bend_factor = 1.0;
    sustain_active = false; portamento_active = false;
    lfo_enabled = false; lfo_depth = 0.0f; modulation = 0;
}

double SPU::ChannelState::get_lfo_ratio(float sample_rate) {
    if (!lfo_enabled || lfo_depth <= 0.0001f) return 1.0;
    lfo_phase += (lfo_rate * 6.283185307f) / sample_rate;
    if (lfo_phase > 6.283185307f) lfo_phase -= 6.283185307f;
    float val = std::sin(lfo_phase) * lfo_depth * lfo_sensitivity;
    return std::pow(2.0, val / 12.0);
}

SPU::SPU(BDParser* _bd, HDParser* _hd) : bd(_bd), hd(_hd) {
    reverb.init_studio_large();
}

void SPU::apply_seq_header(const std::map<int, SQChannelInit>& inits) {
    for (const auto& [idx, init] : inits) {
        if (idx >= 16) continue;
        channels[idx].prog = init.prog_idx;
        channels[idx].vol = init.vol;
        channels[idx].pan = init.pan;
        channels[idx].modulation = init.modulation;
        channels[idx].breath_rate = init.vibrato;
        channels[idx].lfo_depth = init.modulation / 127.0f;
    }
}

void SPU::note_on(int ch_idx, int note, int vel) {
    ChannelState& ch = channels[ch_idx];
    if (ch.prog >= hd->programs.size() || !hd->programs[ch.prog]) return;
    auto prog = hd->programs[ch.prog];

    if (prog->is_sfx) return;

    ch.lfo_phase = 0.0f;

    std::vector<const Tone*> targets;
    for (const auto& tone : prog->tones) {
        if (note >= tone.min_note && note <= tone.max_note) {
            targets.push_back(&tone);
            if (!prog->is_layered && !prog->is_sfx) break;
        }
    }

    if (targets.empty()) return;

    for (const Tone* target_tone : targets) {
        if (target_tone->is_noise()) continue;

        if (target_tone->use_prog_pitch()) {
            if (prog->pitch_mult != 0) ch.pitch_mult = (double)prog->pitch_mult;
        } else {
            if (target_tone->pitch_mult != 0) ch.pitch_mult = (double)target_tone->pitch_mult;
        }
        ch.lfo_sensitivity = ch.pitch_mult / 128.0f;

        if (sample_cache.find(target_tone->bd_offset) == sample_cache.end()) {
            auto raw = bd->get_adpcm_block(target_tone->bd_offset);
            if (!raw.empty()) sample_cache[target_tone->bd_offset] = EngineUtils::decode_adpcm(raw);
            else sample_cache[target_tone->bd_offset] = DecodedSample();
        }
        DecodedSample& smp = sample_cache[target_tone->bd_offset];
        if (smp.pcm.empty() && !target_tone->is_noise()) continue;

        double root = (target_tone->root_key > 0) ? target_tone->root_key : 60;
        double fine = target_tone->pitch_fine / 20.0;
        double base_pitch = std::pow(2.0, (note - (root - fine)) / 12.0);

        u32 reg_combined = ((u32)target_tone->adsr2 << 16) | (u32)target_tone->adsr1;
        auto adsr = std::make_shared<HardwareADSR>(reg_combined);
        adsr->KeyOn();

        Voice v;
        v.data = smp; v.pos = 0.0; v.note_base_freq = base_pitch;
        v.base_pitch_mult = 1.0; v.target_pitch_mult = 1.0; v.noise_mode = target_tone->is_noise();

        if (ch.portamento_active && ch.last_note_pitch > 0.0) {
            v.base_pitch_mult = ch.last_note_pitch / v.note_base_freq;
            v.sliding = true;
            float slide_time = 0.01f + (ch.portamento_time / 127.0f);
            float num_samples = slide_time * 44100.0f;
            if (num_samples < 1.0f) num_samples = 1.0f;
            v.portamento_step = std::pow(v.target_pitch_mult / v.base_pitch_mult, 1.0 / num_samples);
        } else {
            v.sliding = false; v.portamento_step = 1.0;
        }
        ch.last_note_pitch = v.note_base_freq * v.target_pitch_mult;

        if (target_tone->use_modulation()) {
            int breath_idx = -1;
            if (target_tone->use_prog_breath()) breath_idx = prog->breath_idx; else breath_idx = target_tone->breath_idx;

            if (breath_idx != 0xFF && breath_idx != 0x7F && breath_idx < hd->breath_scripts.size()) {
                v.vibrato.depth = ch.modulation / 127.0f;
                v.vibrato.init(hd->breath_scripts[breath_idx]);
                v.vibrato_enabled = v.vibrato.active;

                if (v.vibrato_enabled) {
                    v.target_pitch_mult *= std::pow(2.0, -0.2 / 12.0);
                    float rate_factor = (ch.breath_rate > 0 ? ch.breath_rate : 64) / 127.0f;
                    double target_hz = 2.0 + (rate_factor * 20.0);
                    v.vibrato_rate_val = (double)hd->breath_scripts[breath_idx].size() * target_hz / 44100.0;
                }
            }
        }

        v.tone_pan = Util::clamp_pan(target_tone->pan + (int)prog->master_pan - 64);
        v.base_vol_factor = (target_tone->vol / 127.0f) * (prog->master_vol / 127.0f) * (vel / 127.0f);
        v.ch = ch_idx; v.note_key = note; v.active = true; v.reverb_on = target_tone->is_reverb(); v.adsr = adsr;

        active_voices.push_back(v);
    }
}

void SPU::note_off(int ch_idx, int note) {
    for (auto& v : active_voices) {
        if (v.ch == ch_idx && v.note_key == note) {
            if (channels[ch_idx].sustain_active) v.release_pending = true;
            else v.adsr->KeyOff();
        }
    }
}

void SPU::program_change(int ch_idx, int prog_id) { channels[ch_idx].prog = prog_id; }
void SPU::pitch_bend(int ch_idx, int val) {
    double mult = channels[ch_idx].pitch_mult;
    channels[ch_idx].pitch_bend_factor = std::pow(2.0, (((val - 64) / 64.0) * mult) / 12.0);
}

void SPU::control_change(int ch_idx, int cc, int val) {
    ChannelState& ch = channels[ch_idx];
    switch(cc) {
        case 7: ch.vol = val; break;
        case 11: ch.expr = val; break;
        case 10: ch.pan = val; break;
        case 91: ch.reverb_depth = val; break;
        case 1: ch.modulation = val; ch.lfo_depth = val/127.0f; break;
        case 64: ch.sustain_active = (val >= 64);
        if(!ch.sustain_active) {
            for(auto& v : active_voices) if(v.ch == ch_idx && v.release_pending) v.adsr->KeyOff();
        }
        break;
        case 65: ch.portamento_active = (val >= 64); break;
        case 5: ch.portamento_time = val; break;
        case 121: ch.reset_controllers(); break;
    }
}

void SPU::render(int num_samples, std::vector<float>& dl, std::vector<float>& dr, std::vector<float>& wl, std::vector<float>& wr, float samples_per_tick) {
    dl.assign(num_samples, 0.0f); dr.assign(num_samples, 0.0f);
    wl.assign(num_samples, 0.0f); wr.assign(num_samples, 0.0f);

    active_voices.erase(std::remove_if(active_voices.begin(), active_voices.end(), [](const Voice& v) { return !v.active; }), active_voices.end());

    static FastNoise noise_gen;

    for (int i = 0; i < num_samples; i++) {
        double mod_ratios[16];
        for(int c=0; c<16; c++) mod_ratios[c] = channels[c].get_lfo_ratio(44100.0f);

        for (auto& v : active_voices) {
            if (!v.active) continue;
            ChannelState& ch = channels[v.ch];

            s16 adsr_vol = v.adsr->Tick();
            if (v.adsr->phase == HardwareADSR::Phase::Off) { v.active = false; continue; }

            if (v.sliding) {
                v.base_pitch_mult *= v.portamento_step;
                if ((v.portamento_step > 1.0 && v.base_pitch_mult >= v.target_pitch_mult) ||
                    (v.portamento_step < 1.0 && v.base_pitch_mult <= v.target_pitch_mult)) {
                    v.base_pitch_mult = v.target_pitch_mult;
                v.sliding = false;
                    }
            }

            float vibrato_pitch_offset = 0.0f;
            if (v.vibrato_enabled) {
                v.vibrato.tick(v.vibrato_rate_val);
                vibrato_pitch_offset = v.vibrato.get_pitch_offset();
            }

            double vib_factor = std::pow(2.0, vibrato_pitch_offset / 12.0);
            if (std::isnan(vib_factor) || std::isinf(vib_factor)) vib_factor = 1.0;

            double effective_pitch = v.note_base_freq * v.base_pitch_mult * vib_factor * ch.pitch_bend_factor * mod_ratios[v.ch];
            if (effective_pitch < 0.0) effective_pitch = 0.0;

            float samp_val = 0.0f;

            if (v.noise_mode) {
                v.pos += effective_pitch;
                if (v.pos >= 1.0) { samp_val = (float)noise_gen.next(); v.pos -= 1.0; }
                else samp_val = (float)noise_gen.next();
            } else {
                int pos_i = (int)v.pos; double frac = v.pos - pos_i;
                s16 s0 = 0, s1 = 0;
                if (pos_i < v.data.pcm.size()) s0 = v.data.pcm[pos_i];
                int next_pos = v.data.looping && v.data.loop_end > v.data.loop_start
                ? (pos_i + 1 >= v.data.loop_end ? v.data.loop_start + (pos_i + 1 - v.data.loop_end) : pos_i + 1) : pos_i + 1;
                if (next_pos < v.data.pcm.size()) s1 = v.data.pcm[next_pos];
                samp_val = s0 + (s1 - s0) * frac;
                v.pos += effective_pitch;

                if (v.data.looping && v.data.loop_end > v.data.loop_start) {
                    double loop_len = v.data.loop_end - v.data.loop_start;
                    while (v.pos >= v.data.loop_end) v.pos -= loop_len;
                } else if (v.pos >= v.data.pcm.size()) {
                    v.active = false; continue;
                }
            }

            float vol = (samp_val / 32768.0f) * (adsr_vol / 32767.0f) * v.base_vol_factor * (ch.vol / 127.0f) * (ch.expr / 127.0f);
            int eff_pan = Util::clamp_pan(v.tone_pan + (ch.pan - 64));
            float pan_val = eff_pan / 127.0f;
            float l = vol * std::sqrt(1.0f - pan_val);
            float r = vol * std::sqrt(pan_val);

            if (std::isnan(l)) l = 0.0f; if (std::isnan(r)) r = 0.0f;
            dl[i] += l; dr[i] += r;

            if (v.reverb_on) {
                float send = vol * (ch.reverb_depth / 127.0f) * 0.707f;
                if (std::isnan(send)) send = 0.0f;
                wl[i] += send; wr[i] += send;
            }
        }
    }
}

// =============================================================
// SEQUENCERS (SQ & MIDI)
// =============================================================

std::pair<int, size_t> read_varlen(const std::vector<u8>& data, size_t cursor) {
    int value = 0;
    while (cursor < data.size()) {
        u8 byte = data[cursor++];
        value = (value << 7) | (byte & 0x7F);
        if (!(byte & 0x80)) break;
    }
    return {value, cursor};
}

void write_varlen(std::vector<u8>& buf, uint32_t value) {
    uint32_t buffer = value & 0x7F;
    while ((value >>= 7)) {
        buffer <<= 8;
        buffer |= ((value & 0x7F) | 0x80);
    }
    while (true) {
        buf.push_back(buffer & 0xFF);
        if (buffer & 0x80) buffer >>= 8;
        else break;
    }
}

bool SQParser::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    data.resize(file.tellg());
    file.seekg(0); file.read((char*)data.data(), data.size());
    if (data.size() < 16) return false;
    ticks_per_quarter = Util::readU16(data, 2);
    if (ticks_per_quarter == 0) ticks_per_quarter = 480;
    u16 raw_tempo = Util::readU16(data, 4);
    if (raw_tempo > 0) tempo_bpm = (float)raw_tempo;

    size_t ch_off = 0x10;
    for(int i=0; i<16; i++) {
        if (ch_off + 16 > data.size()) break;
        SQChannelInit init;
        init.prog_idx = data[ch_off + 2]; init.vol = data[ch_off + 3]; init.pan = data[ch_off + 4];
        init.modulation = data[ch_off + 9]; init.pitch_bend = data[ch_off + 10]; init.vibrato = data[ch_off + 12];
        channel_inits[i] = init;
        ch_off += 16;
    }
    parse_events();
    return true;
}

void SQParser::parse_events() {
    events.clear(); size_t cursor = 0x110; int running_status = 0;
    while (cursor < data.size()) {
        auto res = read_varlen(data, cursor); int delta = res.first; cursor = res.second;
        if (cursor >= data.size()) break;
        u8 byte = data[cursor]; int status;
        if (byte >= 0x80) { status = byte; cursor++; if (status < 0xF0) running_status = status; } else status = running_status;
        int cmd = status & 0xF0; int ch = status & 0x0F;
        if (cmd == 0x80 || cmd == 0x90) { int note = data[cursor++]; int vel = data[cursor++]; events.push_back({delta, "note", cmd, ch, note, vel}); }
        else if (cmd == 0xB0) { int cc = data[cursor++]; int val = data[cursor++]; events.push_back({delta, "cc", cmd, ch, 0, 0, cc, val}); }
        else if (cmd == 0xC0) { int val = data[cursor++]; events.push_back({delta, "prog", cmd, ch, 0, 0, val}); }
        else if (cmd == 0xE0) { int val = data[cursor++]; events.push_back({delta, "pitch", cmd, ch, 0, 0, val}); }
        else if (cmd == 0xF0) {
            if (status == 0xFF) {
                int meta = data[cursor++];
                if (meta == 0x2F) { events.push_back({delta, "loop_end", 0,0,0,0}); break; }
                else if (meta == 0x51) {
                    int len = data[cursor++];
                    if (len == 3) { u32 mpqn = (data[cursor]<<16)|(data[cursor+1]<<8)|data[cursor+2]; cursor+=3; events.push_back({delta, "tempo", 0,0,0,0, (int)(60000000.0/mpqn)}); } else cursor += len;
                } else { int len = data[cursor++]; cursor += len; }
            } else { int len = data[cursor++]; cursor += len; }
        } else cursor++;
    }
}

bool SQParser::saveToMidi(const std::string& filename) {
    if (events.empty()) return false;
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) return false;
    out.write("MThd", 4); u32 head = __builtin_bswap32(6); out.write((char*)&head, 4);
    u16 fmt = 0; out.write((char*)&fmt, 2); u16 trks = __builtin_bswap16(1); out.write((char*)&trks, 2);
    u16 div = __builtin_bswap16(ticks_per_quarter); out.write((char*)&div, 2);
    out.write("MTrk", 4); std::vector<u8> track;
    for(const auto& ev : events) {
        write_varlen(track, ev.delta);
        if(ev.type=="note") { track.push_back(ev.cmd|ev.ch); track.push_back(ev.note); track.push_back(ev.vel); }
        else if(ev.type=="cc") { track.push_back(ev.cmd|ev.ch); track.push_back(ev.cc_val); track.push_back(ev.val); }
        else if(ev.type=="prog" || ev.type=="pitch") { track.push_back(ev.cmd|ev.ch); track.push_back(ev.val); }
        else if(ev.type=="tempo") {
            track.push_back(0xFF); track.push_back(0x51); track.push_back(3);
            int mpqn = 60000000 / (ev.val > 0 ? ev.val : 120);
            track.push_back((mpqn>>16)&0xFF); track.push_back((mpqn>>8)&0xFF); track.push_back(mpqn&0xFF);
        } else if(ev.type=="loop_end") { track.push_back(0xFF); track.push_back(0x2F); track.push_back(0); }
    }
    if(events.back().type != "loop_end") { write_varlen(track, 0); track.push_back(0xFF); track.push_back(0x2F); track.push_back(0); }
    u32 len = __builtin_bswap32(track.size()); out.write((char*)&len, 4); out.write((char*)track.data(), track.size());
    return true;
}

struct AbsEvent {
    u32 abs_time; SQEvent ev;
    bool operator<(const AbsEvent& o) const { return abs_time < o.abs_time; }
};

bool MidiParser::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if(!file.is_open()) return false;
    data.resize(file.tellg()); file.seekg(0); file.read((char*)data.data(), data.size());
    if(data.size() < 14 || std::memcmp(data.data(), "MThd", 4) != 0) return false;
    parse_midi(); return true;
}

void MidiParser::parse_midi() {
    events.clear(); u16 num_trks = Util::readU16BE(data, 10); ticks_per_quarter = Util::readU16BE(data, 12);
    if(ticks_per_quarter & 0x8000) ticks_per_quarter = 480;
    std::vector<AbsEvent> all; size_t cursor = 14;
    for(int t=0; t<num_trks; t++) {
        if(cursor+8 > data.size()) break; if(std::memcmp(data.data()+cursor, "MTrk", 4)!=0) break;
        u32 len = Util::readU32BE(data, cursor+4); cursor += 8; size_t end = cursor + len;
        u32 cur_time = 0; u8 running = 0;
        while(cursor < end && cursor < data.size()) {
            auto res = read_varlen(data, cursor); cur_time += res.first; cursor = res.second; if(cursor>=data.size()) break;
            u8 st = data[cursor]; if(st >= 0x80) { cursor++; if(st < 0xF0) running = st; } else st = running;
            if(st == 0xFF) {
                u8 type = data[cursor++]; auto lenRes = read_varlen(data, cursor); size_t mlen = lenRes.first; cursor = lenRes.second;
                if(type == 0x51 && mlen==3) {
                    u32 mpqn = (data[cursor]<<16)|(data[cursor+1]<<8)|data[cursor+2];
                    SQEvent e; e.type="tempo"; e.val = (int)(60000000.0/mpqn); all.push_back({cur_time, e});
                } cursor += mlen;
            } else if(st == 0xF0 || st == 0xF7) { auto l = read_varlen(data, cursor); cursor = l.first + l.second; }
            else {
                SQEvent e; e.cmd = st&0xF0; e.ch = st&0x0F;
                if(e.cmd == 0x90) { e.type="note"; e.note=data[cursor++]; e.vel=data[cursor++]; if(e.vel==0) e.cmd=0x80; }
                else if(e.cmd == 0x80) { e.type="note"; e.note=data[cursor++]; e.vel=0; cursor++; }
                else if(e.cmd == 0xB0) { e.type="cc"; e.cc_val=data[cursor++]; e.val=data[cursor++]; }
                else if(e.cmd == 0xC0) { e.type="prog"; e.val=data[cursor++]; }
                else if(e.cmd == 0xE0) { cursor++; e.type="pitch"; e.val=data[cursor++]; } else cursor++;
                all.push_back({cur_time, e});
            }
        }
        cursor = end;
    }
    std::stable_sort(all.begin(), all.end());
    u32 prev = 0; for(const auto& ae : all) { SQEvent e = ae.ev; e.delta = ae.abs_time - prev; events.push_back(e); prev = ae.abs_time; }
    events.push_back({0, "loop_end", 0,0,0,0});
}

// =============================================================
// WAV EXPORT IMPLEMENTATION
// =============================================================

struct WavHeader {
    char riff[4] = {'R','I','F','F'};
    u32 overall_size;
    char wave[4] = {'W','A','V','E'};
    char fmt[4] = {'f','m','t',' '};
    u32 fmt_length = 16;
    u16 format_type = 1;
    u16 channels = 2;
    u32 sample_rate = 44100;
    u32 byte_rate = 44100 * 4;
    u16 block_align = 4;
    u16 bits_per_sample = 16;
    char data[4] = {'d','a','t','a'};
    u32 data_size;
};

bool ExportSequenceToWav(const std::string& sqPath, const std::string& wavPath, HDParser* hd, BDParser* bd, bool useReverb, bool isMidi, std::function<void(int, int)> progressCallback) {
    std::shared_ptr<SeqInterface> seq;
    if (isMidi) seq = std::make_shared<MidiParser>();
    else seq = std::make_shared<SQParser>();

    if (!seq->load(sqPath)) return false;

    SPU spu(bd, hd);
    spu.apply_seq_header(seq->channel_inits);

    std::vector<float> mix_l, mix_r;
    float current_bpm = seq->tempo_bpm <= 0 ? 120.0f : seq->tempo_bpm;
    size_t event_idx = 0;
    size_t total_events = seq->events.size();

    while (event_idx < seq->events.size()) {

        if (progressCallback && (event_idx % 100 == 0)) {
            progressCallback((int)event_idx, (int)total_events);
        }

        const auto& ev = seq->events[event_idx];
        if (ev.type == "loop_end") break;

        float sec_per_tick = (60.0f / current_bpm) / seq->ticks_per_quarter;
        float samples_per_tick = sec_per_tick * 44100.0f;

        if (ev.delta > 0) {
            int num_samples = (int)(ev.delta * samples_per_tick);
            if (num_samples > 0) {
                std::vector<float> dl, dr, wl, wr;
                spu.render(num_samples, dl, dr, wl, wr, samples_per_tick);

                if (useReverb) {
                    std::vector<float> rl, rr;
                    spu.reverb.process(wl, wr, rl, rr);
                    for(size_t i=0; i<dl.size(); i++) {
                        mix_l.push_back(dl[i] + rl[i] * 0.5f);
                        mix_r.push_back(dr[i] + rr[i] * 0.5f);
                    }
                } else {
                    mix_l.insert(mix_l.end(), dl.begin(), dl.end());
                    mix_r.insert(mix_r.end(), dr.begin(), dr.end());
                }
            }
        }

        if (ev.type == "note") {
            if (ev.cmd == 0x90 && ev.vel > 0) spu.note_on(ev.ch, ev.note, ev.vel);
            else spu.note_off(ev.ch, ev.note);
        }
        else if (ev.type == "prog") spu.program_change(ev.ch, ev.val);
        else if (ev.type == "pitch") spu.pitch_bend(ev.ch, ev.val);
        else if (ev.type == "cc") spu.control_change(ev.ch, ev.cc_val, ev.val);
        else if (ev.type == "tempo") current_bpm = (float)ev.val;

        event_idx++;
    }

    if (progressCallback) progressCallback((int)total_events, (int)total_events);

    int tail_samples = 44100 * 2;
    std::vector<float> dl, dr, wl, wr;
    spu.render(tail_samples, dl, dr, wl, wr, 44100.0f);

    if(useReverb) {
        std::vector<float> rl, rr;
        spu.reverb.process(wl, wr, rl, rr);
        for(size_t i=0; i<dl.size(); i++) {
            mix_l.push_back(dl[i] + rl[i] * 0.5f);
            mix_r.push_back(dr[i] + rr[i] * 0.5f);
        }
    } else {
        mix_l.insert(mix_l.end(), dl.begin(), dl.end());
        mix_r.insert(mix_r.end(), dr.begin(), dr.end());
    }

    std::ofstream f(wavPath, std::ios::binary);
    if (!f.is_open()) return false;

    WavHeader h;
    h.data_size = (u32)mix_l.size() * 4;
    h.overall_size = h.data_size + 36;
    f.write((char*)&h, sizeof(h));

    std::vector<s16> pcm;
    pcm.reserve(mix_l.size() * 2);

    for(size_t i=0; i<mix_l.size(); i++) {
        float l = std::clamp(mix_l[i], -1.0f, 1.0f);
        float r = std::clamp(mix_r[i], -1.0f, 1.0f);

        pcm.push_back((s16)(l * 32767.0f));
        pcm.push_back((s16)(r * 32767.0f));
    }

    f.write((char*)pcm.data(), pcm.size() * 2);
    return true;
}
