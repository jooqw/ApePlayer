#include "mid.h"
#include <fstream>
#include <cstring>
#include <algorithm>

static void write_varlen_fp(FILE* fp, uint32_t value) {
    uint32_t buffer = value & 0x7F;
    while ((value >>= 7)) {
        buffer <<= 8;
        buffer |= ((value & 0x7F) | 0x80);
    }
    while (true) {
        fputc(buffer & 0xFF, fp);
        if (buffer & 0x80) buffer >>= 8;
        else break;
    }
}

bool SaveSQToMidi(const std::vector<u8>& data, const std::string& filename) {
    if (data.size() < 0x110) return false;
    
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) return false;
    
    // Write MThd header
    fputc(0x4D, fp); fputc(0x54, fp); fputc(0x68, fp); fputc(0x64, fp); // "MThd"
    fputc(0x00, fp); fputc(0x00, fp); fputc(0x00, fp); fputc(0x06, fp); // Header size = 6
    fputc(0x00, fp); fputc(0x00, fp); // Format 0
    fputc(0x00, fp); fputc(0x01, fp); // 1 track
    fputc(data[3], fp); fputc(data[2], fp); // Delta time (division)
    
    // Write MTrk header
    fputc(0x4D, fp); fputc(0x54, fp); fputc(0x72, fp); fputc(0x6B, fp); // "MTrk"
    
    // Track size placeholder (will fill in later)
    long trackSizePos = ftell(fp);
    fputc(0x00, fp); fputc(0x00, fp); fputc(0x00, fp); fputc(0x00, fp);
    
    // Initial tempo event
    fputc(0x04, fp);
    fputc(0xFF, fp);
    fputc(0x51, fp);
    fputc(0x03, fp);
    u16 tempo = Util::readU16(data, 4);
    int val = tempo > 0 ? (60000000 / tempo) : 500000;
    fputc((val >> 16) & 0xFF, fp);
    fputc((val >> 8) & 0xFF, fp);
    fputc(val & 0xFF, fp);
    
    // Time signature
    fputc(0x08, fp);
    fputc(0xFF, fp);
    fputc(0x58, fp);
    fputc(0x04, fp);
    fputc(0x04, fp);
    fputc(0x02, fp);
    fputc(0x18, fp);
    fputc(0x08, fp);

    // Write data, SQ format is almost identical to MIDI, main difference is pitch bend (1 byte vs 2 bytes)
    fputc(0x00, fp);
    
    size_t cursor = 0x110;
    u8 runningStatus = 0;
    
    while (cursor < data.size()) {
        // Delta
        while (cursor < data.size()) {
            u8 byte = data[cursor++];
            fputc(byte, fp);
            if (!(byte & 0x80)) break;
        }
        
        if (cursor >= data.size()) break;
        
        u8 currentByte = data[cursor];
        bool useRunningStatus = false;
        u8 statusByte;
        
        if (currentByte >= 0x80 && currentByte <= 0xEF) {
            statusByte = currentByte;
            runningStatus = statusByte;
            cursor++;
            fputc(statusByte, fp);
        } else if (currentByte >= 0xF0) {
            statusByte = currentByte;
            runningStatus = 0;
            cursor++;
            fputc(statusByte, fp);
        } else {
            statusByte = runningStatus;
            useRunningStatus = true;
        }
        
        u8 cmd = statusByte & 0xF0;

        switch (cmd) {
            case 0x80:  // Note Off
            case 0x90:  // Note On
            case 0xA0:  // Aftertouch
            case 0xB0:  // Control Change
                // 2 data bytes
                fputc(data[cursor++], fp);
                fputc(data[cursor++], fp);
                break;
                
            case 0xC0:  // Program Change
            case 0xD0:  // Channel Pressure
                // 1 data byte
                fputc(data[cursor++], fp);
                break;
                
            case 0xE0: { // Pitch Bend - SQ uses 1 byte (0-127, center=64), MIDI needs 2 bytes (14-bit, center=8192)
                u8 sqValue = data[cursor++];
                int midiValue = (sqValue * 16383) / 127;
                u8 lsb = midiValue & 0x7F;
                u8 msb = (midiValue >> 7) & 0x7F;
                fputc(lsb, fp);
                fputc(msb, fp);
                break;
            }
                
            case 0xF0: { // System/Meta
                if (statusByte == 0xFF) {
                    // Meta event
                    u8 metaType = data[cursor++];
                    fputc(metaType, fp);
                    
                    if (metaType == 0x2F) {
                        // End of track
                        u8 len = data[cursor++];
                        fputc(len, fp);
                        goto end_track;
                    } else if (metaType == 0x51) {
                        // Tempo - convert from BPM to microseconds per quarter
                        u8 len = data[cursor++];
                        fputc(0x03, fp);  // Always 3 bytes for tempo
                        if (len == 1 && cursor < data.size()) {
                            u8 bpm = data[cursor++];
                            int tempoval = bpm > 0 ? (60000000 / bpm) : 500000;
                            fputc((tempoval >> 16) & 0xFF, fp);
                            fputc((tempoval >> 8) & 0xFF, fp);
                            fputc(tempoval & 0xFF, fp);
                        } else {
                            // Already in correct format, copy as-is
                            for (int i = 0; i < len && cursor < data.size(); i++) {
                                fputc(data[cursor++], fp);
                            }
                        }
                    } else {
                        // Other meta events
                        u8 len = data[cursor++];
                        fputc(len, fp);
                        for (int i = 0; i < len && cursor < data.size(); i++) {
                            fputc(data[cursor++], fp);
                        }
                    }
                } else if (statusByte == 0xF0 || statusByte == 0xF7) {
                    // SysEx - copy length and data
                    auto res = Util::read_varlen(data, cursor);
                    int len = res.first;
                    cursor = res.second;
                    write_varlen_fp(fp, len);
                    for (int i = 0; i < len && cursor < data.size(); i++) {
                        fputc(data[cursor++], fp);
                    }
                }
                break;
            }
                
            default:
                break;
        }
    }
    
end_track:
    // write track size
    long endPos = ftell(fp);
    u32 trackSize = endPos - trackSizePos - 4;
    
    fseek(fp, trackSizePos, SEEK_SET);
    fputc((trackSize >> 24) & 0xFF, fp);
    fputc((trackSize >> 16) & 0xFF, fp);
    fputc((trackSize >> 8) & 0xFF, fp);
    fputc(trackSize & 0xFF, fp);
    
    fclose(fp);
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
    events.clear(); 
    u16 num_trks_local = Util::readU16BE(data, 10); 
    ticks_per_quarter = Util::readU16BE(data, 12);
    if(ticks_per_quarter & 0x8000) ticks_per_quarter = 480;
    std::vector<AbsEvent> all; size_t cursor = 14;

    for(int t=0; t<num_trks_local; t++) {
        if(cursor+8 > data.size()) break; if(std::memcmp(data.data()+cursor, "MTrk", 4)!=0) break;
        u32 len = Util::readU32BE(data, cursor+4); cursor += 8; size_t end = cursor + len;
        u32 cur_time = 0; u8 running = 0;
        while(cursor < end && cursor < data.size()) {
            auto res = Util::read_varlen(data, cursor); cur_time += res.first; cursor = res.second; if(cursor>=data.size()) break;
            u8 st = data[cursor]; if(st >= 0x80) { cursor++; if(st < 0xF0) running = st; } else st = running;
            if(st == 0xFF) {
                u8 type = data[cursor++]; auto lenRes = Util::read_varlen(data, cursor); size_t mlen = lenRes.first; cursor = lenRes.second;
                if(type == 0x51 && mlen==3) {
                    u32 mpqn = (data[cursor]<<16)|(data[cursor+1]<<8)|data[cursor+2];
                    SQEvent e; e.type="tempo"; e.val = (int)(60000000.0/mpqn); all.push_back({cur_time, e});
                } cursor += mlen;
            } else if(st == 0xF0 || st == 0xF7) { auto l = Util::read_varlen(data, cursor); cursor = l.first + l.second; }
            else {
                SQEvent e; e.cmd = st&0xF0; e.ch = st&0x0F;
                if(e.cmd == 0x90) { e.type="note"; e.note=data[cursor++]; e.vel=data[cursor++]; if(e.vel==0) e.cmd=0x80; }
                else if(e.cmd == 0x80) { e.type="note"; e.note=data[cursor++]; e.vel=0; cursor++; }
                else if(e.cmd == 0xB0) { e.type="cc"; e.cc_val=data[cursor++]; e.val=data[cursor++]; }
                else if(e.cmd == 0xC0) { e.type="prog"; e.val=data[cursor++]; }
                else if(e.cmd == 0xE0) { 
                    // MIDI pitch bend is 14-bit (LSB + MSB), convert to 0-127 range for internal use
                    u8 lsb = data[cursor++]; 
                    u8 msb = data[cursor++]; 
                    int midiValue = lsb | (msb << 7);
                    e.type="pitch"; 
                    e.val = (midiValue * 127) / 16383;  // Convert back to 0-127 range
                } else cursor++;
                all.push_back({cur_time, e});
            }
        }
        cursor = end;
    }
    std::stable_sort(all.begin(), all.end());
    u32 prev = 0; for(const auto& ae : all) { SQEvent e = ae.ev; e.delta = ae.abs_time - prev; events.push_back(e); prev = ae.abs_time; }
    events.push_back({0, "loop_end", 0,0,0,0});
}
