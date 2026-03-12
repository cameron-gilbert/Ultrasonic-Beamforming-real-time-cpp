#include "MicrophoneArray.h"
#include <QFile>
#include <QTextStream>

MicrophoneArray::MicrophoneArray()
    : m_loaded(false)
{
}

bool MicrophoneArray::loadFromExcel(const QString& csvPath)
{
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    m_xMm.clear();
    m_yMm.clear();

    QTextStream in(&file);

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        // Skip header lines
        QString lowerLine = line.toLower();
        if (lowerLine.contains("column") ||
            lowerLine.contains("microphone") ||
            lowerLine.contains("number") ||
            (lowerLine.contains("x") && lowerLine.contains("y") && lowerLine.contains("mm"))) {
            continue;
        }

        // Data line: mic_name, x, y
        QStringList parts = line.split(',');
        bool okX, okY;
        double x = parts[1].trimmed().toDouble(&okX);
        double y = parts[2].trimmed().toDouble(&okY);
        if (okX && okY) {
            m_xMm.append(x);
            m_yMm.append(y);
        }
    }

    file.close();

    // Validate we have 102 microphones
    if (m_xMm.size() == 102 && m_yMm.size() == 102) {
        m_loaded = true;
        return true;
    }

    m_xMm.clear();
    m_yMm.clear();
    m_loaded = false;
    return false;
}
