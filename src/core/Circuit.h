#pragma once

#include <QMap>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>

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
};

struct Wire {
    ItemId id = 0;
    QVector<QPointF> points;    // polyline vertices, at least two
    bool bus = false;           // drawn thick; does not conduct by itself
};

struct TextNote {
    ItemId id = 0;
    QPointF pos;
    QString text;
    double pointSize = 10;
};

struct TitleBlock {
    QString title, author, date, revision;
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

    TitleBlock titleBlock;

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
    // Any change that affects connectivity or contents.
    void modified();

private:
    ItemId takeId(ItemId requested);

    QMap<ItemId, Component> m_components;
    QMap<ItemId, Wire> m_wires;
    QMap<ItemId, TextNote> m_texts;
    ItemId m_nextId = 1;
};

} // namespace cm
