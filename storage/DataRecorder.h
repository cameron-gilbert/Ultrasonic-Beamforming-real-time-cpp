#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <atomic>

class MatFileWriter;

class DataRecorder : public QObject
{
    Q_OBJECT

public:
    explicit DataRecorder(QObject *parent = nullptr);
    ~DataRecorder();

    // Thread-safe read — uses atomic flag
    bool isRecording() const { return m_recording.load(std::memory_order_relaxed); }

    double progress() const;
    qint64 totalSamplesCollected() const { return m_totalSamplesCollected; }

public slots:
    void startRecording(double durationSeconds, double azimuth, double elevation,
                        const QString &outputPath, int testNumber);
    void setIntegerDelays(const QVector<int> &integerDelays);
    // Enable or disable delay application without clearing the stored delays.
    void setApplyDelays(bool apply);
    void stopRecording();
    void addSamples(int micIndex, const QVector<float> &samples);

signals:
    void recordingComplete(const QString &filename);
    void recordingError(const QString &error);
    void progressUpdated(double progress, double elapsedSec);

private:
    QString saveRecording();
    QString generateFilename();
    QVector<float> applyDelaysToRecording();

    static constexpr int NumMics = 102;
    static constexpr int SamplesPerPacket = 512;

    std::atomic<bool> m_recording{false};
    std::atomic<int>  m_addSamplesDepth{0};  // >1 means re-entrant / concurrent overlap
    double m_azimuth = 0.0;
    double m_elevation = 0.0;
    QString m_outputPath;
    int m_testNumber = 0;

    QVector<float> m_recordingBuffer;
    qint64 m_expectedSamples = 0;
    qint64 m_totalSamplesCollected = 0;

    QVector<int> m_samplesPerMic;
    int m_targetSamplesPerMic = 0;

    MatFileWriter *m_matWriter = nullptr;
    int m_packetCheckCounter = 0;
    int m_micsComplete = 0;

    QVector<int> m_integerDelays;
    bool m_applyDelays = false;
    int m_maxDelay = 0;
};
