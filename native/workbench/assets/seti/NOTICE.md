# Seti file icons: attribution and source

Seti UI by Jesse Weed and contributors, copyright (c) 2014 Jesse Weed.
Licensed under the MIT License; the complete original notice is `LICENSE.md`.

Upstream project: https://github.com/jesseweed/seti-ui

All upstream files are pinned to commit
`2d6c5e68b4ded73c92dac291845ee44e1182d511` (2025-10-28):

- Original, unmodified TrueType font:
  https://github.com/jesseweed/seti-ui/blob/2d6c5e68b4ded73c92dac291845ee44e1182d511/styles/_fonts/seti/seti.ttf
- Font character definitions:
  https://github.com/jesseweed/seti-ui/blob/2d6c5e68b4ded73c92dac291845ee44e1182d511/styles/_fonts/seti.less
- File associations:
  https://github.com/jesseweed/seti-ui/blob/2d6c5e68b4ded73c92dac291845ee44e1182d511/styles/components/icons/mapping.less
- Original icon colors:
  https://github.com/jesseweed/seti-ui/blob/2d6c5e68b4ded73c92dac291845ee44e1182d511/styles/ui-variables.less
- Complete license:
  https://github.com/jesseweed/seti-ui/blob/2d6c5e68b4ded73c92dac291845ee44e1182d511/LICENSE.md

The built-in VS Code Seti file theme uses this same upstream icon family:
https://github.com/microsoft/vscode/tree/main/extensions/theme-seti

The bundled font has not been converted, subsetted, or redrawn. Qt loads it
from an embedded resource; no installed font or network access is required.
The application maps supported file extensions to the original glyphs and
renders them at the requested display size using the original Seti colors
through centralized theme tokens. Headers use the original C/C++ glyphs in
Seti purple. Executables, raw binaries, and native libraries use the existing
licensed Codicons family because Seti has no corresponding binary/library
file glyphs. Source-file support in the helper does not add a source browser
or imply source files are present in an analyzed binary.

Font SHA-256:
`655e602ea20d969733b95e28d8e39d31c0c807888d6f7d39684712f85af1923c`

License SHA-256:
`d4d2d888c6a0a220a7b0e2d6013cf37539b0e05ef937002b3b33bdb364575b88`
