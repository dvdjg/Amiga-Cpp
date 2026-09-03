# 08 · Fotos reales → EHB (64) / 32 / 16 colores, dither Floyd–Steinberg

Las cinco imágenes foto-realistas (apple_guy, aussie_bum, pac_man,
forgotten_relict, landscape_painting) reducidas con Lanczos a ~300 KB de
buffer y cuantizadas a EHB (paleta `ehb` half-max), 32 y 16 colores, todas
con dither **Floyd–Steinberg** (el mejor para fotos).

- **EHB** (`64c_*`): 32 bases + half generado por hardware, paleta que
  maximiza el uso de los hal‑brite (`--palette ehb`, nunca peor que el óptimo).
- **32 colores** (`32c_*`): 5 bits/píxel, paleta k-means.
- **16 colores** (`16c_*`): 4 bits/píxel, paleta k-means (más pérdida).
Todas con **`--dither best`** = Floyd + **serpentina** + **deadband 14** + **clamp 16**:
evita el punteado visible en zonas de color casi uniforme (cielos) sin perder los
degradados. (Medido en landscape: cielo con píxeles aislados 9.7 % → 3.5 % y
PSNR 25.5 → 27.7 dB frente al Floyd clásico.)

- `apple_guy/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=194.2 PSNR=30.0 dB · AVISO: sin patrón de repetición (1140/1170 celdas únicas, 2.6% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `apple_guy/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=305.0 PSNR=28.1 dB · AVISO: sin patrón de repetición (1103/1170 celdas únicas, 5.7% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `apple_guy/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=471.5 PSNR=26.2 dB · AVISO: sin patrón de repetición (1119/1170 celdas únicas, 4.4% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `aussie_bum/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=230.8 PSNR=29.3 dB · AVISO: sin patrón de repetición (1135/1148 celdas únicas, 1.1% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `aussie_bum/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=410.1 PSNR=26.8 dB · AVISO: sin patrón de repetición (1073/1148 celdas únicas, 6.5% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `aussie_bum/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=622.0 PSNR=25.0 dB · AVISO: sin patrón de repetición (1085/1148 celdas únicas, 5.5% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `pac_man/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=199.3 PSNR=29.9 dB · AVISO: sin patrón de repetición (1145/1147 celdas únicas, 0.2% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `pac_man/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=256.9 PSNR=28.8 dB · AVISO: sin patrón de repetición (1100/1147 celdas únicas, 4.1% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `pac_man/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=430.1 PSNR=26.6 dB · AVISO: sin patrón de repetición (1095/1147 celdas únicas, 4.5% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `forgotten_relict/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=377.3 PSNR=27.1 dB · AVISO: sin patrón de repetición (1159/1170 celdas únicas, 0.9% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `forgotten_relict/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=442.7 PSNR=26.4 dB · AVISO: sin patrón de repetición (1155/1170 celdas únicas, 1.3% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `forgotten_relict/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=486.6 PSNR=26.0 dB · AVISO: sin patrón de repetición (1135/1170 celdas únicas, 3.0% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `landscape_painting/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=332.1 PSNR=27.7 dB · AVISO: sin patrón de repetición (1024/1170 celdas únicas, 12.5% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `landscape_painting/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=369.7 PSNR=27.2 dB · AVISO: sin patrón de repetición (1020/1170 celdas únicas, 12.8% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `landscape_painting/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=480.9 PSNR=26.1 dB · AVISO: sin patrón de repetición (1012/1170 celdas únicas, 13.5% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.

Cada carpeta lleva `reconstruct.png` (resultado), `palette.json`/`.h` y
`tilebank.h`/`.bin` listos para incrustar en C/C++.
