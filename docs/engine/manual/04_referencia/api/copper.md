# Referencia — `copper` (`eng/api/copper.hpp`)

`eng::Copper` es la **fachada de Copper por intención**: construye la copperlist con `wait_line` +
acción, **sin** nombrar registros ni `WAIT`/`MOVE`. Envuelve el `copper::Scheduler` del `copper::Plan`
de la escena. El ciclo de vida (doble buffer, `begin`/`commit`, instalación) lo lleva el `Scene`/`Device`.

## Métodos

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| ctor | `Copper(copper::Scheduler&)` | el scheduler de la escena | — (se obtiene de `device().copper_builder()` o `scene.scheduler()`). |
| `wait_line` | `void wait_line(u16 line)` | línea raster 0..255 | — (los MOVEs siguientes esperan a esa línea). |
| `set_color` | `void set_color(u8 index, u16 color)` | índice 0..31, RGB444 `0x0RGB` | — (`COLOR<index>`). |
| `set_palette` | `void set_palette(PaletteWords, u8 first = 0, u8 count = 32)` | paleta + rango | — (tramo de paleta). |
| `set_scroll` | `void set_scroll(u16 bplcon1)` | delay 0..15 por nibble | — (`BPLCON1`, fine scroll del PF1). |
| `words_used` | `u16 words_used() const` | — | palabras de Copper escritas (presupuesto consumido). |

```cpp
eng::Copper c { scene.scheduler() };
c.set_palette(pal);
c.wait_line(120);
c.set_color(1, 0x0f00);   // un split de paleta a media pantalla
```

> La escena ligada y su `copper::Plan`/`Scheduler` se obtienen de `app.device()`:
> `device().copper()`, `device().copper_scheduler()`, `device().copper_builder()`. Ver
> [`device.md`](device.md).

> Fuente: `engine/include/eng/api/copper.hpp`. Contrato: `docs/engine/architecture/DISPLAY_COMPOSITION.md`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
