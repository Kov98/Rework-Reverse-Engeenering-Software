# Native executable checks

These pytest tests drive the real C++ executables. Python is only the test
runner; the desktop and CLI do not use the historical Python/Ghidra engine.

Build the workbench with Qt and deploy its runtime DLLs/plugins next to the
executables. For the complete native suite, also configure:

```powershell
cmake -S . -B build -DREWORKBENCH_WITH_CAPSTONE=ON -DREWORKBENCH_BUILD_EXAMPLE_EXTENSION=ON -DREWORKBENCH_BUILD_TEST_FIXTURES=ON -DREWORKBENCH_BUILD_UI_TESTS=ON
cmake --build build --parallel 1
Copy-Item build/native/workbench/example_extension/libreworkbench_example_extension.dll build/bin/
.venv/Scripts/python.exe -m pytest tests/native -v --junitxml=build/native-results.xml
```

Qt and Capstone paths and the compiler must match the chosen build environment.
See [validation.md](../../docs/validation.md) for the environment actually used.
The extension DLL must be in `build/bin` alongside the CLI. Qt deployment uses
`windeployqt` for the GUI and CLI. The default paths target Windows; select
another build with `REWORKBENCH_NATIVE_CLI` and `REWORKBENCH_NATIVE_GUI`.

Missing executables skip the corresponding tests. Missing decoder support skips
the six scalar decoding/decompilation/interpreter cases. The optional example
extension and debugger fixture each have a separate availability check. Always
read the passed/failed/skipped counts when validating a build.

Coverage includes format and architecture identification, universal slice
selection, truncated input, raw configuration, Unicode project paths, content
hashes, project reopening/moving/reuse, modified sample rejection, scalar results
and conditional flow, unknown inputs, bounded loops, repeatable decompilation,
patch copy preservation/rejection, explicit debugger consent, debugger
launch/registers/detach, extension hash/opt-in checks, pagination validation, and
opening/closing the actual Windows main window.

The optional UI suite needs the Qt Test development component, Qt6Test.dll and
the platforms/qoffscreen.dll plugin in the deployed test directory. The pytest
wrapper selects the offscreen platform. It exercises actual C++ widgets for
Dark Modern editor colors, saved view order/visibility and fonts, layout reset, local AI
request preparation and function tab navigation. Test settings use an isolated
temporary directory and do not overwrite the desktop user's preferences.

The revised workspace also covers the compact command center, sidebar/panel
toggles, quick function search, tab cycling, first-location history, precise
reference origins, following instruction targets, and destruction of visible
windows without an explicit close call. Settings use explicit IniFormat;
changing QSettings::defaultFormat alone does not redirect the constructor that
implicitly selects the native organization/application store on Windows.

Theme-specific checks cover strings containing comment markers, multiline
comments, call/variable/number colors, embedded Codicons with tint at 2x pixel
density, and table row heights when increasing the code font.

On Windows the offscreen wrapper points QT_QPA_FONTDIR at the installed Windows
Fonts directory. That plugin does not discover native Windows fonts on its own;
without this setting, adding an icon font can make it the fallback for ordinary
text and invalidate font measurements. Microsoft fonts are read in place, never
bundled. The effective-family test logs family, fixed pitch, logical pixel size,
window DPI and device pixel ratio; it can also be run with QT_QPA_PLATFORM=windows.

The refined suite also exercises adaptive empty Quick Open geometry, distinct
label/description/shortcut roles, disabled-result keyboard navigation, query
Home-key editing, actual file/symbol glyph rendering, 100% initial graph scale,
an explicit graph overview, idempotent line geometry, preservation of older
custom font attributes, reduced-motion persistence, explorer expansion and local
AI requests with more than 5,000 short context lines. A compact hexadecimal view
with a long filename verifies that toolbar controls retain their height and the
file label stays within the available width while preserving the full tooltip.

Rework Dark checks also inspect alpha at the actual menu/picker corners before
and after filtering/resizing, effective fonts after stylesheet polish, the
official application icon/title, and graph text bounds with a custom 24pt font.
Run the surface test as the first Windows scenario as well: installing Qt's first
stylesheet can reload native class fonts, which an offscreen run does not expose.
The existing navigation fixture samples the selected row near its right edge
and the neutral left margin, catching native branch backgrounds that can cover
a custom rounded selection. `REWORKBENCH_UI_CAPTURE_DIR` optionally saves direct
Qt widget images of the real menu, command registry and Quick Open. That path
also verifies dispatch of the actual Restore Layout command, preserved fonts and
the restored assistant dock; it sends no global desktop keystrokes.

Navigation cases exercise exact instruction history, analysis switches, closing
an inactive tab, same-snapshot document reuse, rename invalidation, and a Find
result whose assembly instruction needs another 200-row page. Renderer fixtures
check zero-based instruction origins, unmapped synthetic lines, output cuts and
cache validation. Editor cases cover scoped Ctrl+F/F3 shortcuts, literal/case
search, circular navigation, the 1,000-highlight boundary, document replacement
and real arrow/selection keys without allowing text edits. Optional captures
include `navigation-find.png`, taken from the real MainWindow and its own raw
fixture; it is not a mockup or a composite image.

`debug_target.cpp` is a finite, quiet target authored for this suite. Qt's
help/version commands can show modal dialogs when a debugged target has no
terminal; a quiet target avoids user interaction and keeps target output from
interfering with the CLI JSON stream. The GUI smoke test creates its own process
and closes that process normally using `WM_CLOSE`.

These are initial integration tests, not exhaustive ISA/ABI/type recovery,
decompiler equivalence, malformed-file fuzzing, debugger lifecycle, or performance
certification. Test fixtures are self-authored and are not untrusted live targets.
