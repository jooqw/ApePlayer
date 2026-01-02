#include "renderwav.h"
#include "../engine/audio.h"
#include "../engine/adsr.h"
#include "../format/sq.h"
#include "../format/mid.h"
#include "../engine/vibrato.h"
#include "../engine/reverb.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>

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

struct SynthVoice {
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
    double vibrato_depth_rate_val = 0.0;

    bool noise_mode = false;
};

class SynthEngine {
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
        void reset_controllers() {
            vol = 127; expr = 127; pan = 64;
            pitch_bend_factor = 1.0;
            sustain_active = false; portamento_active = false;
            lfo_enabled = false; lfo_depth = 0.0f; modulation = 0;
        }
        double get_lfo_ratio(float sample_rate) {
            if (!lfo_enabled || lfo_depth <= 0.0001f) return 1.0;
            lfo_phase += (lfo_rate * 6.283185307f) / sample_rate;
            if (lfo_phase > 6.283185307f) lfo_phase -= 6.283185307f;
            float val = std::sin(lfo_phase) * lfo_depth * lfo_sensitivity;
            return std::pow(2.0, val / 12.0);
        }
    };

    ChannelState channels[16];
    ReverbEngine reverb;
    std::vector<SynthVoice> active_voices;
    std::map<u32, DecodedSample> sample_cache;

    BDParser* bd = nullptr;
    HDParser* hd = nullptr;

    SynthEngine() { reverb.init_studio_large(); }

    void set_data(BDParser* _bd, HDParser* _hd) { bd = _bd; hd = _hd; }

    void note_on(int ch_idx, int note, int vel) {
        if (!hd || !bd) return;
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

            SynthVoice v;
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

                const float max_vibrato_depth_semitones = 0.5f; // modest depth
                float depth_norm = ch.modulation / 127.0f;
                v.vibrato.depth = depth_norm * max_vibrato_depth_semitones;

                const std::vector<u8>* depth_wave = nullptr;
                if (breath_idx != 0xFF && breath_idx != 0x7F && breath_idx < hd->breath_scripts.size()) {
                    depth_wave = &hd->breath_scripts[breath_idx];
                }

                v.vibrato.init({}, depth_wave ? *depth_wave : std::vector<u8>{}, 0, 0);
                v.vibrato_enabled = v.vibrato.active && v.vibrato.depth > 0.0f;

                if (v.vibrato_enabled) {
                    float rate_factor = (ch.breath_rate > 0 ? ch.breath_rate : 64) / 127.0f;
                    double target_hz = 0.5 + (rate_factor * 9.5);

                    size_t wave_size = v.vibrato.lfo_table.empty() ? 256 : v.vibrato.lfo_table.size();
                    size_t depth_size = v.vibrato.depth_table.empty() ? wave_size : v.vibrato.depth_table.size();
                    v.vibrato_rate_val = (double)wave_size * target_hz / 44100.0;
                    v.vibrato_depth_rate_val = (double)depth_size * target_hz / 44100.0;
                }
            }

            v.tone_pan = Util::clamp_pan(target_tone->pan + (int)prog->master_pan - 64);
            v.base_vol_factor = (target_tone->vol / 127.0f) * (prog->master_vol / 127.0f) * (vel / 127.0f);
            v.ch = ch_idx; v.note_key = note; v.active = true; v.reverb_on = target_tone->is_reverb(); v.adsr = adsr;

            active_voices.push_back(v);
        }
    }

    void note_off(int ch_idx, int note) {
        for (auto& v : active_voices) {
            if (v.ch == ch_idx && v.note_key == note) {
                if (channels[ch_idx].sustain_active) v.release_pending = true;
                else v.adsr->KeyOff();
            }
        }
    }

    void program_change(int ch_idx, int prog_id) { channels[ch_idx].prog = prog_id; }
    void pitch_bend(int ch_idx, int val) {
        double mult = channels[ch_idx].pitch_mult;
        channels[ch_idx].pitch_bend_factor = std::pow(2.0, (((val - 64) / 64.0) * mult) / 12.0);
    }

    void control_change(int ch_idx, int cc, int val) {
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

    void render_block(int num_samples, std::vector<float>& dl, std::vector<float>& dr, std::vector<float>& wl, std::vector<float>& wr, float samples_per_tick) {
        dl.assign(num_samples, 0.0f); dr.assign(num_samples, 0.0f);
        wl.assign(num_samples, 0.0f); wr.assign(num_samples, 0.0f);

        active_voices.erase(std::remove_if(active_voices.begin(), active_voices.end(), [](const SynthVoice& v) { return !v.active; }), active_voices.end());

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
                    double depth_step = v.vibrato_depth_rate_val > 0.0 ? v.vibrato_depth_rate_val : v.vibrato_rate_val;
                    v.vibrato.tick(v.vibrato_rate_val, depth_step);
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
};

bool ExportSequenceToWav(const std::string& sqPath, const std::string& wavPath, HDParser* hd, BDParser* bd, bool useReverb, bool isMidi, std::function<void(int, int)> progressCallback) {
    std::shared_ptr<SeqInterface> seq;
    if (isMidi) seq = std::make_shared<MidiParser>();
    else seq = std::make_shared<SQParser>();

    if (!seq->load(sqPath)) return false;

    SynthEngine spu;
    spu.set_data(bd, hd);
    
    // Apply seq header
    for (const auto& [idx, init] : seq->channel_inits) {
        if (idx >= 16) continue;
        spu.channels[idx].prog = init.prog_idx;
        spu.channels[idx].vol = init.vol;
        spu.channels[idx].pan = init.pan;
        spu.channels[idx].modulation = init.modulation;
        spu.channels[idx].breath_rate = init.vibrato;
        spu.channels[idx].lfo_depth = init.modulation / 127.0f;
    }

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
                spu.render_block(num_samples, dl, dr, wl, wr, samples_per_tick);

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
    spu.render_block(tail_samples, dl, dr, wl, wr, 44100.0f);

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
