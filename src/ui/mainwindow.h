#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include "../engine/audio.h"
#include "../format/hd.h"
#include "../format/bd.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QTreeWidgetItem;
QT_END_NAMESPACE

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
    void onSeq2Midi();

    void onPlayClicked();
    void onStopClicked();
    void onTreeSelectionChanged();
    void onLoopToggled(bool checked);

    void onRenderWav();
    void onAbout();
    void onAboutQt();

private:
    void fillTree();
    void addPropRow(const QString& name, const QString& value);
    void updatePropertyView(int pid, int tid);
    void playSample(int pid, int tid);
    void log(const QString& msg);

    Ui::MainWindow *ui;
    std::unique_ptr<HDParser> m_hd;
    std::unique_ptr<BDParser> m_bd;
    std::unique_ptr<AudioEngine> m_audio;
    WaveformWidget* m_waveform;
};

#endif // MAINWINDOW_H
