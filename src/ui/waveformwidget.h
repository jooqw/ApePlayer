#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H
#include <QWidget>
#include <vector>
#include <cstdint>

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void setData(const std::vector<int16_t>& pcmData, bool looping = false, int loopStart = 0, int loopEnd = 0);
    void clear();
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    std::vector<int16_t> m_data;
    bool m_loop = false;
    int m_ls = 0;
    int m_le = 0;
};
#endif // WAVEFORMWIDGET_H
