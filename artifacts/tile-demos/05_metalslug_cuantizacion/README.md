# 05 · Metal Slug — cuantización a EHB / 31 / 15 / 7 colores

Fondo de Metal Slug Mission 2 recortado en dos regiones con contenido real
(elegidas por máxima varianza de color):
- **edificios** (x=640, y=544) — estructuras urbanas de la franja media.
- **agua_objetos** (x=1280, y=1344) — franja baja con objetos/agua.

Para cada región se prueba el mismo algoritmo a 4 profundidades de paleta
(la EHB genera 64 colores con solo 32 bases: half = base/2 por hardware).

- `edificios/64c_bright` — EHB (64 = 32 base + half), mitad brillante→bases: mitad brillante → bases · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=36.5 PSNR=37.3 dB
- `edificios/31c_mediancut` — 31 colores (1 slot reservado), median-cut: median-cut · 31 colores (5 bits) · dither=floyd(1) · MSE=61.9 PSNR=35.0 dB
- `edificios/15c_mediancut` — 15 colores (1 slot reservado), median-cut: median-cut · 15 colores (4 bits) · dither=floyd(1) · MSE=63.7 PSNR=34.9 dB
- `edificios/7c_kmeans` — 7 colores, k-means: kmeans · 7 colores (3 bits) · dither=floyd(1) · MSE=131.4 PSNR=31.7 dB
- `agua_objetos/64c_bright` — EHB (64 = 32 base + half), mitad brillante→bases: mitad brillante → bases · 64 colores (6 bits/EHB) · dither=floyd(1) · MSE=7.3 PSNR=44.2 dB
- `agua_objetos/31c_mediancut` — 31 colores (1 slot reservado), median-cut: median-cut · 31 colores (5 bits) · dither=floyd(1) · MSE=39.8 PSNR=36.9 dB
- `agua_objetos/15c_mediancut` — 15 colores (1 slot reservado), median-cut: median-cut · 15 colores (4 bits) · dither=floyd(1) · MSE=46.6 PSNR=36.2 dB
- `agua_objetos/7c_kmeans` — 7 colores, k-means: kmeans · 7 colores (3 bits) · dither=floyd(1) · MSE=218.4 PSNR=29.5 dB

Cada carpeta lleva `reconstruct.png` (resultado), `palette.json`/`.h` (paleta
adaptada en orden Amiga) y `tilebank.h`/`.bin` listos para incluir en C/C++.
Observa cómo baja la calidad y sube la textura de dithering conforme se
reducen colores: EHB≈64 muy bueno, 31 bueno, 15 aceptable, 7 estilizado.
