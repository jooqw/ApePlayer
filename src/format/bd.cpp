#include "bd.h"
#include <fstream>
#include <cstring>

bool BDParser::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    data.resize(file.tellg());
    file.seekg(0); file.read((char*)data.data(), data.size());
    return true;
}

std::vector<u8> BDParser::get_adpcm_block(u32 start_offset) {
    std::vector<u8> raw_blocks;
    size_t cursor = start_offset;
    if (cursor >= data.size()) return {};
    while (true) {
        if (cursor + 16 > data.size()) break;
        size_t current_size = raw_blocks.size();
        raw_blocks.resize(current_size + 16);
        std::memcpy(raw_blocks.data() + current_size, data.data() + cursor, 16);
        u8 flags = data[cursor + 1];
        cursor += 16;
        if (flags & 1) break;
        if (raw_blocks.size() > 1024 * 1024) break;
    }
    return raw_blocks;
}
