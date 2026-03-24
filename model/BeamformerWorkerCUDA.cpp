// BeamformerWorkerCUDA.cpp
// QObject implementation of the CUDA beamformer.
// Compiled by MSVC (not nvcc), so Qt headers are safe here.
// CUDA operations are performed via the plain-C launchers in
// BeamformerKernelLauncher.h (defined in BeamformerWorkerCUDA.cu).

#include "BeamformerWorkerCUDA.h"
#include "BeamformerKernelLauncher.h"

#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// Constructor / destructor
// ──────────────────────────────────────────────────────────────────────────────

BeamformerWorkerCUDA::BeamformerWorkerCUDA(QObject *parent)
    : QObject(parent)
{
    const int deviceCount = cuda_deviceCount();
    if (deviceCount == 0) {
        m_initLog = "[CUDA] No CUDA device found — falling back to CPU";
        return;
    }
    char nameBuf[256] = {};
    cuda_deviceName(0, nameBuf, sizeof(nameBuf));
    m_initLog = QString("[CUDA] Device 0: %1").arg(nameBuf);
    m_cudaAvailable = true;
}

BeamformerWorkerCUDA::~BeamformerWorkerCUDA()
{
    freeCudaBuffers();
}

// ──────────────────────────────────────────────────────────────────────────────
// Public setters (may be called from any thread — protected by m_mutex)
// ──────────────────────────────────────────────────────────────────────────────

void BeamformerWorkerCUDA::setMicPositions(const QVector<double>& xMm,
                                            const QVector<double>& yMm)
{
    QMutexLocker lock(&m_mutex);
    m_rawXMm = xMm;
    m_rawYMm = yMm;
    recomputePendingLocked();
}

void BeamformerWorkerCUDA::setSpeedOfSound(float speedOfSound)
{
    QMutexLocker lock(&m_mutex);
    m_speedOfSound = speedOfSound;
    recomputePendingLocked();
}

void BeamformerWorkerCUDA::setGridRes(float gridRes)
{
    QMutexLocker lock(&m_mutex);
    m_gridRes = gridRes;
}

// ──────────────────────────────────────────────────────────────────────────────
// Private helpers
// ──────────────────────────────────────────────────────────────────────────────

void BeamformerWorkerCUDA::recomputePendingLocked()
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

void BeamformerWorkerCUDA::freeCudaBuffers()
{
    cuda_free_buffer(m_d_data);      m_d_data      = nullptr;
    cuda_free_buffer(m_d_micPos);    m_d_micPos    = nullptr;
    cuda_free_buffer(m_d_vList);     m_d_vList     = nullptr;
    cuda_free_buffer(m_d_powerGrid); m_d_powerGrid = nullptr;
    m_d_dataSize = 0;
    m_d_gridSize = 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// processBlock — called on the beamformer worker thread
// ──────────────────────────────────────────────────────────────────────────────

void BeamformerWorkerCUDA::processBlock(const QVector<float>* frameBuffer)
{
    // Emit deferred init message on first call (signal is now connected)
    if (!m_initLog.isEmpty()) {
        emit logMessage(m_initLog);
        m_initLog.clear();
    }

    // Pull pending geometry if dirty
    float gridRes = 0.1f;
    {
        QMutexLocker lock(&m_mutex);
        if (m_geometryDirty) {
            m_micPosScaled  = m_pendingMicPos;
            m_maxAbsDelay   = m_pendingMaxAbsDelay;
            m_geometryDirty = false;

            // Upload mic positions to device
            if (m_cudaAvailable) {
                cuda_free_buffer(m_d_micPos);
                m_d_micPos = nullptr;
                const int numMics = m_micPosScaled.size();
                if (cuda_malloc(&m_d_micPos, (size_t)numMics * 2 * sizeof(float))) {
                    // MicPosScaled is {float sx, float sy} — same layout as float2
                    if (cuda_uploadFloat2Pairs(m_d_micPos,
                            reinterpret_cast<const float*>(m_micPosScaled.constData()),
                            numMics)) {
                        emit logMessage(QString("[CUDA] Mic geometry uploaded: %1 mics, maxDelay=%2 samples")
                            .arg(numMics).arg(m_maxAbsDelay));
                    }
                }
            }
        }
        gridRes = m_gridRes;
    }

    if (!frameBuffer || frameBuffer->size() < kNumMics * kSamplesPerPacket) {
        emit scanComplete({}, 0, 0);
        return;
    }

    const int numSamples = frameBuffer->size() / kNumMics;
    const int safeStart  = m_maxAbsDelay;
    const int safeLen    = numSamples - 2 * m_maxAbsDelay;
    if (safeLen <= 0) {
        emit scanComplete({}, 0, 0);
        return;
    }

    // Build vList on host
    QVector<float> vList;
    for (float v = -1.0f; v <= 1.0f + 1e-5f; v += gridRes)
        vList.append(v);
    const int nGrid = vList.size();

    // ── CPU fallback if CUDA not available ──────────────────────────────────
    if (!m_cudaAvailable || !m_d_micPos) {
        emit logMessage(QString("[CUDA] CPU fallback: %1")
            .arg(m_cudaAvailable ? "mic geometry not yet uploaded" : "no CUDA device"));
        const float* data = frameBuffer->constData();
        int offsets[kNumMics];
        QVector<float> powerGrid(nGrid * nGrid, 0.0f);
        for (int ix = 0; ix < nGrid; ++ix) {
            const float vx = vList[ix];
            for (int iy = 0; iy < nGrid; ++iy) {
                const float vy = vList[iy];
                if (vx * vx + vy * vy > 1.0f) continue;
                for (int mic = 0; mic < static_cast<int>(m_micPosScaled.size()); ++mic) {
                    const float proj =
                        m_micPosScaled[mic].sx * vx + m_micPosScaled[mic].sy * vy;
                    offsets[mic] = mic * numSamples + safeStart
                                   - static_cast<int>(std::round(proj));
                }
                float power = 0.0f;
                for (int k = 0; k < safeLen; ++k) {
                    float s = 0.0f;
                    for (int mic = 0; mic < static_cast<int>(m_micPosScaled.size()); ++mic)
                        s += data[offsets[mic] + k];
                    power += s * s;
                }
                powerGrid[ix * nGrid + iy] = power;
            }
        }
        emit scanComplete(powerGrid, nGrid, nGrid);
        return;
    }

    // ── CUDA path ────────────────────────────────────────────────────────────

    const int dataFloats = frameBuffer->size();

    // Resize data buffer if needed
    if (dataFloats > m_d_dataSize) {
        cuda_free_buffer(m_d_data);
        m_d_data = nullptr;
        if (!cuda_malloc(&m_d_data, (size_t)dataFloats * sizeof(float))) {
            emit logMessage("[CUDA] Failed to allocate data buffer");
            emit scanComplete({}, 0, 0);
            return;
        }
        m_d_dataSize = dataFloats;
    }

    // Resize grid buffers if needed
    const int gridFloats = nGrid * nGrid;
    if (gridFloats > m_d_gridSize || nGrid > m_d_gridSize) {
        cuda_free_buffer(m_d_powerGrid); m_d_powerGrid = nullptr;
        cuda_free_buffer(m_d_vList);     m_d_vList     = nullptr;
        if (!cuda_malloc(&m_d_powerGrid, (size_t)gridFloats * sizeof(float)) ||
            !cuda_malloc(&m_d_vList,     (size_t)nGrid      * sizeof(float))) {
            emit logMessage("[CUDA] Failed to allocate grid buffers");
            emit scanComplete({}, 0, 0);
            return;
        }
        m_d_gridSize = std::max(gridFloats, nGrid);
    }

    // Upload audio data and vList
    cuda_uploadFloats(m_d_data,  frameBuffer->constData(), dataFloats);
    cuda_uploadFloats(m_d_vList, vList.constData(),        nGrid);
    cuda_memset_zero(m_d_powerGrid, (size_t)gridFloats * sizeof(float));

    // Launch kernel
    char errBuf[256] = {};
    const bool ok = cuda_launchBeamform(
        m_d_data, m_d_micPos, m_d_vList, m_d_powerGrid,
        nGrid, numSamples, safeStart, safeLen, kNumMics,
        errBuf, sizeof(errBuf));
    if (!ok) {
        emit logMessage(QString("[CUDA] Kernel error: %1").arg(errBuf));
        emit scanComplete({}, 0, 0);
        return;
    }

    // Download result
    QVector<float> powerGrid(gridFloats);
    cuda_downloadFloats(powerGrid.data(), m_d_powerGrid, gridFloats);

    emit scanComplete(powerGrid, nGrid, nGrid);
}
