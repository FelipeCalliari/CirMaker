#include "ui/SignalGenDialog.h"

#include "core/Generator.h"
#include "library/SymbolDef.h"
#include "sim/analog/SpiceNetlister.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace cm {

namespace {

struct Field { const char* key; const char* caption; const char* fallback; };

// Page fields, in the order of SignalGenDialog::waves().
const QList<QList<Field>>& pageFields()
{
    static const QList<QList<Field>> pages = {
        {{"offset", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "DC offset (VO):"), "0"}, {"amplitude", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Peak amplitude (VA):"), "1"},
         {"frequency", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Frequency (Hz):"), "1k"}, {"delay", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Start delay (s):"), "0"},
         {"damping", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Damping factor (1/s):"), "0"}},
        {{"offset", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "DC offset (VO):"), "0"}, {"amplitude", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Peak amplitude (VA):"), "1"},
         {"frequency", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Frequency (Hz):"), "1k"}},
        {{"offset", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "DC offset (VO):"), "0"}, {"amplitude", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Peak amplitude (VA):"), "1"},
         {"frequency", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Frequency (Hz):"), "1k"}},
        {{"v1", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Initial amplitude (VI):"), "0"}, {"v2", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Pulse amplitude (VP):"), "5"},
         {"period", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Period (s):"), "1m"}, {"width", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Pulse width (s):"), "0.5m"},
         {"rise", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Rise time (s):"), "1n"}, {"fall", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Fall time (s):"), "1n"},
         {"delay", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Delay to start (s):"), "0"}},
        {{"offset", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "DC offset (VO):"), "0"}, {"amplitude", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Peak amplitude (VA):"), "1"},
         {"carrier", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Carrier frequency (Hz):"), "10k"}, {"mdi", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Modulation index:"), "0.5"},
         {"signal", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Signal frequency (Hz):"), "1k"}},
        {{"offset", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "DC offset (VO):"), "0"}, {"amplitude", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Peak amplitude (VA):"), "1"},
         {"carrier", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Carrier frequency (Hz):"), "10k"}, {"mdi", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Modulation index:"), "1"},
         {"signal", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Signal frequency (Hz):"), "1k"}},
        {{"v1", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Initial amplitude (VI):"), "0"}, {"v2", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Pulse amplitude (VP):"), "5"},
         {"rd", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Rise time delay (s):"), "0"}, {"rt", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Rise time constant (s):"), "1m"},
         {"fd", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Fall time delay (s):"), "5m"}, {"ft", QT_TRANSLATE_NOOP("cm::SignalGenDialog", "Fall time constant (s):"), "1m"}},
        {},
    };
    return pages;
}

} // namespace

QStringList SignalGenDialog::waves()
{
    return {QStringLiteral("sine"), QStringLiteral("square"), QStringLiteral("triangle"), QStringLiteral("pulse"),
            QStringLiteral("am"), QStringLiteral("fm"), QStringLiteral("exp"), QStringLiteral("pwl")};
}

QString SignalGenDialog::parsePwlFile(const QString& text, double maxAmp, double offset, double maxTime)
{
    QVector<double> values;
    for (QString line : text.split(u'\n')) {
        line = line.trimmed();
        if (!line.startsWith(u'+')) continue;          // "*" comments and anything else
        for (const QString& t : line.mid(1).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
            const std::optional<double> v = spice::parseNumber(t);
            if (!v) break;                             // trailing junk ends the line
            values << *v;
        }
    }
    if (values.size() % 2) values.removeLast();
    const int n = int(values.size()) / 2;
    if (n == 0) return {};
    double lo = values[1], hi = values[1];
    for (int i = 0; i < n; ++i) lo = std::min(lo, values[2 * i + 1]), hi = std::max(hi, values[2 * i + 1]);
    const double t0 = values[0], t1 = values[2 * (n - 1)];
    QStringList out;
    for (int i = 0; i < n; ++i) {
        double t = values[2 * i], v = values[2 * i + 1];
        if (maxTime > 0 && t1 > t0) t = (t - t0) / (t1 - t0) * maxTime;
        if (maxAmp > 0 && hi > lo) v = ((v - lo) / (hi - lo) - 0.5) * maxAmp;
        out << spice::formatNumber(t) + u' ' + spice::formatNumber(v + offset);
    }
    return out.join(u'\n');
}

SignalGenDialog::SignalGenDialog(const Component& c, const SymbolDef& symbol, QWidget* parent) : QDialog(parent), m_c(c)
{
    setWindowTitle(tr("%1 properties").arg(symbol.name));
    QVariantMap props = symbol.defaultProps;
    for (auto it = c.props.cbegin(); it != c.props.cend(); ++it) props.insert(it.key(), it.value());

    auto* top = new QVBoxLayout(this);
    auto* head = new QFormLayout;
    m_designator = new QLineEdit(c.designator);
    head->addRow(tr("&Designator:"), m_designator);
    m_label = new QLineEdit(c.label);
    head->addRow(tr("&Label:"), m_label);
    m_wave = new QComboBox;
    const QStringList captions = {tr("Sine wave"), tr("Square"), tr("Triangle"), tr("Pulse/Sawtooth"), tr("AM signal"),
                                  tr("FM signal"), tr("Exponential"), tr("Piece-wise linear")};
    for (int i = 0; i < waves().size(); ++i) m_wave->addItem(captions[i], waves()[i]);
    head->addRow(tr("&Wave:"), m_wave);
    m_unit = new QComboBox;
    m_unit->addItem(tr("Volts"), false);
    m_unit->addItem(tr("Amps"), true);
    m_unit->setCurrentIndex(props.value(QStringLiteral("current")).toBool() ? 1 : 0);
    head->addRow(tr("&Source:"), m_unit);
    top->addLayout(head);

    m_pages = new QStackedWidget;
    for (int page = 0; page < pageFields().size(); ++page) {
        auto* w = new QWidget;
        auto* form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        for (const Field& f : pageFields()[page]) {
            auto* edit = new QLineEdit(props.value(QLatin1String(f.key), QLatin1String(f.fallback)).toString());
            m_fields[page].insert(QLatin1String(f.key), edit);
            form->addRow(tr(f.caption), edit);
        }
        if (waves()[page] == u"pwl") {
            m_pwl = new QPlainTextEdit(props.value(QStringLiteral("pwl")).toString());
            m_pwl->setPlaceholderText(tr("time value, one point per line (e.g. 0 0, 1m 5, 2m 0)"));
            form->addRow(tr("&Points:"), m_pwl);
            auto* load = new QPushButton(tr("Load .PWL &file..."));
            connect(load, &QPushButton::clicked, this, &SignalGenDialog::loadPwl);
            form->addRow(QString(), load);
        }
        m_pages->addWidget(w);
    }
    auto* group = new QGroupBox(tr("Wave parameters"));
    (new QVBoxLayout(group))->addWidget(m_pages);
    top->addWidget(group);
    connect(m_wave, &QComboBox::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_wave->setCurrentIndex(std::max(0, int(waves().indexOf(props.value(QStringLiteral("wave"), QStringLiteral("sine")).toString().toLower()))));
    m_pages->setCurrentIndex(m_wave->currentIndex());

    auto* acBox = new QGroupBox(tr("AC analysis"));
    auto* acForm = new QFormLayout(acBox);
    m_ac = new QCheckBox(tr("Use as AC &source"));
    m_ac->setChecked(props.value(QStringLiteral("ac"), true).toBool());
    acForm->addRow(m_ac);
    m_acMag = new QLineEdit(props.value(QStringLiteral("acMag"), QStringLiteral("1")).toString());
    m_acPhase = new QLineEdit(props.value(QStringLiteral("acPhase"), QStringLiteral("0")).toString());
    acForm->addRow(tr("Magnitude:"), m_acMag);
    acForm->addRow(tr("Phase (degrees):"), m_acPhase);
    top->addWidget(acBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    top->addWidget(buttons);
}

void SignalGenDialog::loadPwl()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Piece-wise linear data"), {},
                                                      tr("PWL data (*.pwl *.PWL *.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot open %1").arg(path));
        return;
    }
    const QString text = QString::fromLatin1(f.readAll());
    bool ok = false;
    const QString scaling = QInputDialog::getText(this, tr("Scale PWL data"),
                                                  tr("Max. amplitude, offset, max. time (0 = keep the file's):"),
                                                  QLineEdit::Normal, QStringLiteral("0 0 0"), &ok);
    if (!ok) return;
    const QStringList s = scaling.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    auto at = [&](int i) { return spice::parseNumber(s.value(i)).value_or(0); };
    const QString points = parsePwlFile(text, at(0), at(1), at(2));
    if (points.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("No \"+ time value\" lines in %1").arg(path));
        return;
    }
    m_pwl->setPlainText(points);
}

Component SignalGenDialog::result() const
{
    Component c = m_c;
    c.designator = m_designator->text().trimmed();
    c.label = m_label->text().trimmed();
    const int page = m_wave->currentIndex();
    c.props.insert(QStringLiteral("wave"), waves()[page]);
    for (auto it = m_fields[page].cbegin(); it != m_fields[page].cend(); ++it)
        c.props.insert(it.key(), it.value()->text().trimmed());
    if (waves()[page] == u"pwl")
        c.props.insert(QStringLiteral("pwl"), m_pwl->toPlainText().trimmed());
    if (m_unit->currentData().toBool()) c.props.insert(QStringLiteral("current"), true);
    else c.props.remove(QStringLiteral("current"));
    c.props.insert(QStringLiteral("ac"), m_ac->isChecked());
    c.props.insert(QStringLiteral("acMag"), m_acMag->text().trimmed());
    c.props.insert(QStringLiteral("acPhase"), m_acPhase->text().trimmed());
    // CircuitMaker's label follows the waveform ("-1/1V"); one of the user's own stays
    if (generator::automaticLabel(c.label)) c.label = generator::label(c.props);
    return c;
}

} // namespace cm
