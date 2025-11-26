#include "apeloader.h"
#include <QFile>
#include <QDataStream>
#include <QFileInfo>

void ApeLoader::clear() {
    m_instruments.clear();
    m_bdPath.clear();
}

bool ApeLoader::loadFiles(const QString& hdPath, const QString& explicitBdPath) {
    clear();

    if (!explicitBdPath.isEmpty()) {
        m_bdPath = explicitBdPath;
    } else {
        QFileInfo fi(hdPath);
        QString base = fi.absolutePath() + "/" + fi.completeBaseName();
        if (QFile::exists(base + ".bd")) m_bdPath = base + ".bd";
        else if (QFile::exists(base + ".BD")) m_bdPath = base + ".BD";
        else return false;
    }

    QFile hdFile(hdPath);
    if (!hdFile.open(QIODevice::ReadOnly)) return false;

    QDataStream in(&hdFile);
    in.setByteOrder(QDataStream::LittleEndian);

    char magic[4];
    // HDsize, BDsize, Padding
    hdFile.seek(12);
    in.readRawData(magic, 4);
    if (strncmp(magic, "SShd", 4) != 0) return false;

    quint32 ptr_inst, data_size;
    in >> ptr_inst >> data_size;

    hdFile.seek(ptr_inst);
    quint16 numpatches;
    in >> numpatches;

    hdFile.seek(ptr_inst + 4);
    std::vector<quint16> ptrList(numpatches);
    for(int i=0; i<numpatches; ++i) in >> ptrList[i];

    auto readPatch = [&](quint64 offset) {
        hdFile.seek(offset);
        ApePatchHeader ph;
        in.readRawData((char*)&ph, sizeof(ph));
        LoadedInstrument inst;
        int count = (ph.unk1 % 128) + 1;
        for(int i=0; i<count; ++i) {
            ApeInstrumentPart part;
            in.readRawData((char*)&part, sizeof(part));
            inst.parts.push_back(part);
        }
        return inst;
    };

    m_instruments.push_back(readPatch(hdFile.pos()));
    for(auto ptr : ptrList) m_instruments.push_back(readPatch(ptr_inst + ptr));

    return true;
}

std::vector<uint8_t> ApeLoader::extractVagSample(uint16_t offsetIndex) {
    if (m_bdPath.isEmpty()) return {};
    QFile bdFile(m_bdPath);
    if (!bdFile.open(QIODevice::ReadOnly)) return {};

    qint64 pos = (qint64)(offsetIndex + 2) * 8;
    if (!bdFile.seek(pos)) return {};

    std::vector<uint8_t> buffer;
    while (true) {
        QByteArray chunk = bdFile.read(16);
        if (chunk.size() < 16) break;
        bool allZero = true;
        for (char c : chunk) if (c != 0) allZero = false;
        if (allZero) break;
        buffer.insert(buffer.end(), chunk.begin(), chunk.end());
    }
    return buffer;
}
