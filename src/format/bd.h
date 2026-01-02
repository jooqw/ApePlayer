#ifndef BD_H
#define BD_H

#include "../common.h"
#include <string>
#include <vector>

class BDParser {
public:
    std::vector<u8> data;
    bool load(const std::string& filename);
    std::vector<u8> get_adpcm_block(u32 start_offset);
};

#endif // BD_H
