#ifndef SF2EXPORTER_H
#define SF2EXPORTER_H

#include <QString>
class HDParser;
class BDParser;

class Sf2Exporter {
public:
    static bool exportToSf2(const QString& path, HDParser* hd, BDParser* bd);
};

#endif // SF2EXPORTER_H
