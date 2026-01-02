#ifndef VIBRATO_H
#define VIBRATO_H

#include "../common.h"
#include <vector>

class VibratoEngine {
public:
    std::vector<u8> lfo_table;
    std::vector<u8> depth_table;
    double phase = 0.0;
    double depth_phase = 0.0;
    bool active = false; float depth = 0.0f;

    void init(const std::vector<u8>& wave_data,
              const std::vector<u8>& depth_data,
              u8 start_phase = 0,
              u8 start_depth_phase = 0);
    void tick(double rate_step, double depth_rate_step);
    float get_pitch_offset() const;
};

#endif // VIBRATO_H
