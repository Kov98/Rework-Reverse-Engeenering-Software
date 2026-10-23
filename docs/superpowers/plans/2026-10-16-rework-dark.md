# Rework Dark implementation plan

**Goal:** Refine the existing C++/Qt IDE with the supplied Rework identity and verify the actual Windows rendering.

**Architecture:** Retain the current engine, dock system, saved settings and document navigation. Extend the central theme with appearance metrics; use the original logo in application/Windows resources. Keep syntax roles independent of branding.

**Tech stack:** C++20, Qt 6.8, CMake, Windows resources, existing Codicons and Seti fonts.

**Spec:** User mission supplied in attachment `1445328f-493e-4a3a-83a9-8212df7eee13/Pasted text.txt`.

## Constraints and review focus

- Preserve the engine, internal names and settings namespace; display the product as Rework.
- Preserve the source logo byte-for-byte and its alpha/aspect ratio when packaging ICO frames.
- Do not force zoom, install fonts or substitute fictitious semantic/AI features.
- Check real fonts after QSS polish, native popup corners, keyboard focus and 125% layout.
- Preserve existing custom code fonts, popup activation rules, tree hit testing and docking.

## Work

- [x] Inspect the existing font, style, icon and popup implementation; measure the logo color and transparency.
- [x] Integrate original logo resources, executable icon/version metadata, window identity and About.
- [x] Centralize Rework Dark graphite/purple colors, radii, spacing and typography; remove inconsistent small QSS overrides and refine menus/controls/assistant.
- [x] Refine icon geometry, popup surfaces, explorer interaction backgrounds and graph font-dependent bounds.
- [x] Build Release serially; run the native/Qt suite and targeted Windows 125% cases.
- [x] Inspect real screenshots of the workspace, menus, palette, tree and assistant; correct observed defects, document results and limits.

Changes proceed directly under the user's implementation request. No commit, publishing, marketplace or engine expansion is included.
