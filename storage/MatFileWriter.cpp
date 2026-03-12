#include "MatFileWriter.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>

MatFileWriter::MatFileWriter()
{
}

MatFileWriter::~MatFileWriter()
{
}

bool MatFileWriter::writeRecording(
    const QString &filename,
    const QVector<float> &data,
    int numMics,
    int numSamples,
    double azimuth,
    double elevation,
    double sampleRate)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QString("Failed to open file: %1").arg(filename);
        return false;
    }
    
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    // Write MAT-File header
    if (!writeMatHeader(stream)) {
        file.close();
        return false;
    }
    
    // Convert float data to double for MATLAB, transposing from mic-major (C row-major)
    // to sample-major (MATLAB column-major) so that MATLAB reads data as [numMics x numSamples].
    // MAT Level 5 column-major: M[mic, sample] lives at file offset sample*numMics + mic.
    // Our buffer:  buf[mic * numSamples + sample]
    // File order:  transposed[sample * numMics + mic] = buf[mic * numSamples + sample]
    QVector<double> doubleData(data.size());
    for (int mic = 0; mic < numMics; ++mic) {
        for (int sample = 0; sample < numSamples; ++sample) {
            doubleData[sample * numMics + mic] = static_cast<double>(data[mic * numSamples + sample]);
        }
    }

    // Write main data matrix: declared [numMics x numSamples], MATLAB sees data(mic, sample)
    if (!writeDoubleMatrix(stream, "data", doubleData, numMics, numSamples)) {
        file.close();
        return false;
    }
    
    // Write metadata scalars
    writeDoubleScalar(stream, "azimuth", azimuth);
    writeDoubleScalar(stream, "elevation", elevation);
    writeDoubleScalar(stream, "sample_rate", sampleRate);
    writeDoubleScalar(stream, "num_mics", static_cast<double>(numMics));
    writeDoubleScalar(stream, "duration", static_cast<double>(numSamples) / sampleRate);
    
    // Write timestamp string
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    writeStringVariable(stream, "timestamp", timestamp);
    
    file.close();
    return true;
}

bool MatFileWriter::writeMatHeader(QDataStream &stream)
{
    MatHeader header;
    memset(&header, 0, sizeof(header));
    
    // Descriptive text (116 bytes)
    QString desc = QString("MATLAB 5.0 MAT-file, Created by UltrasonicHost %1")
                   .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    QByteArray descBytes = desc.toLatin1();
    memcpy(header.text, descBytes.constData(), qMin(115, descBytes.size()));
    
    // Subsystem data offset (8 bytes of zeros)
    memset(header.subsys, 0, 8);
    
    // Version
    header.version = 0x0100;
    
    // Endian indicator ('IM' for little-endian on Intel/AMD)
    header.endian[0] = 'I';
    header.endian[1] = 'M';
    
    // Write header
    stream.writeRawData(header.text, 116);
    stream.writeRawData(header.subsys, 8);
    stream << header.version;
    stream.writeRawData(header.endian, 2);
    
    return true;
}

bool MatFileWriter::writeDoubleMatrix(QDataStream &stream, const QString &varName,
                                     const QVector<double> &data, int rows, int cols)
{
    // Data element: miMATRIX
    QByteArray matrixData;
    QDataStream matrixStream(&matrixData, QIODevice::WriteOnly);
    matrixStream.setByteOrder(QDataStream::LittleEndian);
    
    // Array Flags (complex, global, logical, class, nzmax)
    QByteArray arrayFlags;
    QDataStream flagStream(&arrayFlags, QIODevice::WriteOnly);
    flagStream.setByteOrder(QDataStream::LittleEndian);
    quint32 flags = mxDOUBLE_CLASS;  // Non-complex double array
    quint32 nzmax = 0;
    flagStream << flags << nzmax;
    writeDataElement(matrixStream, miUINT32, arrayFlags);
    
    // Dimensions Array
    QByteArray dims;
    QDataStream dimStream(&dims, QIODevice::WriteOnly);
    dimStream.setByteOrder(QDataStream::LittleEndian);
    dimStream << static_cast<quint32>(rows) << static_cast<quint32>(cols);
    writeDataElement(matrixStream, miINT32, dims);
    
    // Array Name
    QByteArray nameBytes = varName.toLatin1();
    writeDataElement(matrixStream, miINT8, nameBytes);
    
    // Real part (data)
    QByteArray realData;
    QDataStream realStream(&realData, QIODevice::WriteOnly);
    realStream.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < data.size(); ++i) {
        realStream << data[i];
    }
    writeDataElement(matrixStream, miDOUBLE, realData);
    
    // Write the complete matrix element
    writeDataElement(stream, miMATRIX, matrixData);
    
    return true;
}

bool MatFileWriter::writeDoubleScalar(QDataStream &stream, const QString &varName, double value)
{
    QVector<double> vec(1);
    vec[0] = value;
    return writeDoubleMatrix(stream, varName, vec, 1, 1);
}

bool MatFileWriter::writeStringVariable(QDataStream &stream, const QString &varName, const QString &value)
{
    // Data element: miMATRIX (character array)
    QByteArray matrixData;
    QDataStream matrixStream(&matrixData, QIODevice::WriteOnly);
    matrixStream.setByteOrder(QDataStream::LittleEndian);
    
    // Array Flags
    QByteArray arrayFlags;
    QDataStream flagStream(&arrayFlags, QIODevice::WriteOnly);
    flagStream.setByteOrder(QDataStream::LittleEndian);
    quint32 flags = mxCHAR_CLASS;
    quint32 nzmax = 0;
    flagStream << flags << nzmax;
    writeDataElement(matrixStream, miUINT32, arrayFlags);
    
    // Dimensions (1 × N string)
    QByteArray dims;
    QDataStream dimStream(&dims, QIODevice::WriteOnly);
    dimStream.setByteOrder(QDataStream::LittleEndian);
    dimStream << static_cast<quint32>(1) << static_cast<quint32>(value.length());
    writeDataElement(matrixStream, miINT32, dims);
    
    // Array Name
    QByteArray nameBytes = varName.toLatin1();
    writeDataElement(matrixStream, miINT8, nameBytes);
    
    // Character data (UTF-16)
    QByteArray charData;
    QDataStream charStream(&charData, QIODevice::WriteOnly);
    charStream.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < value.length(); ++i) {
        charStream << static_cast<quint16>(value[i].unicode());
    }
    writeDataElement(matrixStream, miUINT16, charData);
    
    // Write the complete matrix element
    writeDataElement(stream, miMATRIX, matrixData);
    
    return true;
}

void MatFileWriter::writeDataElement(QDataStream &stream, quint32 type, const QByteArray &data)
{
    quint32 numBytes = data.size();
    
    // Small data element format (< 4 bytes) - not used for our data
    // We always use regular format
    
    stream << type << numBytes;
    stream.writeRawData(data.constData(), numBytes);
    
    // Padding to 8-byte boundary
    int padding = (8 - (numBytes % 8)) % 8;
    writePadding(stream, padding);
}

void MatFileWriter::writePadding(QDataStream &stream, int size)
{
    for (int i = 0; i < size; ++i) {
        stream << static_cast<quint8>(0);
    }
}
