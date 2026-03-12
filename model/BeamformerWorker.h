#ifndef BEAMFORMERWORKER_H
#define BEAMFORMERWORKER_H

#include <QObject>
#include <QVector>
#include <QMutex>

// BeamformerWorker — ports beamforming.m to C++.
// Lives on its own thread. Given a full [numMics x SamplesPerPacket] frame buffer
// it sweeps a steering-vector grid over the unit circle and emits a power heatmap.
//
// Grid: vx, vy in {-1.0, -0.9, ..., +1.0} (gridRes=0.1 -> 21x21=441 points, ~314 valid).
// Per direction: per-mic integer sample delays are applied via direct index into
// safeRange (avoids circshift wrap-around, matches MATLAB fix).
class BeamformerWorker : public QObject
{
    Q_OBJECT

public:
    explicit BeamformerWorker(QObject *parent = nullptr);

    // Thread-safe. Positions in mm; array lies in xy-plane (z=0 for all mics).
    void setMicPositions(const QVector<double>& xMm, const QVector<double>& yMm);

    // Thread-safe. Updates speed of sound (m/s) and recomputes scaled mic positions.
    // Call whenever the spinbox value changes.
    void setSpeedOfSound(float speedOfSound);

    // Thread-safe. Sets the beamforming grid resolution (step size in normalised units).
    void setGridRes(float gridRes);

public slots:
    // Accepts raw ping-pong pointer — main thread guarantees buffer is not
    // overwritten while m_workerBusy is true.
    void processBlock(const QVector<float>* frameBuffer);

signals:
    // powerGrid is row-major [ix * nGrid + iy].
    // ix=0 -> vx=-1, ix=nGrid-1 -> vx=+1.
    // iy=0 -> vy=-1, iy=nGrid-1 -> vy=+1.
    // Points outside the unit circle have power == 0.
    void scanComplete(QVector<float> powerGrid, int nx, int ny);

private:
    static constexpr float kFs               = 48000.0f;
    static constexpr int   kNumMics          = 102;
    static constexpr int   kSamplesPerPacket = 512;

    struct MicPosScaled { float sx; float sy; };

    // Recomputes pending scaled positions from stored raw mm + current speed of sound.
    // Must be called with m_mutex held.
    void recomputePendingLocked();

    // Pending geometry — written by main thread, protected by m_mutex.
    QMutex                m_mutex;
    QVector<double>       m_rawXMm;             // raw mic X positions (mm)
    QVector<double>       m_rawYMm;             // raw mic Y positions (mm)
    float                 m_speedOfSound = 343.0f;
    float                 m_gridRes      = 0.1f;
    QVector<MicPosScaled> m_pendingMicPos;
    int                   m_pendingMaxAbsDelay = 0;
    bool                  m_geometryDirty      = false;

    // Active geometry — owned exclusively by the worker thread.
    QVector<MicPosScaled> m_micPosScaled;
    int                   m_maxAbsDelay = 0;
};

#endif // BEAMFORMERWORKER_H
