# Referencia — `field/`

El motor de **playfields y scroll** (`eng::playfield`). Un playfield es una zona de bitplanes con un **mapeo** de coordenadas lógicas a físicas y una política de scroll. Este módulo reúne la base de playfield, los motores de scroll (por tiles, X-Limited/corkscrew, tiras), la geometría runtime y el seam de dibujo.

```
   Playfield (base: framebuffer + mapeo lógico→físico + primitivas CPU)   ← NO dibuja
     ├─ CanvasPlayfield      (interleaved: HUD, fondo estático, capa de actores)
     └─ ContiguousPlayfield  (planos uno tras otro: escenas EHB/HAM)
            ▲ m_target (Ref, no propietario)
   Surface (origen + tamaño + clip)   ← ÚNICO contexto de dibujo
            ▲
   DrawTarget (Surface + Rasterizer + FramePlan + clip)
```

## Grupos

| Grupo | Cabeceras |
|---|---|
| **Playfield** | `playfield_base.hpp` (`Playfield`, `PlayfieldHardwareView`, `RasterPolicy`, operación lógica por escritura), `canvas_playfield.hpp` (interleaved), `contiguous_playfield.hpp`, `flat_playfield.hpp`, `mirror_playfield.hpp`, `double_buffer_playfield.hpp` (**no posee memoria**: `bind` lo liga a dos bitmaps del display), `soft_dpf.hpp`/`plane_view.hpp`. |
| **Dibujo** | `surface.hpp` (origen + tamaño + clip: único contexto de dibujo), `draw_target.hpp` (`Surface` + `Rasterizer` + `FramePlan` + clip), `raster.hpp` (seam: la `Surface` pide operaciones y un `Rasterizer` las resuelve), `cpu_primitives.hpp` (relleno/líneas portables). |
| **Scroll** | `scroll_layer.hpp` (interfaz conducida por `App`), `scroll_plan.hpp` (vocabulario declarativo), `scroll_profile.hpp` (perfil estático), `scroll_route.hpp` (ruta por fases), `scroll_variant.hpp` (variantes nombradas), `scroll_engine.hpp` (`ScrollEngine`/`BigBufferScroll`), `scroll_ladder.hpp` (registro de motores). |
| **Corkscrew** | `xlimited_base.hpp` (policies + `XlimitedConfigT`), `xlimited_playfield.hpp` (`XLimitedPlayfield`, 8-way), `xlimited_scene.hpp`, `xlimited_composer.hpp` (`XlimitedDisplayComposer`/`Dual`), `xlimited_mapping.hpp` (mapeo/geometría), `xlimited_robocod.hpp` (fondo RoboCod), `xlimited_scroll_layer.hpp` (adaptación a `ScrollLayer`). |
| **Tiras** | `strip_scroller.hpp` (anillo de Copper + tira entrante), `strip_composer.hpp` (copperlist una vez + parche por frame), `strip_layer.hpp` (`StripScrollLayer`, la capa de fachada), `runtime_scroll_geometry.hpp` (geometría del anillo en runtime). |
| **Tiles** | `tilemap_view.hpp` (banco + mapa + paleta), `tile_map.hpp`/`tile_source.hpp`, `chunk_cache.hpp`/`streaming_map.hpp`. |
| **Display** | `field_display.hpp` (registros de geometría/modo/paleta de un campo), `amiga_display_mapper.hpp`, `world_layer.hpp`. |

## Notas

- `field` es un **alias deprecado** de `playfield` (`nullable`): el repo usa `playfield::`, el nombre viejo sigue compilando para código no migrado (`ROADMAP_GAME_API.md` §8).
- `tile_demo.hpp` y varios `scene/*` están marcados **NO VERIFICADOS** (sin consumidor con gate visual): existen como mecanismo, aún no como contrato probado.

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
