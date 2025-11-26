#ifndef VAGDECODER_H
#define VAGDECODER_H

#include "common.h"
#include <vector>

class VagDecoder {
public:
    static VagResult decode(const std::vector<uint8_t>& vagData);
};

#endif // VAGDECODER_H
