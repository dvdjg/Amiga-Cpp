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

Los tiles se generan en 64 variantes para que la escena sea visualmente legible y
para que FrameScope pueda seguir marcas no periodicas entre frames. No es un
formato artistico final: representa lo que mas adelante llegara desde UAF como
tiles planarizados, metadatos de prioridad y presupuestos de carga.

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
coincidir.

Para verla a ritmo real sin que el runner cierre WinUAE:

```powershell
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver -KeepRunning
```
