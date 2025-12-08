#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QIODevice>
#include <QByteArray>
#include <memory>
#include "engine.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QAudioSink;
class WaveformWidget;

class RawAudioSource : public QIODevice {
    Q_OBJECT
public:
    RawAudioSource(const std::vector<int16_t>& data, QObject* parent);

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return 0; }
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }

    void setLooping(bool loop, int start, int end) {
        m_loop = loop;
        m_ls = (qint64)start * 2;
        m_le = (qint64)end * 2;
    }

private:
    QByteArray m_buffer;
    qint64 m_pos;
    bool m_loop;
    qint64 m_ls;
    qint64 m_le;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onOpenHd();
    void onCloseFile();
    void onExportSf2();
    void onBulkExport();
    void onSeq2Midi();

    void onPlayClicked();
    void onStopClicked();
    void onTableSelectionChanged();
    void onLoopToggled(bool checked);

    void onRenderWav();

private:
    void stopAudio();
    void playSample(int progIdx, int toneIdx);
    void fillTable();
    void resetUi();

    void log(const QString& msg);

    void logProgramDetails(int progId);

    Ui::MainWindow *ui;
    std::unique_ptr<HDParser> m_hd;
    std::unique_ptr<BDParser> m_bd;
    WaveformWidget* m_waveform;
    QAudioSink* m_sink = nullptr;
    RawAudioSource* m_source = nullptr;
};

#endif // MAINWINDOW_H
