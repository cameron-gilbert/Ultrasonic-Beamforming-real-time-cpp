#pragma once
#include "IDataProvider.h"
#include <QTimer>

class SimulatedDataProvider : public IDataProvider
{
    Q_OBJECT
public:
    explicit SimulatedDataProvider(QObject *parent = nullptr);
    void start() override;
    void stop() override;

private slots:
    void generatePacket();

private:
    QTimer  m_timer;
    quint16 m_frameNumber = 0;
    quint16 m_micIndex    = 0;
};
