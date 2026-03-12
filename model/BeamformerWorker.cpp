#include "BeamformerWorker.h" // model/BeamformerWorker.h
#include <cmath>
#include <algorithm>

BeamformerWorker::BeamformerWorker(QObject *parent)
    : QObject(parent)
{}

void BeamformerWorker::setMicPositions(const QVector<double>& xMm,
                                        const QVector<double>& yMm)
{
    QMutexLocker lock(&m_mutex);
    m_rawXMm = xMm;
    m_rawYMm = yMm;
    recomputePendingLocked();
}

void BeamformerWorker::setSpeedOfSound(float speedOfSound)
{
    QMutexLocker lock(&m_mutex);
    m_speedOfSound = speedOfSound;
    recomputePendingLocked();
}

void BeamformerWorker::setGridRes(float gridRes)
{
    QMutexLocker lock(&m_mutex);
    m_gridRes = gridRes;
}

void BeamformerWorker::recomputePendingLocked()
{
    const int n = std::min(m_rawXMm.size(), m_rawYMm.size());
    QVector<MicPosScaled> pos(n);
    float maxNorm = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float sx = static_cast<float>(m_rawXMm[i] / 1000.0 * kFs / m_speedOfSound);
        const float sy = static_cast<float>(m_rawYMm[i] / 1000.0 * kFs / m_speedOfSound);
        pos[i] = {sx, sy};
        const float norm = std::sqrt(sx * sx + sy * sy);
        if (norm > maxNorm) maxNorm = norm;
    }
    m_pendingMicPos      = std::move(pos);
    m_pendingMaxAbsDelay = static_cast<int>(std::ceil(maxNorm));
    m_geometryDirty      = true;
}

void BeamformerWorker::processBlock(const QVector<float>* frameBuffer)
{
    // Pull pending geometry onto the worker thread if it changed.
    // This is the only copy — happens at most once per geometry load, not per scan.
    float gridRes = 0.1f;
    {
        QMutexLocker lock(&m_mutex);
        if (m_geometryDirty) {
            m_micPosScaled = m_pendingMicPos;   // one-time copy
            m_maxAbsDelay  = m_pendingMaxAbsDelay;
            m_geometryDirty = false;
        }
        gridRes = m_gridRes;
    }

    if (!frameBuffer || frameBuffer->size() < kNumMics * kSamplesPerPacket) {
        emit scanComplete({}, 0, 0);
        return;
    }

    const float* data       = frameBuffer->constData();
    // Derive how many samples each mic contributed — works for any accumulation window.
    const int numSamples    = frameBuffer->size() / kNumMics;
    const int safeStart  = m_maxAbsDelay;
    const int safeLen    = numSamples - 2 * m_maxAbsDelay;
    if (safeLen <= 0) {
        emit scanComplete({}, 0, 0);
        return;
    }

    // Build vList: -1.0, -1.0+gridRes, ..., +1.0
    QVector<float> vList;
    for (float v = -1.0f; v <= 1.0f + 1e-5f; v += gridRes)
        vList.append(v);
    const int nGrid = vList.size();

    QVector<float> powerGrid(nGrid * nGrid, 0.0f);

    int offsets[kNumMics];

    for (int ix = 0; ix < nGrid; ++ix) {
        const float vx = vList[ix];
        for (int iy = 0; iy < nGrid; ++iy) {
            const float vy = vList[iy];
            if (vx * vx + vy * vy > 1.0f)
                continue;

            for (int mic = 0; mic < kNumMics; ++mic) {
                const float proj = m_micPosScaled[mic].sx * vx + m_micPosScaled[mic].sy * vy;
                offsets[mic] = mic * numSamples + safeStart - static_cast<int>(std::round(proj));
            }

            float power = 0.0f;
            for (int k = 0; k < safeLen; ++k) {
                float s = 0.0f;
                for (int mic = 0; mic < kNumMics; ++mic)
                    s += data[offsets[mic] + k];
                power += s * s;
            }

            powerGrid[ix * nGrid + iy] = power;
        }
    }

    emit scanComplete(powerGrid, nGrid, nGrid);
}
