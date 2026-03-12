#pragma once

#include <QObject>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QString>

/**
 * @brief Manages audio output to PC speakers
 * 
 * Takes beamformed audio samples and plays them through the system
 * audio device in real-time.
 */
class AudioOutput : public QObject
{
    Q_OBJECT
    
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();
    
    /**
     * @brief Initialize audio output with 48 kHz, 16-bit mono
     * @return true if initialization succeeded
     */
    bool initialize();
    
    /**
     * @brief Start audio playback
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop audio playback
     */
    void stop();
    
    /**
     * @brief Write audio samples to output buffer
     * @param samples Audio samples to play
     * @return Number of samples actually written
     */
    qint64 writeSamples(const QVector<qint16> &samples);
    
    /**
     * @brief Check if audio output is ready to accept more data
     * @return true if ready
     */
    bool isReady() const;
    
    /**
     * @brief Get current volume (0.0 to 1.0)
     * @return Volume level
     */
    qreal getVolume() const;

    /**
     * @brief Get compact audio sink status for diagnostics
     * @return Formatted state/error/buffer summary
     */
    QString debugStatus() const;
    
    /**
     * @brief Set volume (0.0 to 1.0)
     * @param volume Volume level
     */
    void setVolume(qreal volume);
    
signals:
    void errorOccurred(const QString &error);
    void stateChanged(QAudio::State state);
    
private:
    QAudioFormat m_format;
    QAudioSink *m_audioOutput;
    QIODevice *m_outputDevice;
    
    /**
     * @brief Configure audio format (48 kHz, 16-bit, mono)
     */
    void setupFormat();
};
