#ifndef BEAMFORMINGCALCULATOR_H
#define BEAMFORMINGCALCULATOR_H

#include <QVector>
#include "MicrophoneArray.h"

class BeamformingCalculator
{
public:
    BeamformingCalculator();
    
    // Set microphone array geometry
    void setMicrophoneArray(const MicrophoneArray& array);
    
    // Compute delays for given steering angles and temperature
    // azimuth: degrees (0-360), elevation: degrees (0-90), temperature: Celsius
    void computeDelays(double azimuthDeg, double elevationDeg, double temperatureC);
    
    // Get computed integer delays (0-127 samples)
    const QVector<int>& integerDelays() const { return m_kDelay; }
    
    // Get computed fractional delays (0-15, representing 0-15/16 of a sample)
    const QVector<int>& fractionalDelays() const { return m_nFrac; }
    
    // Get raw time delays in seconds (for debugging)
    const QVector<double>& timeDelays() const { return m_timeDelays; }

private:
    MicrophoneArray m_array;
    QVector<int> m_kDelay;      // Integer sample delays
    QVector<int> m_nFrac;       // Fractional delays (0-15)
    QVector<double> m_timeDelays; // Raw time delays in seconds
    double m_sampleRate;
};

#endif // BEAMFORMINGCALCULATOR_H
