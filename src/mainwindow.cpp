#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "waveformwidget.h"
#include "sf2exporter.h"
#include "engine.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QAudioSink>
#include <QMediaDevices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>
#include <QScrollBar>
#include <algorithm>

RawAudioSource::RawAudioSource(const std::vector<int16_t>& data, QObject* parent)
: QIODevice(parent), m_pos(0), m_loop(false), m_ls(0), m_le(0)
{
    m_buffer = QByteArray((const char*)data.data(), data.size() * sizeof(int16_t));
    m_le = m_buffer.size();
}

qint64 RawAudioSource::readData(char *data, qint64 maxlen) {
    qint64 totalRead = 0;
    maxlen = maxlen & ~1; // Align to 16-bit sample

    while (totalRead < maxlen) {
        qint64 endOfData = m_loop ? m_le : m_buffer.size();
        qint64 chunk = std::min(maxlen - totalRead, endOfData - m_pos);

        if (chunk <= 0) {
            if (m_loop) {
                m_pos = m_ls;
                continue;
            } else {
                break;
            }
        }

        memcpy(data + totalRead, m_buffer.constData() + m_pos, chunk);
        m_pos += chunk;
        totalRead += chunk;
    }
    return totalRead;
}

qint64 RawAudioSource::bytesAvailable() const {
    if (m_loop) return m_buffer.size();
    return m_buffer.size() - m_pos;
}

MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent), ui(new Ui::MainWindow), m_hd(std::make_unique<HDParser>()), m_bd(std::make_unique<BDParser>())
{
    ui->setupUi(this);

    m_waveform = new WaveformWidget(this);
    ui->waveformLayout->addWidget(m_waveform);

    ui->splitter->setStretchFactor(0, 3);
    ui->splitter->setStretchFactor(1, 2);

    connect(ui->actionOpen_HD, &QAction::triggered, this, &MainWindow::onOpenHd);
    connect(ui->actionExportSF2, &QAction::triggered, this, &MainWindow::onExportSf2);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::onCloseFile);

    connect(ui->btnPlay, &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(ui->btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(ui->chkLoop, &QCheckBox::toggled, this, &MainWindow::onLoopToggled);

    connect(ui->tableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onTableSelectionChanged);
    connect(ui->tableWidget, &QTableWidget::cellDoubleClicked, this, &MainWindow::onPlayClicked);

    connect(ui->btnBrowseSq, &QToolButton::clicked, [this](){
        QString path = QFileDialog::getOpenFileName(this, "Open SQ/MIDI", "", "Sequence (*.sq *.mid *.MID)");
        if(!path.isEmpty()) ui->le_sqPath->setText(path);
    });

        connect(ui->btnRenderWav, &QPushButton::clicked, this, &MainWindow::onRenderWav);
        connect(ui->btnBulk, &QPushButton::clicked, this, &MainWindow::onBulkExport);

        QTimer::singleShot(500, this, [this](){
            QMessageBox::information(this, "Welcome",
                                     "This program supports loading .HD/.BD/.SQ files\n\n"
                                     "IT'S NOT ACCURATE!! Contribute if you feel to.\n"
                                     "List of things that needs fix:\n"
                                     " - Vibrato\n"
                                     " - Panning etc...");
        });
}

MainWindow::~MainWindow() {
    stopAudio();
    delete ui;
}

void MainWindow::log(const QString& msg) {
    ui->logOutput->appendPlainText(msg);
    ui->logOutput->verticalScrollBar()->setValue(ui->logOutput->verticalScrollBar()->maximum());
}

void MainWindow::onOpenHd() {
    QString path = QFileDialog::getOpenFileName(this, "Open HD", "", "HD Files (*.hd *.HD)");
    if(path.isEmpty()) return;

    ui->logOutput->clear();
    log("Loading HD: " + path);

    if(!m_hd->load(path.toStdString())) {
        QMessageBox::critical(this, "Error", "Failed to load HD file.");
        log("Error: HD load failed (Invalid magic or size).");
        return;
    }

    int instCount = 0;
    int drumCount = 0;
    int nullCount = 0;
    for(const auto& p : m_hd->programs) {
        if(!p) { nullCount++; continue; }
        if(p->is_sfx) drumCount++;
        else instCount++;
    }

    QString stats = QString("HD Parsed Successfully.\n"
    "--------------------------------------------------\n"
    " Total Programs: %1\n"
    "   - Instruments: %2\n"
    "   - Drum/SFX Kits: %3\n"
    "   - Null/Empty: %4\n"
    " Breath/LFO Scripts: %5\n"
    "--------------------------------------------------")
    .arg(m_hd->programs.size())
    .arg(instCount)
    .arg(drumCount)
    .arg(nullCount)
    .arg(m_hd->breath_scripts.size());
    log(stats);

    QFileInfo fi(path);
    QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    QString bdPath;

    if(QFile::exists(base + ".bd")) bdPath = base + ".bd";
    else if(QFile::exists(base + ".BD")) bdPath = base + ".BD";

    if(bdPath.isEmpty() || !m_bd->load(bdPath.toStdString())) {
        bdPath = QFileDialog::getOpenFileName(this, "Locate BD", fi.absolutePath(), "BD Files (*.bd *.BD)");
        if(bdPath.isEmpty() || !m_bd->load(bdPath.toStdString())) {
            log("Warning: No BD loaded.");
            QMessageBox::warning(this, "Warning", "No BD loaded. Playback disabled.");
        } else {
            log("Loaded BD: " + bdPath);
            log(QString("BD Size: %1 bytes").arg(m_bd->data.size()));
        }
    } else {
        log("Loaded BD: " + bdPath);
        log(QString("BD Size: %1 bytes").arg(m_bd->data.size()));
    }

    fillTable();
    ui->statusbar->showMessage("Loaded: " + fi.fileName());
}

void MainWindow::onRenderWav() {
    if(m_hd->programs.empty() || m_bd->data.empty()) {
        log("Error: HD/BD not loaded.");
        QMessageBox::warning(this, "Error", "Please load an HD and BD file first.");
        return;
    }
    QString sqPath = ui->le_sqPath->text();
    if(sqPath.isEmpty()) {
        log("Error: No Sequence file selected.");
        QMessageBox::warning(this, "Error", "Please select a SQ or MIDI file.");
        return;
    }

    QString wavPath = QFileDialog::getSaveFileName(this, "Save WAV", "", "WAV Files (*.wav)");
    if(wavPath.isEmpty()) return;

    log("Starting WAV Render...");
    log("Sequence: " + QFileInfo(sqPath).fileName());
    log("Output: " + wavPath);

    ui->btnRenderWav->setEnabled(false);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    QApplication::processEvents();

    bool isMidi = sqPath.endsWith(".mid", Qt::CaseInsensitive) || sqPath.endsWith(".midi", Qt::CaseInsensitive);

    auto progressFunc = [this](int current, int total) {
        if (total > 0) {
            ui->progressBar->setRange(0, total);
            ui->progressBar->setValue(current);
        }
        QApplication::processEvents();
    };

    bool ok = ExportSequenceToWav(sqPath.toStdString(), wavPath.toStdString(), m_hd.get(), m_bd.get(), ui->chkReverb->isChecked(), isMidi, progressFunc);

    ui->btnRenderWav->setEnabled(true);
    ui->progressBar->setValue(ui->progressBar->maximum());

    if(ok) {
        log("WAV Render Successful.");
        QMessageBox::information(this, "Success", "WAV Rendered successfully!");
    } else {
        log("WAV Render Failed.");
        QMessageBox::critical(this, "Error", "Rendering failed.");
    }
}

void MainWindow::onBulkExport() {
    QString inDir = QFileDialog::getExistingDirectory(this, "Input Folder (HD files)");
    if(inDir.isEmpty()) return;
    QString outDir = QFileDialog::getExistingDirectory(this, "Output Folder");
    if(outDir.isEmpty()) return;

    QDir dir(inDir);
    QStringList filters; filters << "*.hd" << "*.HD";
    QFileInfoList hdFiles = dir.entryInfoList(filters, QDir::Files);

    if(hdFiles.empty()) {
        log("No .hd files found in directory.");
        return;
    }

    log("Starting Bulk Export on " + QString::number(hdFiles.size()) + " files...");

    ui->progressBar->setRange(0, hdFiles.size());
    ui->progressBar->setValue(0);

    int count = 0;
    int index = 0;

    for(const QFileInfo& info : hdFiles) {
        log("Processing: " + info.fileName());

        HDParser thd;
        BDParser tbd;

        if(!thd.load(info.absoluteFilePath().toStdString())) {
            log("  -> Failed to load HD.");
            index++;
            ui->progressBar->setValue(index);
            continue;
        }

        QString base = info.absolutePath() + "/" + info.completeBaseName();
        if(!tbd.load((base + ".bd").toStdString())) {
            tbd.load((base + ".BD").toStdString());
        }

        if(!tbd.data.empty()) {
            QString sf2Name = outDir + "/" + info.completeBaseName() + ".sf2";
            if(Sf2Exporter::exportToSf2(sf2Name, &thd, &tbd)) {
                log("  -> Exported SF2.");
                count++;
            } else {
                log("  -> SF2 Export failed.");
            }

            QString destMid = outDir + "/" + info.completeBaseName() + ".mid";
            QString midSource = base + ".mid";
            if (!QFile::exists(midSource)) midSource = base + ".MID";

            if (QFile::exists(midSource)) {
                if (QFile::exists(destMid)) QFile::remove(destMid);
                QFile::copy(midSource, destMid);
                log("  -> Copied existing MIDI.");
            } else {
                QString sqPath = base + ".sq";
                if(!QFile::exists(sqPath)) sqPath = base + ".SQ";

                if(QFile::exists(sqPath)) {
                    SQParser sq;
                    if(sq.load(sqPath.toStdString())) {
                        sq.saveToMidi(destMid.toStdString());
                        log("  -> Converted SQ to MIDI.");
                    }
                }
            }
        } else {
            log("  -> BD file missing.");
        }

        index++;
        ui->progressBar->setValue(index);
        QApplication::processEvents();
    }

    log("Bulk Export Finished. Processed: " + QString::number(count));
    QMessageBox::information(this, "Done", QString("Processed %1 files.").arg(count));
}

void MainWindow::fillTable() {
    ui->tableWidget->setRowCount(0);
    ui->tableWidget->setSortingEnabled(false);

    for(const auto& p : m_hd->programs) {
        if(!p) continue;
        for(size_t i=0; i<p->tones.size(); ++i) {
            int r = ui->tableWidget->rowCount();
            ui->tableWidget->insertRow(r);

            auto* idItem = new QTableWidgetItem(QString::number(p->id));
            idItem->setData(Qt::UserRole, p->id);
            idItem->setData(Qt::UserRole+1, (int)i);

            ui->tableWidget->setItem(r, 0, idItem);

            QString typeStr;
            if (p->is_sfx) typeStr = QString("SFX (0xFF)");
            else if (p->is_layered) typeStr = QString("Layered (0x%1)").arg(QString::number(p->type, 16).toUpper());
            else typeStr = QString("Split (0x%1)").arg(QString::number(p->type, 16).toUpper());

            ui->tableWidget->setItem(r, 1, new QTableWidgetItem(typeStr));
            ui->tableWidget->setItem(r, 2, new QTableWidgetItem(QString("0x%1").arg(QString::number(p->tones[i].bd_offset, 16).toUpper())));
            ui->tableWidget->setItem(r, 3, new QTableWidgetItem(QString("%1 - %2").arg(p->tones[i].min_note).arg(p->tones[i].max_note)));
        }
    }
    ui->tableWidget->setSortingEnabled(true);
}

void MainWindow::logProgramDetails(int progId) {
    auto it = std::find_if(m_hd->programs.begin(), m_hd->programs.end(), [progId](auto& p){ return p && p->id == progId; });
    if(it == m_hd->programs.end()) return;
    auto p = *it;

    QString msg;
    msg += QString("=== PROGRAM ID: %1 ===\n").arg(p->id);
    msg += QString("Raw Type Byte: 0x%1\n").arg(QString::number(p->type, 16).toUpper());
    msg += QString("Master Vol: %1, Master Pan: %2\n").arg(p->master_vol).arg(p->master_pan);
    msg += QString("Calculated Flags: SFX=%1, Layered=%2\n").arg(p->is_sfx).arg(p->is_layered);
    msg += QString("Tone Count: %1\n").arg(p->tones.size());
    msg += "--------------------------------------------------\n";

    for(size_t i=0; i<p->tones.size(); ++i) {
        const auto& t = p->tones[i];
        msg += QString("TONE #%1\n").arg(i);
        msg += QString("  Offset: 0x%1\n").arg(QString::number(t.bd_offset, 16).toUpper());
        msg += QString("  Key Range: %1 to %2 (Root: %3)\n").arg(t.min_note).arg(t.max_note).arg(t.root_key);
        msg += QString("  ADSR1: 0x%1  ADSR2: 0x%2\n").arg(QString::number(t.adsr1, 16).toUpper()).arg(QString::number(t.adsr2, 16).toUpper());
        msg += QString("  Vol: %1, Pan: %2\n").arg(t.vol).arg(t.pan);
        msg += QString("  Flags: 0x%1 (Noise=%2, Reverb=%3)\n").arg(QString::number(t.flags, 16).toUpper()).arg(t.is_noise()).arg(t.is_reverb());
        msg += "--------------------------------------------------\n";
    }
    log(msg);
}

void MainWindow::onTableSelectionChanged() {
    auto l = ui->tableWidget->selectedItems();
    if(l.isEmpty()) return;
    int r = l.first()->row();

    int pid = ui->tableWidget->item(r,0)->data(Qt::UserRole).toInt();
    int tid = ui->tableWidget->item(r,0)->data(Qt::UserRole+1).toInt();

    logProgramDetails(pid);

    auto it = std::find_if(m_hd->programs.begin(), m_hd->programs.end(), [pid](auto& p){ return p && p->id==pid; });
    if(it == m_hd->programs.end()) return;
    const auto& t = (*it)->tones[tid];

    ui->le_offset->setText(QString("0x%1").arg(QString::number(t.bd_offset, 16).toUpper()));
    ui->le_adsr->setText(QString("1:%1 2:%2").arg(QString::number(t.adsr1, 16).toUpper()).arg(QString::number(t.adsr2, 16).toUpper()));
    ui->le_vol->setText(QString("V:%1 P:%2").arg(t.vol).arg(t.pan));

    auto raw = m_bd->get_adpcm_block(t.bd_offset);
    if (!raw.empty()) {
        auto dec = EngineUtils::decode_adpcm(raw);
        m_waveform->setData(dec.pcm);

        ui->chkLoop->blockSignals(true);
        ui->chkLoop->setChecked(dec.looping);
        ui->chkLoop->blockSignals(false);
    } else {
        m_waveform->clear();
    }
}

void MainWindow::onPlayClicked() {
    auto l = ui->tableWidget->selectedItems();
    if(l.isEmpty()) return;
    int r = l.first()->row();
    playSample(ui->tableWidget->item(r,0)->data(Qt::UserRole).toInt(),
               ui->tableWidget->item(r,0)->data(Qt::UserRole+1).toInt());
}

void MainWindow::playSample(int pid, int tid) {
    stopAudio();

    auto it = std::find_if(m_hd->programs.begin(), m_hd->programs.end(), [pid](auto& p){ return p && p->id==pid; });
    if(it == m_hd->programs.end()) return;
    const auto& t = (*it)->tones[tid];

    auto raw = m_bd->get_adpcm_block(t.bd_offset);
    if(raw.empty()) return;

    auto dec = EngineUtils::decode_adpcm(raw);
    if(dec.pcm.empty()) return;

    m_source = new RawAudioSource(dec.pcm, this);
    m_source->setLooping(ui->chkLoop->isChecked(), dec.looping ? dec.loop_start : 0, dec.looping ? dec.loop_end : dec.pcm.size());

    if (!m_source->open(QIODevice::ReadOnly)) { delete m_source; m_source = nullptr; return; }

    QAudioFormat fmt;
    fmt.setSampleRate(44100);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
    m_sink->setVolume(1.0);
    m_sink->start(m_source);
}

void MainWindow::stopAudio() {
    if(m_sink) { m_sink->stop(); delete m_sink; m_sink = nullptr; }
    if(m_source) { m_source->close(); delete m_source; m_source = nullptr; }
}
void MainWindow::onStopClicked() { stopAudio(); }
void MainWindow::onLoopToggled(bool c) { }
void MainWindow::onCloseFile() { stopAudio(); m_hd->clear(); m_bd->data.clear(); fillTable(); resetUi(); log("Closed."); ui->statusbar->showMessage("Closed."); }
void MainWindow::resetUi() { ui->le_offset->clear(); ui->le_adsr->clear(); ui->le_vol->clear(); m_waveform->clear(); }

void MainWindow::onExportSf2() {
    if(m_hd->programs.empty()) return;

    QMessageBox::warning(this, "Accuracy Warning",
                         "Note: SF2 export is an approximation.\n\n"
                         "The PS SPU hardware features specific capabilities (Procedural Noise, "
                         "LFO Shapes, Hardware Portamento) that do not exist in the "
                         "SoundFont 2.01 standard.\n\n"
                         "While pitch and envelopes are (belived to be) converted accurately, the exported file "
                         "will not sound 100% identical to the in-game audio.");

    QString path = QFileDialog::getSaveFileName(this, "Export SF2", "out.sf2", "SF2 (*.sf2)");
    if(!path.isEmpty()) {
        log("Exporting SF2...");

        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool success = Sf2Exporter::exportToSf2(path, m_hd.get(), m_bd.get());
        QApplication::restoreOverrideCursor();

        if(success) {
            log("SF2 Export Success: " + path);
            QMessageBox::information(this, "Success", "SF2 exported successfully.");
        } else {
            log("SF2 Export Failed.");
            QMessageBox::critical(this, "Error", "Export failed. Check console for details.");
        }
    }
}
