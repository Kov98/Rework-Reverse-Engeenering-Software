# Native investigation tools

These C++20 sources add deliberately bounded investigation APIs. Current build and
runtime coverage is recorded in [validation.md](../../../docs/validation.md).
This documentation describes their contracts and limits; initial smoke tests do
not establish correctness for arbitrary binaries or parity with another product.

## Byte and function comparisons

`compare_binaries(leftPath, rightPath, stop)` compares every byte of two regular
files up to 1 GiB each. SHA-256 covers each complete input, including its different
length. Consecutive changed bytes become half-open offset/length ranges. Total
changed bytes and total ranges remain exact; the returned range list stops at
4096 records and carries `ranges_truncated`. Input sizes and full hashes are checked
again to reject files that changed during the operation. Work reads in 1 MiB
chunks and checks cancellation between chunks. Large integers are JSON strings.

`compare_functions(leftSnapshot, leftIndex, rightSnapshot, rightIndex, stop)` uses
a bounded longest common subsequence over at most 4096 decoded instructions per
function. Tokens retain register names and ordinary immediate constants, while
normalizing direct branch/call destinations and RIP/EIP-relative displacements.
It returns instruction counts, matching token count, shape similarity, architecture
compatibility, partial-input status and exact concatenated instruction-byte equality.
This is a similarity aid; it does not prove semantic equivalence or recover a
binary-wide function matching automatically. Cross-architecture similarity is
reported explicitly and should not be interpreted as equivalent behavior.

## Original-preserving patch export

`export_patched_copy(source, destination, patches, projectRoot, protectedPaths, stop)`
accepts a patch array such as:

```json
[
  {"offset":"0x100", "expected":"750a", "replacement":"9090"}
]
```

Offsets are unsigned decimal or `0x` strings. Expected/replacement values are
strict hexadecimal without separators. They must be nonempty and equal in length.
The exporter rejects insertions, deletions, overlapping patches, ranges beyond
EOF, source files above 8 GiB, more than 4096 patches, patches above 1 MiB each
and aggregate patch content above 16 MiB. Expected original bytes are checked before export and again while
streaming replacements, including patches spanning a chunk boundary.

The source is read only. A separate `QSaveFile`, with direct-write fallback
disabled, is committed only after repeated complete source hashes agree and
cancellation/protected destinations are rechecked. The result records the original
and exported SHA-256, size and patch count. File permissions are preserved where
Qt's filesystem implementation supports it. Checksum/signature regeneration is
not implemented; patched executables can become invalid or fail signature checks.

The destination cannot be the source, a symbolic link, a hard link to a protected
file, any explicitly protected path, or a descendant of an explicitly protected
directory. When a project root is supplied, only its `exports` directory may
receive output; sample copies, the manifest, database and other internals remain
protected. The parent directory must already exist. GUI/CLI callers should pass
both imported copies and original provenance paths as `protectedPaths`. Path and
hash checks detect ordinary concurrent changes; they do not establish a security
boundary against an adversary concurrently replacing filesystem junctions or paths.

## Bounded scalar interpreter

`emulate_function(snapshot, functionIndex, initialState, stop)` interprets the
snapshot's decoded instructions as data. It never maps host memory, starts a
process, performs a syscall, opens the sample implicitly, or invokes a callee.
An example initial-state document is:

```json
{
  "registers":{"rax":"0x1", "rcx":"0x2", "rsp":"0x1008"},
  "flags":{"zf":false},
  "memory":[{"address":"0x1000", "bytes":"00000000000000000000000000000000", "writable":true}],
  "max_steps":10000,
  "timeout_ms":2000,
  "trace_limit":256
}
```

The optional `entry` string must select an already decoded instruction in the
supplied function. Unknown registers and flags remain unknown. Register aliases
track known bits, partial writes and x64 zero extension after 32-bit writes.
Contradictory aliases are rejected. Arithmetic is fixed width and unsigned host
overflow is used only for the intended modular machine arithmetic. ZF, CF, SF,
OF, PF and AF are modeled for the supported operations. Memory exists only in
the explicit nonoverlapping mappings and enforces the supplied writable bit.

Implemented x86/x64 scalar operations: MOV/MOVABS, LEA, ADD/SUB/CMP,
AND/OR/XOR/TEST, NOT/NEG/INC/DEC, ordinary register/immediate PUSH and register POP,
NOP/ENDBR, direct JMP and common flag-based Jcc forms. A return ends the function
before touching a caller return address. CALL, indirect transfers, unsupported
prefixes, segments, mixed-width operations, unknown inputs, missing memory,
traps and all unmodeled instructions stop with an explicit reason. Stack memory
operands and SP push/pop subtleties are rejected. No floating point, SIMD,
exceptions, atomics, OS model or other architectures are implemented.

Limits: 4096 decoded instructions, 100000 executed steps, 10 seconds, 1 MiB
explicit memory, 256 mappings, 80 initial register entries and 2048 trace records.
Defaults are 10000 steps, 2 seconds and 256 trace records. A stopped instruction
is atomic: its tentative register/flag/memory updates are discarded. The result
contains state, known masks, trace, PC and a stop reason. Returning from a function
does not assert that the full program behavior or analysis snapshot is correct.

## Windows native debugger

`NativeDebugger` has explicit `launch`, `attach`, `wait_event`, `continue_event`,
`single_step`, `set_breakpoint`, `remove_breakpoint`, `read_memory`, `registers`
and `detach` operations. Every call and destruction must occur on the creating
thread; Windows debug events are thread-affine. Only Windows x86/x64 hosts with
a matching target architecture are supported. WOW64 from an x64 debugger,
ARM targets, kernel debugging and remote debugging are rejected or unavailable.
The non-Windows implementation returns an explicit capability error.

Launch passes an explicit executable to `CreateProcessW`, quotes each argument
using Windows argument quoting rules, prevents handle inheritance and debugs
only that process. Attach uses `DebugActiveProcess`; no privilege escalation or
implicit target selection occurs. Kill-on-debugger-exit is disabled. Process and
DLL file handles from debug events are closed; Windows owns the event process
and thread handles, while the session retains its own process handle.

Memory/register access, breakpoint changes and stepping require a pending paused
debug event. Memory reads stop at 64 KiB. Breakpoints are **one shot** INT3
breakpoints, limited to 128: a hit restores the byte, rewinds the instruction
pointer and reports consumption. Repeated breakpoints require a later explicit
reinstallation after stepping. This avoids claiming a persistent breakpoint
implementation while other target threads could pass an unpatched instruction.
Breakpoints require committed executable memory and reject an existing INT3.
Single-step uses the event thread's trap flag and clears the owned flag on the
matching single-step event. Existing trap flags are rejected. Unknown application
exceptions are passed to the target unless explicitly handled. The initial
loader breakpoint and owned breakpoint/step exceptions are handled by the debugger.

Detach restores owned breakpoints and trap flags before disconnecting. If the
target is running with modifications, it requests a debug break and waits up to
2 seconds to obtain a paused event. Cleanup failures are errors; destruction
attempts detach but cannot report an exception and never deliberately terminates
the target. Protected processes, target exit, inaccessible pages and OS errors
can make cleanup fail; an interactive caller should retain a session and expose
explicit detach errors. The one-shot wrapper explicitly detaches before returning.

`run_debug_session(request, stop)` provides bounded one-shot automation. It accepts
`execute_target:true` and exactly one `executable` path or `process_id` integer
string. Example document (provided as data, not executed during development):

```json
{
  "execute_target":true,
  "executable":"C:/fixtures/owned-program.exe",
  "arguments":[],
  "operations":[
    {"op":"wait", "timeout_ms":1000},
    {"op":"registers"},
    {"op":"read", "address":"0x140001000", "length":16},
    {"op":"continue"},
    {"op":"wait", "timeout_ms":1000},
    {"op":"detach"}
  ]
}
```

Additional operations: `breakpoint`/`remove-breakpoint` with `address`, `step` with
an optional event `thread_id`, and `continue` with an optional boolean `handled`.
The most recent event thread is the default for registers/step. This wrapper has
1024-operation, 30-second and 4 MiB total memory-read limits. A wait is at most
10 seconds, polling cancellation every 100 ms. Errors from operations return
`ok:false`, preserved event output and an error message after clean detach.
Failure to detach throws rather than claiming success. No live debug session is
stored between wrapper calls. Detaching resumes a surviving target; it does not
close the target program.

## Opt-in native extensions

`discover_extensions(directory, stop)` inspects Qt plugin metadata without
loading libraries or calling constructors. The scan is nonrecursive, skips
symbolic links and is capped at 4096 directory entries, 128 libraries, 64 MiB per
library and 64 KiB manifest JSON. Each result contains compatibility/error state,
path, SHA-256 and manifest. Discovery never invokes an extension.

Extensions implement `reworkbench::NativeExtension` from `plugin.hpp` as a Qt
plugin with IID `org.reworkbench.NativeExtension/1.0`, runtime API version 1 and a
JSON metadata file such as:

```json
{"api_version":1,"id":"example.extension","commands":["describe"]}
```

`invoke_extension(path, expectedSha256, command, arguments, explicitOptIn, stop)`
requires the exact selected hash and an explicitly true opt-in argument. The
command must be declared in metadata, metadata and runtime API versions must
agree, arguments are limited to 1 MiB JSON and returned JSON to 8 MiB. The hash
is checked before loading and after obtaining the instance. Native plugin code
can execute as soon as loading begins. `PreventUnloadHint` keeps code mapped for
the process lifetime to avoid callbacks pointing into an unloaded library.

Extensions are trusted native code in the application's process: they are not
sandboxed, cancellation cannot interrupt their native invocation, and there is
no hard runtime or memory limit inside an extension. Hash checks detect ordinary
file changes; they are not an authenticity signature and cannot prevent all
concurrent replacement races. For untrusted extensions a separate worker-process
protocol and OS sandbox remain future work. No extension was loaded or invoked
while writing these sources.

## Source references and deferred acceptance

Debugger handle/event ownership follows Microsoft's
[debugging event documentation](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events)
and the
[CREATE_PROCESS_DEBUG_INFO contract](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-create_process_debug_info).
Detach uses
[DebugActiveProcessStop](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocessstop).

When runtime checks are authorized, acceptance must include chunk-boundary patches,
overlap and stale expected-byte rejection, hardlink/symlink protections, source
changes during export/comparison, x64 alias/flag/stack semantics, unsupported-input
stop state, timeout/cancellation, debugger event ownership and detach during each
lifecycle phase, one-shot breakpoint rewind, application exceptions and plugin
metadata/API/hash rejection. These checks have not been performed.
