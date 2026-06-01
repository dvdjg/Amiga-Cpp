# 101_ehb_tile_scroll_driver

Primer MVP del driver Amiga de scroll EHB con prefetch bidireccional.

La demo usa una superficie EHB de 480x416 pixels, muestra una ventana de 320x256
con `BPLCON1` y punteros de bitplane animados, y ejecuta `TileBlockCopy` reales
por Blitter hacia margenes offscreen horizontales y verticales. Es la continuacion
natural de
`100_virtual_tile_scene_scroll`: conserva la escena atractiva, pero empieza a
materializar el plan retenido en hardware.

La superficie reserva diez columnas y diez filas ocultas de tiles. La intencion
es que una escena no tenga que rellenar toda una franja justo antes de mostrarla:
el scheduler puede ir predibujando tiles sueltos en orden de urgencia, sincronizado
a VBlank y con presupuesto pequeno por frame.

El scroll sigue una ruta didactica: derecha dos tiles, vuelta a la izquierda,
arriba dos tiles, vuelta abajo y, por ultimo, una orbita de cuatro tiles de radio.
La prueba de secuencia debe detectar movimiento real; una captura estatica solo
valida que el estado final es coherente.

La camara mantiene cada pixel varios VBlanks. No es una limitacion del driver:
esta demo prioriza que humanos, capturas automatizadas y el verificador de fine
scroll puedan observar los cruces criticos sin depender de timeouts largos.

Los tiles se generan en 64 variantes simbolicas: cada uno tiene borde, marcador de
variante y un glifo hexadecimal `0..F`. La escena sigue siendo una demo tecnica,
pero ahora las capturas son mas legibles para humanos y para Vision Review: un
fallo puede describirse como "aparece un parche sobre el tile 7" o "se rompe una
fila de glifos", no solo como ruido de textura. No es un formato artistico final:
representa lo que mas adelante llegara desde UAF como tiles planarizados,
metadatos de prioridad y presupuestos de carga.

El prefetch escribe solo franjas lineales garantizadas fuera del viewport visible,
incluyendo el caso de fine scroll. En `runStatus.detail` el nibble bajo publica
flags de prefetch (`0x1` columnas, `0x2` filas).

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\101_ehb_tile_scroll_driver -DebugBuild
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver
.\tools\analyze\analyze-demo.ps1 demos\101_ehb_tile_scroll_driver
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver -SequenceFrames 4 -SequenceIntervalMs 80
.\tools\analyze\analyze-frame-sequence.ps1 out\run\101_ehb_tile_scroll_driver\sequence -ExpectAnimated
.\demos\101_ehb_tile_scroll_driver\analyze-sequence.ps1 -Warp
```

`analyze-sequence.ps1` es la prueba reutilizable fuerte: captura 12 frames cada
120 ms, comprueba que la animacion cambia y ejecuta FrameScope con perfil
`amiga-scroll`, recorte automatico de viewport y `-RequireProfileMatch`. La prueba
falla si la telemetria lateral de camara y el movimiento observado dejan de
coincidir. Tambien ejecuta `tools/analyze/assert-no-inner-black.ps1`: esta demo no
usa negro dentro de sus tiles simbolicos, asi que cualquier mancha negra en el
playfield suele indicar un `COPJMP1` a media pantalla, un puntero de bitplane
reiniciado en zona visible o corrupcion planar.

`analyze-fine-scroll.ps1` es la prueba mas especifica para el bug de columnas:
captura por telemetria `cameraX=94,95,96,97` y `cameraX=112,111,110,109`, y
comprueba que el borde izquierdo no salta y que cada paso equivale a un pixel
lowres en la direccion correcta.

Para verla a ritmo real sin que el runner cierre WinUAE:

```powershell
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver -KeepRunning
```
