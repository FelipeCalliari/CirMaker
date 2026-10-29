#pragma once

#include <QMap>
#include <QObject>
#include <QPointF>
#include <QString>
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
};

struct TitleBlock {
    QString title, author, date, revision;
    bool visible = false;
    QPointF pos;                // top-left corner in scene coordinates
    friend bool operator==(const TitleBlock&, const TitleBlock&) = default;
};

// Analog (SPICE) analysis to run. Values are SPICE numbers ("1m", "10k").
struct AnalysisSettings {
    enum class Type { OperatingPoint, Transient, AC, DCSweep };
    bool analogMode = false;
    Type type = Type::Transient;
    QString tranStop = QStringLiteral("10m"), tranStep = QStringLiteral("10u");
    QString acStart = QStringLiteral("10"), acStop = QStringLiteral("1Meg");
    int acPointsPerDecade = 20;
    QString dcSource, dcStart = QStringLiteral("0"), dcStop = QStringLiteral("5"), dcStep = QStringLiteral("0.05");
    friend bool operator==(const AnalysisSettings&, const AnalysisSettings&) = default;
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
    ItemId m_nextId = 1;
};

} // namespace cm
