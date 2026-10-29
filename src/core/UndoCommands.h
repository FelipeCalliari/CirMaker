#pragma once

#include "core/Circuit.h"

#include <QUndoCommand>

namespace cm {

// Snapshot of a set of items, used to add or remove them as one undo step.
struct Selection {
    QVector<Component> components;
    QVector<Wire> wires;
    QVector<TextNote> texts;
    bool isEmpty() const { return components.isEmpty() && wires.isEmpty() && texts.isEmpty(); }
};

class AddItemsCommand : public QUndoCommand {
public:
    AddItemsCommand(Circuit* circuit, Selection items, const QString& text);
    void redo() override;
    void undo() override;
    // Ids assigned on the first redo.
    const Selection& items() const { return m_items; }

private:
    Circuit* m_circuit;
    Selection m_items;
};

class RemoveItemsCommand : public QUndoCommand {
public:
    RemoveItemsCommand(Circuit* circuit, Selection items, const QString& text);
    void redo() override;
    void undo() override;

private:
    Circuit* m_circuit;
    Selection m_items;
};

// Replaces items with new versions (move, rotate, edit properties, reshape a wire).
class ModifyItemsCommand : public QUndoCommand {
public:
    ModifyItemsCommand(Circuit* circuit, Selection before, Selection after, const QString& text);
    void redo() override;
    void undo() override;

private:
    static void apply(Circuit* c, const Selection& s);
    Circuit* m_circuit;
    Selection m_before, m_after;
};

} // namespace cm
