#include "vibrato.h"
#include <array>
#include <cmath>
#include <algorithm>

static const std::array<u8, 256> kVibratoSineTable = []() {
    std::array<u8, 256> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        double s = std::sin((double)i * 2.0 * M_PI / 256.0);
        int v = (int)std::round(127.5 + 127.5 * s);
        table[i] = (u8)std::clamp(v, 0, 255);
    }
    return table;
}();

void VibratoEngine::init(const std::vector<u8>& wave_data,
                         const std::vector<u8>& depth_data,
                         u8 start_phase,
                         u8 start_depth_phase) {
    // PS1 driver uses a sine table, allows override but default to sine
    lfo_table.clear();
    if (!wave_data.empty()) lfo_table = wave_data;
    else lfo_table.assign(kVibratoSineTable.begin(), kVibratoSineTable.end());

    // Breath apply and smooth and wrap to avoid edge clicks
    depth_table.clear();
    if (!depth_data.empty()) {
        depth_table = depth_data;
        if (depth_table.size() > 3) {
            std::vector<u8> smoothed(depth_table.size());
            size_t n = depth_table.size();
            for (size_t i = 0; i < n; ++i) {
                int prev = depth_table[(i + n - 1) % n];
                int cur  = depth_table[i];
                int next = depth_table[(i + 1) % n];
                int avg = (prev + cur + next) / 3;
                smoothed[i] = (u8)std::clamp(avg, 0, 255);
            }
            depth_table.swap(smoothed);
        }
        if (depth_table.size() >= 2) depth_table.back() = depth_table.front();
    }

    active = !lfo_table.empty();
    if (active) {
        double max_sz = (double)lfo_table.size();
        phase = std::fmod((double)start_phase, max_sz);
        if (phase < 0.0) phase += max_sz;
    } else phase = 0.0;

    if (!depth_table.empty()) {
        double max_sz = (double)depth_table.size();
        depth_phase = std::fmod((double)start_depth_phase, max_sz);
        if (depth_phase < 0.0) depth_phase += max_sz;
    } else depth_phase = 0.0;
}

void VibratoEngine::tick(double rate_step, double depth_rate_step) {
    if (!active || lfo_table.empty()) return;
    phase += rate_step;
    double max_sz = (double)lfo_table.size();
    if (phase >= max_sz) phase = std::fmod(phase, max_sz);
    if (phase < 0.0) { phase = std::fmod(phase, max_sz); if (phase < 0.0) phase += max_sz; }

    if (!depth_table.empty()) {
        depth_phase += depth_rate_step;
        double depth_sz = (double)depth_table.size();
        if (depth_phase >= depth_sz) depth_phase = std::fmod(depth_phase, depth_sz);
        if (depth_phase < 0.0) { depth_phase = std::fmod(depth_phase, depth_sz); if (depth_phase < 0.0) depth_phase += depth_sz; }
    }
}

float VibratoEngine::get_pitch_offset() const {
    if (!active || lfo_table.empty()) return 0.0f;

    auto sample_table = [](const std::vector<u8>& tbl, double ph) -> double {
        size_t sz = tbl.size();
        int idx0 = (int)ph;
        double frac = ph - idx0;
        idx0 = idx0 % sz; if (idx0 < 0) idx0 += sz;
        int idx1 = (idx0 + 1) % sz;
        double val0 = (double)tbl[idx0]; double val1 = (double)tbl[idx1];
        double interp = val0 + (val1 - val0) * frac;
        uint32_t v = (uint32_t)std::round(interp); return (double)std::min(v, 255U);
    };

    double lfo_val = sample_table(lfo_table, phase);
    double center_offset = (lfo_val / 255.0) - 0.5; // [-0.5, +0.5]

    double depth_scale = 1.0;
    if (!depth_table.empty()) {
        double dval = sample_table(depth_table, depth_phase);
        depth_scale = dval / 255.0; // [0,1]
    }

    return (float)(center_offset * 2.0 * depth * depth_scale);
}
