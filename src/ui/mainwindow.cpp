#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "waveformwidget.h"
#include "version.h"

#include "../engine/audio.h"
#include "../format/hd.h"
#include "../format/bd.h"
#include "../format/sq.h"
#include "../format/mid.h"
#include "../exporters/sf2exporter.h"
#include "../exporters/renderwav.h"
#include "../common.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QScrollBar>
#include <QRegularExpression>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent)
, ui(new Ui::MainWindow)
, m_hd(std::make_unique<HDParser>())
, m_bd(std::make_unique<BDParser>())
, m_audio(std::make_unique<AudioEngine>())
{
    ui->setupUi(this);

    if (!m_audio->init()) {
        QMessageBox::critical(this, "Audio Error", "Failed to initialize Miniaudio backend.");
    }

    m_waveform = new WaveformWidget(this);
    ui->waveformLayout->addWidget(m_waveform);

    ui->splitter->setStretchFactor(0, 3);
    ui->splitter->setStretchFactor(1, 2);
    ui->treeWidget->setColumnWidth(0, 220);
    ui->treeWidget->setColumnWidth(1, 80);

    connect(ui->actionOpen_HD, &QAction::triggered, this, &MainWindow::onOpenHd);
    connect(ui->actionExportSF2, &QAction::triggered, this, &MainWindow::onExportSf2);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::onCloseFile);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);
    connect(ui->actionAboutQt, &QAction::triggered, this, &MainWindow::onAboutQt);

    connect(ui->btnPlay, &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(ui->btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(ui->chkLoop, &QCheckBox::toggled, this, &MainWindow::onLoopToggled);

    connect(ui->treeWidget, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onTreeSelectionChanged);
    connect(ui->treeWidget, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onPlayClicked);

    connect(ui->btnBrowseSq, &QToolButton::clicked, [this](){
        QString path = QFileDialog::getOpenFileName(this, "Open SQ/MIDI", "", "Sequence (*.sq *.mid *.MID)");
        if(!path.isEmpty()) ui->le_sqPath->setText(path);
    });

        connect(ui->btnRenderWav, &QPushButton::clicked, this, &MainWindow::onRenderWav);
        connect(ui->btnBulk, &QPushButton::clicked, this, &MainWindow::onBulkExport);
        connect(ui->btnSeq2Midi, &QPushButton::clicked, this, &MainWindow::onSeq2Midi);

        QTimer::singleShot(500, this, [this](){
            log("Ready. Audio engine initialized.");
        });
}

MainWindow::~MainWindow() {
    m_audio->stop();
    delete ui;
}

void MainWindow::log(const QString& msg) {
    ui->logOutput->appendPlainText(msg);
    ui->logOutput->verticalScrollBar()->setValue(ui->logOutput->verticalScrollBar()->maximum());
}

void MainWindow::onOpenHd() {
    QString path = QFileDialog::getOpenFileName(this, "Open HD", "", "HD Files (*.hd *.HD)");
    if (path.isEmpty()) return;

    ui->logOutput->clear();
    log("Loading HD: " + path);

    if (!m_hd->load(path.toStdString())) {
        QMessageBox::critical(this, "Error", "Failed to load HD file.");
        log("Error: HD load failed.");
        return;
    }

    int instCount = 0;
    int drumCount = 0;
    for (const auto& p : m_hd->programs) {
        if (!p) continue;
        p->is_sfx ? drumCount++ : instCount++;
    }
    log(QString("HD Loaded. Instruments: %1, Drums: %2").arg(instCount).arg(drumCount));

    QFileInfo fi(path);
    QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    QString bdPath;

    if (QFile::exists(base + ".bd")) bdPath = base + ".bd";
    else if (QFile::exists(base + ".BD")) bdPath = base + ".BD";

    if (bdPath.isEmpty() || !m_bd->load(bdPath.toStdString())) {
        bdPath = QFileDialog::getOpenFileName(this, "Locate BD", fi.absolutePath(), "BD Files (*.bd *.BD)");
        if (bdPath.isEmpty() || !m_bd->load(bdPath.toStdString())) {
            log("Warning: No BD loaded. Playback disabled.");
        } else {
            log("Loaded BD: " + bdPath);
        }
    } else {
        log("Loaded BD: " + bdPath);
    }

    fillTree();
    ui->statusbar->showMessage("Loaded: " + fi.fileName());
}

void MainWindow::onCloseFile() {
    m_audio->stop();
    m_hd->clear();
    m_bd->data.clear();
    ui->treeWidget->clear();
    ui->propTable->setRowCount(0);
    m_waveform->clear();
    log("Closed.");
}

void MainWindow::fillTree() {
    ui->treeWidget->clear();
    ui->propTable->setRowCount(0);

    for (const auto& p : m_hd->programs) {
        if (!p) continue;

        auto* progItem = new QTreeWidgetItem(ui->treeWidget);
        progItem->setText(0, QString("Program %1").arg(p->id));
        progItem->setText(1, p->is_sfx ? "SFX" : (p->is_layered ? "Layered" : "Inst"));

        progItem->setData(0, Qt::UserRole, p->id);
        progItem->setData(0, Qt::UserRole + 1, -1);

        if (p->is_layered && !p->is_sfx) {
            for (size_t i = 0; i < p->tones.size(); i += 2) {
                auto* toneItem = new QTreeWidgetItem(progItem);
                if (i + 1 < p->tones.size()) {
                    toneItem->setText(0, QString("Tone %1 & %2").arg(i).arg(i + 1));
                } else {
                    toneItem->setText(0, QString("Tone %1 (Single)").arg(i));
                }
                toneItem->setText(1, "Layer pair");
                toneItem->setData(0, Qt::UserRole, p->id);
                toneItem->setData(0, Qt::UserRole + 1, (int)i);
            }
        } else {
            for (size_t i = 0; i < p->tones.size(); ++i) {
                auto* toneItem = new QTreeWidgetItem(progItem);
                toneItem->setText(0, QString("Tone %1").arg(i));
                toneItem->setText(1, "Voice");
                toneItem->setData(0, Qt::UserRole, p->id);
                toneItem->setData(0, Qt::UserRole + 1, (int)i);
            }
        }
    }
}

void MainWindow::addPropRow(const QString& name, const QString& value) {
    int r = ui->propTable->rowCount();
    ui->propTable->insertRow(r);
    ui->propTable->setItem(r, 0, new QTableWidgetItem(name));
    ui->propTable->setItem(r, 1, new QTableWidgetItem(value));
}

void MainWindow::onTreeSelectionChanged() {
    auto sel = ui->treeWidget->selectedItems();
    if (sel.isEmpty()) return;

    int pid = sel.first()->data(0, Qt::UserRole).toInt();
    int tid = sel.first()->data(0, Qt::UserRole + 1).toInt();
    updatePropertyView(pid, tid);
}

void MainWindow::updatePropertyView(int pid, int tid) {
    ui->propTable->setRowCount(0);

    auto it = std::find_if(m_hd->programs.begin(), m_hd->programs.end(),
                           [pid](auto& p){ return p && p->id == pid; });
    if (it == m_hd->programs.end()) return;
    auto p = *it;

    if (tid == -1) {
        m_waveform->clear();
        addPropRow("Program ID", QString::number(p->id));
        addPropRow("Type", QString("0x%1").arg(QString::number(p->type, 16).toUpper()));
        addPropRow("Vol / Pan", QString("%1 / %2").arg(p->master_vol).arg(p->master_pan));
        addPropRow("Tone Count", QString::number(p->tones.size()));
    } else {
        if (tid < 0 || tid >= (int)p->tones.size()) return;

        const auto& t = p->tones[tid];

        addPropRow("Tone Index", QString::number(tid));
        addPropRow("Offset", QString("0x%1").arg(QString::number(t.bd_offset, 16).toUpper()));
        addPropRow("Key Range", QString("%1 - %2").arg(t.min_note).arg(t.max_note));
        addPropRow("Root Key", QString::number(t.root_key));
        addPropRow("ADSR 1/2", QString("%1 / %2").arg(QString::number(t.adsr1, 16)).arg(QString::number(t.adsr2, 16)));
        addPropRow("Vol / Pan", QString("%1 / %2").arg(t.vol).arg(t.pan));

        if (p->is_layered && !p->is_sfx && (tid + 1 < (int)p->tones.size())) {
            const auto& t2 = p->tones[tid + 1];
            addPropRow("--- Layer 2 ---", "");
            addPropRow("Offset", QString("0x%1").arg(QString::number(t2.bd_offset, 16).toUpper()));
            addPropRow("Vol / Pan", QString("%1 / %2").arg(t2.vol).arg(t2.pan));
        }

        auto raw = m_bd->get_adpcm_block(t.bd_offset);
        if (!raw.empty()) {
            auto dec = EngineUtils::decode_adpcm(raw);
            m_waveform->setData(dec.pcm, dec.looping, dec.loop_start, dec.loop_end);

            ui->chkLoop->blockSignals(true);
            ui->chkLoop->setChecked(dec.looping);
            ui->chkLoop->blockSignals(false);
        } else {
            m_waveform->clear();
        }
    }
}

void MainWindow::onPlayClicked() {
    auto sel = ui->treeWidget->selectedItems();
    if (sel.isEmpty()) return;

    int pid = sel.first()->data(0, Qt::UserRole).toInt();
    int tid = sel.first()->data(0, Qt::UserRole + 1).toInt();

    if (tid != -1) {
        playSample(pid, tid);
    }
}

void MainWindow::playSample(int pid, int tid) {
    m_audio->stop();

    auto it = std::find_if(m_hd->programs.begin(), m_hd->programs.end(),
                           [pid](auto& p){ return p && p->id == pid; });
    if (it == m_hd->programs.end()) return;
    const auto& prog = *it;

    std::vector<VoiceRequest> requests;

    auto addVoice = [&](int index) {
        if (index >= (int)prog->tones.size()) return;
        const auto& t = prog->tones[index];

        auto raw = m_bd->get_adpcm_block(t.bd_offset);
        if (raw.empty()) return;

        auto dec = EngineUtils::decode_adpcm(raw);
        if (dec.pcm.empty()) return;

        VoiceRequest req;
        req.pcm = dec.pcm;
        req.loop = ui->chkLoop->isChecked();
        req.loopStart = dec.loop_start;
        req.loopEnd = dec.loop_end;
        req.vol = t.vol;
        req.pan = t.pan;
        requests.push_back(req);

        if (requests.size() == 1) {
            m_waveform->setData(dec.pcm, dec.looping, dec.loop_start, dec.loop_end);
        }
    };

    addVoice(tid);

    if (prog->is_layered && !prog->is_sfx) {
        addVoice(tid + 1);
    }

    if (!requests.empty()) {
        m_audio->play(requests);
    }
}

void MainWindow::onStopClicked() {
    m_audio->stop();
}

void MainWindow::onLoopToggled(bool checked) {
    m_audio->setLooping(checked);
}

void MainWindow::onRenderWav() {
    if (m_hd->programs.empty() || m_bd->data.empty()) {
        QMessageBox::warning(this, "Error", "Please load an HD and BD file first.");
        return;
    }

    QString sqPath = ui->le_sqPath->text();
    if (sqPath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a SQ or MIDI file.");
        return;
    }

    QString wavPath = QFileDialog::getSaveFileName(this, "Save WAV", "", "WAV Files (*.wav)");
    if (wavPath.isEmpty()) return;

    log("Starting WAV Render...");
    ui->btnRenderWav->setEnabled(false);
    ui->progressBar->setValue(0);

    QApplication::processEvents();

    bool isMidi = sqPath.endsWith(".mid", Qt::CaseInsensitive) || sqPath.endsWith(".midi", Qt::CaseInsensitive);

    auto progressFunc = [this](int current, int total) {
        if (total > 0) {
            ui->progressBar->setMaximum(total);
            ui->progressBar->setValue(current);
        }
        QApplication::processEvents();
    };

    bool ok = ExportSequenceToWav(sqPath.toStdString(), wavPath.toStdString(),
                                  m_hd.get(), m_bd.get(),
                                  ui->chkReverb->isChecked(), isMidi, progressFunc);

    ui->btnRenderWav->setEnabled(true);
    ui->progressBar->setValue(ui->progressBar->maximum());
    log(ok ? "WAV Render Successful." : "WAV Render Failed.");
}

void MainWindow::onBulkExport() {
    QString inDir = QFileDialog::getExistingDirectory(this, "Input Folder (HD files)");
    if (inDir.isEmpty()) return;
    QString outDir = QFileDialog::getExistingDirectory(this, "Output Folder");
    if (outDir.isEmpty()) return;

    QDir dir(inDir);
    QFileInfoList hdFiles = dir.entryInfoList({"*.hd", "*.HD"}, QDir::Files);

    if (hdFiles.empty()) {
        log("No .hd files found.");
        return;
    }

    log("Starting Bulk Export: " + QString::number(hdFiles.size()) + " files.");
    ui->progressBar->setRange(0, hdFiles.size());
    ui->progressBar->setValue(0);

    int count = 0;
    int index = 0;

    for (const QFileInfo& info : hdFiles) {
        log("Processing: " + info.fileName());

        HDParser thd;
        BDParser tbd;

        if (!thd.load(info.absoluteFilePath().toStdString())) {
            log("  -> Failed to load HD.");
            ui->progressBar->setValue(++index);
            continue;
        }

        QString base = info.absolutePath() + "/" + info.completeBaseName();
        if (!tbd.load((base + ".bd").toStdString())) {
            tbd.load((base + ".BD").toStdString());
        }

        if (!tbd.data.empty()) {
            QString sf2Name = outDir + "/" + info.completeBaseName() + ".sf2";
            if (Sf2Exporter::exportToSf2(sf2Name, &thd, &tbd)) {
                log("  -> Exported SF2.");
                count++;
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
                if (!QFile::exists(sqPath)) sqPath = base + ".SQ";
                if (QFile::exists(sqPath)) {
                    SQParser sq;
                    if (sq.load(sqPath.toStdString())) {
                        SaveSQToMidi(sq.getData(), destMid.toStdString());
                        log("  -> Converted SQ to MIDI.");
                    }
                }
            }
        } else {
            log("  -> BD file missing.");
        }

        ui->progressBar->setValue(++index);
        QApplication::processEvents();
    }

    log("Bulk Export Finished. Processed: " + QString::number(count));
    QMessageBox::information(this, "Done", QString("Processed %1 files.").arg(count));
}

void MainWindow::onExportSf2() {
    if (m_hd->programs.empty()) return;

    QMessageBox::warning(this, "Accuracy Warning",
                         "SF2 export is an approximation. Hardware features (Noise, LFOs) "
                         "cannot be perfectly replicated in SoundFont 2.0 format.");

    QString path = QFileDialog::getSaveFileName(this, "Export SF2", "out.sf2", "SF2 (*.sf2)");
    if (path.isEmpty()) return;

    log("Exporting SF2...");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool success = Sf2Exporter::exportToSf2(path, m_hd.get(), m_bd.get());
    QApplication::restoreOverrideCursor();

    if (success) {
        log("SF2 Export Success: " + path);
        QMessageBox::information(this, "Success", "SF2 exported successfully.");
    } else {
        log("SF2 Export Failed.");
        QMessageBox::critical(this, "Error", "Export failed.");
    }
}

void MainWindow::onSeq2Midi() {
    QString inputPath = QFileDialog::getOpenFileName(this, "Select SQ File", "", "SQ Files (*.sq *.SQ)");
    if (inputPath.isEmpty()) return;

    QString defaultOutput = inputPath;
    defaultOutput.replace(QRegularExpression("\\.sq$", QRegularExpression::CaseInsensitiveOption), ".mid");

    QString outputPath = QFileDialog::getSaveFileName(this, "Save MIDI File", defaultOutput, "MIDI Files (*.mid)");
    if (outputPath.isEmpty()) return;

    SQParser sq;
    if (sq.load(inputPath.toStdString())) {
        if (SaveSQToMidi(sq.getData(), outputPath.toStdString())) {
            log("Success: MIDI file created.");
            QMessageBox::information(this, "Success", "SQ converted to MIDI.");
        } else {
            log("Error: Failed to save MIDI.");
            QMessageBox::critical(this, "Error", "Failed to save MIDI.");
        }
    } else {
        log("Error: Failed to load SQ.");
        QMessageBox::critical(this, "Error", "Failed to load SQ.");
    }
}

void MainWindow::onAbout() {
    QString aboutText = QString(
        "<h2>%1</h2>"
        "<p>Version %2</p>"
        "<p>%3</p>"
        "<p><a href=\"%4\">%4</a></p>"
        "<p>Built with Qt %5</p>"
    ).arg(APP_NAME, APP_VERSION, APP_DESCRIPTION, APP_HOMEPAGE, QT_VERSION_STR);

    QMessageBox::about(this, QString("About %1").arg(APP_NAME), aboutText);
}

void MainWindow::onAboutQt() {
    QMessageBox::aboutQt(this, "About Qt");
}
