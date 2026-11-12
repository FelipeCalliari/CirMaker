#include "sim/analog/NgspiceEngine.h"
#include "sim/analog/SpiceNetlister.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStringList>

#include <cstring>

namespace cm::spice {

namespace {

// Subset of ngspice's sharedspice.h (ABI stable since ngspice 26).
using NgBool = bool;
struct NgComplex { double real, imag; };
struct VectorInfo {
    char* v_name;
    int v_type;
    short v_flags;
    double* v_realdata;
    NgComplex* v_compdata;
    int v_length;
};
using SendChar = int(char*, int, void*);
using SendStat = int(char*, int, void*);
using ControlledExit = int(int, NgBool, NgBool, int, void*);
using SendData = int(void*, int, int, void*);
using SendInitData = int(void*, int, void*);
using BGThreadRunning = int(NgBool, int, void*);

// The engine whose run is in progress; callbacks have no other context
// because ngspice is a process-wide singleton.
struct CallbackState {
    QString log;
    bool exitRequested = false;
} g_state;

int onChar(char* text, int, void*)
{
    g_state.log += QString::fromLocal8Bit(text) + u'\n';
    return 0;
}
int onStat(char*, int, void*) { return 0; }
int onExit(int, NgBool, NgBool, int, void*)
{
    g_state.exitRequested = true;
    return 0;
}
int onBgThread(NgBool, int, void*) { return 0; }

} // namespace

struct NgspiceEngine::Api {
    int (*init)(SendChar*, SendStat*, ControlledExit*, SendData*, SendInitData*, BGThreadRunning*, void*) = nullptr;
    int (*command)(char*) = nullptr;
    int (*circ)(char**) = nullptr;
    char* (*curPlot)() = nullptr;
    char** (*allVecs)(char*) = nullptr;
    char** (*allPlots)() = nullptr;         // optional
    VectorInfo* (*vecInfo)(char*) = nullptr;
};

const Vector* Result::find(const QString& name) const
{
    auto it = vectors.constFind(name.toLower());
    if (it != vectors.cend())
        return &*it;
    it = vectors.constFind(QStringLiteral("v(%1)").arg(name.toLower()));
    return it == vectors.cend() ? nullptr : &*it;
}

std::optional<double> Result::last(const QString& name) const
{
    const Vector* v = find(name);
    if (!v || v->size() == 0)
        return std::nullopt;
    return v->isComplex() ? std::abs(v->complex.last()) : v->real.last();
}

NgspiceEngine& NgspiceEngine::instance()
{
    static NgspiceEngine engine;
    return engine;
}

bool NgspiceEngine::available(QString* why)
{
    QMutexLocker lock(&m_mutex);
    const bool ok = m_api || load(why);
    if (!ok && why) *why = m_loadError;
    return ok;
}

bool NgspiceEngine::load(QString* why)
{
    m_lib = new QLibrary(QStringLiteral("ngspice"));
    for (const QString& name : {QStringLiteral("ngspice"), QStringLiteral("libngspice.so.0"), QStringLiteral("libngspice-0")}) {
        m_lib->setFileName(name);
        if (m_lib->load())
            break;
    }
    if (!m_lib->isLoaded()) {
        m_loadError = QStringLiteral("libngspice was not found (install ngspice built as a shared library): %1")
                          .arg(m_lib->errorString());
        delete m_lib;
        m_lib = nullptr;
        if (why) *why = m_loadError;
        return false;
    }
    auto* api = new Api;
    api->init = reinterpret_cast<decltype(api->init)>(m_lib->resolve("ngSpice_Init"));
    api->command = reinterpret_cast<decltype(api->command)>(m_lib->resolve("ngSpice_Command"));
    api->circ = reinterpret_cast<decltype(api->circ)>(m_lib->resolve("ngSpice_Circ"));
    api->curPlot = reinterpret_cast<decltype(api->curPlot)>(m_lib->resolve("ngSpice_CurPlot"));
    api->allVecs = reinterpret_cast<decltype(api->allVecs)>(m_lib->resolve("ngSpice_AllVecs"));
    api->vecInfo = reinterpret_cast<decltype(api->vecInfo)>(m_lib->resolve("ngGet_Vec_Info"));
    api->allPlots = reinterpret_cast<decltype(api->allPlots)>(m_lib->resolve("ngSpice_AllPlots"));
    if (!api->init || !api->command || !api->circ || !api->curPlot || !api->allVecs || !api->vecInfo) {
        m_loadError = QStringLiteral("libngspice does not export the shared-library API");
        delete api;
        unload();
        if (why) *why = m_loadError;
        return false;
    }
    g_state = {};
    // the Windows package carries ngspice beside the program: its init file
    // in ngspice/scripts and the XSPICE code models (digital.cm has d_cosim)
    // in ngspice/lib, loaded here since spinit cannot name a relative path
    QString bundled;
    if (QCoreApplication::instance())
        bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/ngspice");
    const bool hasBundle = !bundled.isEmpty() && QFileInfo::exists(bundled + QStringLiteral("/lib"));
    if (hasBundle && qEnvironmentVariableIsEmpty("SPICE_SCRIPTS"))
        qputenv("SPICE_SCRIPTS", QDir::toNativeSeparators(bundled + QStringLiteral("/scripts")).toLocal8Bit());
    api->init(onChar, onStat, onExit, nullptr, nullptr, onBgThread, nullptr);
    if (hasBundle) {
        const QStringList models = QDir(bundled + QStringLiteral("/lib")).entryList({QStringLiteral("*.cm")}, QDir::Files);
        for (const QString& model : models) {
            QByteArray line = "codemodel " + shortPathName(bundled + QStringLiteral("/lib/") + model).toLocal8Bit();
            api->command(line.data());
        }
    }
    m_api = api;
    return true;
}

void NgspiceEngine::unload()
{
    delete m_api;
    m_api = nullptr;
    if (m_lib) {
        m_lib->unload();
        delete m_lib;
        m_lib = nullptr;
    }
}

Result NgspiceEngine::run(const QString& netlist)
{
    Result r = runOnce(netlist);
    QStringList lines = netlist.split(u'\n');
    // a transient asked to start from the initial conditions that ngspice
    // cannot carry through (Put: its time step collapses where CircuitMaker's
    // SPICE goes on) runs again from the operating point
    if (!r.ok) {
        static const QRegularExpression uic(QStringLiteral("\\s+uic\\s*$"), QRegularExpression::CaseInsensitiveOption);
        QStringList plain = lines;
        bool had = false;
        for (QString& l : plain)
            if (l.startsWith(QStringLiteral(".tran "), Qt::CaseInsensitive) && l.contains(uic)) l.remove(uic), had = true;
        if (had) {
            Result again = runOnce(plain.join(u'\n'));
            if (again.ok) return again;
        }
    }
    if (r.ok || !r.error.contains(QStringLiteral("operating point"), Qt::CaseInsensitive))
        return r;
    bool changed = false;
    for (QString& l : lines)
        if (l.startsWith(QStringLiteral(".tran "), Qt::CaseInsensitive) && !l.contains(QStringLiteral("uic"), Qt::CaseInsensitive)) {
            l += QStringLiteral(" uic");
            changed = true;
        }
    if (!changed)
        return r;
    Result again = runOnce(lines.join(u'\n'));
    if (!again.ok)
        return r;
    again.fromInitialConditions = true;
    return again;
}

Result NgspiceEngine::runOnce(const QString& netlist)
{
    QMutexLocker lock(&m_mutex);
    Result r;
    if (!m_api && !load(&r.error))
        return r;

    auto command = [&](const char* cmd) {
        QByteArray c(cmd);
        return m_api->command(c.data());
    };
    g_state.log.clear();
    g_state.exitRequested = false;

    // ngSpice_Circ takes a null-terminated array of lines
    QList<QByteArray> lines;
    for (const QString& l : netlist.split(u'\n'))
        lines << l.toLocal8Bit();
    QVector<char*> ptrs;
    for (QByteArray& l : lines)
        ptrs << l.data();
    ptrs << nullptr;

    const int circError = m_api->circ(ptrs.data());
    if (circError == 0 && !g_state.exitRequested)
        command("run");

    r.log = g_state.log;
    for (const QString& line : r.log.split(u'\n')) {
        const QString l = line.trimmed();
        if (l.contains(QStringLiteral("rror")) || l.contains(QStringLiteral("singular matrix"))
            || l.contains(QStringLiteral("aborted"))) {
            r.error = l.startsWith(QStringLiteral("stderr ")) ? l.mid(7) : l;
            break;
        }
    }

    const QString plot = QString::fromLocal8Bit(m_api->curPlot());
    r.plotType = plot;
    while (!r.plotType.isEmpty() && r.plotType.back().isDigit())
        r.plotType.chop(1);
    // the current plot first; then the others of this run (".noise" makes a
    // spectrum and an integrated plot), without overwriting its vectors
    QStringList plots{plot};
    if (m_api->allPlots)
        if (char** all = m_api->allPlots())
            for (int i = 0; all[i]; ++i)
                if (const QString p = QString::fromLocal8Bit(all[i]); p != plot && p != u"const") plots << p;
    for (const QString& name : std::as_const(plots)) {
        if (name == u"const") continue;
        QByteArray plotName = name.toLocal8Bit();
        if (char** names = m_api->allVecs(plotName.data())) {
            for (int i = 0; names[i]; ++i) {
                QByteArray full = plotName + '.' + QByteArray(names[i]);
                const VectorInfo* info = m_api->vecInfo(full.data());
                if (!info) continue;
                Vector v;
                v.name = QString::fromLocal8Bit(info->v_name).toLower();
                if (r.vectors.contains(v.name)) continue;
                if (info->v_compdata) {
                    v.complex.resize(info->v_length);
                    for (int k = 0; k < info->v_length; ++k)
                        v.complex[k] = {info->v_compdata[k].real, info->v_compdata[k].imag};
                } else if (info->v_realdata) {
                    v.real = QVector<double>(info->v_realdata, info->v_realdata + info->v_length);
                }
                r.vectors.insert(v.name, v);
            }
        }
    }
    if (r.plotType == u"tran") r.scale = QStringLiteral("time");
    else if (r.plotType == u"ac" || (r.plotType == u"noise" && r.vectors.contains(QStringLiteral("frequency"))))
        r.scale = QStringLiteral("frequency");
    else if (r.plotType == u"dc") {
        for (auto it = r.vectors.cbegin(); it != r.vectors.cend(); ++it)
            if (it.key().endsWith(QStringLiteral("sweep")))
                r.scale = it.key();
    }

    command("remcirc");
    command("destroy all");
    r.ok = r.error.isEmpty() && circError == 0 && !r.vectors.isEmpty();
    if (!r.ok && r.error.isEmpty())
        r.error = QStringLiteral("the simulation produced no results");
    if (g_state.exitRequested)
        unload();                               // ngspice asked to be unloaded; reload next time
    return r;
}

} // namespace cm::spice
