#include "reworkbench/debugger.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <vector>

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__))
#define REWORKBENCH_WIN_DEBUGGER 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#define REWORKBENCH_WIN_DEBUGGER 0
#endif

namespace reworkbench {
namespace {
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }
[[maybe_unused, noreturn]] void unavailable() { fail(QStringLiteral("Live debugging requires a Windows x86/x64 build and a target with the same architecture.")); }
quint64 number(const QJsonValue& value, const QString& field) {
    if (!value.isString()) fail(field + QStringLiteral(" must be an unsigned integer string."));
    auto text = value.toString();
    const bool hexadecimal = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive);
    if (hexadecimal) text.remove(0, 2);
    if (text.isEmpty() || text.size() > 20) fail(field + QStringLiteral(" is invalid."));
    for (const QChar c : text) {
        const auto u = c.unicode();
        if (!(u >= '0' && u <= '9') && !(hexadecimal && ((u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')))) fail(field + QStringLiteral(" is invalid."));
    }
    bool ok = false; const auto result = text.toULongLong(&ok, hexadecimal ? 16 : 10);
    if (!ok) fail(field + QStringLiteral(" exceeds 64 bits."));
    return result;
}
quint32 bounded(const QJsonObject& object, const QString& name, quint32 fallback, quint32 maximum) {
    if (!object.contains(name)) return fallback;
    const auto value = object.value(name);
    if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > maximum || value.toDouble() != value.toInt()) fail(name + QStringLiteral(" is outside its integer limit."));
    return static_cast<quint32>(value.toInt());
}
#if REWORKBENCH_WIN_DEBUGGER
thread_local bool debugger_thread_reserved = false;
[[noreturn]] void win_fail(const QString& operation, DWORD error = GetLastError()) {
    fail(operation + QStringLiteral(" failed (Windows error %1).").arg(error));
}
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE input = nullptr) : value(input) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
quint64 pointer(const void* value) { return static_cast<quint64>(reinterpret_cast<quintptr>(value)); }
void validate_address(quint64 address, quint64 length = 1) {
    if (address > std::numeric_limits<quintptr>::max() || (length && length - 1 > std::numeric_limits<quintptr>::max() - address)) fail(QStringLiteral("Debug address exceeds the host pointer width."));
}
void check_architecture(HANDLE process) {
    using IsWow64Process2Fn = BOOL (WINAPI*)(HANDLE, USHORT*, USHORT*);
    const auto library = GetModuleHandleW(L"kernel32.dll");
    const auto query = reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(library, "IsWow64Process2"));
    if (query) {
        USHORT process_machine = 0, native_machine = 0;
        if (!query(process, &process_machine, &native_machine)) win_fail(QStringLiteral("IsWow64Process2"));
        const auto machine = process_machine == IMAGE_FILE_MACHINE_UNKNOWN ? native_machine : process_machine;
#if defined(_M_X64) || defined(__x86_64__)
        if (machine != IMAGE_FILE_MACHINE_AMD64) fail(QStringLiteral("This debugger build supports native x64 targets only; WOW64 and ARM targets require a matching build."));
#else
        if (machine != IMAGE_FILE_MACHINE_I386) fail(QStringLiteral("This debugger build supports x86 targets only."));
#endif
    } else {
        BOOL target = FALSE, self = FALSE;
        if (!IsWow64Process(process, &target) || !IsWow64Process(GetCurrentProcess(), &self)) win_fail(QStringLiteral("IsWow64Process"));
        if (target != self) fail(QStringLiteral("Cross-bitness debugging is unavailable in this build."));
    }
}
std::wstring quote_argument(const QString& argument) {
    const auto input = argument.toStdWString();
    std::wstring output = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : input) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'\"') { output.append(backslashes * 2 + 1, L'\\'); output += c; }
        else { output.append(backslashes, L'\\'); output += c; }
        backslashes = 0;
    }
    output.append(backslashes * 2, L'\\'); output += L'\"';
    return output;
}
void validate_executable(const QString& executable) {
    QFile file(executable);
    if (!file.open(QIODevice::ReadOnly)) fail(QStringLiteral("Cannot read debugger executable."));
    auto header = file.read(64);
    if (header.size() != 64 || header[0] != 'M' || header[1] != 'Z') fail(QStringLiteral("Debugger launch requires a PE executable."));
    const auto byte = [&](int i) { return static_cast<quint32>(static_cast<quint8>(header[i])); };
    const quint32 pe = byte(60) | (byte(61) << 8) | (byte(62) << 16) | (byte(63) << 24);
    if (pe > 1024 * 1024 || !file.seek(pe)) fail(QStringLiteral("Executable PE header is invalid."));
    header = file.read(24);
    if (header.size() != 24 || header.left(4) != QByteArray("PE\0\0", 4)) fail(QStringLiteral("Executable PE signature is invalid."));
    const auto machine = static_cast<quint16>(static_cast<quint8>(header[4])) | (static_cast<quint16>(static_cast<quint8>(header[5])) << 8);
#if defined(_M_X64) || defined(__x86_64__)
    if (machine != IMAGE_FILE_MACHINE_AMD64) fail(QStringLiteral("This debugger build launches x64 executables only."));
#else
    if (machine != IMAGE_FILE_MACHINE_I386) fail(QStringLiteral("This debugger build launches x86 executables only."));
#endif
    const auto characteristics = static_cast<quint16>(static_cast<quint8>(header[22])) | (static_cast<quint16>(static_cast<quint8>(header[23])) << 8);
    if (characteristics & IMAGE_FILE_DLL) fail(QStringLiteral("A DLL cannot be launched as a debugger process."));
}
#endif
} // namespace

struct NativeDebugger::Impl {
    Qt::HANDLE owner = QThread::currentThreadId();
    quint32 pid = 0;
    void thread_check() const { if (QThread::currentThreadId() != owner) fail(QStringLiteral("Debugger operations must remain on the session's creating thread.")); }
#if REWORKBENCH_WIN_DEBUGGER
    HANDLE process = nullptr; // own duplicate; distinct from OS-owned event handles
    DEBUG_EVENT event{};
    bool pending = false;
    bool owned_exception = false;
    bool initial_breakpoint = true;
    DWORD stepping_thread = 0;
    std::map<quint64, quint8> breakpoints; // one-shot INT3 breakpoints
    mutable std::map<quint64, DWORD> protection_restores;
    ~Impl() { if (process) CloseHandle(process); }
    void require_active() const { thread_check(); if (!pid || !process) fail(QStringLiteral("No active debugger session.")); }
    void require_paused() const { require_active(); if (!pending || event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) fail(QStringLiteral("Operation requires a paused debug event.")); }
    Handle open_thread(DWORD id, DWORD rights) const {
        Handle result(OpenThread(rights | THREAD_QUERY_INFORMATION, FALSE, id));
        if (!result.value) win_fail(QStringLiteral("OpenThread"));
        if (GetProcessIdOfThread(result.value) != pid) fail(QStringLiteral("Thread does not belong to the debugged process."));
        // Guaranteed copy elision requires direct construction at return.
        const auto value = result.value; result.value = nullptr;
        return Handle(value);
    }
    CONTEXT context(DWORD id) const {
        const auto thread = open_thread(id, THREAD_GET_CONTEXT);
        CONTEXT result{}; result.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(thread.value, &result)) win_fail(QStringLiteral("GetThreadContext"));
        return result;
    }
    void set_context(DWORD id, CONTEXT& context) const {
        const auto thread = open_thread(id, THREAD_SET_CONTEXT);
        if (!SetThreadContext(thread.value, &context)) win_fail(QStringLiteral("SetThreadContext"));
    }
    void write_byte(quint64 address, quint8 value) const {
        validate_address(address);
        const auto target = reinterpret_cast<void*>(static_cast<quintptr>(address));
        DWORD previous = 0;
        if (!VirtualProtectEx(process, target, 1, PAGE_EXECUTE_READWRITE, &previous)) win_fail(QStringLiteral("VirtualProtectEx"));
        const auto restore_protection = protection_restores.try_emplace(address, previous).first->second;
        SIZE_T written = 0;
        const BOOL wrote = WriteProcessMemory(process, target, &value, 1, &written);
        const DWORD write_error = wrote ? ERROR_SUCCESS : GetLastError();
        DWORD ignored = 0;
        const BOOL restored = VirtualProtectEx(process, target, 1, restore_protection, &ignored);
        const DWORD restore_error = restored ? ERROR_SUCCESS : GetLastError();
        if (restored) protection_restores.erase(address);
        if (!FlushInstructionCache(process, target, 1)) win_fail(QStringLiteral("FlushInstructionCache"));
        if (!restored) win_fail(QStringLiteral("Restoring page protection"), restore_error);
        if (!wrote || written != 1) win_fail(QStringLiteral("WriteProcessMemory"), write_error);
    }
    void clear_step() {
        if (!stepping_thread) return;
        auto state = context(stepping_thread);
        state.EFlags &= ~DWORD{0x100};
        set_context(stepping_thread, state);
        stepping_thread = 0;
    }
    DWORD continue_status(bool handled) const {
        return event.dwDebugEventCode != EXCEPTION_DEBUG_EVENT || owned_exception || handled ? DBG_CONTINUE : DBG_EXCEPTION_NOT_HANDLED;
    }
#endif
};

NativeDebugger::NativeDebugger() : impl_(std::make_unique<Impl>()) {}
NativeDebugger::~NativeDebugger() {
    try { detach(); } catch (...) { /* Explicit detach reports failures; destruction never terminates a target. */ }
}
bool NativeDebugger::active() const { impl_->thread_check(); return impl_->pid != 0; }
quint32 NativeDebugger::process_id() const { impl_->thread_check(); return impl_->pid; }

void NativeDebugger::launch(const QString& executable, const QStringList& arguments, const QString& cwd) {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->thread_check();
    if (impl_->pid) fail(QStringLiteral("Detach the current target before launching another."));
    if (debugger_thread_reserved) fail(QStringLiteral("Only one active debug session is allowed on a thread."));
    const auto info = QFileInfo(executable);
    if (!info.exists() || !info.isFile() || info.canonicalFilePath().isEmpty()) fail(QStringLiteral("Executable does not exist."));
    validate_executable(info.canonicalFilePath());
    if (arguments.size() > 256) fail(QStringLiteral("Debugger arguments exceed the 256-item limit."));
    std::wstring command = quote_argument(info.canonicalFilePath());
    for (const auto& argument : arguments) {
        if (argument.contains(QChar::Null)) fail(QStringLiteral("Arguments cannot contain NUL."));
        command += L' '; command += quote_argument(argument);
        if (command.size() >= 32767) fail(QStringLiteral("Debugger command line is too long."));
    }
    const auto directory = cwd.isEmpty() ? info.absolutePath() : QFileInfo(cwd).canonicalFilePath();
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) fail(QStringLiteral("Debugger working directory is invalid."));
    const auto native_executable = QDir::toNativeSeparators(info.canonicalFilePath()).toStdWString();
    const auto native_directory = QDir::toNativeSeparators(directory).toStdWString();
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION created{};
    if (!CreateProcessW(native_executable.c_str(), command.data(), nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS,
                        nullptr, native_directory.c_str(), &startup, &created)) win_fail(QStringLiteral("CreateProcessW"));
    Handle initial_process(created.hProcess), initial_thread(created.hThread);
    impl_->pid = created.dwProcessId;
    debugger_thread_reserved = true;
    if (!DuplicateHandle(GetCurrentProcess(), created.hProcess, GetCurrentProcess(), &impl_->process, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        const auto error = GetLastError(); DebugSetProcessKillOnExit(FALSE);
        if (DebugActiveProcessStop(impl_->pid)) { impl_->pid = 0; debugger_thread_reserved = false; }
        win_fail(QStringLiteral("DuplicateHandle"), error);
    }
    if (!DebugSetProcessKillOnExit(FALSE)) {
        const auto error = GetLastError(); detach(); win_fail(QStringLiteral("DebugSetProcessKillOnExit"), error);
    }
    impl_->initial_breakpoint = true;
#else
    Q_UNUSED(executable); Q_UNUSED(arguments); Q_UNUSED(cwd); unavailable();
#endif
}

void NativeDebugger::attach(quint32 process_id) {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->thread_check();
    if (impl_->pid) fail(QStringLiteral("Detach the current target before attaching."));
    if (debugger_thread_reserved) fail(QStringLiteral("Only one active debug session is allowed on a thread."));
    if (!process_id || process_id == GetCurrentProcessId()) fail(QStringLiteral("Cannot attach to process zero or the debugger itself."));
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | SYNCHRONIZE, FALSE, process_id));
    if (!process.value) win_fail(QStringLiteral("OpenProcess"));
    check_architecture(process.value);
    if (!DebugActiveProcess(process_id)) win_fail(QStringLiteral("DebugActiveProcess"));
    impl_->pid = process_id; impl_->process = process.value; process.value = nullptr;
    debugger_thread_reserved = true;
    if (!DebugSetProcessKillOnExit(FALSE)) {
        const auto error = GetLastError(); detach(); win_fail(QStringLiteral("DebugSetProcessKillOnExit"), error);
    }
    impl_->initial_breakpoint = true;
#else
    Q_UNUSED(process_id); unavailable();
#endif
}

QJsonObject NativeDebugger::wait_event(quint32 timeout_ms) {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->require_active();
    if (impl_->pending) fail(QStringLiteral("Continue the pending event before waiting again."));
    if (timeout_ms > 10000) fail(QStringLiteral("A debug wait is limited to 10 seconds."));
    DEBUG_EVENT event{};
    if (!WaitForDebugEvent(&event, timeout_ms)) {
        const auto error = GetLastError();
        if (error == ERROR_SEM_TIMEOUT) return {{QStringLiteral("kind"), QStringLiteral("timeout")}};
        win_fail(QStringLiteral("WaitForDebugEvent"), error);
    }
    impl_->event = event; impl_->pending = true; impl_->owned_exception = false;
    QJsonObject result{{QStringLiteral("process_id"), QString::number(event.dwProcessId)}, {QStringLiteral("thread_id"), QString::number(event.dwThreadId)}};
    switch (event.dwDebugEventCode) {
    case CREATE_PROCESS_DEBUG_EVENT:
        result.insert(QStringLiteral("kind"), QStringLiteral("create-process"));
        result.insert(QStringLiteral("image_base"), hex(pointer(event.u.CreateProcessInfo.lpBaseOfImage)));
        result.insert(QStringLiteral("entry"), hex(static_cast<quint64>(reinterpret_cast<quintptr>(event.u.CreateProcessInfo.lpStartAddress))));
        if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
        // Windows owns event hProcess/hThread and closes them on continued exit.
        break;
    case CREATE_THREAD_DEBUG_EVENT:
        result.insert(QStringLiteral("kind"), QStringLiteral("create-thread"));
        result.insert(QStringLiteral("entry"), hex(static_cast<quint64>(reinterpret_cast<quintptr>(event.u.CreateThread.lpStartAddress))));
        break;
    case EXIT_THREAD_DEBUG_EVENT:
        result.insert(QStringLiteral("kind"), QStringLiteral("exit-thread"));
        result.insert(QStringLiteral("exit_code"), hex(event.u.ExitThread.dwExitCode));
        if (impl_->stepping_thread == event.dwThreadId) impl_->stepping_thread = 0;
        break;
    case EXIT_PROCESS_DEBUG_EVENT:
        result.insert(QStringLiteral("kind"), QStringLiteral("exit-process"));
        result.insert(QStringLiteral("exit_code"), hex(event.u.ExitProcess.dwExitCode));
        break;
    case LOAD_DLL_DEBUG_EVENT:
        result.insert(QStringLiteral("kind"), QStringLiteral("load-library"));
        result.insert(QStringLiteral("base"), hex(pointer(event.u.LoadDll.lpBaseOfDll)));
        if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
        break;
    case UNLOAD_DLL_DEBUG_EVENT:
        result.insert(QStringLiteral("kind"), QStringLiteral("unload-library"));
        result.insert(QStringLiteral("base"), hex(pointer(event.u.UnloadDll.lpBaseOfDll)));
        break;
    case OUTPUT_DEBUG_STRING_EVENT:
        result.insert(QStringLiteral("kind"), QStringLiteral("debug-string"));
        result.insert(QStringLiteral("address"), hex(pointer(event.u.DebugString.lpDebugStringData)));
        result.insert(QStringLiteral("length"), static_cast<int>(event.u.DebugString.nDebugStringLength));
        result.insert(QStringLiteral("unicode"), event.u.DebugString.fUnicode != 0);
        break;
    case EXCEPTION_DEBUG_EVENT: {
        const auto& exception = event.u.Exception;
        const auto address = pointer(exception.ExceptionRecord.ExceptionAddress);
        result.insert(QStringLiteral("kind"), QStringLiteral("exception"));
        result.insert(QStringLiteral("code"), hex(exception.ExceptionRecord.ExceptionCode));
        result.insert(QStringLiteral("address"), hex(address));
        result.insert(QStringLiteral("first_chance"), exception.dwFirstChance != 0);
        if (exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
            const auto found = impl_->breakpoints.find(address);
            if (found != impl_->breakpoints.end()) {
                auto state = impl_->context(event.dwThreadId);
#if defined(_M_X64) || defined(__x86_64__)
                state.Rip = address;
#else
                state.Eip = static_cast<DWORD>(address);
#endif
                impl_->set_context(event.dwThreadId, state);
                impl_->write_byte(address, found->second);
                impl_->breakpoints.erase(found);
                impl_->owned_exception = true;
                result.insert(QStringLiteral("one_shot_breakpoint"), true);
                result.insert(QStringLiteral("instruction_pointer_rewound"), true);
            } else if (impl_->initial_breakpoint) {
                impl_->initial_breakpoint = false;
                impl_->owned_exception = true;
                result.insert(QStringLiteral("initial_breakpoint"), true);
            }
        } else if (exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP && impl_->stepping_thread == event.dwThreadId) {
            impl_->clear_step(); impl_->owned_exception = true;
            result.insert(QStringLiteral("requested_single_step"), true);
        }
        result.insert(QStringLiteral("handled_by_debugger"), impl_->owned_exception);
        break;
    }
    default:
        result.insert(QStringLiteral("kind"), QStringLiteral("other"));
        result.insert(QStringLiteral("event_code"), static_cast<int>(event.dwDebugEventCode));
        break;
    }
    return result;
#else
    Q_UNUSED(timeout_ms); unavailable();
#endif
}

void NativeDebugger::continue_event(bool exception_handled) {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->require_active();
    if (!impl_->pending) fail(QStringLiteral("No pending debug event."));
    const bool exited = impl_->event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT;
    if (!ContinueDebugEvent(impl_->event.dwProcessId, impl_->event.dwThreadId, impl_->continue_status(exception_handled))) win_fail(QStringLiteral("ContinueDebugEvent"));
    impl_->pending = false; impl_->owned_exception = false;
    if (exited) {
        impl_->pid = 0; impl_->breakpoints.clear(); impl_->stepping_thread = 0;
        impl_->protection_restores.clear(); debugger_thread_reserved = false;
        CloseHandle(impl_->process); impl_->process = nullptr;
    }
#else
    Q_UNUSED(exception_handled); unavailable();
#endif
}

void NativeDebugger::single_step(quint32 thread_id) {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->require_paused();
    if (thread_id != impl_->event.dwThreadId) fail(QStringLiteral("Single-step must select the thread of the pending event."));
    if (impl_->event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT && !impl_->owned_exception)
        fail(QStringLiteral("An application exception must be explicitly continued before a single-step can be requested."));
    if (impl_->stepping_thread) fail(QStringLiteral("A single-step is already pending."));
    auto state = impl_->context(thread_id);
    if (state.EFlags & 0x100) fail(QStringLiteral("Thread already has a trap flag; it is not owned by this debugger."));
    state.EFlags |= 0x100;
    impl_->set_context(thread_id, state); impl_->stepping_thread = thread_id;
    continue_event(true);
#else
    Q_UNUSED(thread_id); unavailable();
#endif
}

QByteArray NativeDebugger::read_memory(quint64 address, quint32 length) const {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->require_paused();
    if (length > 64 * 1024) fail(QStringLiteral("Debug memory reads are limited to 64 KiB."));
    validate_address(address, length);
    QByteArray result(static_cast<qsizetype>(length), '\0');
    if (!length) return result;
    SIZE_T read = 0;
    if (!ReadProcessMemory(impl_->process, reinterpret_cast<const void*>(static_cast<quintptr>(address)), result.data(), length, &read) || read != length)
        win_fail(QStringLiteral("ReadProcessMemory"));
    return result;
#else
    Q_UNUSED(address); Q_UNUSED(length); unavailable();
#endif
}

void NativeDebugger::set_breakpoint(quint64 address) {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->require_paused();
    if (impl_->breakpoints.contains(address)) fail(QStringLiteral("A breakpoint already exists at this address."));
    if (impl_->breakpoints.size() >= 128) fail(QStringLiteral("At most 128 one-shot breakpoints are supported."));
    MEMORY_BASIC_INFORMATION memory{};
    validate_address(address);
    if (!VirtualQueryEx(impl_->process, reinterpret_cast<const void*>(static_cast<quintptr>(address)), &memory, sizeof(memory))) win_fail(QStringLiteral("VirtualQueryEx"));
    const DWORD protection = memory.Protect & 0xff;
    if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) ||
        (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY))
        fail(QStringLiteral("Software breakpoints require committed executable memory without guard pages."));
    const auto original = static_cast<quint8>(read_memory(address, 1)[0]);
    if (original == 0xcc) fail(QStringLiteral("An existing INT3 cannot be owned as a software breakpoint."));
    // Record first so cleanup can restore a byte if later protection/cache calls fail.
    impl_->breakpoints.emplace(address, original);
    impl_->write_byte(address, 0xcc);
#else
    Q_UNUSED(address); unavailable();
#endif
}

void NativeDebugger::remove_breakpoint(quint64 address) {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->require_paused();
    const auto found = impl_->breakpoints.find(address);
    if (found == impl_->breakpoints.end()) fail(QStringLiteral("No owned breakpoint at this address."));
    if (static_cast<quint8>(read_memory(address, 1)[0]) != 0xcc) fail(QStringLiteral("Breakpoint memory was changed by the target; refusing to overwrite it."));
    impl_->write_byte(address, found->second); impl_->breakpoints.erase(found);
#else
    Q_UNUSED(address); unavailable();
#endif
}

QJsonObject NativeDebugger::registers(quint32 thread_id) const {
#if REWORKBENCH_WIN_DEBUGGER
    impl_->require_paused();
    const auto state = impl_->context(thread_id);
    QJsonObject result;
#if defined(_M_X64) || defined(__x86_64__)
    result = {{QStringLiteral("rax"), hex(state.Rax)}, {QStringLiteral("rbx"), hex(state.Rbx)}, {QStringLiteral("rcx"), hex(state.Rcx)}, {QStringLiteral("rdx"), hex(state.Rdx)},
        {QStringLiteral("rsi"), hex(state.Rsi)}, {QStringLiteral("rdi"), hex(state.Rdi)}, {QStringLiteral("rbp"), hex(state.Rbp)}, {QStringLiteral("rsp"), hex(state.Rsp)},
        {QStringLiteral("r8"), hex(state.R8)}, {QStringLiteral("r9"), hex(state.R9)}, {QStringLiteral("r10"), hex(state.R10)}, {QStringLiteral("r11"), hex(state.R11)},
        {QStringLiteral("r12"), hex(state.R12)}, {QStringLiteral("r13"), hex(state.R13)}, {QStringLiteral("r14"), hex(state.R14)}, {QStringLiteral("r15"), hex(state.R15)},
        {QStringLiteral("rip"), hex(state.Rip)}, {QStringLiteral("eflags"), hex(state.EFlags)}};
#else
    result = {{QStringLiteral("eax"), hex(state.Eax)}, {QStringLiteral("ebx"), hex(state.Ebx)}, {QStringLiteral("ecx"), hex(state.Ecx)}, {QStringLiteral("edx"), hex(state.Edx)},
        {QStringLiteral("esi"), hex(state.Esi)}, {QStringLiteral("edi"), hex(state.Edi)}, {QStringLiteral("ebp"), hex(state.Ebp)}, {QStringLiteral("esp"), hex(state.Esp)},
        {QStringLiteral("eip"), hex(state.Eip)}, {QStringLiteral("eflags"), hex(state.EFlags)}};
#endif
    result.insert(QStringLiteral("thread_id"), QString::number(thread_id));
    return result;
#else
    Q_UNUSED(thread_id); unavailable();
#endif
}

void NativeDebugger::detach() {
    impl_->thread_check();
    if (!impl_->pid) return;
#if REWORKBENCH_WIN_DEBUGGER
    if (!impl_->pending && (!impl_->breakpoints.empty() || !impl_->protection_restores.empty() || impl_->stepping_thread)) {
        // Restore modifications only while the OS has stopped all target threads.
        if (!DebugBreakProcess(impl_->process)) win_fail(QStringLiteral("DebugBreakProcess during detach"));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (int attempts = 0; attempts < 128 && !impl_->pending; ++attempts) {
            if (std::chrono::steady_clock::now() >= deadline) fail(QStringLiteral("Timed out pausing the target for clean detach."));
            const auto event = wait_event(100);
            if (event.value(QStringLiteral("kind")).toString() == QStringLiteral("timeout")) continue;
            if (impl_->event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT && impl_->event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT
                && !event.value(QStringLiteral("one_shot_breakpoint")).toBool()
                && !event.value(QStringLiteral("initial_breakpoint")).toBool()) {
                impl_->owned_exception = true; break;
            }
            continue_event(false);
            if (!impl_->pid) return;
        }
        if (!impl_->pending) fail(QStringLiteral("Cannot pause target for clean detach."));
    }
    if (impl_->pending && impl_->event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) { continue_event(false); return; }
    if (impl_->stepping_thread) impl_->clear_step();
    for (const auto& [address, original] : impl_->breakpoints) {
        const auto current = static_cast<quint8>(read_memory(address, 1)[0]);
        if (current == 0xcc) impl_->write_byte(address, original);
        // A target that replaced INT3 owns its new byte. Do not overwrite it.
    }
    impl_->breakpoints.clear();
    for (auto it = impl_->protection_restores.begin(); it != impl_->protection_restores.end();) {
        DWORD ignored = 0;
        if (!VirtualProtectEx(impl_->process, reinterpret_cast<void*>(static_cast<quintptr>(it->first)), 1, it->second, &ignored))
            win_fail(QStringLiteral("Restoring page protection during detach"));
        it = impl_->protection_restores.erase(it);
    }
    if (impl_->pending) continue_event(false);
    if (!DebugActiveProcessStop(impl_->pid)) win_fail(QStringLiteral("DebugActiveProcessStop"));
    impl_->pid = 0; impl_->stepping_thread = 0;
    debugger_thread_reserved = false;
    CloseHandle(impl_->process); impl_->process = nullptr;
#else
    unavailable();
#endif
}

QJsonObject run_debug_session(const QJsonObject& request, std::stop_token stop) {
    if (!request.value(QStringLiteral("execute_target")).isBool() || !request.value(QStringLiteral("execute_target")).toBool())
        fail(QStringLiteral("Live debugging requires execute_target:true in the request."));
    const bool has_launch = request.contains(QStringLiteral("executable"));
    const bool has_attach = request.contains(QStringLiteral("process_id"));
    if (has_launch == has_attach) fail(QStringLiteral("Specify exactly one executable or process_id."));
    if (!request.value(QStringLiteral("operations")).isArray()) fail(QStringLiteral("Debugger operations must be an array."));
    const auto operations = request.value(QStringLiteral("operations")).toArray();
    if (operations.isEmpty() || operations.size() > 1024) fail(QStringLiteral("Supply between 1 and 1024 debugger operations."));
    const QSet<QString> allowed{QStringLiteral("wait"), QStringLiteral("continue"), QStringLiteral("step"), QStringLiteral("registers"),
        QStringLiteral("read"), QStringLiteral("breakpoint"), QStringLiteral("remove-breakpoint"), QStringLiteral("detach")};
    quint32 requested_memory = 0;
    // Reject malformed automation before starting or attaching a live target.
    for (const auto& value : operations) {
        if (!value.isObject()) fail(QStringLiteral("Every debugger operation must be an object."));
        const auto op = value.toObject();
        const auto command = op.value(QStringLiteral("op")).toString();
        if (!allowed.contains(command)) fail(QStringLiteral("Unknown debugger operation: ") + command);
        if (command == QStringLiteral("wait")) (void)bounded(op, QStringLiteral("timeout_ms"), 1000, 10000);
        if (command == QStringLiteral("continue") && op.contains(QStringLiteral("handled")) && !op.value(QStringLiteral("handled")).isBool()) fail(QStringLiteral("handled must be boolean."));
        if (command == QStringLiteral("read") || command == QStringLiteral("breakpoint") || command == QStringLiteral("remove-breakpoint")) (void)number(op.value(QStringLiteral("address")), QStringLiteral("address"));
        if (command == QStringLiteral("read")) {
            const auto length = bounded(op, QStringLiteral("length"), 64, 65536);
            if (length > 4 * 1024 * 1024 - requested_memory) fail(QStringLiteral("The session memory-read budget is 4 MiB."));
            requested_memory += length;
        }
        if ((command == QStringLiteral("step") || command == QStringLiteral("registers")) && op.contains(QStringLiteral("thread_id"))) {
            const auto id = number(op.value(QStringLiteral("thread_id")), QStringLiteral("thread_id"));
            if (!id || id > std::numeric_limits<quint32>::max()) fail(QStringLiteral("thread_id is outside its supported range."));
        }
    }
    if (request.contains(QStringLiteral("cwd")) && !request.value(QStringLiteral("cwd")).isString()) fail(QStringLiteral("cwd must be a path string."));
    NativeDebugger debugger;
    check_cancelled(stop);
    if (has_launch) {
        if (!request.value(QStringLiteral("executable")).isString()) fail(QStringLiteral("executable must be a path string."));
        if (request.contains(QStringLiteral("arguments")) && !request.value(QStringLiteral("arguments")).isArray()) fail(QStringLiteral("arguments must be an array of strings."));
        QStringList args;
        for (const auto& arg : request.value(QStringLiteral("arguments")).toArray()) {
            if (!arg.isString()) fail(QStringLiteral("Every debugger argument must be a string."));
            args.push_back(arg.toString());
        }
        debugger.launch(request.value(QStringLiteral("executable")).toString(), args, request.value(QStringLiteral("cwd")).toString());
    } else {
        const auto pid = number(request.value(QStringLiteral("process_id")), QStringLiteral("process_id"));
        if (pid > std::numeric_limits<quint32>::max()) fail(QStringLiteral("process_id exceeds 32 bits."));
        debugger.attach(static_cast<quint32>(pid));
    }
    const auto process_id = debugger.process_id();
    QJsonArray output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    quint32 thread = 0;
    QString error;
    bool cancelled = false;
    quint32 memory_read_total = 0;
    try {
        for (const auto& value : operations) {
            check_cancelled(stop);
            if (std::chrono::steady_clock::now() >= deadline) fail(QStringLiteral("The debug session exceeded its 30-second limit."));
            if (!value.isObject()) fail(QStringLiteral("Every debugger operation must be an object."));
            const auto op = value.toObject();
            const auto command = op.value(QStringLiteral("op")).toString();
            if (command == QStringLiteral("wait")) {
                const auto timeout = bounded(op, QStringLiteral("timeout_ms"), 1000, 10000);
                // Short waits allow cancellation without abandoning an OS debug event.
                const auto wait_deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout));
                QJsonObject result;
                do {
                    check_cancelled(stop);
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(wait_deadline - std::chrono::steady_clock::now()).count();
                    result = debugger.wait_event(static_cast<quint32>(std::clamp<qint64>(remaining, 0, 100)));
                    if (result.value(QStringLiteral("kind")).toString() != QStringLiteral("timeout")) break;
                } while (std::chrono::steady_clock::now() < wait_deadline);
                if (result.contains(QStringLiteral("thread_id"))) thread = static_cast<quint32>(number(result.value(QStringLiteral("thread_id")), QStringLiteral("thread_id")));
                output.push_back(result);
            } else if (command == QStringLiteral("continue")) {
                if (op.contains(QStringLiteral("handled")) && !op.value(QStringLiteral("handled")).isBool()) fail(QStringLiteral("handled must be boolean."));
                debugger.continue_event(op.value(QStringLiteral("handled")).toBool(false));
                output.push_back(QJsonObject{{QStringLiteral("kind"), QStringLiteral("continued")}});
            } else if (command == QStringLiteral("step") || command == QStringLiteral("registers")) {
                const auto id = op.contains(QStringLiteral("thread_id")) ? number(op.value(QStringLiteral("thread_id")), QStringLiteral("thread_id")) : thread;
                if (!id || id > std::numeric_limits<quint32>::max()) fail(QStringLiteral("A valid event thread_id is required."));
                if (command == QStringLiteral("step")) { debugger.single_step(static_cast<quint32>(id)); output.push_back(QJsonObject{{QStringLiteral("kind"), QStringLiteral("stepping")}}); }
                else output.push_back(QJsonObject{{QStringLiteral("kind"), QStringLiteral("registers")}, {QStringLiteral("values"), debugger.registers(static_cast<quint32>(id))}});
            } else if (command == QStringLiteral("read")) {
                const auto address = number(op.value(QStringLiteral("address")), QStringLiteral("address"));
                const auto length = bounded(op, QStringLiteral("length"), 64, 65536);
                if (length > 4 * 1024 * 1024 - memory_read_total) fail(QStringLiteral("The session memory-read budget is 4 MiB."));
                memory_read_total += length;
                output.push_back(QJsonObject{{QStringLiteral("kind"), QStringLiteral("memory")}, {QStringLiteral("address"), hex(address)}, {QStringLiteral("bytes"), QString::fromLatin1(debugger.read_memory(address, length).toHex())}});
            } else if (command == QStringLiteral("breakpoint") || command == QStringLiteral("remove-breakpoint")) {
                const auto address = number(op.value(QStringLiteral("address")), QStringLiteral("address"));
                if (command == QStringLiteral("breakpoint")) debugger.set_breakpoint(address); else debugger.remove_breakpoint(address);
                output.push_back(QJsonObject{{QStringLiteral("kind"), command}, {QStringLiteral("address"), hex(address)}});
            } else if (command == QStringLiteral("detach")) {
                debugger.detach(); output.push_back(QJsonObject{{QStringLiteral("kind"), QStringLiteral("detached")}}); break;
            } else fail(QStringLiteral("Unknown debugger operation: ") + command);
        }
    } catch (const Cancelled&) { cancelled = true; error = QStringLiteral("Cancelled; clean detach requested."); }
    catch (const std::exception& exception) { error = QString::fromUtf8(exception.what()); }
    // Explicitly report cleanup failures instead of reporting success while a
    // target remains attached or an owned breakpoint could remain installed.
    debugger.detach();
    QJsonObject result{{QStringLiteral("process_id"), QString::number(process_id)}, {QStringLiteral("events"), output},
        {QStringLiteral("detached"), !debugger.active()}, {QStringLiteral("cancelled"), cancelled}, {QStringLiteral("ok"), error.isEmpty()},
        {QStringLiteral("breakpoint_mode"), QStringLiteral("one-shot")}, {QStringLiteral("termination_requested"), false}};
    if (!error.isEmpty()) result.insert(QStringLiteral("error"), error);
    return result;
}
} // namespace reworkbench
