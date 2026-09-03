# 08 · Fotos reales → EHB (64) / 32 / 16 colores, dither Floyd–Steinberg

Las cinco imágenes foto-realistas (apple_guy, aussie_bum, pac_man,
forgotten_relict, landscape_painting) reducidas con Lanczos a ~300 KB de
buffer y cuantizadas a EHB (paleta `ehb` half-max), 32 y 16 colores, todas
con dither **Floyd–Steinberg** (el mejor para fotos).

- **EHB** (`64c_floyd`): 32 bases + half generado por hardware, paleta que
  maximiza el uso de los hal‑brite (`--palette ehb`, nunca peor que el óptimo).
- **32 colores** (`32c_floyd`): 5 bits/píxel, paleta k-means.
- **16 colores** (`16c_floyd`): 4 bits/píxel, paleta k-means (más pérdida).

- `apple_guy/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=256.1 PSNR=28.8 dB · AVISO: sin patrón de repetición (1169/1170 celdas únicas, 0.1% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `apple_guy/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=374.3 PSNR=27.2 dB · AVISO: sin patrón de repetición (1151/1170 celdas únicas, 1.6% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `apple_guy/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=552.1 PSNR=25.5 dB · AVISO: sin patrón de repetición (1150/1170 celdas únicas, 1.7% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `aussie_bum/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=325.3 PSNR=27.8 dB · AVISO: sin patrón de repetición (1140/1148 celdas únicas, 0.7% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `aussie_bum/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=552.4 PSNR=25.5 dB · AVISO: sin patrón de repetición (1111/1148 celdas únicas, 3.2% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `aussie_bum/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=868.2 PSNR=23.5 dB · AVISO: sin patrón de repetición (1131/1148 celdas únicas, 1.5% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `pac_man/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=277.7 PSNR=28.5 dB · AVISO: sin patrón de repetición (1145/1147 celdas únicas, 0.2% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `pac_man/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=354.1 PSNR=27.4 dB · AVISO: sin patrón de repetición (1123/1147 celdas únicas, 2.1% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `pac_man/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=709.0 PSNR=24.4 dB · AVISO: sin patrón de repetición (1118/1147 celdas únicas, 2.5% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `forgotten_relict/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=472.4 PSNR=26.2 dB · AVISO: sin patrón de repetición (1166/1170 celdas únicas, 0.3% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `forgotten_relict/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=582.1 PSNR=25.3 dB · AVISO: sin patrón de repetición (1162/1170 celdas únicas, 0.7% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `forgotten_relict/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=746.7 PSNR=24.2 dB · AVISO: sin patrón de repetición (1146/1170 celdas únicas, 2.1% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `landscape_painting/64c_floyd` — EHB (64 = 32 base + half): EHB half-max (brillos ajustados a los half) · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=550.1 PSNR=25.5 dB · AVISO: sin patrón de repetición (1165/1170 celdas únicas, 0.4% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `landscape_painting/32c_floyd` — 32 colores: kmeans · 32 colores (5 bits) · dither=floyd(1) · MSE=542.2 PSNR=25.6 dB · AVISO: sin patrón de repetición (1159/1170 celdas únicas, 0.9% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `landscape_painting/16c_floyd` — 16 colores: kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=712.0 PSNR=24.4 dB · AVISO: sin patrón de repetición (1140/1170 celdas únicas, 2.6% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.

Cada carpeta lleva `reconstruct.png` (resultado), `palette.json`/`.h` y
`tilebank.h`/`.bin` listos para incrustar en C/C++.
