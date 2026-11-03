#pragma once

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include <complex>
#include <optional>

class QLibrary;

namespace cm::spice {

struct Vector {
    QString name;                               // lower case, as ngspice names it: "out", "v1#branch"
    QVector<double> real;
    QVector<std::complex<double>> complex;      // AC analysis
    bool isComplex() const { return !complex.isEmpty(); }
    int size() const { return int(isComplex() ? complex.size() : real.size()); }
};

struct Result {
    bool ok = false;
    QString error;                              // first error reported by ngspice
    QString log;                                // everything ngspice printed
    QString plotType;                           // "op", "tran", "ac", "dc"
    QString scale;                              // name of the x vector: "time", "frequency", ...
    QHash<QString, Vector> vectors;
    bool fromInitialConditions = false;         // the transient skipped the operating point (UIC)

    const Vector* find(const QString& name) const;
    // Value of a node voltage / current at the last point (operating point).
    std::optional<double> last(const QString& name) const;
};

// libngspice loaded at run time. ngspice keeps global state and is not
// reentrant, so runs are serialised; run() blocks and may be called from a
// worker thread.
class NgspiceEngine {
public:
    static NgspiceEngine& instance();

    bool available(QString* why = nullptr);
    // A transient whose operating point does not converge (a gate
    // oscillator: event-driven feedback has no steady state) is run again
    // from the initial conditions, as SPICE's UIC.
    Result run(const QString& netlist);

private:
    Result runOnce(const QString& netlist);
    NgspiceEngine() = default;
    bool load(QString* why);
    void unload();

    struct Api;
    QLibrary* m_lib = nullptr;
    Api* m_api = nullptr;
    QMutex m_mutex;
    QString m_loadError;
};

} // namespace cm::spice
