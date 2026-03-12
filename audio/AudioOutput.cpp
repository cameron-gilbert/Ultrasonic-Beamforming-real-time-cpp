#include "AudioOutput.h"
#include <QAudioDevice>
#include <QMediaDevices>
// #include <QDebug> // REMOVED FOR PERFORMANCE

static const char* audioStateName(QAudio::State state)
{
    switch (state) {
    case QAudio::ActiveState: return "Active";
    case QAudio::SuspendedState: return "Suspended";
    case QAudio::StoppedState: return "Stopped";
    case QAudio::IdleState: return "Idle";
    }
    return "Unknown";
}

static const char* audioErrorName(QAudio::Error error)
{
    switch (error) {
    case QAudio::NoError: return "NoError";
    case QAudio::OpenError: return "OpenError";
    case QAudio::IOError: return "IOError";
    case QAudio::UnderrunError: return "UnderrunError";
    case QAudio::FatalError: return "FatalError";
    }
    return "Unknown";
}

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
    , m_audioOutput(nullptr)
    , m_outputDevice(nullptr)
{
    setupFormat();
}

AudioOutput::~AudioOutput()
{
    stop();
}

void AudioOutput::setupFormat()
{
    m_format.setSampleRate(48000);
    m_format.setChannelCount(1);  // Mono
    m_format.setSampleFormat(QAudioFormat::Int16);
    
    // REMOVED FOR PERFORMANCE: qDebug() << "AudioOutput: Format configured - 48kHz, 16-bit, Mono";
}

bool AudioOutput::initialize()
{
    // Already initialized — nothing to do
    if (m_audioOutput)
        return true;

    // Get default audio output device
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    
    if (device.isNull()) {
        emit errorOccurred("No audio output device available");
        return false;
    }
    
    // Create audio output
    m_audioOutput = new QAudioSink(device, m_format, this);
    
    // Keep latency low, but the buffer must hold at least one full FPGA frame.
    // One beamformed block at X=1 is 512 samples = 1024 bytes at 48 kHz/16-bit mono.
    // A 2 KB buffer comfortably holds one block plus scheduler jitter on Windows.
    m_audioOutput->setBufferSize(2048);
    
    // Connect state change signal
    connect(m_audioOutput, &QAudioSink::stateChanged,
            this, &AudioOutput::stateChanged);
    
    return true;
}

bool AudioOutput::start()
{
    if (!m_audioOutput) {
        // REMOVED FOR PERFORMANCE: qDebug() << "AudioOutput::start() - Not initialized";
        return false;
    }
    
    // Start the audio output and get the IO device
    m_outputDevice = m_audioOutput->start();
    
    if (!m_outputDevice) {
        emit errorOccurred("Failed to start audio output");
        return false;
    }
    
    // Prime the audio buffer with silence to start playback immediately
    // Write 100ms of silence (4800 samples) to avoid initial delay
    QVector<qint16> silence(4800, 0);
    m_outputDevice->write(reinterpret_cast<const char*>(silence.constData()), silence.size() * sizeof(qint16));
    
    // REMOVED FOR PERFORMANCE: qDebug() << "AudioOutput: Started";
    return true;
}

void AudioOutput::stop()
{
    if (m_audioOutput) {
        m_audioOutput->stop();
        m_outputDevice = nullptr;
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
}

qint64 AudioOutput::writeSamples(const QVector<qint16> &samples)
{
    if (!m_outputDevice || !m_outputDevice->isWritable()) {
        return 0;
    }
    
    // Convert samples to byte array
    const char *data = reinterpret_cast<const char*>(samples.constData());
    qint64 bytesToWrite = samples.size() * sizeof(qint16);
    
    // Write to output device
    qint64 bytesWritten = m_outputDevice->write(data, bytesToWrite);
    
    // Return number of samples written
    return bytesWritten / sizeof(qint16);
}

bool AudioOutput::isReady() const
{
    if (!m_audioOutput) {
        return false;
    }
    
    // Accept Active or Idle state — Idle means buffer drained (underrun) but sink
    // is still running and will resume playback as soon as data is written.
    // Do NOT gate on bytesFree() > 0: on Windows WASAPI, bytesFree() can return 0
    // even in IdleState, which would silently block audio on the beamforming path.
    return (m_audioOutput->state() == QAudio::ActiveState ||
            m_audioOutput->state() == QAudio::IdleState);
}

qreal AudioOutput::getVolume() const
{
    if (m_audioOutput) {
        return m_audioOutput->volume();
    }
    return 1.0;
}

QString AudioOutput::debugStatus() const
{
    if (!m_audioOutput) {
        return QStringLiteral("audio=null");
    }

    return QStringLiteral("state=%1 error=%2 bytesFree=%3 bufferSize=%4 writable=%5 volume=%6")
        .arg(audioStateName(m_audioOutput->state()))
        .arg(audioErrorName(m_audioOutput->error()))
        .arg(m_audioOutput->bytesFree())
        .arg(m_audioOutput->bufferSize())
        .arg(m_outputDevice && m_outputDevice->isWritable() ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(m_audioOutput->volume(), 0, 'f', 2);
}

void AudioOutput::setVolume(qreal volume)
{
    // Clamp volume to valid range
    volume = qBound(0.0, volume, 1.0);
    
    if (m_audioOutput) {
        m_audioOutput->setVolume(volume);
        // REMOVED FOR PERFORMANCE: qDebug() << "AudioOutput: Volume set to" << volume;
    }
}
