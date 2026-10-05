# Demo 203: mapa retenido de World sobre XLimited

La demo conecta una capa `World::Tilemap` basada en `TileMap16` con `XlimitedScene` mediante `WorldTileMapView`. La cámara retenida de la capa conduce el scroll X; el driver resuelve las celdas `PackedTileCell` y materializa la banda nueva en el playfield. Una nave naranja animada sobre PF2 sirve de referencia fija para distinguir el movimiento de cámara del desplazamiento del mundo.

```powershell
$env:AMIGA_BIN_PATH = "$env:USERPROFILE/Documents/programa/AI/Amiga/vscode-amiga-debug/bin/win32"
& 'C:\Program Files\Git\bin\bash.exe' ./tools/test-regression.sh --demo demos/techniques/amiga/playfield/203_world_tilemap_xlimited --warp
```

El gate espera `READY`, captura una secuencia y verifica movimiento del mapa y telemetría de cámara. `flicker-baseline.json` exige cero candidatos y cero bloques con cambio residual después de compensar el paneo global; sus regiones excluidas cubren los bordes coarse y el actor fijo PF2, cuyo cambio legítimo se valida por separado. Las pruebas sintéticas conservan sensibilidad a paneo, flicker y corrupción. La regresión actual obtiene cero candidatos/bloques y Ollama no observa anomalías. La ventana de 60 s del consumer `WorldTileMapView` y la del benchmark `TileLayerMap` directo dieron ambas 49,92 fps y 142 102 ciclos/frame: en esta prueba, la abstracción retenida no añade coste medible al bucle principal. El resultado equivale a un frame por VBlank PAL nominal (~49,9–50 Hz), que es el objetivo. La nave de referencia se pinta una sola vez en `init`.
