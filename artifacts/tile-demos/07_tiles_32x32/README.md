# 07 Ã‚Â· Tiles de 32Ãƒâ€”32 y patrÃƒÂ³n de repeticiÃƒÂ³n

El mismo algoritmo con `--tile 32`. A tamaÃƒÂ±o de tile mayor hay MENOS celdas, y
la repeticiÃƒÂ³n exacta cambia: en un atlas (Beginning Fields) con tiles 32Ãƒâ€”32 casi
todo tile es ÃƒÂºnico; en una foto real (aussie) tambiÃƒÂ©n. La tool lo detecta y lo
avisa en consola automÃƒÂ¡ticamente.

- `atlas_32x32` Ã¢â‚¬â€ Beginning Fields, EHB, tile 32:
   Ã‚Â· (original cuantizado vs reconstruido): 100.00% de 409600 índices
  AVISO: sin patrón de repetición (400/400 celdas únicas, 0.0% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `foto_32x32` Ã¢â‚¬â€ aussie_bum (Lanczos a ~300 KB), 16 colores con Floyd, tile 32:
   Ã‚Â· kmeans · 16 colores (4 bits) · dither=floyd(1) · MSE=768.7 PSNR=24.0 dB
  AVISO: sin patrón de repetición (280/280 celdas únicas, 0.0% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.
- `metalslug_32x32` Ã¢â‚¬â€ recorte agua_objetos, EHB bright, tile 32:
   Ã‚Â· mitad brillante → bases · 64 colores (6 bits/EHB) · dither=none(1) · MSE=4.3 PSNR=46.6 dB
  (sin aviso)

Cuando la tool emite el AVISO de "sin patrÃƒÂ³n de repeticiÃƒÂ³n", el tilebank es
equivalente a la imagen: `tilebank.bin == imagen indexada` y `tilebank.png` Ã¢â€°Ë†
`reconstruct.png`. Abre los dos PNG en esas carpetas y compruÃƒÂ©balo.
