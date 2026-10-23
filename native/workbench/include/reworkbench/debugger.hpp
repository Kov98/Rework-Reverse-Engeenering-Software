#pragma once
#include "reworkbench/types.hpp"

#include <QJsonObject>
#include <memory>

namespace reworkbench {

// Explicit live process debugging. Calls must remain on the creating thread.
// Non-Windows hosts reject operations. Destruction attempts detach, never terminate.
class NativeDebugger final {
public:
    NativeDebugger();
    ~NativeDebugger();
    NativeDebugger(const NativeDebugger&) = delete;
    NativeDebugger& operator=(const NativeDebugger&) = delete;
    void launch(const QString& executable, const QStringList& arguments = {}, const QString& cwd = {});
    void attach(quint32 process_id);
    [[nodiscard]] QJsonObject wait_event(quint32 timeout_ms = 1000);
    void continue_event(bool exception_handled = false);
    void single_step(quint32 thread_id);
    void set_breakpoint(quint64 address);
    void remove_breakpoint(quint64 address);
    [[nodiscard]] QByteArray read_memory(quint64 address, quint32 length) const;
    [[nodiscard]] QJsonObject registers(quint32 thread_id) const;
    void detach();
    [[nodiscard]] bool active() const;
    [[nodiscard]] quint32 process_id() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Explicit one-shot automation: launch/attach; a bounded list of operations; detach.
// Session state never survives this call. See docs/investigation.md for schema.
[[nodiscard]] QJsonObject run_debug_session(const QJsonObject& request, std::stop_token stop = {});
} // namespace reworkbench
