#include "DataRecorder.h"
#include "MatFileWriter.h"
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <cmath>
#include <algorithm>  // For std::copy - PERFORMANCE

DataRecorder::DataRecorder(QObject *parent)
    : QObject(parent)
{
    m_matWriter = new MatFileWriter();
    m_samplesPerMic.resize(NumMics);
}

DataRecorder::~DataRecorder()
{
    delete m_matWriter;
}

void DataRecorder::startRecording(double durationSeconds, double azimuth, double elevation,
                                  const QString &outputPath, int testNumber)
{
    if (m_recording) {
        qWarning() << "Already recording!";
        return;
    }
    
    m_recording = true;
    m_azimuth = azimuth;
    m_elevation = elevation;
    m_outputPath = outputPath;
    m_testNumber = testNumber;
    
    // Calculate expected samples
    // Collect extra samples equal to the max integer delay so that after alignment
    // the output still has exactly ceil(durationSeconds * 48000) samples per mic.
    m_targetSamplesPerMic = static_cast<int>(std::ceil(durationSeconds * 48000.0));
    m_maxDelay = 0;
    if (m_applyDelays && !m_integerDelays.isEmpty()) {
        m_maxDelay = *std::max_element(m_integerDelays.begin(), m_integerDelays.end());
        m_targetSamplesPerMic += m_maxDelay;
    }
    m_expectedSamples = NumMics * m_targetSamplesPerMic;
    
    // Pre-allocate buffer
    m_recordingBuffer.resize(m_expectedSamples);
    m_recordingBuffer.fill(0.0f);
    
    // Reset counters
    m_totalSamplesCollected = 0;
    m_samplesPerMic.fill(0);
    m_packetCheckCounter = 0;
    m_micsComplete = 0;
    
    qDebug() << "[RECORDER] Started recording:"
             << "Duration:" << durationSeconds << "sec"
             << "| Az:" << azimuth << "° El:" << elevation << "°"
             << "| Expected samples:" << m_expectedSamples
             << "| Target per mic:" << m_targetSamplesPerMic
             << "| Integer delays:" << (m_applyDelays ? "YES" : "NO");
}

void DataRecorder::setIntegerDelays(const QVector<int> &integerDelays)
{
    if (integerDelays.size() == NumMics) {
        m_integerDelays = integerDelays;
        m_applyDelays = true;
        qDebug() << "[RECORDER] Integer delays set for" << NumMics << "mics";
    } else {
        m_applyDelays = false;
        qWarning() << "[RECORDER] Invalid delay array size:" << integerDelays.size();
    }
}

void DataRecorder::setApplyDelays(bool apply)
{
    m_applyDelays = apply;
    qDebug() << "[RECORDER] Apply delays on save:" << (apply ? "YES" : "NO");
}

void DataRecorder::stopRecording()
{
    if (!m_recording)
        return;
    
    m_recording = false;

    qDebug() << "[RECORDER] Recording stopped manually"
             << "| Collected:" << m_totalSamplesCollected
             << "/ Expected:" << m_expectedSamples;
    
    // Save what we have
    if (m_totalSamplesCollected > 0) {
        QString filename = saveRecording();
        if (!filename.isEmpty()) {
            emit recordingComplete(filename);
        } else {
            emit recordingError("Failed to save recording");
        }
    }
}

void DataRecorder::addSamples(int micIndex, const QVector<float> &samples)
{
    const int depth = ++m_addSamplesDepth;
    if (depth > 1) {
        qWarning() << "[RECORDER] addSamples OVERLAP: called while still processing "
                      "previous packet (depth=" << depth << ", mic=" << micIndex << ") "
                      "– worker was still busy recording last samples!";
    }

    auto guard = qScopeGuard([this]{ --m_addSamplesDepth; });

    if (!m_recording) {
        return;
    }
    
    // Check if this mic has already collected enough samples
    if (m_samplesPerMic[micIndex] >= m_targetSamplesPerMic) {
        // This mic is done, ignore additional packets
        return;
    }
    
    // Calculate how many samples to copy (might be less than 512 if near end)
    int samplesNeeded = m_targetSamplesPerMic - m_samplesPerMic[micIndex];
    int samplesToCopy = qMin(SamplesPerPacket, samplesNeeded);
    
    // PERFORMANCE: Fast copy using std::copy (optimized to memcpy internally)
    // Buffer layout: [mic0_sample0, mic0_sample1, ..., mic1_sample0, ...]
    int baseIndex = micIndex * m_targetSamplesPerMic + m_samplesPerMic[micIndex];
    
    // Fast copy: std::copy is optimized for contiguous memory (much faster than for-loop)
    std::copy(samples.constData(), 
              samples.constData() + samplesToCopy, 
              m_recordingBuffer.data() + baseIndex);
    
    m_samplesPerMic[micIndex] += samplesToCopy;
    m_totalSamplesCollected += samplesToCopy;

    // Check if this mic just finished collecting its required samples
    if (m_samplesPerMic[micIndex] >= m_targetSamplesPerMic) {
        if (++m_micsComplete == NumMics) {
            m_recording = false;
            double elapsed = static_cast<double>(m_totalSamplesCollected) / (NumMics * 48000.0);
            emit progressUpdated(1.0, elapsed);
            QString filename = saveRecording();
            if (!filename.isEmpty())
                emit recordingComplete(filename);
            else
                emit recordingError(m_matWriter->lastError());
            return;
        }
    }

    // Emit progress periodically
    if (++m_packetCheckCounter >= 50) {
        m_packetCheckCounter = 0;
        double elapsed = static_cast<double>(m_totalSamplesCollected) / (NumMics * 48000.0);
        emit progressUpdated(progress(), elapsed);
    }
}

double DataRecorder::progress() const
{
    if (m_expectedSamples == 0) {
        return 0.0;
    }
    return static_cast<double>(m_totalSamplesCollected) / static_cast<double>(m_expectedSamples);
}


QString DataRecorder::saveRecording()
{
    QString filename = generateFilename();
    
    qDebug() << "[RECORDER] Saving to:" << filename;
    qDebug() << "[RECORDER] Data size:" << m_recordingBuffer.size() << "samples"
             << "(" << (m_recordingBuffer.size() * sizeof(float) / (1024.0 * 1024.0)) << "MB)";
    
    // Apply integer delays if configured (FPGA fractional delays already applied)
    QVector<float> dataToSave;
    int samplesPerMic = m_targetSamplesPerMic;
    
    if (m_applyDelays) {
        qDebug() << "[RECORDER] Applying integer delays before saving...";
        dataToSave = applyDelaysToRecording();
        samplesPerMic = m_targetSamplesPerMic - m_maxDelay;
        qDebug() << "[RECORDER] Applied delays. New samples per mic:" << samplesPerMic;
    } else {
        qDebug() << "[RECORDER] Saving WITHOUT integer delays (will need MATLAB processing)";
        dataToSave = m_recordingBuffer;
    }
    
    bool success = m_matWriter->writeRecording(
        filename,
        dataToSave,
        NumMics,
        samplesPerMic,
        m_azimuth,
        m_elevation,
        48000.0
    );
    
    if (!success) {
        qWarning() << "[RECORDER] Save failed:" << m_matWriter->lastError();
        return QString();
    }
    qDebug() << "[RECORDER] Save successful:" << filename;
    return filename;
}

QVector<float> DataRecorder::applyDelaysToRecording()
{
    // Apply integer delays to each microphone channel
    // This matches the beamformer logic: channels with larger delays read from earlier samples
    // Input: m_recordingBuffer with layout [mic0_samples..., mic1_samples..., ...]
    // Output: Time-aligned buffer (all channels aligned to the same wavefront arrival time)
    
    // Find max delay to determine output length
    int maxDelay = m_maxDelay;
    int outputSamplesPerMic = m_targetSamplesPerMic - maxDelay;
    
    if (outputSamplesPerMic <= 0) {
        qWarning() << "[RECORDER] Max delay too large!" << maxDelay;
        return m_recordingBuffer;
    }
    
    // Allocate delayed buffer
    QVector<float> delayedBuffer(NumMics * outputSamplesPerMic, 0.0f);
    
    // Apply delay to each mic (matching beamformer circular buffer logic)
    // For alignment: mic with maxDelay reads from sample[0], others offset forward
    for (int mic = 0; mic < NumMics; ++mic) {
        int delay = m_integerDelays[mic];
        
        // IMPORTANT: To match beamformer, we need to OFFSET by (maxDelay - delay)
        // - Mic with maxDelay: reads from sample[0] (newest wavefront arrival)
        // - Mic with smaller delay: reads from sample[maxDelay - delay] (older data)
        // This aligns all channels to the same wavefront
        int offset = maxDelay - delay;
        
        // Source: mic's samples starting at offset
        const float* src = m_recordingBuffer.constData() + (mic * m_targetSamplesPerMic) + offset;
        
        // Destination: mic's section in delayed buffer
        float* dst = delayedBuffer.data() + (mic * outputSamplesPerMic);
        
        // Copy time-aligned samples
        std::copy(src, src + outputSamplesPerMic, dst);
    }
    
    return delayedBuffer;
}

QString DataRecorder::generateFilename()
{
    // Format: ultrasonic_test_001_az045_el030_20260216_143052.mat
    QDateTime now = QDateTime::currentDateTime();
    
    QString azStr = QString("%1%2")
                    .arg(m_azimuth >= 0 ? "+" : "")
                    .arg(static_cast<int>(m_azimuth), 3, 10, QChar('0'));
    
    QString elStr = QString("%1%2")
                    .arg(m_elevation >= 0 ? "+" : "")
                    .arg(static_cast<int>(m_elevation), 3, 10, QChar('0'));
    
    QString filename = QString("ultrasonic_test_%1_az%2_el%3_%4.mat")
                       .arg(m_testNumber, 3, 10, QChar('0'))
                       .arg(azStr)
                       .arg(elStr)
                       .arg(now.toString("yyyyMMdd_HHmmss"));
    
    // Combine with output path
    QDir dir(m_outputPath);
    return dir.absoluteFilePath(filename);
}
