# 02 · Redimensionado de calidad (Lanczos-3 vs área vs bilineal vs vecino)

Fuente: fan-art "apple_guy" (1191×671). En Amiga casi siempre hay que ajustar
la resolución a un presupuesto de Chip RAM: aquí el objetivo es 640×368.

Se comparan 4 métodos (`--resample`):
- **lanczos** — la mejor interpolación; ideal para degradados y bordes.
- **area** — caja (box); óptimo para REDUCCIONES (media de cada bloque fuente).
- **bilinear** — suave y rápido.
- **nearest** — vecino más próximo; rompedor, útil solo para pruebas.

Carpetas `resample_<método>/source_resized.png` — el mismo origen reescalado con
cada método, y `resample_lanczos/quant_16c` cuantiza el resultado a 16 colores
con Floyd–Steinberg (kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=549.6 PSNR=25.5 dB).

Abre las cuatro versiones lado a lado para ver diferencias en los bordes:
lanczos mantiene contraste, area suaviza el ruido, nearest crea dientes de sierra.
