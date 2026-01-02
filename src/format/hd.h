#ifndef HD_H
#define HD_H

#include "../common.h"
#include <vector>
#include <memory>
#include <string>

class HDParser {
public:
    std::vector<std::shared_ptr<Program>> programs;
    std::vector<std::vector<u8>> breath_scripts;

    bool load(const std::string& filename);
    void clear();
    void print_debug_info() const;

private:
    std::vector<u8> data;
    void parse();
    void parse_programs(u32 base_offset);
    void parse_breath_waves(u32 base_offset);
};

#endif // HD_H
