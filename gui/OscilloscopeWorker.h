#pragma once

#include <QObject>
#include <QVector>
#include <QSize>
#include <QImage>

// Runs on its own thread. onNewFrame renders a QImage directly on each incoming frame.
// paintEvent on the widget just blits — all rendering is off the main thread.
class OscilloscopeWorker : public QObject
{
    Q_OBJECT
public:
    explicit OscilloscopeWorker(QObject *parent = nullptr);

public slots:
    void onNewFrame(QVector<float> samples);
    // powerGrid is row-major [ix*ny + iy]; points outside the unit circle have power==0.
    // Renders a hot-colormap heatmap (ignores FPS throttle — scans are already infrequent).
    void onNewHeatmap(QVector<float> powerGrid, int nx, int ny);
    void onSizeChanged(QSize size);
    void onScaleChanged(int scale);
    void onFpsChanged(int fps);  // target display FPS; worker skips frames to match

signals:
    void imageReady(QImage image);

private:
    QImage renderFrame(const QVector<float> &samples) const;
    QImage renderHeatmap(const QVector<float> &powerGrid, int nx, int ny) const;

    QSize m_widgetSize;
    int   m_scale        = 0;
    int   m_frameSkip    = 1;   // render every Nth frame (1 = all frames)
    int   m_frameCounter = 0;

    // Heatmap display scaling — mutable so renderHeatmap can update them while staying logically const
    mutable float m_noiseFloor      = 0.0f;  // EMA of mean power; represents ambient noise level
    mutable float m_displayCeiling  = 0.0f;  // slow-decaying peak; snaps up instantly on strong signal
};
