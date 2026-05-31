# 100_virtual_tile_scene_scroll

MVP visual de escenario virtual con tiles.

Esta demo es el primer ejemplo pensado como "juego pequeño" en vez de prueba
seca. Usa una escena virtual mayor que la pantalla, una camara 2D y una capa de
tiles retenida. El render actual es didactico y redibuja el viewport para que el
MVP sea facil de inspeccionar; el siguiente paso sera sustituirlo por un
`TileScrollDriver` Amiga con margenes ocultos, Blitter, punteros de bitplane y
scroll fino por Copper.

La escena usa EHB con cambios completos de paleta por zonas Copper para que el
mismo vocabulario de tiles produzca cielo, jungla y subsuelo con lecturas
cromaticas distintas.

Tambien valida una politica de scroll importante: los tiles offscreen no se
preparan todos de golpe. La demo encola la columna derecha que entrara en pantalla
y acepta solo cuatro updates logicos en el frame, dejando al futuro driver Amiga la
traduccion a Blitter real.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\100_virtual_tile_scene_scroll -DebugBuild
.\tools\run\run-demo.ps1 demos\100_virtual_tile_scene_scroll
.\tools\analyze\analyze-demo.ps1 demos\100_virtual_tile_scene_scroll
```
