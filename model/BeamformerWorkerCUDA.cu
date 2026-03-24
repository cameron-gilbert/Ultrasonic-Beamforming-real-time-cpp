// BeamformerWorkerCUDA.cu
// CUDA kernel ONLY — no Qt headers.
// nvcc cannot handle Qt's C++17 fold expressions, so all QObject code lives in
// BeamformerWorkerCUDA.cpp which calls the plain-C launchers in
// BeamformerKernelLauncher.h.

#include "BeamformerKernelLauncher.h"
#include <cuda_runtime.h>
#include <cstdio>

// ──────────────────────────────────────────────────────────────────────────────
// Device kernel
// One thread per grid point (ix, iy).
// Each thread sums safeLen beamformed samples and computes total power.
// ──────────────────────────────────────────────────────────────────────────────
__global__ void beamformKernel(
    const float* __restrict__ data,        // [kNumMics * numSamples] — mic-major
    const float2* __restrict__ micPos,     // [kNumMics] scaled positions (samples)
    const float* __restrict__ vList,       // [nGrid]
    float* __restrict__       powerGrid,   // [nGrid * nGrid] output
    int nGrid,
    int numSamples,
    int safeStart,
    int safeLen,
    int numMics)
{
    const int ix = blockIdx.x * blockDim.x + threadIdx.x;
    const int iy = blockIdx.y * blockDim.y + threadIdx.y;
    if (ix >= nGrid || iy >= nGrid) return;

    const float vx = vList[ix];
    const float vy = vList[iy];
    if (vx * vx + vy * vy > 1.0f) {
        powerGrid[ix * nGrid + iy] = 0.0f;
        return;
    }

    // Pre-compute integer offsets for each mic into the flat data array.
    // Stack-allocate — numMics is compile-time constant 102.
    int offsets[102];
    for (int mic = 0; mic < numMics; ++mic) {
        const float proj = micPos[mic].x * vx + micPos[mic].y * vy;
        offsets[mic] = mic * numSamples + safeStart - __float2int_rn(proj);
    }

    float power = 0.0f;
    for (int k = 0; k < safeLen; ++k) {
        float s = 0.0f;
        for (int mic = 0; mic < numMics; ++mic)
            s += data[offsets[mic] + k];
        power += s * s;
    }

    powerGrid[ix * nGrid + iy] = power;
}

// ──────────────────────────────────────────────────────────────────────────────
// Internal helper
// ──────────────────────────────────────────────────────────────────────────────
static bool checkCuda(cudaError_t err, const char* ctx, char* errBuf, int errBufLen)
{
    if (err != cudaSuccess) {
        if (errBuf && errBufLen > 0)
            snprintf(errBuf, errBufLen, "%s: %s", ctx, cudaGetErrorString(err));
        return false;
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Plain-C launchers (called from BeamformerWorkerCUDA.cpp via BeamformerKernelLauncher.h)
// ──────────────────────────────────────────────────────────────────────────────
extern "C" {

int cuda_deviceCount()
{
    int n = 0;
    cudaGetDeviceCount(&n);
    return n;
}

void cuda_deviceName(int device, char* buf, int bufLen)
{
    cudaDeviceProp p;
    cudaGetDeviceProperties(&p, device);
    snprintf(buf, bufLen, "%s (Compute %d.%d, %d MB)",
             p.name, p.major, p.minor,
             (int)(p.totalGlobalMem / (1024*1024)));
}

bool cuda_malloc(void** ptr, size_t bytes)
{
    return checkCuda(cudaMalloc(ptr, bytes), "cudaMalloc", nullptr, 0);
}

void cuda_free_buffer(void* ptr)
{
    if (ptr) cudaFree(ptr);
}

bool cuda_memset_zero(void* ptr, size_t bytes)
{
    return checkCuda(cudaMemset(ptr, 0, bytes), "cudaMemset", nullptr, 0);
}

bool cuda_uploadFloats(void* dst, const float* src, int count)
{
    return checkCuda(cudaMemcpy(dst, src, (size_t)count * sizeof(float),
                                cudaMemcpyHostToDevice), "uploadFloats", nullptr, 0);
}

bool cuda_uploadFloat2Pairs(void* dst, const float* src, int count)
{
    // src is interleaved [x0,y0, x1,y1, ...] — matches float2 memory layout
    return checkCuda(cudaMemcpy(dst, src, (size_t)count * 2 * sizeof(float),
                                cudaMemcpyHostToDevice), "uploadFloat2Pairs", nullptr, 0);
}

bool cuda_downloadFloats(float* dst, const void* src, int count)
{
    return checkCuda(cudaMemcpy(dst, src, (size_t)count * sizeof(float),
                                cudaMemcpyDeviceToHost), "downloadFloats", nullptr, 0);
}

bool cuda_launchBeamform(
    const void* d_data, const void* d_micPos, const void* d_vList,
    void* d_powerGrid,
    int nGrid, int numSamples, int safeStart, int safeLen, int numMics,
    char* errBuf, int errBufLen)
{
    const dim3 block(16, 16);
    const dim3 grid((nGrid + 15) / 16, (nGrid + 15) / 16);

    beamformKernel<<<grid, block>>>(
        static_cast<const float*>(d_data),
        static_cast<const float2*>(d_micPos),
        static_cast<const float*>(d_vList),
        static_cast<float*>(d_powerGrid),
        nGrid, numSamples, safeStart, safeLen, numMics);

    return checkCuda(cudaGetLastError(),    "kernel", errBuf, errBufLen) &&
           checkCuda(cudaDeviceSynchronize(), "sync",   errBuf, errBufLen);
}

} // extern "C"
