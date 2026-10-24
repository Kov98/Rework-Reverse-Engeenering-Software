# Rework brand assets

`rework-logo.png` is the user's official logo, copied byte-for-byte from
`C:/Users/pedri/Downloads/Logo R de Fita Roxa em 3D.png` on 2026-10-16.

- SHA-256: `f1e4c58ee87001516c25ec3f0f42aee5bc8243202febbc8736c7d8e8ae561233`
- Dimensions: 1313 × 1198 pixels, RGBA, 1,011,306 bytes.
- Alpha spans 0–255; 759,092 pixels are fully transparent and 811,282 have
  partial alpha. The source's transparency, canvas and edge pixels are preserved.
- Representative purple: `#6018F8`, the most common 8-step quantized RGB sample
  among purple pixels with alpha greater than 200 (70,547 pixels).

`rework.ico` contains 32-bit transparent PNG frames at 16, 20, 24, 32, 40, 48,
64, 128 and 256 pixels. Every frame scales the complete original canvas
proportionally and centers it on a transparent square; no cropping, background
removal, recoloring, retouching or generative processing is applied.

To reproduce the ICO using Windows PowerShell and System.Drawing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./native/workbench/assets/brand/convert_logo.ps1
```

`brand.qrc` embeds the assets for Qt. `rework.rc.in` supplies the Windows
executable icon and display metadata; internal target names and settings
identities stay compatible with existing projects and installations.
