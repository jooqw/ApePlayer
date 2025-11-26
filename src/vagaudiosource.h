#ifndef VAGAUDIOSOURCE_H
#define VAGAUDIOSOURCE_H

#include <QIODevice>
#include <QByteArray>
#include <vector>

class VagAudioSource : public QIODevice {
    Q_OBJECT
public:
    explicit VagAudioSource(const std::vector<int16_t>& pcmData,
                            int loopStartSample,
                            int loopEndSample,
                            QObject *parent = nullptr);

    void setLooping(bool enable);

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override { return 0; }
    qint64 bytesAvailable() const override;

private:
    QByteArray m_data;
    qint64 m_pos;
    qint64 m_loopStartByte;
    qint64 m_loopEndByte;
    bool m_looping;
};

#endif // VAGAUDIOSOURCE_H
