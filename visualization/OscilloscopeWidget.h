#pragma once
#include <QWidget>
#include <QImage>

// Dumb display widget. All rendering is done off-thread by OscilloscopeWorker
// which emits a fully-rendered QImage. paintEvent just blits it.
class OscilloscopeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OscilloscopeWidget(QWidget *parent = nullptr);

    // Called from main thread when worker finishes a frame (or with null image to clear).
    void setImage(QImage image);

signals:
    void sizeChanged(QSize newSize);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage m_image;
};
