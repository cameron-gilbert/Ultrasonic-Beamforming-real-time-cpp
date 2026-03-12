#include "BeamformingCalculator.h"
#include <QtMath>
#include <algorithm>

BeamformingCalculator::BeamformingCalculator()
    : m_sampleRate(48000.0)
{
}

void BeamformingCalculator::setMicrophoneArray(const MicrophoneArray& array)
{
    m_array = array;
    int numMics = array.count();
    m_kDelay.resize(numMics);
    m_nFrac.resize(numMics);
    m_timeDelays.resize(numMics);
}

void BeamformingCalculator::computeDelays(double azimuthDeg, double elevationDeg, double temperatureC)
{
    int numMics = m_array.count();
    if (numMics == 0) return;

    // Calculate speed of sound (m/s)
    double speedOfSound = 331.3 + 0.606 * temperatureC;

    // Convert angles to radians
    double azimuthRad = qDegreesToRadians(azimuthDeg);
    double elevationRad = qDegreesToRadians(elevationDeg);

    // Array lies in the xy plane. Coordinate system:
    //   x+ = right, y+ = up, z+ = forward (array pointing direction)
    // Azimuth φ: rotation around y-axis, 0° = straight ahead, +ve = right
    // Elevation θ: rotation above xy plane, 0° = horizontal, +ve = up
    //
    // Steering direction projected onto the array plane (z drops out, mic z=0):
    //   dirX = cos(el) * sin(az)   — right component
    //   dirY = sin(el)             — up component
    
    double dirX = qCos(elevationRad) * qSin(azimuthRad);
    double dirY = qSin(elevationRad);

    const QVector<double>& xMm = m_array.xPositions();
    const QVector<double>& yMm = m_array.yPositions();

    // Calculate time delays for each microphone
    QVector<double> delays(numMics);
    for (int i = 0; i < numMics; ++i) {
        // Convert mm to meters
        double xM = xMm[i] / 1000.0;
        double yM = yMm[i] / 1000.0;

        // Time delay = (position dot direction) / speed_of_sound
        delays[i] = (xM * dirX + yM * dirY) / speedOfSound;
    }

    // Normalize delays so minimum is zero
    double minDelay = *std::min_element(delays.begin(), delays.end());
    for (int i = 0; i < numMics; ++i) {
        delays[i] -= minDelay;
    }

    // Convert to sample delays and decompose into integer + fractional parts
    for (int i = 0; i < numMics; ++i) {
        m_timeDelays[i] = delays[i];
        
        // Total delay in samples
        double delaySamples = delays[i] * m_sampleRate;
        
        // Integer part (k_i)
        m_kDelay[i] = static_cast<int>(delaySamples);
        
        // Fractional part (n_i) - quantize to 1/16 sample resolution
        double fraction = delaySamples - m_kDelay[i];
        m_nFrac[i] = static_cast<int>(fraction * 16.0);
        
        // Clamp to valid ranges
        if (m_kDelay[i] > 127) m_kDelay[i] = 127;
        if (m_kDelay[i] < 0) m_kDelay[i] = 0;
        if (m_nFrac[i] > 15) m_nFrac[i] = 15;
        if (m_nFrac[i] < 0) m_nFrac[i] = 0;
    }
}
