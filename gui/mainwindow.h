#pragma once

#include <QMainWindow>
#include <QVector>
#include <QtGlobal>
#include <QElapsedTimer>
#include <QBitArray>
#include <../model/MicrophonePacket.h>
#include <QString>
#include <memory>
#include <QThread>
#include <QMutex>
#include "OscilloscopeWorker.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class IDataProvider;
class TCPControl;
class BeamformerWorker;
class BeamformerWorkerCUDA;
class AudioOutput;
class MicrophoneArray;
class BeamformingCalculator;
class DataRecorder;
class OscilloscopeWorker;

static constexpr int NumMics = 102;
static constexpr int SamplesPerPacket = MicrophonePacket::SampleCount;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void on_connectButton_clicked();

    // FPGA Control slots
    void on_samplingEnableCheckBox_toggled(bool checked);
    void on_testEnableCheckBox_toggled(bool checked);
    void on_simEnableCheckBox_toggled(bool checked);
    void on_simFreqSpinBox_valueChanged(int value);
    
    void handleStatusMessage(const QString &msg);

    void handlePacketReceived(const QByteArray &raw);
    void handleError(const QString &message);

    void on_micComboBox_currentIndexChanged(int index);
    
    // Audio and beamforming control
    void toggleAudio(bool enable);
    void toggleBeamforming(bool enable);

    
    // Recording control
    void startRecording();
    void stopRecording();
    void handleRecordingComplete(const QString &filename);
    void handleRecordingError(const QString &error);
    void updateRecordingProgress(double progress, double elapsedSec);

    void onScanComplete(QVector<float> powerGrid, int nx, int ny);

signals:
    void frameReady(const QVector<float>* frameBuffer);
    void newOscFrame(QVector<float> samples);

    void recordSamples(int micIndex, QVector<float> samples);  // queued → recorder thread

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    // void applyModernStyle();  // REMOVED for performance
    
    bool loadGeometry(const QString &path);


    Ui::MainWindow *ui;
    bool m_isConnected = false;
    IDataProvider *m_dataProvider = nullptr;
    TCPControl *m_controlClient = nullptr;
    
    // Beamforming components
    MicrophoneArray *m_micArray = nullptr;
    BeamformingCalculator *m_beamCalc = nullptr;
    BeamformerWorker     *m_beamformerWorker     = nullptr;
    BeamformerWorkerCUDA *m_beamformerWorkerCUDA = nullptr;
    bool                  m_useCuda              = false;
    QThread *m_beamformerThread = nullptr;
    OscilloscopeWorker *m_oscWorker = nullptr;
    QThread *m_oscThread = nullptr;
    AudioOutput *m_audioOutput = nullptr;
    QMutex m_audioMutex;  // Protect audio output access from worker thread
    bool m_audioEnabled = false;
    
    // Recording components
    DataRecorder *m_dataRecorder = nullptr;
    QThread *m_recorderThread = nullptr;

    quint16 m_selectedMic = 0;
    // Scan-level ping-pong: each buffer accumulates m_scanIntervalFrames complete frames.
    // Layout is mic-major: mic i's samples occupy [i * samplesPerMic .. (i+1)*samplesPerMic).
    // Sized in constructor and resized when the interval spinbox changes.
    QVector<float> m_scanBuffers[2];
    int m_activeScanBuffer = 0;
    
    // Single frame display for oscilloscope (tracks last seen frame number only)
    quint32 m_currentOscFrameNumber = 0;

    // 5-second interval stats (matching PowerShell diagnostics)
    QElapsedTimer m_sessionTimer;
    QElapsedTimer m_intervalTimer;
    qint64 m_totalPackets = 0;
    qint64 m_totalFrames = 0;
    qint64 m_totalBytes = 0;
    qint64 m_intervalPackets = 0;
    qint64 m_intervalBytes = 0;
    qint64 m_intervalIncompleteFrames = 0;
    qint32 m_lastFrameNumber = -1;
    qint32 m_micsInCurrentFrame = 0;
    
    // Frame loss detection counters
    qint64 m_swFrameLossCount = 0;
    qint64 m_hwFrameLossCount = 0;
    qint64 m_offsetChangesCount = 0;
    
    qint64 m_audioSamplesWritten = 0;
    int m_audioBlocksProcessed = 0;
    
    qint64 m_totalSamplesReceived = 0;  // Total samples across all mics

    bool m_geometryLoaded = false;

    //hw frame tracking
    bool   m_hwOffsetInitialized = false;
    qint64 m_swMinusHwOffset = 0;
    quint32 m_lastHwFrame = 0;
    quint32 m_lastSwFrame = 0;
    
    // UI update throttling (beamformed path only — raw mic path uses OscilloscopeWorker)

    bool m_beamformingEnabled = false;  // When true: oscilloscope shows heatmap; audio always raw mic
    int m_audioGain = 500;  // Audio amplification (real ±8 → ±4000 to match simulation level)
    void recomputeDelays();

    void sendFractionalDelays();


    bool   m_hasActiveFrame = false;
    quint32 m_activeFrameNumber = 0;
    QBitArray m_micSeen { NumMics };
    // Ping-pong buffers: main thread writes into m_activeBuffer,
    // worker thread reads the other. Swap on finalization — zero copy.
    QVector<float> m_pingPongBuffers[2] = {
        QVector<float>(NumMics * SamplesPerPacket, 0.0f),
        QVector<float>(NumMics * SamplesPerPacket, 0.0f)
    };
    int m_activeBuffer = 0;  // index of the buffer the main thread is currently filling
    bool m_workerBusy = false;  // true while worker thread is processing a frame

    // frames between scans (default 5 s at 93.75 fps: round(5 * 48000/512) = 469)
    int m_scanIntervalFrames = 1;
    int m_framesSinceLastScan = 0;

    void beginNewFrame(quint32 frameNumber);
    void finalizeFrameIfComplete(bool forceDrop);
};


