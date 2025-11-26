#ifndef SF2EXPORTER_H
#define SF2EXPORTER_H
#include <QString>
class ApeLoader;
class Sf2Exporter {
public:
    static bool exportToSf2(const QString& path, ApeLoader* loader);
};
#endif // SF2EXPORTER_H
