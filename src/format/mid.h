#ifndef MID_H
#define MID_H

#include "sq.h"
#include <vector>
#include <string>

class MidiParser : public SeqInterface {
    std::vector<u8> data;
public:
    bool load(const std::string& filename) override;
private:
    void parse_midi();
};

bool SaveSQToMidi(const std::vector<u8>& sqData, const std::string& filename);

#endif // MID_H
