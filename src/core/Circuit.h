#pragma once

#include <QMap>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace cm {

using ItemId = quint64;

struct Component {
    ItemId id = 0;
    QString symbolId;
    QString designator;     // "U1", "S3"
    QString label;          // value / net-label text shown next to the symbol
    QPointF pos;
    int rotation = 0;       // quarter turns, clockwise
    bool mirrored = false;
    QVariantMap props;      // model parameters (pulser period, switch state, ...)
    // Text display; unset = the symbol's default. Offsets are the text
    // centre relative to pos, in scene orientation.
    std::optional<bool> showDesignator, showLabel;
    std::optional<QPointF> designatorAt, labelAt;
};

struct Wire {
    ItemId id = 0;
    QVector<QPointF> points;    // polyline vertices, at least two
    bool bus = false;           // drawn thick; does not conduct by itself
    QString name;               // bus name, shown in a box at the bus end
};

struct TextNote {
    ItemId id = 0;
    QPointF pos;
    QString text;
    double pointSize = 10;
    // a node name (CircuitMaker's): names the net of the wire at pos + nameAt
    // without joining anything; SPICE and probes use the name
    std::optional<QPointF> nameAt;
};

struct TitleBlock {
    QString title, author, date, revision;
    bool visible = false;
    QPointF pos;                // top-left corner in scene coordinates
    friend bool operator==(const TitleBlock&, const TitleBlock&) = default;
};

// Analog (SPICE) analysis to run. Values are SPICE numbers ("1m", "10k").
struct AnalysisSettings {
    enum class Type { OperatingPoint, Transient, AC, DCSweep, Fourier, TransferFunction, Noise };
    // Repeats the analysis: over temperatures, over a device's values, or
    // with random tolerances (Monte Carlo); the curves are overlaid.
    enum class Sweep { None, Temperature, Parameter, MonteCarlo };
    bool analogMode = false;
    Type type = Type::Transient;
    QString tranStop = QStringLiteral("10m"), tranStep = QStringLiteral("10u");
    QString acStart = QStringLiteral("10"), acStop = QStringLiteral("1Meg");
    int acPointsPerDecade = 20;
    QString dcSource, dcStart = QStringLiteral("0"), dcStop = QStringLiteral("5"), dcStep = QStringLiteral("0.05");
    // Fourier: of the transient; empty frequency = the first signal generator's
    QString fourierFreq;
    int fourierHarmonics = 9;
    // Transfer function and noise: output net name, input source designator
    QString output, inputSource;
    Sweep sweep = Sweep::None;
    QString tempStart = QStringLiteral("0"), tempStop = QStringLiteral("100"), tempStep = QStringLiteral("25");
    QString paramDevice, paramStart = QStringLiteral("1k"), paramStop = QStringLiteral("10k"), paramStep = QStringLiteral("3k");
    int mcRuns = 10;
    QString mcTolerance = QStringLiteral("10");      // percent, on R, C and L values
    // SPICE options for the run (".OPTIONS METHOD=GEAR ITL4=100" without the keyword)
    QString spiceOptions;
    // CircuitMaker's Analyses Setup as read from a .ckt, written back: the
    // analyses enabled (1 operating point, 4 DC, 8 AC, 16 transient; 2048
    // "Always set defaults"; -1: none read) and the DC sweep's second source
    int cmAnalyses = -1;
    // and the header's option bits (-1: none read) and "0 0 30 100 10" line
    int cmOptions = -1;
    QString cmSimLine;
    QString dcSource2, dcStart2 = QStringLiteral("0"), dcStop2 = QStringLiteral("0"), dcStep2 = QStringLiteral("0");
    // Digital breakpoints (CircuitMaker Digital Options): level or edge
    // conditions, all of them or any one.
    bool breakEdge = true, breakAll = false;
    friend bool operator==(const AnalysisSettings&, const AnalysisSettings&) = default;
};

// CircuitMaker "Circuit Fault Data": the fault lock and what a student may
// not do while it is locked, plus the analog fault element values.
struct FaultSettings {
    QString password;               // faults::hashPassword() digest; empty = not locked
    // "edit" (cut/copy/paste/move), "delete", "wire", "library", "properties",
    // "hints", "replace", "replaceStatus", "trace", "nodeNames"
    QStringList disabled;
    QString high = QStringLiteral("5"), low = QStringLiteral("0");
    QString open = QStringLiteral("1G"), shorted = QStringLiteral("1m");
    bool locked() const { return !password.isEmpty(); }
    // A student restriction is in force (only while locked).
    bool disables(const QString& option) const { return locked() && disabled.contains(option); }
    friend bool operator==(const FaultSettings&, const FaultSettings&) = default;
};

// The schematic document. Mutated only through these methods (normally via the
// undo commands), which emit the signals the scene uses to stay in sync.
class Circuit : public QObject {
    Q_OBJECT
public:
    explicit Circuit(QObject* parent = nullptr);

    const QMap<ItemId, Component>& components() const { return m_components; }
    const QMap<ItemId, Wire>& wires() const { return m_wires; }
    const QMap<ItemId, TextNote>& texts() const { return m_texts; }

    const Component* component(ItemId id) const;
    const Wire* wire(ItemId id) const;
    const TextNote* text(ItemId id) const;

    ItemId addComponent(Component c);
    void updateComponent(const Component& c);
    void removeComponent(ItemId id);

    ItemId addWire(Wire w);
    void updateWire(const Wire& w);
    void removeWire(ItemId id);

    ItemId addText(TextNote t);
    void updateText(const TextNote& t);
    void removeText(ItemId id);

    void clear();
    bool isEmpty() const { return m_components.isEmpty() && m_wires.isEmpty() && m_texts.isEmpty(); }

    // First free designator for a prefix: "U" -> "U4" when U1..U3 exist.
    QString nextDesignator(const QString& prefix) const;

    const TitleBlock& titleBlock() const { return m_titleBlock; }
    void setTitleBlock(const TitleBlock& tb);

    const AnalysisSettings& analysis() const { return m_analysis; }
    void setAnalysis(const AnalysisSettings& a);
    // Waveform probe points; each one plots the net under it.
    const QVector<QPointF>& probes() const { return m_probes; }
    void setProbes(const QVector<QPointF>& probes);
    // A probe's own name (CircuitMaker files name theirs: "Clk", "GCM_2"),
    // by its point; empty: none
    // A CircuitMaker probe placed as a part (SAVE-) keeps its record.
    struct ProbeTag {
        QPointF at;
        QString name, cmRecord;
        QStringList cmHeader;       // the .ckt header's entries for it: "name|analysis|index|colour"
        std::optional<QPoint> cmAt; // its point in the .ckt, kept while the probe stays put
    };
    QString probeName(QPointF at) const;
    const ProbeTag* probeTag(QPointF at) const;
    const QVector<ProbeTag>& probeNames() const { return m_probeNames; }
    void setProbeNames(const QVector<ProbeTag>& names) { m_probeNames = names; }
    const FaultSettings& faultSettings() const { return m_faults; }
    void setFaultSettings(const FaultSettings& f);

signals:
    void componentAdded(cm::ItemId id);
    void componentChanged(cm::ItemId id);
    void componentRemoved(cm::ItemId id);
    void wireAdded(cm::ItemId id);
    void wireChanged(cm::ItemId id);
    void wireRemoved(cm::ItemId id);
    void textAdded(cm::ItemId id);
    void textChanged(cm::ItemId id);
    void textRemoved(cm::ItemId id);
    void cleared();
    void titleBlockChanged();
    void analysisChanged();
    void probesChanged();
    void faultSettingsChanged();
    // Any change that affects connectivity or contents.
    void modified();

private:
    ItemId takeId(ItemId requested);

    QMap<ItemId, Component> m_components;
    QMap<ItemId, Wire> m_wires;
    QMap<ItemId, TextNote> m_texts;
    TitleBlock m_titleBlock;
    AnalysisSettings m_analysis;
    QVector<QPointF> m_probes;
    QVector<ProbeTag> m_probeNames;
    FaultSettings m_faults;
    ItemId m_nextId = 1;
};

} // namespace cm
