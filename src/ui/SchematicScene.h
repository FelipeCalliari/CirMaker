#pragma once

#include "core/Circuit.h"
#include "core/NetlistBuilder.h"
#include "core/UndoCommands.h"
#include "sim/digital/Logic.h"

#include <QGraphicsScene>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>

class QUndoStack;

namespace cm {

class ComponentItem;
class WireItem;
class TextNoteItem;
class TitleBlockItem;
class LibraryRegistry;
struct SymbolDef;
namespace sim { class EventSimulator; }

class SchematicScene : public QGraphicsScene {
    Q_OBJECT
public:
    static constexpr double kGrid = 10;
    enum class Tool { Select, Wire, Bus, NetLabel, Text, Probe, Delete, Zoom, Place };

    SchematicScene(Circuit* circuit, QUndoStack* undo, const LibraryRegistry& library, QObject* parent = nullptr);
    ~SchematicScene() override;

    Circuit* circuit() const { return m_circuit; }
    const LibraryRegistry& library() const { return m_library; }
    const Netlist& netlist() const { return m_netlist; }
    // The circuit the netlist describes: the document with macros expanded.
    // While simulating with faults: the faulted copy.
    const Circuit& simCircuit() const { return m_faulted ? *m_faulted : m_flat ? *m_flat : *m_circuit; }
    // Circuit and netlist for an analog run, with the analog faults applied.
    struct AnalogInput { std::unique_ptr<Circuit> circuit; Netlist netlist; };
    AnalogInput analogInput() const;

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    void beginPlace(const QString& symbolId);

    // editing (no-ops while simulating)
    void rotateSelection();
    void mirrorSelection();
    void deleteSelection();
    void selectAll();
    void copySelection();
    void cutSelection();
    void duplicateSelection();
    // Edit menu tools from CircuitMaker
    void nudgeSelection(QPointF delta);
    void straightenWires();
    void resetLabelPositions();
    // Renumbers designations per prefix (only `prefix` when not empty),
    // in reading order, starting at `first`; selection only when not empty.
    void setDesignations(const QString& prefix, int first);
    void setPropDelays(int ticks);
    int highlightedNet() const { return m_highlightNet; }
    // Node under the probe tool while simulating (-1: none).
    int probedNet() const { return m_probeNet; }
    void setShowNodeNames(bool on) { m_showNodeNames = on && !restricted(QStringLiteral("nodeNames")); update(); }
    void setShowPinDots(bool on) { m_showPinDots = on; update(); }
    // CircuitMaker "Trace": colour wires by logic level while simulating
    void setTrace(bool on) { m_trace = on && !restricted(QStringLiteral("trace")); update(); }
    // Fault lock: CircuitMaker's disabled circuit options ("edit", "delete",
    // "wire", "library", "properties", "trace"...) in force for the student.
    void setRestrictions(const QStringList& options);
    bool restricted(const QString& option) const { return m_restrictions.contains(option); }
    bool trace() const { return m_trace; }
    void paste();
    // Adds a copy of `source` moved by offset, selected, as one undo step.
    void insertCircuit(const Circuit& source, QPointF offset, const QString& undoText);
    void mergeCircuit(const Circuit& source);
    bool hasSelection() const { return !selectedItems().isEmpty(); }
    Selection selection() const;
    void editItem(QGraphicsItem* item);
    // RAM/ROM contents: the "data" property, or the live memory while simulating.
    static bool isMemory(const SymbolDef& symbol);
    void editMemory(ItemId id);
    // Selects components whose designator, label or device name contains
    // text (case-insensitive); returns how many were selected.
    int findAndSelect(const QString& text);

    // analog results shown on the schematic until the next edit
    struct AnalogOverlay {
        QHash<int, double> netVoltage;          // operating point only
        QHash<ItemId, QString> meterText;
        QHash<ItemId, int> displayState;
    };
    void setAnalogOverlay(AnalogOverlay overlay);
    void clearAnalogOverlay();
    bool analogOverlayShown() const { return m_overlay.has_value(); }
    int analogState(ItemId id) const { return m_overlay ? m_overlay->displayState.value(id, 0) : 0; }
    QString meterText(ItemId id) const { return m_overlay ? m_overlay->meterText.value(id) : QString(); }
    // Net under each probe point (-1 when the probe sits on nothing).
    QVector<int> probeNets() const;
    void refreshNetlist() { rebuildNetlist(); }
    // Re-reads symbols after a library change (a macro was redefined).
    void refreshSymbols();

    // title block (no-ops while simulating)
    void editTitleBlock();
    void setTitleBlockVisible(bool visible);

    // simulation
    bool simulating() const { return m_sim != nullptr; }
    bool running() const { return m_timer.isActive(); }
    void run();
    void pause();
    void stepSimulation();
    void resetSimulation();
    void stopSimulation();
    void setTicksPerFrame(int ticks) { m_ticksPerFrame = std::max(1, ticks); }
    int ticksPerFrame() const { return m_ticksPerFrame; }
    quint64 simTime() const;
    sim::EventSimulator* simulator() const { return m_sim.get(); }
    // Nets shown in the digital waveform window: SCOPE devices and probes.
    struct Lane { QString name; int net; ItemId component = 0; };   // component: the SCOPE
    const QVector<Lane>& waveformLanes() const { return m_lanes; }
    int simState(ItemId id) const;
    // Breakpoint of a SCOPE: 0 none, 1 break on 0, 2 break on 1 (property "break").
    int breakCondition(ItemId scope) const;
    void setBreakCondition(ItemId scope, int condition);
    QColor wireColor(ItemId id) const;

    static QPointF snap(QPointF p);

    // Items' scene bounds without the grid (and the border, when shown), for export/printing.
    QRectF contentsRect() const;
    // Map border with lettered/numbered zones around the drawing.
    void setBorderVisible(bool on);
    bool borderVisible() const { return m_border; }
    QRectF borderRect() const;
    static constexpr double kZone = 300, kBorderMargin = 30;
    void setGridVisible(bool on) { m_gridVisible = on; update(); }
    void setGridLines(bool lines) { m_gridLines = lines; update(); }
    bool gridVisible() const { return m_gridVisible; }
    // Hides grid and selection while rendering to an export device.
    void setExporting(bool on);

signals:
    void toolChanged(cm::SchematicScene::Tool tool);
    void simulationChanged();          // started, stopped, paused, resumed
    void simTimeChanged(quint64 ticks);
    void statusMessage(const QString& text);
    void cursorMoved(QPointF scenePos);
    // Zoom tool click: factor > 1 zooms in around the point
    void zoomRequested(QPointF scenePos, double factor);
    // Right click with the select tool; item is null over empty space.
    void contextMenuRequested(QGraphicsItem* item, QPoint screenPos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;

private:
    // model -> items
    void onComponentAdded(ItemId id);
    void onComponentChanged(ItemId id);
    void onComponentRemoved(ItemId id);
    void onWireAdded(ItemId id);
    void onWireChanged(ItemId id);
    void onWireRemoved(ItemId id);
    void onTextAdded(ItemId id);
    void onTextChanged(ItemId id);
    void onTextRemoved(ItemId id);
    void onCleared();
    void onTitleBlockChanged();
    void scheduleNetlist();
    void rebuildNetlist();
    void updateSceneRect();
    QRectF itemsRect() const;
    void drawBorder(QPainter* p) const;

    // wire tool
    void wireClick(QPointF p);
    void finishWire();
    bool onBus(QPointF p) const;
    bool onConductor(QPointF p) const;      // pin or ordinary wire
    QString askNetName(const QString& title, const QString& suggestion);
    void placeNetLabel(QPointF p);
    void deleteAt(QPointF p, bool snipWire);
    void toggleProbe(QPointF p);
    int netAt(QPointF p) const;
    void cancelWire();
    bool isConnectionPoint(QPointF p) const;
    bool pinAt(QPointF p, QPointF* pinPos) const;

    // reshaping a single wire
    struct WireEdit {
        ItemId wire = 0;
        QVector<QPointF> original;
        QPointF start;
        int segment = -1;       // segment being slid, or
        int endpoint = -1;      // free end being dragged
    };
    bool beginWireEdit(QGraphicsSceneMouseEvent* e);
    static int segmentAt(const QVector<QPointF>& pts, QPointF p);
    QVector<QPointF> editedWire(QPointF cursor) const;
    void updateHoverCursor(QPointF p);

    // dragging
    void beginDrag();
    QHash<ItemId, QVector<QPointF>> rubberBand() const;
    void updateDragPreview();
    void commitDrag();

    void cancelPlace();
    void placeGhost(QPointF scenePos);
    void transformSelection(bool rotate);
    void simInteractPress(QGraphicsSceneMouseEvent* e);
    void simInteractRelease();
    void advance();
    void persistSimProps();
    bool breakpointHit();
    QWidget* dialogParent() const;

    Circuit* m_circuit;
    QUndoStack* m_undo;
    const LibraryRegistry& m_library;

    QHash<ItemId, ComponentItem*> m_componentItems;
    QHash<ItemId, WireItem*> m_wireItems;
    QHash<ItemId, TextNoteItem*> m_textItems;

    Netlist m_netlist;
    std::unique_ptr<Circuit> m_flat;
    std::unique_ptr<Circuit> m_faulted;     // simulation copy with the faults applied
    QStringList m_flatWarnings;
    bool m_netlistPending = false;

    Tool m_tool = Tool::Select;
    bool m_wireFromSelect = false;      // wire started by dragging from a pin
    bool m_drawingBus = false;
    QString m_lastNetName;
    QVector<QPointF> m_wirePoints;
    QPointF m_cursor;

    ComponentItem* m_ghost = nullptr;
    Component m_ghostComponent;

    struct DragState {
        bool active = false;
        QHash<ItemId, QPointF> componentStart;
        QHash<ItemId, QPointF> textStart;
        QVector<ItemId> wires;
        std::optional<QPointF> titleStart;
    } m_drag;

    TitleBlockItem* m_titleItem = nullptr;
    std::optional<WireEdit> m_wireEdit;
    std::optional<AnalogOverlay> m_overlay;
    QVector<Lane> m_lanes;

    std::unique_ptr<sim::EventSimulator> m_sim;
    QTimer m_timer;
    int m_ticksPerFrame = 2;
    ItemId m_heldButton = 0;
    QString m_heldRelease;
    bool m_gridVisible = true;
    bool m_gridLines = true;
    bool m_trace = true;
    bool m_showNodeNames = false;
    bool m_showPinDots = true;
    int m_highlightNet = -1;
    bool m_exporting = false;
    bool m_suppressMenu = false;
    bool m_border = false;
    QStringList m_restrictions;
    QHash<ItemId, int> m_breaks;        // SCOPE breakpoints while simulating
    QHash<int, sim::Logic> m_breakPrev; // break nets' values one tick earlier
    bool m_breakMet = false;            // level breakpoints: condition held last tick
    int m_probeNet = -1;                // node under the probe tool while simulating        // the right press ended a wire or a tool
};

} // namespace cm
