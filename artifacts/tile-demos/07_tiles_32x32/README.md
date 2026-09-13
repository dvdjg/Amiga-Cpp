# 07 · Tiles de 32×32 y patrón de repetición

El mismo algoritmo con `--tile 32`. A tamaño de tile mayor hay MENOS celdas, y
la repetición exacta cambia: en un atlas (Beginning Fields) con tiles 32×32 casi
todo tile es único; en una foto real (aussie) también. La tool lo detecta y lo
avisa en consola automáticamente.

- `atlas_32x32` — Beginning Fields, EHB, tile 32:
   · (original cuantizado vs reconstruido): 100.00% de 409600 índices
  AVISO: sin patrón de repetición (400/400 celdas únicas, 0.0% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `foto_32x32` — aussie_bum (Lanczos a ~300 KB), 16 colores con Floyd, tile 32:
   · kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=768.7 PSNR=24.0 dB
  AVISO: sin patrón de repetición (280/280 celdas únicas, 0.0% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `metalslug_32x32` — recorte agua_objetos, EHB bright, tile 32:
   · mitad brillante → bases · 64 colores (6 bits/EHB) · dither=none(1) · MSE=4.3 PSNR=46.6 dB
  (sin aviso)

Cuando la tool emite el AVISO de "sin patrón de repetición", el tilebank es
equivalente a la imagen: `tilebank.bin == imagen indexada` y `tilebank.png` ≈
`reconstruct.png`. Abre los dos PNG en esas carpetas y compruébalo.
