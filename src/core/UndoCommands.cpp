#include "core/UndoCommands.h"

namespace cm {

AddItemsCommand::AddItemsCommand(Circuit* circuit, Selection items, const QString& text)
    : QUndoCommand(text), m_circuit(circuit), m_items(std::move(items)) {}

void AddItemsCommand::redo()
{
    for (Component& c : m_items.components)
        c.id = m_circuit->addComponent(c);
    for (Wire& w : m_items.wires)
        w.id = m_circuit->addWire(w);
    for (TextNote& t : m_items.texts)
        t.id = m_circuit->addText(t);
}

void AddItemsCommand::undo()
{
    for (const Component& c : m_items.components)
        m_circuit->removeComponent(c.id);
    for (const Wire& w : m_items.wires)
        m_circuit->removeWire(w.id);
    for (const TextNote& t : m_items.texts)
        m_circuit->removeText(t.id);
}

RemoveItemsCommand::RemoveItemsCommand(Circuit* circuit, Selection items, const QString& text)
    : QUndoCommand(text), m_circuit(circuit), m_items(std::move(items)) {}

void RemoveItemsCommand::redo()
{
    for (const Component& c : m_items.components)
        m_circuit->removeComponent(c.id);
    for (const Wire& w : m_items.wires)
        m_circuit->removeWire(w.id);
    for (const TextNote& t : m_items.texts)
        m_circuit->removeText(t.id);
}

void RemoveItemsCommand::undo()
{
    for (const Component& c : m_items.components)
        m_circuit->addComponent(c);
    for (const Wire& w : m_items.wires)
        m_circuit->addWire(w);
    for (const TextNote& t : m_items.texts)
        m_circuit->addText(t);
}

ModifyItemsCommand::ModifyItemsCommand(Circuit* circuit, Selection before, Selection after, const QString& text)
    : QUndoCommand(text), m_circuit(circuit), m_before(std::move(before)), m_after(std::move(after)) {}

void ModifyItemsCommand::apply(Circuit* c, const Selection& s)
{
    for (const Component& x : s.components)
        c->updateComponent(x);
    for (const Wire& x : s.wires)
        c->updateWire(x);
    for (const TextNote& x : s.texts)
        c->updateText(x);
}

void ModifyItemsCommand::redo() { apply(m_circuit, m_after); }
void ModifyItemsCommand::undo() { apply(m_circuit, m_before); }

SetTitleBlockCommand::SetTitleBlockCommand(Circuit* circuit, TitleBlock after, const QString& text)
    : QUndoCommand(text), m_circuit(circuit), m_before(circuit->titleBlock()), m_after(std::move(after)) {}

} // namespace cm
