#include "OscilloscopeWidget.h"
#include <QPainter>
#include <QResizeEvent>

OscilloscopeWidget::OscilloscopeWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
}

void OscilloscopeWidget::setImage(QImage image)
{
    m_image = std::move(image);
    update();
}

void OscilloscopeWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    emit sizeChanged(event->size());
}

void OscilloscopeWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);

    // Normal case: blit the pre-rendered image from the worker thread.
    // Only falls back to the empty midline if no image has arrived yet or
    // after a clear (setImage(QImage{})) e.g. on mic change.
    if (!m_image.isNull() && m_image.size() == size()) {
        p.drawImage(0, 0, m_image);
        return;
    }

    // Empty / size-mismatch fallback — draw black + midline
    const QRect r = rect();

    // background
    p.fillRect(r, Qt::black);

    p.fillRect(r, Qt::black);
    p.setPen(Qt::darkGray);
    p.drawLine(r.left(), r.center().y(), r.right(), r.center().y());
}
