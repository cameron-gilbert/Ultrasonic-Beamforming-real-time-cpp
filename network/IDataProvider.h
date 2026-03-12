#pragma once

#include <QObject>
#include <QByteArray>

class IDataProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IDataProvider() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void packetReceived(QByteArray packet);
    void errorOccurred(QString message);
    void statusMessage(QString message);
    void connected();
};
