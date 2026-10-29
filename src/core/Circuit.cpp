#include "core/Circuit.h"

#include <QRegularExpression>

namespace cm {

Circuit::Circuit(QObject* parent) : QObject(parent) {}

const Component* Circuit::component(ItemId id) const
{
    auto it = m_components.constFind(id);
    return it == m_components.cend() ? nullptr : &*it;
}

const Wire* Circuit::wire(ItemId id) const
{
    auto it = m_wires.constFind(id);
    return it == m_wires.cend() ? nullptr : &*it;
}

const TextNote* Circuit::text(ItemId id) const
{
    auto it = m_texts.constFind(id);
    return it == m_texts.cend() ? nullptr : &*it;
}

// Undo/redo re-inserts items with their original id, so honour a requested id.
ItemId Circuit::takeId(ItemId requested)
{
    if (requested == 0)
        return m_nextId++;
    m_nextId = std::max(m_nextId, requested + 1);
    return requested;
}

ItemId Circuit::addComponent(Component c)
{
    c.id = takeId(c.id);
    m_components.insert(c.id, c);
    emit componentAdded(c.id);
    emit modified();
    return c.id;
}

void Circuit::updateComponent(const Component& c)
{
    if (!m_components.contains(c.id))
        return;
    m_components[c.id] = c;
    emit componentChanged(c.id);
    emit modified();
}

void Circuit::removeComponent(ItemId id)
{
    if (m_components.remove(id)) {
        emit componentRemoved(id);
        emit modified();
    }
}

ItemId Circuit::addWire(Wire w)
{
    w.id = takeId(w.id);
    m_wires.insert(w.id, w);
    emit wireAdded(w.id);
    emit modified();
    return w.id;
}

void Circuit::updateWire(const Wire& w)
{
    if (!m_wires.contains(w.id))
        return;
    m_wires[w.id] = w;
    emit wireChanged(w.id);
    emit modified();
}

void Circuit::removeWire(ItemId id)
{
    if (m_wires.remove(id)) {
        emit wireRemoved(id);
        emit modified();
    }
}

ItemId Circuit::addText(TextNote t)
{
    t.id = takeId(t.id);
    m_texts.insert(t.id, t);
    emit textAdded(t.id);
    emit modified();
    return t.id;
}

void Circuit::updateText(const TextNote& t)
{
    if (!m_texts.contains(t.id))
        return;
    m_texts[t.id] = t;
    emit textChanged(t.id);
    emit modified();
}

void Circuit::removeText(ItemId id)
{
    if (m_texts.remove(id)) {
        emit textRemoved(id);
        emit modified();
    }
}

void Circuit::clear()
{
    m_components.clear();
    m_wires.clear();
    m_texts.clear();
    m_nextId = 1;
    titleBlock = {};
    emit cleared();
    emit modified();
}

QString Circuit::nextDesignator(const QString& prefix) const
{
    const QRegularExpression re(QStringLiteral("^%1(\\d+)").arg(QRegularExpression::escape(prefix)));
    int highest = 0;
    for (const Component& c : m_components) {
        auto m = re.match(c.designator);
        if (m.hasMatch())
            highest = std::max(highest, m.captured(1).toInt());
    }
    return prefix + QString::number(highest + 1);
}

} // namespace cm
