#pragma once

#include "core/Circuit.h"

#include <QByteArray>

namespace cm {

// .cmq: versioned JSON document.
namespace NativeFormat {
inline constexpr int kVersion = 1;

QByteArray serialize(const Circuit& circuit);
bool deserialize(const QByteArray& data, Circuit& circuit, QString* error);

bool save(const QString& path, const Circuit& circuit, QString* error);
bool load(const QString& path, Circuit& circuit, QString* error);
} // namespace NativeFormat

} // namespace cm
