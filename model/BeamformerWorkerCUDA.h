#ifndef BEAMFORMERWORKERCUDA_H
#define BEAMFORMERWORKERCUDA_H

#include <QObject>
#include <QVector>
#include <QMutex>

// CUDA-accelerated beamformer. Drop-in replacement for BeamformerWorker:
// same thread model, same signals, same setMicPositions/setSpeedOfSound/setGridRes API.
//
// Each grid point (vx, vy) maps to one GPU thread.  The audio frame buffer
// is uploaded once per scan; the power grid is downloaded once at the end.
// If no CUDA device is available the constructor sets m_cudaAvailable=false
// and processBlock() falls back to the CPU path.
class BeamformerWorkerCUDA : public QObject
{
    Q_OBJECT

public:
    explicit BeamformerWorkerCUDA(QObject *parent = nullptr);
    ~BeamformerWorkerCUDA();

    bool cudaAvailable() const { return m_cudaAvailable; }

    void setMicPositions(const QVector<double>& xMm, const QVector<double>& yMm);
    void setSpeedOfSound(float speedOfSound);
    void setGridRes(float gridRes);

public slots:
    void processBlock(const QVector<float>* frameBuffer);

signals:
    void scanComplete(QVector<float> powerGrid, int nx, int ny);
    void logMessage(const QString &msg);  // connect to logTextEdit->appendPlainText

private:
    static constexpr float kFs               = 48000.0f;
    static constexpr int   kNumMics          = 102;
    static constexpr int   kSamplesPerPacket = 512;

    struct MicPosScaled { float sx; float sy; };

    void recomputePendingLocked();
    void freeCudaBuffers();

    // Host-side geometry (mutex-protected)
    QMutex                m_mutex;
    QVector<double>       m_rawXMm;
    QVector<double>       m_rawYMm;
    float                 m_speedOfSound = 343.0f;
    float                 m_gridRes      = 0.1f;
    QVector<MicPosScaled> m_pendingMicPos;
    int                   m_pendingMaxAbsDelay = 0;
    bool                  m_geometryDirty      = false;

    // Worker-thread active geometry (no lock needed in processBlock)
    QVector<MicPosScaled> m_micPosScaled;
    int                   m_maxAbsDelay = 0;

    // CUDA device buffers  (void* so the header stays CUDA-free)
    void*  m_d_data       = nullptr;   // float* — audio frame on device
    void*  m_d_micPos     = nullptr;   // float2* — scaled mic positions
    void*  m_d_vList      = nullptr;   // float*  — grid values
    void*  m_d_powerGrid  = nullptr;   // float*  — output power grid
    int    m_d_dataSize   = 0;         // allocated floats in m_d_data
    int    m_d_gridSize   = 0;         // allocated floats in m_d_powerGrid / m_d_vList

    bool   m_cudaAvailable = false;
    QString m_initLog;  // deferred until first processBlock when signal is connected
};

#endif // BEAMFORMERWORKERCUDA_H
