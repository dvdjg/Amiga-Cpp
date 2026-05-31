# 101_ehb_tile_scroll_driver

Primer MVP del driver Amiga de scroll horizontal EHB.

La demo usa una superficie EHB de 384x256 pixels, muestra una ventana de 320x256
con `BPLCON1` animado, y ejecuta `TileBlockCopy` reales por Blitter hacia el
margen offscreen derecho. Es la continuacion natural de
`100_virtual_tile_scene_scroll`: conserva la escena atractiva, pero empieza a
materializar el plan retenido en hardware.

La anchura extra son cuatro columnas de tiles. La intencion es que una escena no
tenga que rellenar toda una columna justo antes de mostrarla: el scheduler puede
ir predibujando tiles sueltos en orden de urgencia, sincronizado a VBlank.

El scroll oscila dentro del margen oculto. La prueba de secuencia debe detectar
movimiento real; una captura estatica solo valida que el estado final es coherente.

La demo tambien usa `EhbHorizontalRingPrefetch`: una camara logica avanza por el
mapa y el driver recicla slots de columna. El analizador exige que al menos una
columna haya sido reciclada y preparada por Blitter.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\101_ehb_tile_scroll_driver -DebugBuild
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver
.\tools\analyze\analyze-demo.ps1 demos\101_ehb_tile_scroll_driver
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver -SequenceFrames 4 -SequenceIntervalMs 80
.\tools\analyze\analyze-frame-sequence.ps1 out\run\101_ehb_tile_scroll_driver\sequence -ExpectAnimated
```

Para verla a ritmo real sin que el runner cierre WinUAE:

```powershell
.\tools\run\run-demo.ps1 demos\101_ehb_tile_scroll_driver -KeepRunning
```
