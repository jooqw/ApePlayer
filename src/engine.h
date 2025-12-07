#ifndef ENGINE_H
#define ENGINE_H

#include "common.h"
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <functional>

class BDParser {
public:
    std::vector<u8> data;
    bool load(const std::string& filename);
    std::vector<u8> get_adpcm_block(u32 start_offset);
};

class HDParser {
public:
    std::vector<std::shared_ptr<Program>> programs;
    std::vector<std::vector<u8>> breath_scripts;

    bool load(const std::string& filename);
    void clear();
    void print_debug_info() const;

private:
    std::vector<u8> data;
    void parse();
    void parse_programs(u32 base_offset);
    void parse_breath_waves(u32 base_offset);
};


class VolumeEnvelope {
public:
    u32 counter = 0; u16 counter_increment = 0; s16 step = 0; u8 rate = 0;
    bool decreasing = false; bool exponential = false; bool phase_invert = false;
    void Reset(u8 rate, u8 rate_mask, bool decreasing, bool exponential, bool phase_invert);
    bool Tick(s16& current_level);
};

class HardwareADSR {
public:
    enum class Phase { Off, Attack, Decay, Sustain, Release };
    Phase phase = Phase::Off;
    VolumeEnvelope envelope;
    s16 current_volume = 0; s16 target_volume = 0; u32 reg_val = 0;

    HardwareADSR(u32 registers) { reg_val = registers; }
    void KeyOn();
    void KeyOff();
    s16 Tick();

    static int16_t simulate_timecents(u32 reg_val, Phase target_phase);

private:
    void UpdateEnvelope();
    u32 get_bits(u32 val, int bit, int count) const { return (val >> bit) & ((1 << count) - 1); }
};

struct ReverbRegs { s16 vLOUT, vROUT; u16 mBASE, dAPF1, dAPF2; s16 vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2; u16 mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2; u16 dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4; u16 dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2; s16 vLIN, vRIN; };

class ReverbEngine {
    std::vector<s16> ram; u32 current_addr = 0, base_addr = 0;
public:
    ReverbRegs regs;
    ReverbEngine();
    void init_studio_large();
    void process(const std::vector<float>& in_l, const std::vector<float>& in_r, std::vector<float>& out_l, std::vector<float>& out_r);
};

class VibratoEngine {
public:
    std::vector<u8> lfo_table; double phase = 0.0; bool active = false; float depth = 0.0f;
    void init(const std::vector<u8>& data);
    void tick(double rate_step);
    float get_pitch_offset() const;
};

struct Voice {
    DecodedSample data;
    double pos = 0.0;
    double base_pitch_mult = 1.0;
    double target_pitch_mult = 1.0;
    double portamento_step = 1.0;
    double note_base_freq = 1.0;
    bool sliding = false;
    float base_vol_factor = 0.0f;
    int tone_pan = 64;
    int ch = 0; int note_key = 0; bool active = false; bool reverb_on = false;
    std::shared_ptr<HardwareADSR> adsr; bool release_pending = false;

    VibratoEngine vibrato;
    bool vibrato_enabled = false;
    double vibrato_rate_val = 0.0;

    bool noise_mode = false;
};

class SPU {
public:
    struct ChannelState {
        int prog = 0; double pitch_bend_factor = 1.0; double pitch_mult = 12.0;
        int vol = 127, expr = 127, pan = 64, reverb_depth = 0;
        int attack_mod = 64, release_mod = 64;
        bool sustain_active = false; bool portamento_active = false; int portamento_time = 0;
        int rpn_msb = 127, rpn_lsb = 127, nrpn_msb = 127, nrpn_lsb = 127;
        int modulation = 0; int breath_rate = 0;
        bool lfo_enabled = false; float lfo_rate = 5.0f; float lfo_depth = 0.0f;
        float lfo_phase = 0.0f; float lfo_sensitivity = 0.0f; double last_note_pitch = -1.0;
        void reset_controllers();
        double get_lfo_ratio(float sample_rate);
    };

    ChannelState channels[16];
    ReverbEngine reverb;
    std::vector<Voice> active_voices;
    std::map<u32, DecodedSample> sample_cache;

    BDParser* bd;
    HDParser* hd;

    SPU(BDParser* _bd, HDParser* _hd);

    void note_on(int ch_idx, int note, int vel);
    void note_off(int ch_idx, int note);
    void program_change(int ch_idx, int prog_id);
    void pitch_bend(int ch_idx, int val);
    void control_change(int ch_idx, int cc, int val);
    void apply_seq_header(const std::map<int, SQChannelInit>& inits);

    void render(int num_samples, std::vector<float>& dl, std::vector<float>& dr, std::vector<float>& wl, std::vector<float>& wr, float samples_per_tick);
};

struct SQEvent { int delta; std::string type; int cmd, ch, note, vel, val, cc_val; };

class SeqInterface {
public:
    std::vector<SQEvent> events;
    std::map<int, SQChannelInit> channel_inits;
    float tempo_bpm = 120.0f;
    int ticks_per_quarter = 480;
    virtual bool load(const std::string& filename) = 0;
};

class SQParser : public SeqInterface {
    std::vector<u8> data;
public:
    bool load(const std::string& filename) override;
    bool saveToMidi(const std::string& filename);
private:
    void parse_events();
};

class MidiParser : public SeqInterface {
    std::vector<u8> data;
public:
    bool load(const std::string& filename) override;
private:
    void parse_midi();
};

class EngineUtils {
public:
    static DecodedSample decode_adpcm(const std::vector<u8>& adpcm_data);
    static int16_t ps2_vol_to_cb(u8 vol);
    static int16_t calculate_adsr_timecents(u32 reg_val, HardwareADSR::Phase phase);
};

bool ExportSequenceToWav(const std::string& sqPath, const std::string& wavPath, HDParser* hd, BDParser* bd, bool useReverb, bool isMidi, std::function<void(int current, int total)> progressCallback = nullptr);

#endif // ENGINE_H
