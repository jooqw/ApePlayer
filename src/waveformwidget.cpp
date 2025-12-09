#include "waveformwidget.h"
#include <QPainter>

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent) {
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumHeight(100);
}

void WaveformWidget::setData(const std::vector<int16_t>& pcmData, bool looping, int loopStart, int loopEnd) {
    m_data = pcmData;
    m_loop = looping;
    m_ls = loopStart;
    m_le = loopEnd;
    update();
}

void WaveformWidget::clear() {
    m_data.clear();
    m_loop = false;
    m_ls = 0;
    m_le = 0;
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

    if (m_loop && m_le > m_ls && m_ls >= 0 && m_le <= (int)m_data.size()) {
        painter.setPen(QColor(255, 200, 0));
        double inv = (step >= 1.0) ? step : 1.0;
        int x0 = (int)(m_ls / inv);
        int x1 = (int)(m_le / inv);
        if (x0 < 0) x0 = 0; if (x0 >= w) x0 = w - 1;
        if (x1 < 0) x1 = 0; if (x1 >= w) x1 = w - 1;
        painter.drawLine(x0, 0, x0, h);
        painter.drawLine(x1, 0, x1, h);
    }
}
