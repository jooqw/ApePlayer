#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "apeloader.h"
#include "vagdecoder.h"
#include "sf2exporter.h"
#include "vagaudiosource.h"
#include "waveformwidget.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QAudioSink>
#include <QMediaDevices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent), ui(new Ui::MainWindow), m_loader(std::make_unique<ApeLoader>())
{
    ui->setupUi(this);

    m_waveform = new WaveformWidget(this);
    ui->waveformLayout->addWidget(m_waveform);
    ui->tableWidget->setColumnWidth(0, 60);

    connect(ui->actionOpen_HD, &QAction::triggered, this, &MainWindow::onOpenHd);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::onCloseFile);

    connect(ui->actionExportSF2, &QAction::triggered, this, &MainWindow::onExportSf2);
    connect(ui->actionBulkExport, &QAction::triggered, this, &MainWindow::onBulkExport); // Bulk Connection

    connect(ui->btnPlay, &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(ui->btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(ui->chkLoop, &QCheckBox::toggled, this, &MainWindow::onLoopToggled);
    connect(ui->sliderPan, &QSlider::valueChanged, this, &MainWindow::onPanChanged);

    connect(ui->tableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onTableSelectionChanged);
    connect(ui->tableWidget, &QTableWidget::cellDoubleClicked, this, &MainWindow::onPlayClicked);
}

MainWindow::~MainWindow() {
    stopAudio();
    delete ui;
}

void MainWindow::onPanChanged(int val) {
    ui->lblPan->setText(QString("Pan: %1").arg(val));
}

void MainWindow::onOpenHd() {
    QString path = QFileDialog::getOpenFileName(this, "Open", "", "HD (*.hd)");
    if(path.isEmpty()) return;

    if(!m_loader->loadFiles(path)) {
        if(QMessageBox::question(this, "BD Missing", "Find BD manually?", QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
            QString bd = QFileDialog::getOpenFileName(this, "Open BD", "", "BD (*.bd)");
            if(!m_loader->loadFiles(path, bd)) return;
        } else return;
    }
    fillTable();
    ui->statusbar->showMessage("Loaded: " + QFileInfo(path).fileName());
}

void MainWindow::onCloseFile() {
    stopAudio();
    m_loader->clear();
    m_waveform->clear();
    resetUi();
    ui->statusbar->showMessage("Closed.");
}

void MainWindow::onExportSf2() {
    if(m_loader->getInstruments().empty()) {
        QMessageBox::warning(this, "Error", "Load a file first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Save SF2", "out.sf2", "SF2 (*.sf2)");
    if(path.isEmpty()) return;

    ui->statusbar->showMessage("Exporting...");
    QApplication::processEvents();

    if(Sf2Exporter::exportToSf2(path, m_loader.get())) {
        QMessageBox::information(this, "OK", "Exported successfully.");
        ui->statusbar->showMessage("Saved: " + path);
    } else {
        QMessageBox::critical(this, "Error", "Export Failed.");
    }
}

void MainWindow::onBulkExport() {
    QString inputPath = QFileDialog::getExistingDirectory(this, "Select Folder with sequenced data(.HD/.BD/.MID)");
    if (inputPath.isEmpty()) return;

    QString outputPath = QFileDialog::getExistingDirectory(this, "Select Output Folder for SF2s");
    if (outputPath.isEmpty()) return;

    QDir inDir(inputPath);
    QDir outDir(outputPath);

    QStringList filters;
    filters << "*.hd" << "*.HD";
    QFileInfoList hdFiles = inDir.entryInfoList(filters, QDir::Files);

    if (hdFiles.empty()) {
        QMessageBox::information(this, "Info", "No .HD files found in input directory.");
        return;
    }

    int successCount = 0;
    int errorCount = 0;
    int total = hdFiles.size();

    for (int i = 0; i < total; ++i) {
        QFileInfo hdInfo = hdFiles[i];
        QString baseName = hdInfo.completeBaseName();

        ui->statusbar->showMessage(QString("Bulk Exporting (%1/%2): %3...").arg(i+1).arg(total).arg(baseName));
        QApplication::processEvents();

        // Use a temporary loader to keep main UI state clean
        ApeLoader tempLoader;

        // Load HD
        if (!tempLoader.loadFiles(hdInfo.absoluteFilePath())) {
            qDebug() << "Skipping" << baseName << "- Failed to load HD/BD pair.";
            errorCount++;
            continue;
        }

        if (tempLoader.getInstruments().empty()) {
            errorCount++;
            continue;
        }

        // Export SF2
        QString sf2Path = outDir.filePath(baseName + ".sf2");
        if (!Sf2Exporter::exportToSf2(sf2Path, &tempLoader)) {
            qDebug() << "Failed to write SF2 for" << baseName;
            errorCount++;
            continue;
        }

        // Copy .mid file
        // some programs auto search for midi files with the same name
        // as the soundfont.
        QString midiSrcPath;
        QDirIterator it(inDir.absolutePath(), QStringList() << "*.mid" << "*.MID", QDir::Files);
        while (it.hasNext()) {
            it.next();
            if (it.fileInfo().completeBaseName().compare(baseName, Qt::CaseInsensitive) == 0) {
                midiSrcPath = it.filePath();
                break;
            }
        }

        if (!midiSrcPath.isEmpty()) {
            QString midiDestPath = outDir.filePath(baseName + ".mid");
            if (QFile::exists(midiDestPath)) QFile::remove(midiDestPath);
            QFile::copy(midiSrcPath, midiDestPath);
        }

        successCount++;
    }

    ui->statusbar->showMessage("Bulk Export Finished.");
    QMessageBox::information(this, "Done",
                             QString("Processed: %1/%2 files.\nSuccess: %3\nErrors/Skipped: %4")
                             .arg(successCount + errorCount).arg(total).arg(successCount).arg(errorCount));
}

void MainWindow::stopAudio() {
    if(m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink=nullptr;
    }
    if(m_source) {
        m_source->close();
        delete m_source;
        m_source=nullptr;
    }
}

void MainWindow::playSample(int i, int j) {
    stopAudio();

    uint16_t offset = m_loader->getInstruments()[i].parts[j].offset;
    auto vag = m_loader->extractVagSample(offset);
    if(vag.empty()) return;

    // Decode (Now stops correctly at flag)
    auto res = VagDecoder::decode(vag);

    // Stereo Expansion
    float pan = ui->sliderPan->value()/100.0f;
    float lG = (1.0f - pan)/2.0f + 0.5f;
    float rG = (pan + 1.0f)/2.0f;

    std::vector<int16_t> stereo;
    stereo.reserve(res.pcm.size()*2);
    for(auto s : res.pcm) {
        stereo.push_back((int16_t)(s * lG));
        stereo.push_back((int16_t)(s * rG));
    }

    m_source = new VagAudioSource(stereo, res.loopStartSample, res.loopEndSample, this);
    m_source->open(QIODevice::ReadOnly);

    ui->chkLoop->blockSignals(true);
    ui->chkLoop->setChecked(res.loopEnabled);
    ui->chkLoop->blockSignals(false);

    m_source->setLooping(res.loopEnabled);

    QAudioFormat fmt;
    fmt.setSampleRate(GEN_FREQ);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
    m_sink->start(m_source);
}

void MainWindow::previewSample(int i, int j) {
    const auto& p = m_loader->getInstruments()[i].parts[j];

    ui->le_vagOffset->setText(QString::number(p.offset));
    ui->le_rootKey->setText(QString::number(p.key_root));
    ui->le_volume->setText(QString::number(p.vol));
    ui->sliderPan->setValue(((int)p.pan - 128) * 100 / 128);

    auto vag = m_loader->extractVagSample(p.offset);
    if(!vag.empty()) {
        auto res = VagDecoder::decode(vag);
        m_waveform->setData(res.pcm);

        // Update check
        ui->chkLoop->blockSignals(true);
        ui->chkLoop->setChecked(res.loopEnabled);
        ui->chkLoop->blockSignals(false);
    }
}

void MainWindow::onTableSelectionChanged() {
    auto l = ui->tableWidget->selectedItems();
    if(l.isEmpty()) return;
    int r = l.first()->row();
    previewSample(ui->tableWidget->item(r,0)->data(Qt::UserRole).toInt(),
                  ui->tableWidget->item(r,0)->data(Qt::UserRole+1).toInt());
}

void MainWindow::onPlayClicked() {
    auto l = ui->tableWidget->selectedItems();
    if(l.isEmpty()) return;
    int r = l.first()->row();
    playSample(ui->tableWidget->item(r,0)->data(Qt::UserRole).toInt(),
               ui->tableWidget->item(r,0)->data(Qt::UserRole+1).toInt());
}

void MainWindow::onStopClicked() { stopAudio(); }
void MainWindow::onLoopToggled(bool c) { if(m_source) m_source->setLooping(c); }

void MainWindow::resetUi() {
    ui->tableWidget->setRowCount(0);
    ui->le_vagOffset->clear();
    ui->le_rootKey->clear();
    ui->le_volume->clear();
    ui->le_reverb->clear();
    ui->le_env_att->clear();
    ui->le_env_sl->clear();
    ui->le_env_sr_rr->clear();
}


void MainWindow::updateDetails(int i, int j) {
    const auto& part = m_loader->getInstruments()[i].parts[j];

    // 1. Basic Info
    ui->le_vagOffset->setText(QString::number(part.offset));

    // 2. Key Info
    QString noteName = QString::number(part.key_root);
    // Convert 60 to "C4"
    ui->le_rootKey->setText(QString("%1 (Range: %2-%3)").arg(part.key_root).arg(part.key_min).arg(part.key_max));

    ui->le_volume->setText(QString::number(part.vol));

    // 3. Reverb 0x80+ = on
    bool reverbOn = (part.reverb >= 0x80);
    ui->le_reverb->setText(reverbOn ? "On" : "Off");

    // 4. Pan
    int panUi = ((int)part.pan - 128) * 100 / 128;
    ui->sliderPan->setValue(panUi);

    // 5. ADSR (not sure if it works.)
    ui->le_env_att->setText(QString("A: %1").arg(QString::number(part.env_attack, 16).toUpper()));
    ui->le_env_sl->setText(QString("SL: %1").arg(QString::number(part.env_sustain_lvl, 16).toUpper()));

    // 6. 16 bit sustain and release
    ui->le_env_sr_rr->setText(QString("R/S: %1").arg(QString::number(part.env_release_sustain, 16).toUpper().rightJustified(4, '0')));
}

void MainWindow::fillTable() {
    ui->tableWidget->setRowCount(0);
    const auto& insts = m_loader->getInstruments();
    for(size_t i=0; i<insts.size(); ++i) {
        for(size_t j=0; j<insts[i].parts.size(); ++j) {
            int r = ui->tableWidget->rowCount();
            ui->tableWidget->insertRow(r);
            auto* it = new QTableWidgetItem(QString::number(i));
            it->setData(Qt::UserRole, (int)i);
            it->setData(Qt::UserRole+1, (int)j);
            ui->tableWidget->setItem(r,0,it);
            ui->tableWidget->setItem(r,1,new QTableWidgetItem(QString::number(j)));
            ui->tableWidget->setItem(r,2,new QTableWidgetItem(QString::number(insts[i].parts[j].offset)));
            ui->tableWidget->setItem(r,3,new QTableWidgetItem(QString::number(insts[i].parts[j].key_root)));
        }
    }
}
