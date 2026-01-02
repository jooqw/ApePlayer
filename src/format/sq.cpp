#include "sq.h"
#include <fstream>
#include <iostream>

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
        auto res = Util::read_varlen(data, cursor); int delta = res.first; cursor = res.second;
        if (cursor >= data.size()) break;
        u8 byte = data[cursor]; int status;
        if (byte >= 0x80) { status = byte; cursor++; if (status < 0xF0) running_status = status; } else status = running_status;
        int cmd = status & 0xF0; int ch = status & 0x0F;
        if (cmd == 0x80 || cmd == 0x90) { int note = data[cursor++]; int vel = data[cursor++]; events.push_back({delta, "note", cmd, ch, note, vel}); }
        else if (cmd == 0xB0) { int cc = data[cursor++]; int val = data[cursor++]; events.push_back({delta, "cc", cmd, ch, 0, 0, val, cc}); }
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


