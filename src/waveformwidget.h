#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H
#include <QWidget>
#include <vector>
#include <cstdint>

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void setData(const std::vector<int16_t>& pcmData);
    void clear();
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    std::vector<int16_t> m_data;
};
#endif // WAVEFORMWIDGET_H
