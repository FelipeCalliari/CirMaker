#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

#include <optional>

namespace cm {

// Oscilloscope-style plot of analysis results.
class WaveformView : public QWidget {
    Q_OBJECT
public:
    struct Trace {
        QString name;
        QVector<double> y;
        QColor color;
    };

    explicit WaveformView(QWidget* parent = nullptr);

    // xUnit "s", "Hz" or "V"; logX for AC sweeps.
    void setData(const QString& title, const QString& xLabel, const QString& xUnit, const QString& yUnit,
                 QVector<double> x, QVector<Trace> traces, bool logX);
    // Digital lanes: state changes (time in ticks, state 0, 1, 2 = X, 3 = Z).
    struct Lane {
        QString name;
        QVector<std::pair<quint64, int>> changes;
    };
    void setDigital(QVector<Lane> lanes, quint64 now);
    void clear();
    bool isEmpty() const { return m_x.isEmpty(); }
    QString toCsv() const;

    static QColor traceColor(int index);
    static QString siFormat(double v, const QString& unit, int digits = 3);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void paintDigital(QPainter& p);
    QRectF plotRect() const;
    double xToPixel(double x) const;
    double yToPixel(double y) const;
    double pixelToX(double px) const;
    int indexAt(double x) const;

    QString m_title, m_xLabel, m_xUnit, m_yUnit;
    QVector<double> m_x;
    QVector<Trace> m_traces;
    bool m_logX = false;
    double m_x0 = 0, m_x1 = 1, m_y0 = 0, m_y1 = 1;
    std::optional<double> m_cursorX;
    bool m_digital = false;
    QVector<Lane> m_lanes;
    quint64 m_now = 0;
};

} // namespace cm
