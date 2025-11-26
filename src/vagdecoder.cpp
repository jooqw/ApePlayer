#include "vagdecoder.h"
#include <cmath>
#include <algorithm>

static const double VagLut[5][2] = {
    {0.0, 0.0},
    {60.0 / 64.0, 0.0},
    {115.0 / 64.0, -52.0 / 64.0},
    {98.0 / 64.0, -55.0 / 64.0},
    {122.0 / 64.0, -60.0 / 64.0}
};

VagResult VagDecoder::decode(const std::vector<uint8_t>& vagData) {
    VagResult result;
    double s_1 = 0.0;
    double s_2 = 0.0;
    int currentSample = 0;

    // Check for VAGp header, adding this for sefaty measures
    // don't know if other Ape games use this header.
    size_t startOffset = 0;
    if (vagData.size() >= 4 && vagData[0] == 'V' && vagData[1] == 'A') startOffset = 64;

    for (size_t i = startOffset; i < vagData.size(); i += 16) {
        if (i + 16 > vagData.size()) break;

        uint8_t predict_shift = vagData[i];
        uint8_t flags = vagData[i + 1];

        int shift = predict_shift & 0x0F;
        int predict = (predict_shift & 0xF0) >> 4;

        // Check Loop Start (Bit 2 / 0x04)
        if (flags & 4) {
            if (result.loopStartSample == 0)
                result.loopStartSample = currentSample;
        }

        // Check End/Stop (Bit 0 / 0x01)
        // We check this BEFORE decoding samples?

        bool isEndBlock = (flags & 1);
        bool isLooping  = (flags & 2);

        // Decode the 28 samples in this block
        for (int k = 0; k < 14; ++k) {
            uint8_t d = vagData[i + 2 + k];
            int nibbles[2] = { d & 0x0F, (d >> 4) & 0x0F };

            for (int j = 0; j < 2; ++j) {
                int s = nibbles[j] << 12;
                if (s & 0x8000) s = (int)(s | 0xFFFF0000);

                int safePredict = std::clamp(predict, 0, 4);
                double sample = (s >> shift) + (s_1 * VagLut[safePredict][0]) + (s_2 * VagLut[safePredict][1]);
                s_2 = s_1;
                s_1 = sample;

                int finalS = std::clamp((int)std::round(sample), -32768, 32767);
                result.pcm.push_back((int16_t)finalS);
            }
        }

        currentSample += 28;

        // Stop / Loop End
        if (isEndBlock) {
            if (isLooping) {
                result.loopEnabled = true;
                result.loopEndSample = currentSample;
            } else {
                result.loopEnabled = false;
                result.loopEndSample = currentSample;
            }
            break;
        }
    }

    // If we ran out of data without a flag
    if (result.loopEndSample == 0) {
        result.loopEndSample = result.pcm.size();
    }

    // If looped but start wasn't set, SPU loops to 0 i think?
    if (result.loopEnabled && result.loopStartSample == -1) {
        result.loopStartSample = 0;
    }

    return result;
}
