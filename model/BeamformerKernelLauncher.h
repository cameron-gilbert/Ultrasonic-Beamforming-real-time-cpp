#ifndef BEAMFORMERKERNELLAUNCHER_H
#define BEAMFORMERKERNELLAUNCHER_H

// Plain-C interface between the CUDA kernel file (.cu, compiled by nvcc) and
// the Qt wrapper file (.cpp, compiled by MSVC).  No CUDA or Qt headers here.

#include <stddef.h>  // size_t

#ifdef __cplusplus
extern "C" {
#endif

// Returns number of CUDA-capable devices on this machine (0 if none).
int  cuda_deviceCount(void);

// Writes a human-readable device description into buf (null-terminated).
void cuda_deviceName(int device, char* buf, int bufLen);

// Allocates device memory.  Returns true on success.
bool cuda_malloc(void** ptr, size_t bytes);

// Frees device memory (safe to call with nullptr).
void cuda_free_buffer(void* ptr);

// Zeroes device memory.  Returns true on success.
bool cuda_memset_zero(void* ptr, size_t bytes);

// Uploads count floats from host src to device dst.
bool cuda_uploadFloats(void* dst, const float* src, int count);

// Uploads count (x,y) pairs from interleaved host src to device dst.
// src must contain 2*count floats laid out as [x0,y0, x1,y1, ...].
bool cuda_uploadFloat2Pairs(void* dst, const float* src, int count);

// Downloads count floats from device src to host dst.
bool cuda_downloadFloats(float* dst, const void* src, int count);

// Launches the beamforming kernel and waits for completion.
// Returns true on success; on failure writes a message into errBuf.
bool cuda_launchBeamform(
    const void* d_data,
    const void* d_micPos,
    const void* d_vList,
    void*       d_powerGrid,
    int nGrid, int numSamples, int safeStart, int safeLen, int numMics,
    char* errBuf, int errBufLen);

#ifdef __cplusplus
}
#endif

#endif // BEAMFORMERKERNELLAUNCHER_H
