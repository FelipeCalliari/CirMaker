#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace cm::generator {

// A SPICE number with its scale suffix: "1k" 1000, "100m" 0.1, "2.5Meg"
inline double spiceNumber(const QString& text, double fallback = 0)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\\s*([a-zA-Z]*)"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return fallback;
    double v = m.captured(1).toDouble();
    const QString s = m.captured(2).toLower();
    if (s.startsWith(u"meg")) v *= 1e6;
    else if (s.startsWith(u'g')) v *= 1e9;
    else if (s.startsWith(u'k')) v *= 1e3;
    else if (s.startsWith(u"mil")) v *= 25.4e-6;
    else if (s.startsWith(u'm')) v *= 1e-3;
    else if (s.startsWith(u'u')) v *= 1e-6;
    else if (s.startsWith(u'n')) v *= 1e-9;
    else if (s.startsWith(u'p')) v *= 1e-12;
    else if (s.startsWith(u'f')) v *= 1e-15;
    else if (s.startsWith(u't')) v *= 1e12;
    return v;
}

// CircuitMaker's engineering form, three digits: "-100m", "11.8", "2.56"
inline QString eng(double v)
{
    if (v == 0) return QStringLiteral("0");
    static const struct { double scale; const char* prefix; } steps[] = {
        {1e9, "G"}, {1e6, "Meg"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}};
    for (const auto& st : steps)
        if (std::abs(v) >= st.scale * (1 - 1e-9) || st.scale == 1e-12)
            return QString::number(v / st.scale, 'g', 3) + QString::fromLatin1(st.prefix);
    return QString::number(v, 'g', 3);
}

inline double prop(const QVariantMap& p, const char* key, const char* fallback)
{
    return spiceNumber(p.value(QString::fromLatin1(key), QString::fromLatin1(fallback)).toString());
}

// The lowest and highest the waveform reaches.
inline std::pair<double, double> levels(const QVariantMap& p)
{
    const QString wave = p.value(QStringLiteral("wave"), QStringLiteral("sine")).toString();
    if (wave == u"pulse") return {prop(p, "v1", "0"), prop(p, "v2", "5")};
    if (wave == u"exp") {
        const double a = prop(p, "v1", "0"), b = prop(p, "v2", "1");
        return {std::min(a, b), std::max(a, b)};
    }
    if (wave == u"pwl") {
        QVector<double> pts;
        static const QRegularExpression sep(QStringLiteral("[\\s,;]+"));
        for (const QString& t : p.value(QStringLiteral("pwl")).toString().split(sep, Qt::SkipEmptyParts)) pts << spiceNumber(t);
        if (pts.size() < 2) return {0, 0};
        double lo = pts[1], hi = pts[1];
        for (int k = 1; k < pts.size(); k += 2) lo = std::min(lo, pts[k]), hi = std::max(hi, pts[k]);
        return {lo, hi};
    }
    const double off = prop(p, "offset", "0"), amp = prop(p, "amplitude", "1");
    return {off - amp, off + amp};
}

// The label CircuitMaker gives a generator: its levels, "-100m/100mV".
inline QString label(const QVariantMap& p)
{
    const auto [lo, hi] = levels(p);
    return eng(lo) + u'/' + eng(hi) + (p.value(QStringLiteral("current")).toBool() ? u'A' : u'V');
}

// A label CircuitMaker would have written (levels) or ours from before ("1kHz"):
// it follows the waveform. Anything else is the user's own.
inline bool automaticLabel(const QString& text)
{
    static const QRegularExpression levelsForm(QStringLiteral("^\\s*[-+]?[\\d.]+[a-zA-Z]*\\s*/\\s*[-+]?[\\d.]+[a-zA-Z]*\\s*$"));
    return text.trimmed().isEmpty() || text == u"1kHz" || levelsForm.match(text).hasMatch();
}

// The repetition rate, 0 when there is none (piecewise linear, exponential).
inline double frequency(const QVariantMap& p)
{
    const QString wave = p.value(QStringLiteral("wave"), QStringLiteral("sine")).toString();
    if (wave == u"pulse") return 1 / std::max(prop(p, "period", "1m"), 1e-15);
    if (wave == u"fm") return prop(p, "carrier", "10k");
    if (wave == u"pwl" || wave == u"exp") return 0;
    return prop(p, "frequency", "1k");
}

// The frequency as CircuitMaker writes it in the generator: three digits, a
// point when they are not the whole of it, and the prefix: "769. Hz", "1kHz".
inline QString frequencyText(double f)
{
    if (f <= 0) return {};
    static const struct { double scale; const char* prefix; } steps[] = {
        {1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, " "}, {1e-3, "m"}};
    for (const auto& st : steps)
        if (f >= st.scale * (1 - 1e-9) || st.scale == 1e-3) {
            const double v = f / st.scale;
            QString n = QString::number(v, 'g', 3);
            if (!n.contains(u'.') && !n.contains(u'e') && std::abs(n.toDouble() - v) > 1e-9 * std::abs(v)) n += u'.';
            return n + QString::fromLatin1(st.prefix) + QStringLiteral("Hz");
        }
    return {};
}

} // namespace cm::generator
