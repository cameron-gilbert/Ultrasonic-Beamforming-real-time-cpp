#pragma once

#include <QString>
#include <QVector>
#include <QDateTime>

/**
 * @brief Writes MATLAB .mat files (Level 5 format) manually without external dependencies
 * 
 * Exports ultrasonic array data in a format readable by MATLAB's load() function.
 * Creates uncompressed MAT-File Level 5 format with double precision matrices.
 */
class MatFileWriter
{
public:
    MatFileWriter();
    ~MatFileWriter();
    
    /**
     * @brief Write ultrasonic recording data to .mat file
     *
     * @param filename     Output file path (should end in .mat)
     * @param data         Flat mic-major array: [mic0_s0, mic0_s1, ..., mic1_s0, ...]
     * @param numMics      Number of microphones — should be 102
     * @param numSamples   Number of time samples per mic — typically 480,000 for 10 sec
     * @param azimuth      Azimuth angle in degrees
     * @param elevation    Elevation angle in degrees
     * @param sampleRate   Sampling rate in Hz (default: 48000)
     * @return true if file written successfully
     */
    bool writeRecording(
        const QString &filename,
        const QVector<float> &data,  // Flat array: [mic0_sample0, mic0_sample1, ..., mic1_sample0, ...]
        int numMics,
        int numSamples,
        double azimuth,
        double elevation,
        double sampleRate = 48000.0
    );
    
    QString lastError() const { return m_lastError; }
    
private:
    QString m_lastError;
    
    // MAT-File Level 5 structures
    struct MatHeader {
        char text[116];      // Descriptive text
        char subsys[8];      // Subsystem data offset
        quint16 version;     // Version (0x0100)
        char endian[2];      // Endian indicator ('IM' or 'MI')
    };
    
    enum MatDataType {
        miINT8   = 1,
        miINT32  = 5,
        miUINT16 = 4,
        miUINT32 = 6,
        miDOUBLE = 9,
        miMATRIX = 14
    };
    
    enum MatArrayType {
        mxCHAR_CLASS   = 4,
        mxDOUBLE_CLASS = 6
    };
    
    bool writeMatHeader(QDataStream &stream);
    bool writeDoubleMatrix(QDataStream &stream, const QString &varName, 
                          const QVector<double> &data, int rows, int cols);
    bool writeDoubleScalar(QDataStream &stream, const QString &varName, double value);
    bool writeStringVariable(QDataStream &stream, const QString &varName, const QString &value);
    
    void writeDataElement(QDataStream &stream, quint32 type, const QByteArray &data);
    void writePadding(QDataStream &stream, int size);
};
