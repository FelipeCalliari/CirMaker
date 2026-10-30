#include "ui/WaveformView.h"

#include <QMouseEvent>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QWheelEvent>
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

void WaveformView::setDigital(QVector<Lane> lanes, quint64 now)
{
    m_digital = true;
    m_lanes = std::move(lanes);
    m_now = now;
    m_x.clear();
    update();
}

namespace {
constexpr double kNameWidth = 110, kLaneTop = 8;
}

double WaveformView::laneHeight() const
{
    return std::clamp((height() - 40.0) / std::max<qsizetype>(1, m_lanes.size()), 18.0, 46.0);
}

QRectF WaveformView::breakBox(int lane) const
{
    const double h = laneHeight(), mid = kLaneTop + (lane + 0.5) * h;
    return QRectF(4, mid - 6, 12, 12);
}

void WaveformView::mousePressEvent(QMouseEvent* e)
{
    // analysis plot: click places cursor A, Shift+click cursor B
    if (!m_digital && !m_x.isEmpty() && e->button() == Qt::LeftButton && plotRect().contains(e->position())) {
        const double x = m_x[indexAt(pixelToX(e->position().x()))];
        if (e->modifiers() & Qt::ShiftModifier) m_cursorB = x;
        else m_cursorA = x;
        update();
        return;
    }
    if (m_digital && e->button() == Qt::LeftButton) {
        for (int i = 0; i < m_lanes.size(); ++i) {
            if (m_lanes[i].breakOn >= 0 && breakBox(i).adjusted(-3, -3, 3, 3).contains(e->position())) {
                m_lanes[i].breakOn = (m_lanes[i].breakOn + 1) % 3;
                update();
                emit breakChanged(i, m_lanes[i].breakOn);
                return;
            }
        }
    }
    QWidget::mousePressEvent(e);
}

void WaveformView::paintDigital(QPainter& p)
{
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::Text);
    QFont f = font();
    p.setFont(f);
    const double nameWidth = kNameWidth, top = kLaneTop, laneH = laneHeight();
    const QRectF area(nameWidth, top, width() - nameWidth - 12, laneH * m_lanes.size());
    // the most recent window of ticks, like a scrolling logic analyser
    const quint64 window = std::max<quint64>(200, quint64(area.width() / 2));
    const quint64 t0 = m_now > window ? m_now - window : 0;
    const quint64 t1 = std::max(t0 + 1, m_now);
    auto xOf = [&](quint64 t) { return area.left() + double(t - std::min(t, t0)) / double(t1 - t0) * area.width(); };

    // time grid
    p.setPen(QPen(pal.color(QPalette::Mid).lighter(130), 0, Qt::DotLine));
    const double step = std::max(1.0, std::pow(10, std::floor(std::log10(double(t1 - t0) / 5))));
    for (double t = std::ceil(t0 / step) * step; t <= t1; t += step) {
        const double x = xOf(quint64(t));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p.setPen(text);
        p.drawText(QRectF(x - 40, area.bottom() + 2, 80, 16), Qt::AlignHCenter, QString::number(quint64(t)));
        p.setPen(QPen(pal.color(QPalette::Mid).lighter(130), 0, Qt::DotLine));
    }
    for (int i = 0; i < m_lanes.size(); ++i) {
        const Lane& lane = m_lanes[i];
        const double y0 = area.top() + i * laneH + 4, y1 = area.top() + (i + 1) * laneH - 4;
        const QColor c = traceColor(i);
        p.setPen(text);
        p.drawText(QRectF(20, y0, nameWidth - 24, y1 - y0), Qt::AlignRight | Qt::AlignVCenter, lane.name);
        if (lane.breakOn >= 0) {
            // CircuitMaker's breakpoint box: lower half = break on 0, upper half = break on 1
            const QRectF box = breakBox(i);
            p.setPen(QPen(text, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(box);
            if (lane.breakOn) {
                const QRectF half = lane.breakOn == 1 ? QRectF(box.left(), box.center().y(), box.width(), box.height() / 2)
                                                      : QRectF(box.left(), box.top(), box.width(), box.height() / 2);
                p.fillRect(half, QColor(0xe0, 0x20, 0x20));
            }
        }
        int state = 3;
        quint64 from = t0;
        auto segment = [&](quint64 a, quint64 b, int st) {
            if (b <= t0 || a >= t1 + 1) return;
            const double xa = xOf(std::max(a, t0)), xb = xOf(std::min(b, t1));
            p.setPen(QPen(c, 1.8));
            if (st == 0) p.drawLine(QPointF(xa, y1), QPointF(xb, y1));
            else if (st == 1) p.drawLine(QPointF(xa, y0), QPointF(xb, y0));
            else if (st == 3) { p.setPen(QPen(c, 1.2, Qt::DashLine)); p.drawLine(QPointF(xa, (y0 + y1) / 2), QPointF(xb, (y0 + y1) / 2)); }
            else { p.setBrush(QBrush(c, Qt::BDiagPattern)); p.setPen(QPen(c, 1)); p.drawRect(QRectF(QPointF(xa, y0), QPointF(xb, y1))); p.setBrush(Qt::NoBrush); }
        };
        for (const auto& [t, st] : lane.changes) {
            if (t > from) segment(from, t, state);
            if (t >= t0 && t <= t1 && state != st) {   // edge
                p.setPen(QPen(c, 1.8));
                p.drawLine(QPointF(xOf(t), y0), QPointF(xOf(t), y1));
            }
            state = st;
            from = std::max(from, t);
        }
        segment(from, t1, state);
    }
    p.setPen(text);
    p.drawText(QRectF(4, area.bottom() + 2, nameWidth - 8, 16), Qt::AlignRight, tr("ticks"));
}

void WaveformView::clear()
{
    m_digital = false;
    m_lanes.clear();
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
    m_digital = false;
    m_cursorX.reset();
    m_cursorA.reset();
    m_cursorB.reset();
    fit();
}

void WaveformView::fit()
{
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

void WaveformView::zoomX(double factor, double around)
{
    if (m_logX && m_x0 > 0) {
        const double a = std::log10(std::max(around, 1e-300)), l0 = std::log10(m_x0), l1 = std::log10(m_x1);
        m_x0 = std::pow(10, a - (a - l0) / factor);
        m_x1 = std::pow(10, a + (l1 - a) / factor);
    } else {
        m_x0 = around - (around - m_x0) / factor;
        m_x1 = around + (m_x1 - around) / factor;
    }
    update();
}

void WaveformView::zoomY(double factor, double around)
{
    m_y0 = around - (around - m_y0) / factor;
    m_y1 = around + (m_y1 - around) / factor;
    update();
}

void WaveformView::setLogX(bool on)
{
    if (on && (m_x.isEmpty() || m_x.first() <= 0)) return;     // log needs positive x
    m_logX = on;
    update();
}

void WaveformView::setCursors(std::optional<double> a, std::optional<double> b)
{
    m_cursorA = a;
    m_cursorB = b;
    update();
}

double WaveformView::valueAt(const Trace& t, double x) const
{
    const int i = indexAt(x);
    return i >= 0 && i < t.y.size() ? t.y[i] : qQNaN();
}

bool WaveformView::addMathTrace(const QString& expression, QString* error)
{
    auto fail = [&](const QString& why) { if (error) *error = why; return false; };
    static const QRegularExpression func(QStringLiteral("^\\s*(abs|db)\\s*\\(\\s*[Tt](\\d+)\\s*\\)\\s*$"));
    static const QRegularExpression binary(
        QStringLiteral("^\\s*(?:[Tt](\\d+)|([-+]?[\\d.]+(?:[eE][-+]?\\d+)?))\\s*([-+*/])\\s*(?:[Tt](\\d+)|([-+]?[\\d.]+(?:[eE][-+]?\\d+)?))\\s*$"));
    auto trace = [&](const QString& n) -> const Trace* {
        const int i = n.toInt() - 1;
        return i >= 0 && i < m_traces.size() ? &m_traces[i] : nullptr;
    };
    Trace out{expression.trimmed(), {}, traceColor(int(m_traces.size()))};
    if (const auto m = func.match(expression); m.hasMatch()) {
        const Trace* a = trace(m.captured(2));
        if (!a) return fail(tr("There is no trace T%1").arg(m.captured(2)));
        for (double v : a->y)
            out.y << (m.captured(1) == u"abs" ? std::abs(v) : 20 * std::log10(std::max(std::abs(v), 1e-30)));
    } else if (const auto m = binary.match(expression); m.hasMatch()) {
        const Trace* a = m.captured(1).isEmpty() ? nullptr : trace(m.captured(1));
        const Trace* b = m.captured(4).isEmpty() ? nullptr : trace(m.captured(4));
        if ((!m.captured(1).isEmpty() && !a) || (!m.captured(4).isEmpty() && !b))
            return fail(tr("There is no such trace"));
        if (!a && !b) return fail(tr("Use at least one trace"));
        const QChar op = m.captured(3).at(0);
        for (int i = 0; i < m_x.size(); ++i) {
            const double x = a ? a->y.value(i, qQNaN()) : m.captured(2).toDouble();
            const double y = b ? b->y.value(i, qQNaN()) : m.captured(5).toDouble();
            out.y << (op == u'+' ? x + y : op == u'-' ? x - y : op == u'*' ? x * y : y != 0 ? x / y : qQNaN());
        }
    } else {
        return fail(tr("Write T1-T2, T1*2, abs(T1) or db(T1)"));
    }
    m_traces << out;
    update();
    return true;
}

void WaveformView::store()
{
    m_storedX = m_x;
    m_stored = m_traces;
    m_storedTitle = m_title;
    update();
}

void WaveformView::clearStored()
{
    m_stored.clear();
    m_storedX.clear();
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

    if (m_digital) {
        paintDigital(p);
        return;
    }
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

    // traces; the stored reference dashed underneath
    p.save();
    p.setClipRect(r.adjusted(-1, -1, 1, 1));
    for (const Trace& t : std::as_const(m_stored)) {
        QPainterPath path;
        bool started = false;
        for (int i = 0; i < m_storedX.size() && i < t.y.size(); ++i) {
            if (!std::isfinite(t.y[i])) { started = false; continue; }
            const QPointF pt(xToPixel(m_storedX[i]), yToPixel(t.y[i]));
            if (started) path.lineTo(pt); else path.moveTo(pt);
            started = true;
        }
        p.setPen(QPen(t.color.lighter(140), 1.2, Qt::DashLine));
        p.drawPath(path);
    }
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
    for (int n = 0; n < m_traces.size(); ++n) {
        const Trace& t = m_traces[n];
        QString text = QStringLiteral("T%1 %2").arg(n + 1).arg(t.name);
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

    // cursors A and B with the differences between them
    const std::pair<std::optional<double>, QString> marks[] = {{m_cursorA, QStringLiteral("A")}, {m_cursorB, QStringLiteral("B")}};
    for (const auto& [x, name] : marks) {
        if (!x) continue;
        const double px = xToPixel(*x);
        p.setPen(QPen(QColor(0xc0, 0x40, 0x00), 1.2));
        p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
        p.drawText(QPointF(px + 3, r.bottom() - 4), name);
    }
    if (m_cursorA || m_cursorB) {
        QStringList lines;
        if (m_cursorA) lines << QStringLiteral("A: %1").arg(siFormat(*m_cursorA, m_xUnit, 5));
        if (m_cursorB) lines << QStringLiteral("B: %1").arg(siFormat(*m_cursorB, m_xUnit, 5));
        if (m_cursorA && m_cursorB) {
            const double dx = *m_cursorB - *m_cursorA;
            lines << QStringLiteral("ΔX: %1").arg(siFormat(dx, m_xUnit, 5));
            if (m_xUnit == u"s" && dx != 0) lines << QStringLiteral("1/ΔX: %1").arg(siFormat(1 / std::abs(dx), QStringLiteral("Hz"), 5));
            for (int i = 0; i < m_traces.size(); ++i)
                lines << QStringLiteral("ΔT%1: %2").arg(i + 1).arg(siFormat(valueAt(m_traces[i], *m_cursorB) - valueAt(m_traces[i], *m_cursorA), m_yUnit, 4));
        }
        const QFontMetrics fm(p.font());
        const QRectF box(r.left() + 6, r.top() + 6, 40 + fm.horizontalAdvance(QStringLiteral("ΔT9: -000.0 mV")) + 30,
                         lines.size() * fm.height() + 8);
        p.setPen(QPen(pal.color(QPalette::Mid), 1));
        p.setBrush(pal.color(QPalette::Base));
        p.drawRect(box);
        p.setPen(textColor);
        p.drawText(box.adjusted(5, 4, -5, -4), Qt::AlignLeft | Qt::AlignTop, lines.join(u'\n'));
    }
}

void WaveformView::wheelEvent(QWheelEvent* e)
{
    if (m_x.isEmpty() || m_digital) return QWidget::wheelEvent(e);
    const double factor = std::pow(1.25, e->angleDelta().y() / 120.0);
    const QPointF at = e->position();
    if (e->modifiers() & Qt::ControlModifier)
        zoomY(factor, m_y0 + (plotRect().bottom() - at.y()) / plotRect().height() * (m_y1 - m_y0));
    else
        zoomX(factor, pixelToX(at.x()));
    e->accept();
}

void WaveformView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!m_x.isEmpty() && !m_digital) {
        fit();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
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
    if (m_x.isEmpty() && !m_digital) return;
    QMenu menu(this);
    menu.addAction(tr("Copy image"), this, [this] { QApplication::clipboard()->setImage(grab().toImage()); });
    if (m_x.isEmpty()) {        // digital waveforms: image only
        menu.exec(e->globalPos());
        return;
    }
    menu.addAction(tr("&Fit"), this, &WaveformView::fit);
    QAction* log = menu.addAction(tr("&Logarithmic X"), this, [this](bool on) { setLogX(on); });
    log->setCheckable(true);
    log->setChecked(m_logX);
    log->setEnabled(!m_x.isEmpty() && m_x.first() > 0);
    if (m_cursorA || m_cursorB)
        menu.addAction(tr("Clear cursors"), this, [this] { setCursors({}, {}); });
    menu.addAction(tr("Add &math trace..."), this, [this] {
        QStringList names;
        for (int i = 0; i < m_traces.size(); ++i) names << QStringLiteral("T%1 = %2").arg(i + 1).arg(m_traces[i].name);
        bool ok = false;
        const QString expr = QInputDialog::getText(this, tr("Math trace"),
                                                   names.join(u'\n') + QStringLiteral("\n\n") + tr("Expression (T1-T2, T1*2, abs(T1), db(T1)):"),
                                                   QLineEdit::Normal, QStringLiteral("T1-T2"), &ok);
        QString error;
        if (ok && !addMathTrace(expr, &error))
            QMessageBox::warning(this, tr("Math trace"), error);
    });
    menu.addSeparator();
    menu.addAction(tr("&Store as reference"), this, &WaveformView::store);
    if (!m_stored.isEmpty())
        menu.addAction(tr("Clear reference"), this, &WaveformView::clearStored);
    menu.addSeparator();
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
