#ifndef HEALTHGAUGE_H
#define HEALTHGAUGE_H

#include <QWidget>

// A ring gauge showing a percentage. Health gauges are coloured by value; a performance
// gauge may exceed 100 %: the ring stays full and the number shows the real value.
class HealthGauge : public QWidget
{
    Q_OBJECT

public:
    explicit HealthGauge(QWidget *parent = nullptr);

    // percent < 0 shows the placeholder text instead of a ring value.
    void setHealth(int percent, const QString &caption = QString());

    // Performance mode: fixed accent colour, values above 100 are allowed.
    void setPerformanceMode(bool enabled) { m_performance = enabled; update(); }
    int health() const { return m_percent; }

    QSize sizeHint() const override { return QSize(96, 96); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_percent = -1;
    bool m_performance = false;
    QString m_caption;
};

#endif // HEALTHGAUGE_H
