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
        int breakOn = -1;       // breakpoint box: -1 none shown, 0 off, 1 break on 0, 2 break on 1
    };
    void setDigital(QVector<Lane> lanes, quint64 now);
    void clear();
    bool isEmpty() const { return m_x.isEmpty(); }
    QString toCsv() const;

    static QColor traceColor(int index);

    // Analysis window tools: zoom (fit restores), cursors A and B, a trace
    // computed from others, and a stored reference shown dashed.
    void fit();
    void zoomX(double factor, double around);
    void zoomY(double factor, double around);
    void setLogX(bool on);
    bool logX() const { return m_logX; }
    void setCursors(std::optional<double> a, std::optional<double> b);
    // "T1-T2", "T1*2", "abs(T3)", "db(T1)": T<n> is the n-th trace; false
    // with a message when the expression is not understood.
    bool addMathTrace(const QString& expression, QString* error = nullptr);
    // Each trace in a band of its own, scaled to itself (CircuitMaker's
    // "All Cells"), instead of all on one axis.
    void setSeparate(bool on);
    bool separate() const { return m_separate; }
    void store();                   // keeps the shown traces as the reference
    void clearStored();
    const QVector<Trace>& traces() const { return m_traces; }
    std::pair<double, double> xRange() const { return {m_x0, m_x1}; }

signals:
    // The breakpoint box of a digital lane was clicked (0 off, 1 on 0, 2 on 1).
    void breakChanged(int lane, int condition);

public:
    static QString siFormat(double v, const QString& unit, int digits = 3);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void paintDigital(QPainter& p);
    double laneHeight() const;
    QRectF breakBox(int lane) const;
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
    std::optional<double> m_cursorA, m_cursorB;
    QVector<double> m_storedX;
    QVector<Trace> m_stored;
    QString m_storedTitle;
    double valueAt(const Trace& t, double x) const;
    quint64 m_now = 0;
    bool m_separate = false;
    // pixel y of value y of trace k (its own band when separate)
    double traceY(int k, double y) const;
};

} // namespace cm
