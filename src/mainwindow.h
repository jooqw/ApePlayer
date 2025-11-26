#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include <QByteArray>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class ApeLoader;
class QAudioSink;
class VagAudioSource;
class WaveformWidget;

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

    void onPlayClicked();
    void onStopClicked();
    void onTableSelectionChanged();
    void onPanChanged(int val);
    void onLoopToggled(bool checked);

private:
    void stopAudio();
    void playSample(int i, int j);
    void previewSample(int i, int j);
    void fillTable();
    void resetUi();

    void updateDetails(int i, int j);

    Ui::MainWindow *ui;
    std::unique_ptr<ApeLoader> m_loader;
    WaveformWidget* m_waveform;
    QAudioSink* m_sink = nullptr;
    VagAudioSource* m_source = nullptr;
};

#endif // MAINWINDOW_H
