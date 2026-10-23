# bytecore

Optional Rust accelerator for the workbench's byte triage. The Python application
also has a standard-library implementation and does not require this executable.
The session prefers the project's own C++20 scanner in `native/bytecore_cpp`.
When C++ is absent, it discovers this Rust helper at
`native/bytecore/target/release/bytecore.exe`. A selected native helper that fails
falls back directly to Python. No helper is downloaded, installed or compiled
automatically. API callers can also pass an existing binary as
`native_executable` to `reworkbench.application.byte_scan.scan_bytes`.

CLI contract:

```text
bytecore <file> [--min-length N] [--max-strings N]
```

Defaults are `--min-length 4` and `--max-strings 10000`. Minimum length must be
positive; maximum strings may be zero. Flags may appear in either order after
the file. Unknown, repeated or incomplete flags fail. Paths are passed as native
OS arguments, including paths containing spaces.

The executable writes one JSON object followed by a newline to standard output:

```json
{"size":4,"entropy":0.0,"strings":[{"offset":0,"value":"aaaa"}],"strings_truncated":false}
```

`size` counts bytes actually read. Entropy is Shannon entropy in bits per byte
over the entire file (0 through 8; empty input is 0). Strings are maximal runs
of bytes `0x20` through `0x7e`, including spaces, with file offsets starting at
zero. Tabs, newlines and bytes outside printable ASCII end a run. Runs spanning
read boundaries are retained as one string, including a run ending at EOF.

Only the first configured number of qualifying strings is retained. A retained
value contains at most 4096 characters; the original run length determines
whether it qualifies. `strings_truncated` is true if any qualifying run was
omitted or any retained value was shortened. The histogram still covers every
byte after the string limit is reached. Memory usage is bounded by the chunk,
the 256 histogram entries, and the configured output limits, rather than input
file size. Large caller-supplied output limits can still require large memory.

Inputs must be regular files; the program only reads them as data. Failures
write `{"error":"message"}` to standard error and return a nonzero status.
The Python adapter validates the JSON result, bounds the message read, polls for
cancellation and terminates the direct child on cancellation or a 300-second
native timeout. It falls back to Python when the helper is unavailable or
fails. Configure only a trusted helper; its redirected output is disk-backed
while it runs. The adapter rejects using the input file itself (including a
hard link) as the executable and accepts only `.exe` helpers on Windows.

These source files have not been compiled or executed in this change, and no
dependencies were installed, per the requested source-only workflow.
