#ifndef REVERB_H
#define REVERB_H

#include "../common.h"
#include <vector>
#include <cstring>

struct ReverbRegs { 
    s16 vLOUT, vROUT; 
    u16 mBASE, dAPF1, dAPF2; 
    s16 vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2; 
    u16 mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2; 
    u16 dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4; 
    u16 dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2; 
    s16 vLIN, vRIN; 
};

class ReverbEngine {
    std::vector<s16> ram; 
    u32 current_addr = 0, base_addr = 0;
    static const int RAM_SIZE = 512 * 1024;
    static const int RAM_MASK = (RAM_SIZE - 1) / 2;
    
    inline s16 rev_read(const std::vector<s16>& ram, u32 current, u32 base, u32 rel_addr) {
        u32 offset = current + (rel_addr & RAM_MASK);
        if (offset >= RAM_MASK) offset -= (RAM_MASK - base);
        return ram[offset % (RAM_SIZE / 2)];
    }

public:
    ReverbRegs regs;
    ReverbEngine();
    void init_studio_large();
    void process(const std::vector<float>& in_l, const std::vector<float>& in_r, std::vector<float>& out_l, std::vector<float>& out_r);
};

#endif // REVERB_H
