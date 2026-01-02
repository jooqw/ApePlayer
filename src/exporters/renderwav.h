#ifndef RENDERWAV_H
#define RENDERWAV_H

#include <string>
#include <functional>
#include "../format/hd.h"
#include "../format/bd.h"

bool ExportSequenceToWav(const std::string& sqPath, const std::string& wavPath, HDParser* hd, BDParser* bd, bool useReverb, bool isMidi, std::function<void(int current, int total)> progressCallback = nullptr);

#endif // RENDERWAV_H
