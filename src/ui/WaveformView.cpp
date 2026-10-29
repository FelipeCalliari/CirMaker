#include "ui/WaveformView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace cm {

namespace {

// 1-2-5 tick spacing covering [lo, hi] with about `count` ticks.
double niceStep(double span, int count)
{
    const double raw = span / std::max(1, count);
    const double mag = std::pow(10, std::floor(std::log10(raw)));
    const double r = raw / mag;
    return (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
}

} // namespace

WaveformView::WaveformView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(320, 180);
    setAutoFillBackground(true);
}

QColor WaveformView::traceColor(int index)
{
    static const QColor colors[] = {QColor(0xe0, 0x30, 0x30), QColor(0x20, 0x70, 0xd0), QColor(0x20, 0xa0, 0x40),
                                    QColor(0xd0, 0x80, 0x00), QColor(0x90, 0x30, 0xc0), QColor(0x00, 0xa0, 0xa0),
                                    QColor(0x80, 0x50, 0x20), QColor(0xd0, 0x40, 0x90)};
    return colors[index % 8];
}

QString WaveformView::siFormat(double v, const QString& unit, int digits)
{
    if (!std::isfinite(v)) return QStringLiteral("—");
    static const struct { double scale; const char* prefix; } prefixes[] = {
        {1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}};
    const double a = std::abs(v);
    if (a == 0) return QStringLiteral("0 ") + unit;
    for (const auto& p : prefixes)
        if (a >= p.scale * 0.9995)
            return QString::number(v / p.scale, 'g', digits) + u' ' + QString::fromUtf8(p.prefix) + unit;
    return QString::number(v, 'g', digits) + u' ' + unit;
}

void WaveformView::clear()
{
    m_x.clear();
    m_traces.clear();
    m_title.clear();
    update();
}

void WaveformView::setData(const QString& title, const QString& xLabel, const QString& xUnit, const QString& yUnit,
                           QVector<double> x, QVector<Trace> traces, bool logX)
{
    m_title = title;
    m_xLabel = xLabel;
    m_xUnit = xUnit;
    m_yUnit = yUnit;
    m_x = std::move(x);
    m_traces = std::move(traces);
    m_logX = logX;
    m_cursorX.reset();
    if (!m_x.isEmpty()) {
        m_x0 = m_x.first();
        m_x1 = m_x.last();
        if (m_x1 <= m_x0) m_x1 = m_x0 + 1;
        double lo = 1e300, hi = -1e300;
        for (const Trace& t : std::as_const(m_traces))
            for (double v : t.y)
                if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
        if (lo > hi) { lo = 0; hi = 1; }
        const double pad = (hi - lo) * 0.08;
        m_y0 = lo - pad;
        m_y1 = hi + pad;
        if (m_y1 - m_y0 < 1e-12) { m_y0 -= 0.5; m_y1 += 0.5; }
    }
    update();
}

QRectF WaveformView::plotRect() const
{
    return QRectF(70, 28, width() - 70 - 16, height() - 28 - 42);
}

double WaveformView::xToPixel(double x) const
{
    const QRectF r = plotRect();
    double t;
    if (m_logX && m_x0 > 0)
        t = (std::log10(std::max(x, 1e-300)) - std::log10(m_x0)) / (std::log10(m_x1) - std::log10(m_x0));
    else
        t = (x - m_x0) / (m_x1 - m_x0);
    return r.left() + t * r.width();
}

double WaveformView::pixelToX(double px) const
{
    const QRectF r = plotRect();
    const double t = std::clamp((px - r.left()) / r.width(), 0.0, 1.0);
    if (m_logX && m_x0 > 0)
        return std::pow(10, std::log10(m_x0) + t * (std::log10(m_x1) - std::log10(m_x0)));
    return m_x0 + t * (m_x1 - m_x0);
}

double WaveformView::yToPixel(double y) const
{
    const QRectF r = plotRect();
    return r.bottom() - (y - m_y0) / (m_y1 - m_y0) * r.height();
}

int WaveformView::indexAt(double x) const
{
    const auto it = std::lower_bound(m_x.cbegin(), m_x.cend(), x);
    int i = int(it - m_x.cbegin());
    if (i >= m_x.size()) i = int(m_x.size()) - 1;
    if (i > 0 && std::abs(m_x[i - 1] - x) < std::abs(m_x[i] - x)) --i;
    return i;
}

void WaveformView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.base());
    const QRectF r = plotRect();

    if (m_x.isEmpty()) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Run an analog analysis to see waveforms here.\nUse the Probe tool to choose which nets to plot."));
        return;
    }

    // grid and ticks
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);
    const QColor gridColor = pal.color(QPalette::Mid).lighter(130);
    const QColor textColor = pal.color(QPalette::Text);

    const double yStep = niceStep(m_y1 - m_y0, 6);
    for (double y = std::ceil(m_y0 / yStep) * yStep; y <= m_y1; y += yStep) {
        const double py = yToPixel(y);
        p.setPen(QPen(gridColor, 0, std::abs(y) < yStep / 2 ? Qt::SolidLine : Qt::DotLine));
        p.drawLine(QPointF(r.left(), py), QPointF(r.right(), py));
        p.setPen(textColor);
        p.drawText(QRectF(0, py - 8, r.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   siFormat(std::abs(y) < yStep * 1e-6 ? 0 : y, m_yUnit));
    }
    QVector<double> xTicks;
    if (m_logX && m_x0 > 0) {
        for (double d = std::pow(10, std::floor(std::log10(m_x0))); d <= m_x1 * 1.0001; d *= 10)
            if (d >= m_x0 * 0.9999) xTicks << d;
    } else {
        const double xStep = niceStep(m_x1 - m_x0, 8);
        for (double x = std::ceil(m_x0 / xStep) * xStep; x <= m_x1 + xStep * 1e-9; x += xStep)
            xTicks << (std::abs(x) < xStep * 1e-6 ? 0 : x);
    }
    for (double x : std::as_const(xTicks)) {
        const double px = xToPixel(x);
        p.setPen(QPen(gridColor, 0, Qt::DotLine));
        p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
        p.setPen(textColor);
        p.drawText(QRectF(px - 50, r.bottom() + 3, 100, 16), Qt::AlignHCenter | Qt::AlignTop, siFormat(x, m_xUnit));
    }
    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    p.drawText(QRectF(r.left(), r.bottom() + 20, r.width(), 18), Qt::AlignHCenter, m_xLabel);

    // traces
    p.save();
    p.setClipRect(r.adjusted(-1, -1, 1, 1));
    for (const Trace& t : std::as_const(m_traces)) {
        QPainterPath path;
        bool started = false;
        for (int i = 0; i < m_x.size() && i < t.y.size(); ++i) {
            if (!std::isfinite(t.y[i])) { started = false; continue; }
            const QPointF pt(xToPixel(m_x[i]), yToPixel(t.y[i]));
            if (started) path.lineTo(pt); else path.moveTo(pt);
            started = true;
        }
        p.setPen(QPen(t.color, 1.6));
        p.drawPath(path);
    }
    p.restore();

    // legend
    double lx = r.left();
    p.setFont(font());
    p.setPen(textColor);
    p.drawText(QRectF(8, 4, r.left() - 10, 20), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    lx = std::max(lx, 8 + p.fontMetrics().horizontalAdvance(m_title) + 20.0);
    const int idx = m_cursorX ? indexAt(*m_cursorX) : -1;
    for (const Trace& t : std::as_const(m_traces)) {
        QString text = t.name;
        if (idx >= 0 && idx < t.y.size())
            text += QStringLiteral(" = ") + siFormat(t.y[idx], m_yUnit, 4);
        p.setPen(QPen(t.color, 3));
        p.drawLine(QPointF(lx, 14), QPointF(lx + 16, 14));
        p.setPen(textColor);
        p.drawText(QPointF(lx + 20, 18), text);
        lx += 32 + p.fontMetrics().horizontalAdvance(text);
    }

    // cursor
    if (idx >= 0) {
        const double px = xToPixel(m_x[idx]);
        p.setPen(QPen(textColor, 0, Qt::DashLine));
        p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
        for (const Trace& t : std::as_const(m_traces)) {
            if (idx >= t.y.size() || !std::isfinite(t.y[idx])) continue;
            p.setPen(Qt::NoPen);
            p.setBrush(t.color);
            p.drawEllipse(QPointF(px, yToPixel(t.y[idx])), 3.5, 3.5);
        }
        p.setPen(textColor);
        p.drawText(QRectF(px + 4, r.top() + 2, 140, 16), Qt::AlignLeft, siFormat(m_x[idx], m_xUnit, 5));
    }
}

void WaveformView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_x.isEmpty()) return;
    if (plotRect().contains(e->position()))
        m_cursorX = pixelToX(e->position().x());
    else
        m_cursorX.reset();
    update();
}

void WaveformView::leaveEvent(QEvent*)
{
    m_cursorX.reset();
    update();
}

QString WaveformView::toCsv() const
{
    QStringList header{m_xLabel};
    for (const Trace& t : m_traces) header << t.name;
    QString csv = header.join(u',') + u'\n';
    for (int i = 0; i < m_x.size(); ++i) {
        QStringList row{QString::number(m_x[i], 'g', 12)};
        for (const Trace& t : m_traces) row << (i < t.y.size() ? QString::number(t.y[i], 'g', 12) : QString());
        csv += row.join(u',') + u'\n';
    }
    return csv;
}

void WaveformView::contextMenuEvent(QContextMenuEvent* e)
{
    if (m_x.isEmpty()) return;
    QMenu menu(this);
    menu.addAction(tr("Copy data (CSV)"), this, [this] { QApplication::clipboard()->setText(toCsv()); });
    menu.addAction(tr("Save data as CSV..."), this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save waveforms"), QStringLiteral("waveforms.csv"),
                                                          tr("CSV file (*.csv)"));
        if (path.isEmpty()) return;
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(toCsv().toUtf8());
            f.commit();
        }
    });
    menu.addAction(tr("Save image..."), this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save plot image"), QStringLiteral("waveforms.png"),
                                                          tr("PNG image (*.png)"));
        if (!path.isEmpty()) grab().save(path);
    });
    menu.exec(e->globalPos());
}

} // namespace cm
