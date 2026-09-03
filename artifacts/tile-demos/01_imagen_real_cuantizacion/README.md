# 01 Ã‚Â· Imagen real Ã¢â€ â€™ cuantizaciÃƒÂ³n a 16 colores (dithering on/off)

Fuente: fan-art "pac_man_muscle" (999Ãƒâ€”800). Se **redimensiona con Lanczos** a un
ÃƒÂ¡rea de ~300000 px (ver `resized/source_resized.png`) para que el buffer de
ÃƒÂ­ndices a 1 B/px quepa en ~300 KB de RAM.

Objetivo: comparar el resultado a 16 colores **sin dither** (estricto, ideal para
pixel-art: mantiene superficies planas) frente a **FloydÃ¢â‚¬â€œSteinberg** (fotos y
degradados) y **Atkinson**.

Carpetas:
- `source.png` Ã¢â‚¬â€ imagen original (999Ãƒâ€”800).
- `resized/source_resized.png` Ã¢â‚¬â€ redimensionado Lanczos (mÃƒÂºltiplo de 16).
- `dith_none`:  Ã‚Â· (original cuantizado vs reconstruido): 100.00% de 293632 índices Ã‚Â· kmeans · 16 colores (4 bits) · dither=none(1) · MSE=356.5 PSNR=27.4 dB
- `dith_floyd`:  Ã‚Â· (original cuantizado vs reconstruido): 100.00% de 293632 índices Ã‚Â· kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=709.0 PSNR=24.4 dB
- `dith_atkinson`:  Ã‚Â· (original cuantizado vs reconstruido): 100.00% de 293632 índices Ã‚Â· kmeans · 16 colores (4 bits) · dither=atkinson(1) · MSE=663.4 PSNR=24.7 dB

Cada variante contiene `reconstruct.png` (lo que se dibuja), `tilebank.png`,
`palette.json`/`.h`, `tilebank.h`/`.bin` (listos para incrustar en C/C++).
