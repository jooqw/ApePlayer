#include "vagaudiosource.h"
#include <cstring>
#include <algorithm>

VagAudioSource::VagAudioSource(const std::vector<int16_t>& pcmData,
                               int loopStartSample,
                               int loopEndSample,
                               QObject *parent)
    : QIODevice(parent), m_pos(0), m_looping(false)
{
    m_data = QByteArray((const char*)pcmData.data(), pcmData.size() * 2);

    // Stereo = 4 bytes per sample
    m_loopStartByte = (qint64)loopStartSample * 4;
    m_loopEndByte = (qint64)loopEndSample * 4;

    if (m_loopStartByte < 0) m_loopStartByte = 0;
    if (m_loopEndByte > m_data.size()) m_loopEndByte = m_data.size();

    // Ensure End is after Start
    if (m_loopEndByte <= m_loopStartByte) m_loopEndByte = m_data.size();
}

void VagAudioSource::setLooping(bool enable) {
    m_looping = enable;
}

qint64 VagAudioSource::readData(char *data, qint64 maxlen) {
    qint64 totalRead = 0;
    maxlen = maxlen & ~3; // Align to 4 bytes

    while (totalRead < maxlen) {
        // Where do we stop?
        // If looping is enabled on UI AND the file supports it -> Loop End
        // Otherwise -> End of Data
        qint64 targetEnd = m_looping ? m_loopEndByte : m_data.size();

        qint64 remaining = targetEnd - m_pos;

        if (remaining <= 0) {
            if (m_looping) {
                // Loop back
                m_pos = m_loopStartByte;
                continue;
            } else {
                // Stop playback
                break;
            }
        }

        qint64 chunk = std::min(maxlen - totalRead, remaining);
        memcpy(data + totalRead, m_data.constData() + m_pos, chunk);
        m_pos += chunk;
        totalRead += chunk;
    }
    return totalRead;
}

qint64 VagAudioSource::bytesAvailable() const {
    if (m_looping) return m_data.size(); // Infinite
    return m_data.size() - m_pos;
}
