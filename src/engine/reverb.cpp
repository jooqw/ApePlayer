#include "reverb.h"
#include <cstring>

ReverbEngine::ReverbEngine() { 
    ram.resize(256 * 1024, 0); 
    std::memset(&regs, 0, sizeof(ReverbRegs)); 
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
