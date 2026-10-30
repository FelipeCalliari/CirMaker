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

class SetTitleBlockCommand : public QUndoCommand {
public:
    SetTitleBlockCommand(Circuit* circuit, TitleBlock after, const QString& text);
    void redo() override { m_circuit->setTitleBlock(m_after); }
    void undo() override { m_circuit->setTitleBlock(m_before); }

private:
    Circuit* m_circuit;
    TitleBlock m_before, m_after;
};

class SetAnalysisCommand : public QUndoCommand {
public:
    SetAnalysisCommand(Circuit* circuit, AnalysisSettings after, const QString& text)
        : QUndoCommand(text), m_circuit(circuit), m_before(circuit->analysis()), m_after(std::move(after)) {}
    void redo() override { m_circuit->setAnalysis(m_after); }
    void undo() override { m_circuit->setAnalysis(m_before); }

private:
    Circuit* m_circuit;
    AnalysisSettings m_before, m_after;
};

class SetFaultSettingsCommand : public QUndoCommand {
public:
    SetFaultSettingsCommand(Circuit* circuit, FaultSettings after, const QString& text)
        : QUndoCommand(text), m_circuit(circuit), m_before(circuit->faultSettings()), m_after(std::move(after)) {}
    void redo() override { m_circuit->setFaultSettings(m_after); }
    void undo() override { m_circuit->setFaultSettings(m_before); }

private:
    Circuit* m_circuit;
    FaultSettings m_before, m_after;
};

class SetProbesCommand : public QUndoCommand {
public:
    SetProbesCommand(Circuit* circuit, QVector<QPointF> after, const QString& text)
        : QUndoCommand(text), m_circuit(circuit), m_before(circuit->probes()), m_after(std::move(after)) {}
    void redo() override { m_circuit->setProbes(m_after); }
    void undo() override { m_circuit->setProbes(m_before); }

private:
    Circuit* m_circuit;
    QVector<QPointF> m_before, m_after;
};

} // namespace cm
