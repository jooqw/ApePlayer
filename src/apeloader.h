#ifndef APELOADER_H
#define APELOADER_H

#include <QString>
#include <vector>
#include "common.h"

struct LoadedInstrument {
    std::vector<ApeInstrumentPart> parts;
};

class ApeLoader {
public:
    void clear();
    bool loadFiles(const QString& hdPath, const QString& explicitBdPath = QString());
    std::vector<uint8_t> extractVagSample(uint16_t offsetIndex);
    const std::vector<LoadedInstrument>& getInstruments() const { return m_instruments; }

private:
    QString m_bdPath;
    std::vector<LoadedInstrument> m_instruments;
};

#endif // APELOADER_H
