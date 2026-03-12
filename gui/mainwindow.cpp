#include "mainwindow.h"
#include "gui/ui_mainwindow.h"
#include "../visualization/OscilloscopeWidget.h"
#include "../network/TCPControl.h"
#include "../network/IDataProvider.h"
#include "../network/UDPDataProvider.h"
#include "../model/PacketDecoder.h"
#include "../model/MicrophonePacket.h"
#include "../model/MicrophoneArray.h"
#include "../model/BeamformingCalculator.h"
#include "../storage/DataRecorder.h"
#include "../model/BeamformerWorker.h"
#include "../audio/AudioOutput.h"
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QRegularExpression>
#include <QtMath>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QTimer>
#include <QEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>

static QString audioStateToString(QAudio::State state)
{
    switch (state) {
    case QAudio::ActiveState: return QStringLiteral("Active");
    case QAudio::SuspendedState: return QStringLiteral("Suspended");
    case QAudio::StoppedState: return QStringLiteral("Stopped");
    case QAudio::IdleState: return QStringLiteral("Idle");
    }
    return QStringLiteral("Unknown");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("Ultrasonic Beamforming Host");
    ui->statusLabel->setText("Status: Idle");
    
    // Limit log text widget size to prevent memory buildup
    // PERFORMANCE: Keep large enough to hold all info + receiving messages
    ui->logTextEdit->setMaximumBlockCount(500);
    
    // PERFORMANCE: Disable expensive features on text widgets
    ui->logTextEdit->setReadOnly(true);
    ui->logTextEdit->setUndoRedoEnabled(false);
    // Note: Consider disabling auto-scroll in Designer if still slow
    
    // NO STYLING - use default Qt theme for maximum performance
    // applyModernStyle();  // DISABLED for performance
    
    // Initialize beamforming objects 
    m_micArray = new MicrophoneArray();
    m_beamCalc = new BeamformingCalculator();
    m_audioOutput = new AudioOutput(this);
    connect(m_audioOutput, &AudioOutput::errorOccurred,
            this, [this](const QString &msg) {
                ui->logTextEdit->appendPlainText(QString("[AUDIO_ERROR] %1").arg(msg));
            });
    connect(m_audioOutput, &AudioOutput::stateChanged,
            this, [this](QAudio::State state) {
                ui->logTextEdit->appendPlainText(
                    QString("[AUDIO_STATE] %1 | %2")
                        .arg(audioStateToString(state))
                        .arg(m_audioOutput->debugStatus()));
            });
    
    // Setup beamforming worker thread
    m_beamformerThread = new QThread(this);
    m_beamformerWorker = new BeamformerWorker();
    m_beamformerWorker->moveToThread(m_beamformerThread);
    m_beamformerThread->start();

    // populating microphone selector 
    ui->micComboBox->clear();
    for (int i = 0; i < 102; ++i) {
        ui->micComboBox->addItem(QString("Mic %1").arg(i), i);
    }
    ui->micComboBox->setCurrentIndex(0);
    m_selectedMic = 0;
    
    // Position mic combobox dynamically in top-right corner
    auto positionComboBox = [this]() {
        const int width = ui->oscilloscopeWidget->width();
        const int right = width - 10;
        ui->micComboBox->move(right - ui->micComboBox->width(), 5);
        QSpinBox *fpsSpin = ui->oscilloscopeWidget->findChild<QSpinBox*>("oscFpsSpinBox");
        if (fpsSpin)
            fpsSpin->move(right - fpsSpin->width(), 5 + ui->micComboBox->height() + 2);
    };
    
    // Install event filter to reposition on resize
    ui->oscilloscopeWidget->installEventFilter(this);
    
    // Initial positioning
    QTimer::singleShot(0, positionComboBox);

    // Use source directory for geometry file (relative to executable during development)
    // In production, you may want to deploy Book1.csv with the executable
    // microphoneLocations.csv is 2 levels up from the executable (UltrasonicHost/ root)
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    dir.cdUp();
    const QString geoPath = dir.filePath("microphoneLocations.csv");
    ui->logTextEdit->appendPlainText(QString("📂 Loading geometry from: %1").arg(geoPath));

    if (loadGeometry(geoPath)) {
        recomputeDelays();
    } else {
        ui->logTextEdit->appendPlainText("⚠️ WARNING: Geometry not loaded. Beamforming will NOT work!");
        ui->logTextEdit->appendPlainText(QString("⚠️ Failed to load: %1").arg(geoPath));
    }

    auto hookRecompute = [this]() {
        // don't recompute until geometry loaded
        if (m_geometryLoaded) {
            recomputeDelays();
        }
    };

    connect(ui->azimuthDoubleSpinBox,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            [hookRecompute](double) { hookRecompute(); });

    connect(ui->elevationDoubleSpinBox,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            [hookRecompute](double) { hookRecompute(); });

    connect(ui->temperatureDoubleSpinBox,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            [hookRecompute](double) { hookRecompute(); });

    connect(m_beamformerWorker, &BeamformerWorker::scanComplete,
            this, &MainWindow::onScanComplete, Qt::QueuedConnection);
    connect(this, &MainWindow::frameReady,
            m_beamformerWorker, &BeamformerWorker::processBlock);

    // Setup oscilloscope worker thread
    m_oscThread = new QThread(this);
    m_oscWorker = new OscilloscopeWorker();
    m_oscWorker->onScaleChanged(ui->scaleSpinBox->value());  // set before thread starts
    m_oscWorker->moveToThread(m_oscThread);
    m_oscThread->start();
    connect(ui->oscilloscopeWidget, &OscilloscopeWidget::sizeChanged,
            m_oscWorker, &OscilloscopeWorker::onSizeChanged);
    connect(this, &MainWindow::newOscFrame,
            m_oscWorker, &OscilloscopeWorker::onNewFrame);
    connect(m_oscWorker, &OscilloscopeWorker::imageReady,
            this, [this](QImage image) {
                ui->oscilloscopeWidget->setImage(std::move(image));
            });
    // Direct connection: BeamformerWorker feeds heatmap straight to OscilloscopeWorker.
    // Qt will auto-queue across the thread boundary.
    connect(m_beamformerWorker, &BeamformerWorker::scanComplete,
            m_oscWorker, &OscilloscopeWorker::onNewHeatmap);

    // FPS spinbox — floats over top-right of oscilloscope widget, below mic combobox
    {
        QSpinBox *fpsSpin = new QSpinBox(ui->oscilloscopeWidget);
        fpsSpin->setRange(1, 94);
        fpsSpin->setValue(94);
        fpsSpin->setFixedWidth(65);
        fpsSpin->setToolTip("Oscilloscope display FPS");
        fpsSpin->setObjectName("oscFpsSpinBox");
        fpsSpin->show();
        connect(fpsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                m_oscWorker, &OscilloscopeWorker::onFpsChanged);
    }

    // Scan interval + speed of sound — float over bottom-left of oscilloscope widget
    {
        QWidget *scanWidget = new QWidget(ui->oscilloscopeWidget);
        scanWidget->setObjectName("scanIntervalWidget");
        QHBoxLayout *scanLay = new QHBoxLayout(scanWidget);
        scanLay->setContentsMargins(4, 2, 4, 2);
        scanLay->setSpacing(4);

        auto makeLabel = [](const QString &text) {
            QLabel *l = new QLabel(text);
            l->setStyleSheet("color: white; background: transparent;");
            return l;
        };

        QDoubleSpinBox *scanSpin = new QDoubleSpinBox();
        scanSpin->setObjectName("scanIntervalSpinBox");
        scanSpin->setRange(0.001, 10.0);
        scanSpin->setSingleStep(0.1);
        scanSpin->setDecimals(3);
        scanSpin->setValue(0.001);
        scanSpin->setSuffix(" s");
        scanSpin->setFixedWidth(75);
        scanSpin->setToolTip("Minimum time between beamforming scans");

        QDoubleSpinBox *sosSpin = new QDoubleSpinBox();
        sosSpin->setObjectName("speedOfSoundSpinBox");
        sosSpin->setRange(300.0, 400.0);
        sosSpin->setSingleStep(1.0);
        sosSpin->setDecimals(1);
        sosSpin->setValue(343.0);
        sosSpin->setSuffix(" m/s");
        sosSpin->setFixedWidth(90);
        sosSpin->setToolTip("Speed of sound used for beamforming delay calculation");

        scanLay->addWidget(makeLabel("Scan interval:"));
        scanLay->addWidget(scanSpin);
        scanLay->addSpacing(8);
        scanLay->addWidget(makeLabel("Speed of sound:"));
        scanLay->addWidget(sosSpin);
        scanLay->addSpacing(8);

        QDoubleSpinBox *gridResSpin = new QDoubleSpinBox();
        gridResSpin->setObjectName("gridResSpinBox");
        gridResSpin->setRange(0.02, 0.5);
        gridResSpin->setSingleStep(0.02);
        gridResSpin->setDecimals(2);
        gridResSpin->setValue(0.05);
        gridResSpin->setFixedWidth(70);
        gridResSpin->setToolTip("Beamforming grid resolution (normalised units)");

        scanLay->addWidget(makeLabel("Grid res:"));
        scanLay->addWidget(gridResSpin);
        scanWidget->adjustSize();
        scanWidget->show();

        connect(scanSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double val) {
                    m_scanIntervalFrames = qMax(1, qRound(val * 48000.0 / SamplesPerPacket));
                    const int sz = NumMics * SamplesPerPacket * m_scanIntervalFrames;
                    m_scanBuffers[0].assign(sz, 0.0f);
                    m_scanBuffers[1].assign(sz, 0.0f);
                    m_framesSinceLastScan = 0;
                    m_workerBusy = false;
                });
        connect(sosSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double val) {
                    m_beamformerWorker->setSpeedOfSound(static_cast<float>(val));
                });
        connect(gridResSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double val) {
                    m_beamformerWorker->setGridRes(static_cast<float>(val));
                });
    }

    // Connect UI checkboxes to beamforming control slots
    connect(ui->enableAudioCheckbox, &QCheckBox::toggled,
            this, &MainWindow::toggleAudio);
    connect(ui->enableBeamformingCheckbox, &QCheckBox::toggled,
            this, &MainWindow::toggleBeamforming);

    
    // Connect scale spinbox to oscilloscope
    connect(ui->scaleSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            m_oscWorker, &OscilloscopeWorker::onScaleChanged);

    // Initialize state by explicitly calling the toggle functions with current checkbox states
    // This ensures member variables match UI state at startup
    toggleAudio(ui->enableAudioCheckbox->isChecked());
    toggleBeamforming(ui->enableBeamformingCheckbox->isChecked());


    // start timers for performance tracking
    m_sessionTimer.start();
    m_intervalTimer.start();

    // Size the scan accumulation buffers now that m_scanIntervalFrames is set.
    const int scanBufSize = NumMics * SamplesPerPacket * m_scanIntervalFrames;
    m_scanBuffers[0].assign(scanBufSize, 0.0f);
    m_scanBuffers[1].assign(scanBufSize, 0.0f);
    
    // Initialize data recorder on its own thread
    m_recorderThread = new QThread(this);
    m_dataRecorder = new DataRecorder();
    m_dataRecorder->moveToThread(m_recorderThread);
    m_recorderThread->start();
    connect(m_dataRecorder, &DataRecorder::recordingComplete,
            this, &MainWindow::handleRecordingComplete);
    connect(m_dataRecorder, &DataRecorder::recordingError,
            this, &MainWindow::handleRecordingError);
    connect(m_dataRecorder, &DataRecorder::progressUpdated,
            this, &MainWindow::updateRecordingProgress);
    // Wire main-thread signal to recorder slot (queued across threads automatically)
    connect(this, &MainWindow::recordSamples,
            m_dataRecorder, &DataRecorder::addSamples);
    
    // Add recording UI controls (programmatically since we don't have .ui designer access)
    // Create recording groupbox
    QGroupBox *recordingGroup = new QGroupBox("Data Recording for MATLAB", this);
    QVBoxLayout *recordingLayout = new QVBoxLayout(recordingGroup);
    
    // Test parameters
    QFormLayout *paramForm = new QFormLayout();
    
    QSpinBox *testNumSpinBox = new QSpinBox();
    testNumSpinBox->setRange(1, 999);
    testNumSpinBox->setValue(1);
    testNumSpinBox->setObjectName("testNumberSpinBox");
    paramForm->addRow("Test Number:", testNumSpinBox);
    
    QDoubleSpinBox *recordAzSpinBox = new QDoubleSpinBox();
    recordAzSpinBox->setRange(-180.0, 180.0);
    recordAzSpinBox->setValue(0.0);
    recordAzSpinBox->setDecimals(1);
    recordAzSpinBox->setObjectName("recordingAzimuthSpinBox");
    paramForm->addRow("Azimuth (°):", recordAzSpinBox);
    
    QDoubleSpinBox *recordElSpinBox = new QDoubleSpinBox();
    recordElSpinBox->setRange(-90.0, 90.0);
    recordElSpinBox->setValue(0.0);
    recordElSpinBox->setDecimals(1);
    recordElSpinBox->setObjectName("recordingElevationSpinBox");
    paramForm->addRow("Elevation (°):", recordElSpinBox);
    
    QLineEdit *saveDirEdit = new QLineEdit();
    saveDirEdit->setText("C:/Users/Cam/AX7010_Work/UltrasonicHost/recordings");
    saveDirEdit->setObjectName("saveDirLineEdit");
    QPushButton *browseDirButton = new QPushButton("Browse...");
    QHBoxLayout *dirLayout = new QHBoxLayout();
    dirLayout->addWidget(saveDirEdit);
    dirLayout->addWidget(browseDirButton);
    paramForm->addRow("Save Directory:", dirLayout);
    
    connect(browseDirButton, &QPushButton::clicked, this, [this, saveDirEdit]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Save Directory",
                                                         saveDirEdit->text());
        if (!dir.isEmpty()) {
            saveDirEdit->setText(dir);
        }
    });

    QCheckBox *applyDelaysCheckbox = new QCheckBox("Apply beamforming delays on save");
    applyDelaysCheckbox->setObjectName("applyDelaysOnSaveCheckbox");
    applyDelaysCheckbox->setChecked(true);
    applyDelaysCheckbox->setToolTip("When checked, integer delays from the current steering angle are applied to each mic channel before writing the .mat file.");
    paramForm->addRow("", applyDelaysCheckbox);

    recordingLayout->addLayout(paramForm);
    
    // Progress display
    QLabel *progressLabel = new QLabel("Ready to record");
    progressLabel->setObjectName("recordingProgressLabel");
    recordingLayout->addWidget(progressLabel);
    
    QProgressBar *progressBar = new QProgressBar();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setObjectName("recordingProgressBar");
    progressBar->setTextVisible(true);
    recordingLayout->addWidget(progressBar);
    
    // Control buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    QPushButton *startButton = new QPushButton("🔴 Start Recording (10 sec)");
    startButton->setObjectName("startRecordingButton");
    startButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 8px; }");
    buttonLayout->addWidget(startButton);
    
    QPushButton *stopButton = new QPushButton("⏹ Stop");
    stopButton->setObjectName("stopRecordingButton");
    stopButton->setEnabled(false);
    stopButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 8px; }");
    buttonLayout->addWidget(stopButton);
    
    connect(startButton, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::stopRecording);
    
    recordingLayout->addLayout(buttonLayout);
    
    // Place recording group into the container widget beside the log
    QWidget *recContainer = findChild<QWidget*>("recordingPanelContainer");
    if (recContainer) {
        QVBoxLayout *cl = new QVBoxLayout(recContainer);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->addWidget(recordingGroup);
    }
}


MainWindow::~MainWindow()
{
    // Stop and clean up worker threads
    if (m_beamformerThread) {
        m_beamformerThread->quit();
        m_beamformerThread->wait();
        delete m_beamformerWorker;
    }
    if (m_oscThread) {
        m_oscThread->quit();
        m_oscThread->wait();
        delete m_oscWorker;
    }

    if (m_audioOutput) {
        m_audioOutput->stop();
    }
    
    if (m_recorderThread) {
        m_recorderThread->quit();
        m_recorderThread->wait();
        delete m_dataRecorder;
    }
    
    delete m_micArray;
    delete m_beamCalc;
    delete ui;
}

void MainWindow::on_connectButton_clicked()
{
    if (!m_isConnected) {
        QHostAddress fpgaHost("192.168.1.10"); // FPGA board IP (confirmed)
        const quint16 dataPort    = 5000;
        const quint16 controlPort = 6000;

        // connect logic

        // control channel - create BEFORE data channel
        if (m_controlClient) {
            m_controlClient->disconnectFromBoard();
            m_controlClient->deleteLater();
            m_controlClient = nullptr;
        }

        m_pingPongBuffers[0].fill(0.0f);
        m_pingPongBuffers[1].fill(0.0f);
        m_activeBuffer = 0;
        m_workerBusy = false;

        m_controlClient = new TCPControl(fpgaHost, controlPort, this);

        connect(m_controlClient, &TCPControl::connected,
                this, [this]() {
                    ui->logTextEdit->appendPlainText(QString("Control channel connected at %1:%2").arg(m_controlClient->remoteAddress().toString()).arg(m_controlClient->remotePort()));
                });

        connect(m_controlClient, &TCPControl::ackReceived,
                this, [this](quint32 paramId, quint32 paramValue) {
                    // ui->ilogTextEdit->appendPlainText(QString("ACK: Parameter[%1] = 0x%2").arg(paramId).arg(paramValue, 8, 16, QChar('0')));  // REMOVED FOR PERFORMANCE
                });

        connect(m_controlClient, &TCPControl::errorOccurred,
                this, &MainWindow::handleError);

        // data channel - UDP
        if (m_dataProvider) {
            m_dataProvider->stop();
            m_dataProvider->deleteLater();
            m_dataProvider = nullptr;
        }

        auto *udp = new UDPDataProvider(dataPort, this);
        m_dataProvider = udp;

        connect(udp, &UDPDataProvider::bound,
                this, [this]() {
                    // ui->ilogTextEdit->appendPlainText(QString("UDP socket bound to port 5000"));  // REMOVED FOR PERFORMANCE
                    
                    // Once UDP socket is bound, connect control channel
                    if (m_controlClient) {
                        m_controlClient->connectToBoard();
                        // ui->ilogTextEdit->appendPlainText("Connecting to control channel (port 6000)...");  // REMOVED FOR PERFORMANCE
                    }
                });

        connect(udp, &IDataProvider::packetReceived,
                this, &MainWindow::handlePacketReceived);
        connect(udp, &IDataProvider::errorOccurred,
                this, &MainWindow::handleError);


        udp->start();

        m_hwOffsetInitialized = false;
        m_swMinusHwOffset = 0;
        m_lastHwFrame = 0;
        m_lastSwFrame = 0;

        // update UI state
        m_isConnected = true;
        ui->connectButton->setText("Disconnect from FPGA");
        ui->statusLabel->setText("Status: Connected (UDP)");

        // ui->ilogTextEdit->appendPlainText("UDP socket listening on port 5000");  // REMOVED FOR PERFORMANCE
    }
    else {
        // disconnect logic
        // ui->ilogTextEdit->appendPlainText("Disconnecting...");  // REMOVED FOR PERFORMANCE

        // Stop and cleanup data provider first
        if (m_dataProvider) {
            disconnect(m_dataProvider, nullptr, this, nullptr); // Disconnect all signals
            m_dataProvider->stop();
            m_dataProvider->deleteLater();
            m_dataProvider = nullptr;
        }

        // Stop and cleanup control client
        if (m_controlClient) {
            disconnect(m_controlClient, nullptr, this, nullptr); // Disconnect all signals
            m_controlClient->disconnectFromBoard();
            m_controlClient->deleteLater();
            m_controlClient = nullptr;
        }

        // update UI state
        m_isConnected = false;
        ui->connectButton->setText("Connect to FPGA");
        ui->statusLabel->setText("Status: Disconnected");

        // ui->ilogTextEdit->appendPlainText("Disconnected from FPGA.");  // REMOVED FOR PERFORMANCE
    }
}

// FPGA Control Functions
void MainWindow::on_samplingEnableCheckBox_toggled(bool checked)
{
    if (!m_controlClient) {
        // ui->ilogTextEdit->appendPlainText("Cannot send: control channel not connected.");  // REMOVED FOR PERFORMANCE
        return;
    }
    
    const quint32 paramId = 0x00000001;  // PID_SAMPLING_ENABLE
    const quint32 paramValue = checked ? 1 : 0;
    
    if (m_controlClient->sendParameter(paramId, paramValue)) {
        // ui->ilogTextEdit->appendPlainText(QString("Sampling %1").arg(checked ? "ENABLED" : "DISABLED"));  // REMOVED FOR PERFORMANCE
    } else {
        // ui->ilogTextEdit->appendPlainText("Failed to send sampling control.");  // REMOVED FOR PERFORMANCE
    }
}

void MainWindow::on_testEnableCheckBox_toggled(bool checked)
{
    if (!m_controlClient) {
        // ui->ilogTextEdit->appendPlainText("Cannot send: control channel not connected.");  // REMOVED FOR PERFORMANCE
        return;
    }
    
    const quint32 paramId = 0x00000002;  // PID_TEST_ENABLE
    const quint32 paramValue = checked ? 1 : 0;
    
    if (m_controlClient->sendParameter(paramId, paramValue)) {
        // ui->ilogTextEdit->appendPlainText(QString("Test pattern %1").arg(checked ? "ENABLED" : "DISABLED"));  // REMOVED FOR PERFORMANCE
    } else {
        // ui->ilogTextEdit->appendPlainText("Failed to send test enable control.");  // REMOVED FOR PERFORMANCE
    }
}

void MainWindow::on_simEnableCheckBox_toggled(bool checked)
{
    if (!m_controlClient) {
        // ui->ilogTextEdit->appendPlainText("Cannot send: control channel not connected.");  // REMOVED FOR PERFORMANCE
        return;
    }
    
    const quint32 paramId = 0x00000003;  // PID_SIM_ENABLE
    const quint32 paramValue = checked ? 1 : 0;
    
    if (m_controlClient->sendParameter(paramId, paramValue)) {
        // ui->ilogTextEdit->appendPlainText(QString("Simulation %1").arg(checked ? "ENABLED" : "DISABLED"));  // REMOVED FOR PERFORMANCE
    } else {
        // ui->ilogTextEdit->appendPlainText("Failed to send simulation control.");  // REMOVED FOR PERFORMANCE
    }
}

void MainWindow::on_simFreqSpinBox_valueChanged(int value)
{
    if (!m_controlClient) {
        return;  // Don't spam log when not connected
    }
    
    const quint32 paramId = 0x00000004;  // PID_SIM_FREQUENCY
    const quint32 paramValue = static_cast<quint32>(value);
    
    if (m_controlClient->sendParameter(paramId, paramValue)) {
        // ui->ilogTextEdit->appendPlainText(QString("Simulation frequency set to %1 kHz").arg(value));  // REMOVED FOR PERFORMANCE
    } else {
        // ui->ilogTextEdit->appendPlainText("Failed to send simulation frequency.");  // REMOVED FOR PERFORMANCE
    }
}

void MainWindow::handlePacketReceived(const QByteArray &raw)
{
    // Safety check - don't process packets if not connected
    if (!m_isConnected || !m_dataProvider) {
        return;
    }
    
    MicrophonePacket packet;
    if (!PacketDecoder::decode(raw, packet)) {
        return;
    }

    // Frame loss detection
    if (!m_hwOffsetInitialized) {
        m_swMinusHwOffset = qint64(packet.swFrameNumber) - qint64(packet.hwFrameNumber);
        m_hwOffsetInitialized = true;
    } else {
        const qint64 swDelta = qint64(packet.swFrameNumber) - qint64(m_lastSwFrame);
        const qint64 hwDelta = qint64(packet.hwFrameNumber) - qint64(m_lastHwFrame);
        const qint64 currentOffset = qint64(packet.swFrameNumber) - qint64(packet.hwFrameNumber);

        if (swDelta > 1) m_swFrameLossCount += swDelta - 1;
        if (hwDelta > 1) m_hwFrameLossCount += hwDelta - 1;

        if (currentOffset != m_swMinusHwOffset) {
            m_offsetChangesCount++;
            m_swMinusHwOffset = currentOffset;
        }
    }
    m_lastSwFrame = packet.swFrameNumber;
    m_lastHwFrame = packet.hwFrameNumber;


    // Track packet stats
    const qint64 packetBytes = 1094;  // Total packet size
    m_totalPackets++;
    m_intervalPackets++;
    m_totalBytes += packetBytes;
    m_intervalBytes += packetBytes;
    
    // Track frame progress (frame changes when swFrameNumber changes)
    if (packet.swFrameNumber != m_lastFrameNumber) {
        if (m_lastFrameNumber >= 0 && m_micsInCurrentFrame != 102) {
            m_intervalIncompleteFrames++;
        }
        m_totalFrames++;
        m_lastFrameNumber = packet.swFrameNumber;
        m_micsInCurrentFrame = 0;
    }
    m_micsInCurrentFrame++;

    //will ONLY fire if a mic was missing 
    if (!m_hasActiveFrame) {
        beginNewFrame(packet.swFrameNumber);
    } else if (packet.swFrameNumber != m_activeFrameNumber) {
        finalizeFrameIfComplete(false);
        beginNewFrame(packet.swFrameNumber);
    }

    
    if (!m_micSeen.testBit(packet.micIndex)) {
        m_micSeen.setBit(packet.micIndex, true);

        float* dst = m_pingPongBuffers[m_activeBuffer].data() + (packet.micIndex * SamplesPerPacket);
        std::copy(packet.samples.begin(), packet.samples.end(), dst);

        // Audio: play directly from packet for the selected mic — lowest latency, no frame wait.
        if (m_audioEnabled && m_audioOutput && m_audioOutput->isReady()
                && packet.micIndex == m_selectedMic) {
            QVector<qint16> out(SamplesPerPacket);
            for (int i = 0; i < SamplesPerPacket; ++i) {
                float s = packet.samples[i] * m_audioGain;
                if (s >  1.0f) s =  1.0f;
                if (s < -1.0f) s = -1.0f;
                out[i] = static_cast<qint16>(s * 32767.0f);
            }
            m_audioOutput->writeSamples(out);
        }

        // Track total samples for diagnostics (512 samples per packet)
        m_totalSamplesReceived += SamplesPerPacket;
    } else {
        ui->logTextEdit->appendPlainText("Duplicate mic packet received");
    }

    // if 102/102 completed, push into block
    if (m_micSeen.count(true) == NumMics) {
        finalizeFrameIfComplete(false);
    }
    

    // Only update oscilloscope with raw mic waveform when beamforming is OFF.
    // When beamforming is ON, the heatmap is fed from onScanComplete instead.
    if (!m_beamformingEnabled && packet.micIndex == m_selectedMic) {
        if (packet.swFrameNumber != m_currentOscFrameNumber) {
            m_currentOscFrameNumber = packet.swFrameNumber;
            emit newOscFrame(packet.samples);
        }
    }
    
    // Feed samples to data recorder if recording (queued to recorder thread)
    if (m_dataRecorder && m_recorderThread && m_dataRecorder->isRecording()) {
        emit recordSamples(packet.micIndex, packet.samples);
    }
}

void MainWindow::beginNewFrame(quint32 frameNumber)
{
    m_hasActiveFrame = true;
    m_activeFrameNumber = frameNumber;
    m_micSeen.fill(false);
}

//Called either if skipped mic or not
void MainWindow::finalizeFrameIfComplete(bool forceDrop)
{
    if (!m_hasActiveFrame) return;

    const int seenCount = m_micSeen.count(true);
    const bool complete = (seenCount == NumMics);

    // forced drop
    if (forceDrop) {
        // ui->logTextEdit->appendPlainText(QString("Dropping FPGA frame %1 (forced drop)").arg(m_activeFrameNumber));  // REMOVED FOR PERFORMANCE
        m_hasActiveFrame = false;
        return;
    }

    // With ping-pong, unseen slots contain data from 2 frames ago (stale).
    // Copy missing mics from the other buffer, which holds the previous frame's data.
    if (!complete) {
        const QVector<float>& prevFrame = m_pingPongBuffers[1 - m_activeBuffer];
        QVector<float>& curFrame = m_pingPongBuffers[m_activeBuffer];
        for (int mic = 0; mic < NumMics; ++mic) {
            if (!m_micSeen.testBit(mic)) {
                const float* src = prevFrame.constData() + mic * SamplesPerPacket;
                float* dst = curFrame.data() + mic * SamplesPerPacket;
                std::copy(src, src + SamplesPerPacket, dst);
            }
        }
        ui->logTextEdit->appendPlainText(
            QString("Frame %1: %2/%3 mics received, %4 missing (filled from prev frame)")
                .arg(m_activeFrameNumber).arg(seenCount).arg(NumMics).arg(NumMics - seenCount));
    }

    m_activeBuffer ^= 1;

    // Append the just-finalized frame into the active scan buffer (mic-major layout).
    // Each mic's column occupies [mic * samplesPerMic .. (mic+1)*samplesPerMic);
    // within that column, frame f occupies [f*SamplesPerPacket .. (f+1)*SamplesPerPacket).
    if (m_beamformingEnabled && m_framesSinceLastScan < m_scanIntervalFrames) {
        const QVector<float>& src = m_pingPongBuffers[1 - m_activeBuffer];
        const int samplesPerMic   = m_scanIntervalFrames * SamplesPerPacket;
        for (int mic = 0; mic < NumMics; ++mic) {
            const float* s = src.constData() + mic * SamplesPerPacket;
            float*       d = m_scanBuffers[m_activeScanBuffer].data()
                             + mic * samplesPerMic
                             + m_framesSinceLastScan * SamplesPerPacket;
            std::copy(s, s + SamplesPerPacket, d);
        }
    }

    if (m_beamformingEnabled) {
        ++m_framesSinceLastScan;
        if (m_framesSinceLastScan >= m_scanIntervalFrames) {
            if (m_workerBusy) {
                ui->logTextEdit->appendPlainText(
                    "⚠️ Beamformer overrun: worker still busy, restarting accumulation");
                m_framesSinceLastScan = 0;
                // m_activeScanBuffer stays the same — worker owns the other slot
            } else {
                m_workerBusy = true;
                m_framesSinceLastScan = 0;
                emit frameReady(&m_scanBuffers[m_activeScanBuffer]);
                m_activeScanBuffer ^= 1;
            }
        }
    }

    m_hasActiveFrame = false;
}

void MainWindow::onScanComplete(QVector<float> /*powerGrid*/, int /*nx*/, int /*ny*/)
{
    m_workerBusy = false;
}

void MainWindow::recomputeDelays()
{
    if (!m_geometryLoaded) {
        // ui->ilogTextEdit->appendPlainText("Cannot recompute delays: geometry not loaded.");  // REMOVED FOR PERFORMANCE
        return;
    }

    // Read user inputs
    const double phiDeg   = ui->azimuthDoubleSpinBox->value();    // azimuth in degrees
    const double thetaDeg = ui->elevationDoubleSpinBox->value();  // elevation in degrees
    const double tempC    = ui->temperatureDoubleSpinBox->value();// temperature in degrees celsius

    // Use BeamformingCalculator to compute delays
    m_beamCalc->computeDelays(phiDeg, thetaDeg, tempC);

    sendFractionalDelays();
}

void MainWindow::sendFractionalDelays()
{
    if (!m_controlClient){
        // ui->ilogTextEdit->appendPlainText("Cannot send fractional delays: control channel not connected.");  // REMOVED FOR PERFORMANCE
        return;
    }

    const QVector<int>& nFrac = m_beamCalc->fractionalDelays();

    // Pack delays into Control Vector format (0x100-0x10F)
    // Each 32-bit word packs multiple 4-bit fractional delays
    // For simplicity, pack 8 delays per word (8 * 4-bit = 32-bit)
    const int delaysPerWord = 8;
    const int numWords = (NumMics + delaysPerWord - 1) / delaysPerWord;  // Ceil division
    
    for (int wordIdx = 0; wordIdx < numWords && wordIdx < 16; ++wordIdx) {
        quint32 packedValue = 0;
        
        // Pack up to 8 delays into this word
        for (int bit = 0; bit < delaysPerWord; ++bit) {
            int micIdx = wordIdx * delaysPerWord + bit;
            if (micIdx < NumMics) {
                quint32 delay4bit = static_cast<quint32>(nFrac[micIdx] & 0x0F);
                packedValue |= (delay4bit << (bit * 4));  // Each delay uses 4 bits
            }
        }
        
        // Send to Control Vector (PID 0x100 + wordIdx)
        const quint32 paramId = 0x00000100 + wordIdx;
        const bool ok = m_controlClient->sendParameter(paramId, packedValue);
        
        if (!ok) {
            // ui->ilogTextEdit->appendPlainText(QString("Failed to send control vector[%1]").arg(wordIdx));  // REMOVED FOR PERFORMANCE
            return;
        }
    }

    // ui->ilogTextEdit->appendPlainText(QString("Sent %1 fractional delays via %2 control vector words").arg(NumMics).arg(numWords));  // REMOVED FOR PERFORMANCE
}

void MainWindow::handleStatusMessage(const QString &msg)
{
    ui->statusLabel->setText(msg);
    // ui->ilogTextEdit->appendPlainText("[INFO] " + msg);  // REMOVED FOR PERFORMANCE
}

void MainWindow::handleError(const QString &message)
{
    ui->statusLabel->setText("Error");
    // ui->ilogTextEdit->appendPlainText("ERROR: " + message);  // REMOVED FOR PERFORMANCE
}

void MainWindow::on_micComboBox_currentIndexChanged(int index)
{
    // index is the combo box row, itemData holds the mic index stored
    int mic = ui->micComboBox->itemData(index).toInt();
    m_selectedMic = static_cast<quint16>(mic);
    m_currentOscFrameNumber = 0;  // reset so next packet from new mic triggers display

    // Clear oscilloscope only when showing waveform (heatmap is direction-independent, keep it)
    if (!m_beamformingEnabled)
        ui->oscilloscopeWidget->setImage(QImage());
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->oscilloscopeWidget && event->type() == QEvent::Resize) {
        const int width = ui->oscilloscopeWidget->width();
        const int height = ui->oscilloscopeWidget->height();
        const int right = width - 10;
        ui->micComboBox->move(right - ui->micComboBox->width(), 5);
        QSpinBox *fpsSpin = ui->oscilloscopeWidget->findChild<QSpinBox*>("oscFpsSpinBox");
        if (fpsSpin)
            fpsSpin->move(right - fpsSpin->width(), 5 + ui->micComboBox->height() + 2);
        QWidget *scanW = ui->oscilloscopeWidget->findChild<QWidget*>("scanIntervalWidget");
        if (scanW)
            scanW->move(5, height - scanW->height() - 5);
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleAudio(bool enable)
{
    m_audioEnabled = enable;
    
    if (enable) {
        m_workerBusy = false;

        if (!m_audioOutput->initialize()) {
            ui->logTextEdit->appendPlainText("❌ Audio initialization failed");
            m_audioEnabled = false;
            return;
        }
        if (!m_audioOutput->start()) {
            ui->logTextEdit->appendPlainText("❌ Audio start failed");
            m_audioEnabled = false;
            return;
        }
        ui->logTextEdit->appendPlainText("✅ Audio output ENABLED");
    } else {
        m_audioOutput->stop();
        ui->logTextEdit->appendPlainText("🔇 Audio output DISABLED");
    }
}

void MainWindow::toggleBeamforming(bool enable)
{
    m_beamformingEnabled = enable;
    m_workerBusy = false;  // reset in case worker was mid-scan during toggle

    if (enable) {
        m_framesSinceLastScan = 0;  // start counting from now
        ui->logTextEdit->appendPlainText("🎯 Beamforming ENABLED - Playing beamformed audio");
        if (!m_geometryLoaded) {
            ui->logTextEdit->appendPlainText("⚠️ WARNING: Geometry not loaded! No audio will play.");
        }
    } else {
        ui->logTextEdit->appendPlainText("🎤 Beamforming DISABLED - Playing selected microphone");
    }
}


void MainWindow::startRecording()
{
    if (!m_isConnected) {
        ui->logTextEdit->appendPlainText("⚠️ Cannot record: Not connected to FPGA!");
        return;
    }
    
    if (m_dataRecorder->isRecording()) {
        ui->logTextEdit->appendPlainText("⚠️ Already recording!");
        return;
    }
    
    // Get parameters from UI
    QSpinBox *testNumSpinBox = findChild<QSpinBox*>("testNumberSpinBox");
    QDoubleSpinBox *azSpinBox = findChild<QDoubleSpinBox*>("recordingAzimuthSpinBox");
    QDoubleSpinBox *elSpinBox = findChild<QDoubleSpinBox*>("recordingElevationSpinBox");
    QLineEdit *saveDirEdit = findChild<QLineEdit*>("saveDirLineEdit");
    QPushButton *startButton = findChild<QPushButton*>("startRecordingButton");
    QPushButton *stopButton = findChild<QPushButton*>("stopRecordingButton");
    QLabel *progressLabel = findChild<QLabel*>("recordingProgressLabel");
    
    int testNumber = testNumSpinBox ? testNumSpinBox->value() : 1;
    double azimuth = azSpinBox ? azSpinBox->value() : 0.0;
    double elevation = elSpinBox ? elSpinBox->value() : 0.0;
    QString saveDir = saveDirEdit ? saveDirEdit->text() : ".";
    
    // Create directory if it doesn't exist
    QDir dir(saveDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            ui->logTextEdit->appendPlainText(QString("❌ Failed to create directory: %1").arg(saveDir));
            return;
        }
    }
    
    // Update UI
    if (startButton) startButton->setEnabled(false);
    if (stopButton) stopButton->setEnabled(true);
    if (progressLabel) progressLabel->setText("Recording... 0.0 / 10.0 sec");
    
    // Decide whether to apply delays based on the UI checkbox
    QCheckBox *applyDelaysCb = findChild<QCheckBox*>("applyDelaysOnSaveCheckbox");
    const bool applyDelays = applyDelaysCb ? applyDelaysCb->isChecked() : false;

    if (applyDelays && m_beamCalc && m_geometryLoaded) {
        const QVector<int>& integerDelays = m_beamCalc->integerDelays();
        QMetaObject::invokeMethod(m_dataRecorder, "setIntegerDelays",
                                  Qt::QueuedConnection,
                                  Q_ARG(QVector<int>, integerDelays));
        ui->logTextEdit->appendPlainText(QString("✓ Integer delays will be applied on save (az=%1° el=%2°)")
            .arg(azimuth).arg(elevation));
    } else {
        QMetaObject::invokeMethod(m_dataRecorder, "setApplyDelays",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, false));
        if (!applyDelays)
            ui->logTextEdit->appendPlainText(QString("ℹ️ Saving raw (undelayed) data - delays disabled by user"));
        else
            ui->logTextEdit->appendPlainText(QString("⚠️ No geometry loaded - saving raw data without delays"));
    }
    
    // Start recording (queued to recorder thread)
    QMetaObject::invokeMethod(m_dataRecorder, "startRecording",
                              Qt::QueuedConnection,
                              Q_ARG(double, 10.0),
                              Q_ARG(double, azimuth),
                              Q_ARG(double, elevation),
                              Q_ARG(QString, saveDir),
                              Q_ARG(int, testNumber));
    
    ui->logTextEdit->appendPlainText(
        QString("\n🔴 RECORDING STARTED\n"
                "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                "Test Number: %1\n"
                "Azimuth: %2°\n"
                "Elevation: %3°\n"
                "Duration: 10.0 seconds\n"
                "Save Directory: %4\n"
                "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
        .arg(testNumber).arg(azimuth).arg(elevation).arg(saveDir)
    );
}

void MainWindow::stopRecording()
{
    QMetaObject::invokeMethod(m_dataRecorder, "stopRecording", Qt::QueuedConnection);
    
    QPushButton *startButton = findChild<QPushButton*>("startRecordingButton");
    QPushButton *stopButton = findChild<QPushButton*>("stopRecordingButton");
    QLabel *progressLabel = findChild<QLabel*>("recordingProgressLabel");
    
    if (startButton) startButton->setEnabled(true);
    if (stopButton) stopButton->setEnabled(false);
    if (progressLabel) progressLabel->setText("Recording stopped");
    
    ui->logTextEdit->appendPlainText("⏹ Recording stopped manually");
}

void MainWindow::handleRecordingComplete(const QString &filename)
{
    QPushButton *startButton = findChild<QPushButton*>("startRecordingButton");
    QPushButton *stopButton = findChild<QPushButton*>("stopRecordingButton");
    QLabel *progressLabel = findChild<QLabel*>("recordingProgressLabel");
    QProgressBar *progressBar = findChild<QProgressBar*>("recordingProgressBar");
    QSpinBox *testNumSpinBox = findChild<QSpinBox*>("testNumberSpinBox");
    
    if (startButton) startButton->setEnabled(true);
    if (stopButton) stopButton->setEnabled(false);
    if (progressLabel) progressLabel->setText("Recording complete!");
    if (progressBar) progressBar->setValue(100);
    
    // Auto-increment test number
    if (testNumSpinBox) {
        testNumSpinBox->setValue(testNumSpinBox->value() + 1);
    }
    
    ui->logTextEdit->appendPlainText(
        QString("\n✅ RECORDING COMPLETE!\n"
                "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
                "File saved: %1\n"
                "Total samples: %2\n"
                "Ready for next test...\n"
                "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
        .arg(filename)
        .arg(m_dataRecorder->totalSamplesCollected())
    );
    
    // Reset progress bar after 2 seconds
    QTimer::singleShot(2000, this, [progressBar, progressLabel]() {
        if (progressBar) progressBar->setValue(0);
        if (progressLabel) progressLabel->setText("Ready to record");
    });
}

void MainWindow::handleRecordingError(const QString &error)
{
    QPushButton *startButton = findChild<QPushButton*>("startRecordingButton");
    QPushButton *stopButton = findChild<QPushButton*>("stopRecordingButton");
    QLabel *progressLabel = findChild<QLabel*>("recordingProgressLabel");
    
    if (startButton) startButton->setEnabled(true);
    if (stopButton) stopButton->setEnabled(false);
    if (progressLabel) progressLabel->setText("Recording failed!");
    
    ui->logTextEdit->appendPlainText(QString("❌ RECORDING ERROR: %1").arg(error));
}

void MainWindow::updateRecordingProgress(double progress, double elapsedSec)
{
    QProgressBar *progressBar = findChild<QProgressBar*>("recordingProgressBar");
    QLabel *progressLabel = findChild<QLabel*>("recordingProgressLabel");
    
    if (progressBar) {
        progressBar->setValue(static_cast<int>(progress * 100.0));
    }
    
    if (progressLabel) {
        progressLabel->setText(QString("Recording... %1 / 10.0 sec (%2%)")
                               .arg(elapsedSec, 0, 'f', 1)
                               .arg(static_cast<int>(progress * 100.0)));
    }
}

bool MainWindow::loadGeometry(const QString &path)
{
    // Use MicrophoneArray to load geometry
    if (!m_micArray->loadFromExcel(path)) {
        ui->logTextEdit->appendPlainText(QString("❌ Geometry load failed: %1").arg(path));
        return false;
    }

    // Verify we loaded the correct number of microphones
    if (m_micArray->count() != NumMics) {
        ui->logTextEdit->appendPlainText(QString("❌ Geometry load failed: expected %1 microphones, got %2").arg(NumMics).arg(m_micArray->count()));
        return false;
    }

    // Pass the loaded array to the beamforming calculator
    m_beamCalc->setMicrophoneArray(*m_micArray);

    // Pass mic positions to the beamformer worker for the grid scan
    if (m_beamformerWorker)
        m_beamformerWorker->setMicPositions(m_micArray->xPositions(), m_micArray->yPositions());

    m_geometryLoaded = true;
    ui->logTextEdit->appendPlainText(QString("✅ Geometry loaded: %1 microphones from %2").arg(m_micArray->count()).arg(path));

    return true;
}

