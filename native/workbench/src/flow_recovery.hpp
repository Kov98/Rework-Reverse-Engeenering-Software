#pragma once
#include "reworkbench/binary_image.hpp"
#include <QMap>
#include <QSet>

namespace reworkbench::detail {
// Facts are local to one uninterrupted path; unknown effects discard them.
class StaticFlowResolver final {
public:
    StaticFlowResolver(const BinaryFile& file, const BinaryMetadata& metadata, const QSet<QString>& relocation_slots, bool allow_memory_pointers);
    bool resolve(Instruction& instruction);
    void observe(const Instruction& instruction);
private:
    std::optional<quint64> pointer(Address address) const;
    std::optional<quint64> operand_address(const Operand& operand, const Instruction& instruction, bool with_index) const;
    std::optional<quint64> register_value(const QString& name) const;
    const BinaryFile& file_;
    const BinaryMetadata& metadata_;
    const QSet<QString>& relocation_slots_;
    bool allow_memory_pointers_ = false;
    QMap<QString, quint64> constants_;
    std::optional<std::pair<QString, quint64>> comparison_;
    std::optional<std::pair<QString, quint64>> table_bound_;
};
}
