#pragma once

#include "reworkbench/types.hpp"
#include <QJsonObject>

namespace reworkbench::persistence {

inline constexpr qsizetype max_snapshot_bytes = 512 * 1024 * 1024;
QJsonObject address_json(const Address& address);
Address address_from_json(const QJsonObject& object);
QJsonObject note_json(const Note& note);
quint64 unsigned_hex(const QString& text);
QJsonObject snapshot_json(const AnalysisSnapshot& snapshot, std::stop_token stop = {});
AnalysisSnapshot snapshot_from_json(const QJsonObject& object, std::stop_token stop = {});

} // namespace reworkbench::persistence
