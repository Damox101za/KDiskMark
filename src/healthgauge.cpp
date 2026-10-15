#include "healthgauge.h"

#include <QConicalGradient>
#include <QPainter>

HealthGauge::HealthGauge(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(80, 80);
}

void HealthGauge::setHealth(int percent, const QString &caption)
{
    m_percent = percent < 0 ? -1 : (m_performance ? percent : qMin(percent, 100));
    m_caption = caption;
    update();
}

void HealthGauge::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int side = qMin(width(), height());
    const qreal pen = side * 0.11;
    const QRectF ring((width() - side) / 2.0 + pen, (height() - side) / 2.0 + pen,
                      side - 2 * pen, side - 2 * pen);

    // Track
    painter.setPen(QPen(palette().color(QPalette::Mid), pen, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, 0, 360 * 16);

    QColor color(130, 130, 130);
    if (m_percent >= 0) {
        color = m_performance ? QColor(0, 229, 255)
              : m_percent >= 75 ? QColor(0, 230, 118)
              : m_percent >= 40 ? QColor(255, 196, 0)
                                : QColor(255, 61, 87);

        painter.setPen(QPen(color, pen, Qt::SolidLine, Qt::RoundCap));
        // Starts at 12 o'clock and runs clockwise.
        painter.drawArc(ring, 90 * 16, -static_cast<int>(qMin(m_percent, 100) * 3.6 * 16));
    }

    painter.setPen(palette().color(QPalette::WindowText));

    QFont big = font();
    big.setBold(true);
    big.setPointSizeF(side * 0.19);
    painter.setFont(big);

    const QString text = m_percent >= 0 ? QStringLiteral("%1%").arg(m_percent) : tr("N/A");
    painter.drawText(rect(), Qt::AlignCenter, text);

    if (!m_caption.isEmpty()) {
        QFont small = font();
        small.setPointSizeF(qMax(6.0, side * 0.09));
        painter.setFont(small);
        painter.drawText(QRect(0, height() / 2 + side / 6, width(), side / 4), Qt::AlignHCenter | Qt::AlignTop, m_caption);
    }
}
