#pragma once

#include "reworkbench/types.hpp"

#include <memory>

namespace reworkbench::detail {

struct DecodedInstruction {
    Instruction instruction;
    QStringList warnings;
    // A fixed-mode decoder cannot safely continue through these instructions.
    bool stop_path = false;
    bool target_changes_mode = false;
};

class InstructionDecoder final {
public:
    InstructionDecoder(Architecture architecture, ByteOrder order, int bits);
    ~InstructionDecoder();
    InstructionDecoder(const InstructionDecoder&) = delete;
    InstructionDecoder& operator=(const InstructionDecoder&) = delete;
    [[nodiscard]] bool available() const;
    [[nodiscard]] QString explanation() const;
    [[nodiscard]] std::optional<DecodedInstruction> decode(const QByteArray& bytes,
                                                          Address address);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reworkbench::detail
