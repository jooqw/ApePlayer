#include "hd.h"
#include <fstream>
#include <cstring>
#include <iostream>

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
