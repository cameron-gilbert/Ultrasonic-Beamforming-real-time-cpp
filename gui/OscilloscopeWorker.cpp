#include "OscilloscopeWorker.h"
#include <QtGlobal>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

OscilloscopeWorker::OscilloscopeWorker(QObject *parent)
    : QObject(parent)
{
}

void OscilloscopeWorker::onSizeChanged(QSize size)
{
    m_widgetSize = size;
}

void OscilloscopeWorker::onScaleChanged(int scale)
{
    m_scale = scale;
}

void OscilloscopeWorker::onFpsChanged(int fps)
{
    // 93.75 Hz source rate — pick the nearest integer divisor
    const int skip = static_cast<int>(std::round(93.75 / qBound(1, fps, 94)));
    m_frameSkip = std::max(1, skip);
}

void OscilloscopeWorker::onNewFrame(QVector<float> samples)
{
    if (m_widgetSize.isEmpty() || samples.size() < 2)
        return;

    ++m_frameCounter;
    if (m_frameCounter >= m_frameSkip) {
        m_frameCounter = 0;
        emit imageReady(renderFrame(samples));
    }
}

QImage OscilloscopeWorker::renderFrame(const QVector<float> &samples) const
{
    const int w = m_widgetSize.width();
    const int h = m_widgetSize.height();

    QImage image(w, h, QImage::Format_RGB32);
    QPainter p(&image);

    p.fillRect(image.rect(), Qt::black);

    const int maxAbs = m_scale > 0 ? m_scale : 32767;

    const int midY  = h / 2;
    const double yScale = (h / 2.0 - 5.0) / static_cast<double>(maxAbs);

    // Scale labels
    p.setPen(Qt::gray);
    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);
    const int margin     = 10;
    const int halfHeight = (h - 2 * margin) / 2;
    p.drawText(5, margin + 15,                          QString("+%1").arg(maxAbs));
    p.drawText(5, margin + halfHeight * 0.25 + 5,       QString("+%1").arg(maxAbs * 3 / 4));
    p.drawText(5, margin + halfHeight * 0.5  + 5,       QString("+%1").arg(maxAbs / 2));
    p.drawText(5, margin + halfHeight * 0.75 + 5,       QString("+%1").arg(maxAbs / 4));
    p.drawText(5, midY + 5,                             QStringLiteral("0"));
    p.drawText(5, midY + halfHeight * 0.25 + 5,         QString("-%1").arg(maxAbs / 4));
    p.drawText(5, midY + halfHeight * 0.5  + 5,         QString("-%1").arg(maxAbs / 2));
    p.drawText(5, midY + halfHeight * 0.75 + 5,         QString("-%1").arg(maxAbs * 3 / 4));
    p.drawText(5, h - margin + 5,                       QString("-%1").arg(maxAbs));

    // Grid lines
    const int leftMargin = 60;
    p.setPen(QPen(Qt::darkGray, 1, Qt::DotLine));
    p.drawLine(leftMargin, margin,                     w, margin);
    p.drawLine(leftMargin, margin + halfHeight * 0.25, w, margin + halfHeight * 0.25);
    p.drawLine(leftMargin, margin + halfHeight * 0.5,  w, margin + halfHeight * 0.5);
    p.drawLine(leftMargin, margin + halfHeight * 0.75, w, margin + halfHeight * 0.75);
    p.setPen(QPen(Qt::gray, 1, Qt::SolidLine));
    p.drawLine(leftMargin, midY, w, midY);
    p.setPen(QPen(Qt::darkGray, 1, Qt::DotLine));
    p.drawLine(leftMargin, midY + halfHeight * 0.25,   w, midY + halfHeight * 0.25);
    p.drawLine(leftMargin, midY + halfHeight * 0.5,    w, midY + halfHeight * 0.5);
    p.drawLine(leftMargin, midY + halfHeight * 0.75,   w, midY + halfHeight * 0.75);
    p.drawLine(leftMargin, h - margin,                 w, h - margin);

    const int    n          = samples.size();
    const int    graphWidth = w - leftMargin;
    const double xStep      = (n > 1) ? static_cast<double>(graphWidth - 1) / (n - 1) : 0.0;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::green, 2));

    auto sampleToY = [&](float v) {
        return midY - static_cast<int>(v * 32767.0f * yScale);
    };

    QPainterPath path;
    path.moveTo(leftMargin, sampleToY(samples[0]));
    for (int i = 1; i < n; ++i)
        path.lineTo(static_cast<int>(leftMargin + i * xStep), sampleToY(samples[i]));
    p.drawPath(path);

    return image;
}

void OscilloscopeWorker::onNewHeatmap(QVector<float> powerGrid, int nx, int ny)
{
    if (m_widgetSize.isEmpty() || nx <= 0 || ny <= 0 || powerGrid.size() != nx * ny)
        return;
    emit imageReady(renderHeatmap(powerGrid, nx, ny));
}

// Colormap: black → teal (background) → red → yellow → white (signal peak)
static QRgb heatColor(float t)
{
    if (t <= 0.0f) return qRgb(0, 0, 0);
    if (t >= 1.0f) return qRgb(255, 255, 255);

    if (t < 0.33f) {
        // black → teal
        const float s = t / 0.33f;
        return qRgb(0,
                    static_cast<int>(s * 180.0f),
                    static_cast<int>(s * 180.0f));
    } else if (t < 0.5f) {
        // teal → red
        const float s = (t - 0.33f) / 0.17f;
        return qRgb(static_cast<int>(s * 255.0f),
                    static_cast<int>((1.0f - s) * 180.0f),
                    static_cast<int>((1.0f - s) * 180.0f));
    } else if (t < 0.75f) {
        // red → yellow
        const float s = (t - 0.5f) / 0.25f;
        return qRgb(255,
                    static_cast<int>(s * 255.0f),
                    0);
    } else {
        // yellow → white
        const float s = (t - 0.75f) / 0.25f;
        return qRgb(255, 255, static_cast<int>(s * 255.0f));
    }
}

QImage OscilloscopeWorker::renderHeatmap(const QVector<float>& powerGrid, int nx, int ny) const
{
    // Find max power across all grid cells (valid cells > 0; outside unit-circle = 0)
    float maxPower = 1e-20f;
    for (float v : powerGrid)
        if (v > maxPower) maxPower = v;

    // Build a small (nx × ny) image, then scale it to fill the widget.
    // ix maps to x (vx: left=-1, right=+1).
    // iy maps to y (vy: bottom=-1, top=+1) → flip so vy+ = top of screen.
    QImage heatmapSmall(nx, ny, QImage::Format_RGB32);
    for (int ix = 0; ix < nx; ++ix) {
        for (int iy = 0; iy < ny; ++iy) {
            const float power = powerGrid[ix * ny + iy];
            const float t = power / maxPower;
            // Flip iy so that vy=+1 is at the top of the image.
            // Flip ix so that vx=+1 is on the left (mirror for array-facing-viewer).
            heatmapSmall.setPixel(nx - 1 - ix, ny - 1 - iy, heatColor(t));
        }
    }

    const int w = m_widgetSize.width();
    const int h = m_widgetSize.height();
    QImage image(w, h, QImage::Format_RGB32);
    QPainter p(&image);
    p.fillRect(image.rect(), Qt::black);

    // Leave a margin for axis labels
    const int margin = 30;
    const int drawW  = w - 2 * margin;
    const int drawH  = h - 2 * margin;

    if (drawW > 0 && drawH > 0) {
        QImage scaled = heatmapSmall.scaled(drawW, drawH,
                                            Qt::IgnoreAspectRatio,
                                            Qt::FastTransformation);
        p.drawImage(margin, margin, scaled);
    }

    // Axis labels
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);
    // Vx axis labels (bottom edge)
    p.drawText(QRect(margin, h - margin + 2, drawW, margin - 2),
               Qt::AlignLeft,  "-1");
    p.drawText(QRect(margin, h - margin + 2, drawW, margin - 2),
               Qt::AlignHCenter, "Vx");
    p.drawText(QRect(margin, h - margin + 2, drawW, margin - 2),
               Qt::AlignRight, "+1");
    // Vy axis labels (left edge)
    p.drawText(QRect(0, margin, margin - 2, 15),
               Qt::AlignRight | Qt::AlignVCenter, "+1");
    p.drawText(QRect(0, h / 2 - 7, margin - 2, 15),
               Qt::AlignRight | Qt::AlignVCenter, "Vy");
    p.drawText(QRect(0, h - margin - 15, margin - 2, 15),
               Qt::AlignRight | Qt::AlignVCenter, "-1");

    // Draw the unit-circle boundary over the heatmap
    p.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    p.setRenderHint(QPainter::Antialiasing, true);
    const double cx = margin + drawW / 2.0;
    const double cy = margin + drawH / 2.0;
    const double rx = drawW / 2.0;
    const double ry = drawH / 2.0;
    p.drawEllipse(QRectF(cx - rx, cy - ry, 2 * rx, 2 * ry));

    return image;
}
