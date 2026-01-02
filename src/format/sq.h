#ifndef SQ_H
#define SQ_H

#include "../common.h"
#include <vector>
#include <string>
#include <map>

struct SQEvent { 
    int delta; 
    std::string type; 
    int cmd, ch, note, vel, val, cc_val; 
    
    bool operator<(const SQEvent& o) const { return false; } // Dummy
};

class SeqInterface {
public:
    std::vector<SQEvent> events;
    std::map<int, SQChannelInit> channel_inits;
    float tempo_bpm = 120.0f;
    int ticks_per_quarter = 480;
    virtual bool load(const std::string& filename) = 0;
    virtual ~SeqInterface() = default;
};

class SQParser : public SeqInterface {
    std::vector<u8> data;
public:
    bool load(const std::string& filename) override;
    const std::vector<u8>& getData() const { return data; }
private:
    void parse_events();
};

#endif // SQ_H
