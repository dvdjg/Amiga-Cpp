# 04 Ã‚Â· Metal Slug Mission 2 Ã¢â‚¬â€ extracciÃƒÂ³n de bandas (planos del fondo)

Fuente: `Neo Geo _ NGCD - Metal Slug - Backgrounds - Mission 2.png` (4504Ãƒâ€”2617,
fondo continuo de planos horizontales, sin gutters).

`--extract-bands` detecta cortes por el salto de color medio entre filas y
extrae BANDAS horizontales a ancho completo (los "planos" del fondo: cielo,
montaÃƒÂ±as, edificios, agua, objetos). Cada banda se guarda como PNG; `bands.json`
guarda sus rects y `bands_preview.png` es una hoja de contacto para localizarlas.

Bandas extraÃƒÂ­das (jump 55, alineaciÃƒÂ³n 16):
```
   y=0..32 (32px)
   y=32..192 (160px)
   y=192..320 (128px)
   y=320..480 (160px)
   y=480..544 (64px)
   y=544..800 (256px)
   y=800..832 (32px)
   y=832..1056 (224px)
   y=1056..2048 (992px)
   y=2048..2336 (288px)
   y=2336..2368 (32px)
   y=2368..2592 (224px)
```

Estas bandas son la materia prima de la demo 05 (cuantizaciÃƒÂ³n a EHB/31/15/7).
