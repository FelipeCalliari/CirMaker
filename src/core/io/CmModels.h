#pragma once

#include <QString>

namespace cm {

// The .MODEL statements of the Models folder of the user's CircuitMaker 2000
// installation (*.mod, *.lib; not distributed with this program), so a part
// labelled 2N2222 or IRF540 simulates with that model as in CircuitMaker.
namespace cmmodels {

struct Model {
    QString name, type;     // as written: "Q2N2222", "NPN"
    QString text;           // the whole statement, continuation lines joined
};

// The model of this name (case ignored), or an empty Model; the Models
// folder beside the Device.lib that CmDeviceLib::locate finds.
Model find(const QString& name);

} // namespace cmmodels
} // namespace cm
