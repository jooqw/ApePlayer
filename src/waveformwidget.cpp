#include "waveformwidget.h"
#include <QPainter>

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent) {
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumHeight(100);
}

void WaveformWidget::setData(const std::vector<int16_t>& pcmData) {
    m_data = pcmData;
    update();
}

void WaveformWidget::clear() {
    m_data.clear();
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_data.empty()) return;
    painter.setPen(QColor(0, 255, 127));

    int w = width();
    int h = height();
    int cy = h / 2;
    double step = (double)m_data.size() / w;
    if (step < 1.0) step = 1.0;

    int px = 0, py = cy;
    for (int x = 0; x < w; ++x) {
        int idx = (int)(x * step);
        if (idx >= m_data.size()) break;
        int y = cy - (int)((m_data[idx] / 32768.0) * (h / 2));
        painter.drawLine(px, py, x, y);
        px = x; py = y;
    }
}
