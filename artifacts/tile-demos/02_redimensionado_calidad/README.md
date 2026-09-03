# 02 Ã‚Â· Redimensionado de calidad (Lanczos-3 vs ÃƒÂ¡rea vs bilineal vs vecino)

Fuente: fan-art "apple_guy" (1191Ãƒâ€”671). En Amiga casi siempre hay que ajustar
la resoluciÃƒÂ³n a un presupuesto de Chip RAM: aquÃƒÂ­ el objetivo es 640Ãƒâ€”368.

Se comparan 4 mÃƒÂ©todos (`--resample`):
- **lanczos** Ã¢â‚¬â€ la mejor interpolaciÃƒÂ³n; ideal para degradados y bordes.
- **area** Ã¢â‚¬â€ caja (box); ÃƒÂ³ptimo para REDUCCIONES (media de cada bloque fuente).
- **bilinear** Ã¢â‚¬â€ suave y rÃƒÂ¡pido.
- **nearest** Ã¢â‚¬â€ vecino mÃƒÂ¡s prÃƒÂ³ximo; rompedor, ÃƒÂºtil solo para pruebas.

Carpetas `resample_<mÃƒÂ©todo>/source_resized.png` Ã¢â‚¬â€ el mismo origen reescalado con
cada mÃƒÂ©todo, y `resample_lanczos/quant_16c` cuantiza el resultado a 16 colores
con FloydÃ¢â‚¬â€œSteinberg (kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=549.6 PSNR=25.5 dB).

Abre las cuatro versiones lado a lado para ver diferencias en los bordes:
lanczos mantiene contraste, area suaviza el ruido, nearest crea dientes de sierra.
