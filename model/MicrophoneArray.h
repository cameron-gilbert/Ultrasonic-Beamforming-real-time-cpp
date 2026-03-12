#ifndef MICROPHONEARRAY_H
#define MICROPHONEARRAY_H

#include <QVector>
#include <QString>

class MicrophoneArray
{
public:
    MicrophoneArray();
    
    // Load microphone positions from CSV file
    bool loadFromExcel(const QString& csvPath);
    
    // Get microphone positions
    const QVector<double>& xPositions() const { return m_xMm; }
    const QVector<double>& yPositions() const { return m_yMm; }
    
    // Get number of microphones
    int count() const { return m_xMm.size(); }
    
    // Check if geometry is loaded
    bool isLoaded() const { return m_loaded; }

private:
    QVector<double> m_xMm;  // X positions in mm
    QVector<double> m_yMm;  // Y positions in mm
    bool m_loaded;
};

#endif // MICROPHONEARRAY_H
