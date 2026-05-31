# 052_tile_staging_blits

MVP de blits de tiles para scroll.

Los BOBs y blobs no son el unico uso importante del Blitter. En un engine con
scroll, una de las rutas criticas es preparar columnas o filas de tiles en zonas
del playfield que todavia no son visibles, para que la camara pueda avanzar sin
redibujar toda la pantalla por CPU.

Esta demo valida esa idea en dos pasos:

- dieciseis tiles 16x16 se copian por Blitter a un buffer de staging no visible;
- el bloque 64x64 resultante se copia por Blitter al playfield EHB visible.

`FramePlan` distingue esos trabajos como `TileBlockCopy`, aunque el backend use el
mismo minterm de copia que `CopyRect`. La categoria separada nos permite medir y
presupuestar actualizaciones de tilemap sin mezclarlas con sprites, restores o BOBs.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\052_tile_staging_blits -DebugBuild
.\tools\run\run-demo.ps1 demos\052_tile_staging_blits
.\tools\analyze\analyze-demo.ps1 demos\052_tile_staging_blits
```
