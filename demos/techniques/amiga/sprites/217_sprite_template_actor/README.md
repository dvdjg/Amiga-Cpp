# 217_sprite_template_actor - plantilla de franjas por el camino de actores

**Objetivo:** un objeto vertical de **cuatro franjas** servido por **un solo canal** de sprite
("chasing the raster") declarado como **plantilla** (`HwSpriteTemplate`) dentro de un
`ActorDesc`: el engine proyecta los segmentos a intents **encadenados**
(`sprite_template_to_intents`), el `SpriteAllocator` reserva el canal para todo el rango y
`compose_sprites` publica **una placement por franja**; `SpriteManager::emit_placements_into`
arma la primera en una línea temprana y **rearma** el canal en cada franja, intercalando la
**paleta por franja** (`SpritePaletteEvent`) en orden de línea. La demo hermana
[053](../053_sprite_multiplex/README.md) hace lo mismo por el **driver** directo
(`emit_template_into`); ésta es el camino de actores.

Técnica y mecanismo: [sprite-techniques-catalog.md](../../../../docs/reference/amiga/techniques/sprite-techniques-catalog.md)
(técnica 3, multiplexado vertical), AHRM 3.ª cap. 4 y
[SPRITE_CHANNEL_WINDOWS.md](../../../../docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md) §8–§9.

## Qué muestra

- Un **tótem** de cuatro tramos de 24 líneas en forma de losange (anchos 4/16/16/4, separados
  por la línea de gap que exige el DMA) que **recorre toda la pantalla** en X (144..398) e Y
  (44..186).
- **Paleta por franja**: `COLOR17` se conmuta en la línea de cada tramo (rojo → naranja →
  verde → cian) con los switches de la plantilla, sin copias de bitmap.
- Un único `ActorDesc` con `visual` (contenido para el fallback a BOB) y `sprite_template`
  (bitmap + 4 segmentos + 4 switches); el juego no conoce canales ni escribe `SPRxPT`.

## Implementación (el contrato que ilustra)

```text
ActorDesc { sprite_template: bitmap + 4 segmentos + 4 switches }   ← el juego declara QUÉ
        │  SpriteScene::emit(plan, ctx)
        ▼
build_sprite_intents: 4 intents ENCADENADOS (chain_id; gap 1 línea)  ← franjas al mismo canal
        │  SpriteAllocator: reserva el canal para el rango completo de la cadena
        ▼
compose_sprites: 4 placements (mismo canal, vstart 48/73/98/123) + palette_events
        │  SpriteManager::emit_placements_into(placements, arm_line, palette)
        ▼
Copper: WAIT 32 + armado franja 0 · WAIT 48 COLOR17 · WAIT 73 rearm+COLOR17 · WAIT 98 · WAIT 123
```

- El **rearme por franja** ya lo hacía el emisor de placements (HOST-428); la novedad es la
  **cadena**: `SpriteIntent::chain_id/chain_index/chain_span` + reserva del canal en el
  `SpriteAllocator` (HOST-003) y la publicación por franja en `compose_sprites` (HOST-072).
- La **paleta por franja** viaja como `SpritePaletteEvent` (0-based, como
  `HwSpritePaletteSwitch`) y no como `CopperIntent::PaletteLine`, cuyo contrato indexa los
  colores de forma absoluta (`colors[first+i]`); el emisor la intercala con los rearmes
  (HOST-428) y el compositor la ancla al top del actor en la escala del sprite.
- Plantillas `attach` (par de 15 colores en cadena) no están soportadas por este camino:
  rechazo controlado (`result.ok == false`).
- **Clasificación de reparto**: el tótem se declara **fijo** (`assign_rank = 1`) con
  **canal preferido 0**; el allocator lo asigna antes que los libres y conserva el canal. El
  canal importa por el par de color: los switches de la plantilla escriben `COLOR17` (registros
  del **par 0/1**), así que un canal del par 2/3+ pintaría el sprite con `COLOR21/25/29` y los
  switches no se verían (se comprobó en emulador). Fijar el par es parte del contrato de una
  plantilla con palette splitting. La telemetría publica el canal del primer placement en
  `detail` (bits 16-23).

## Estado: validada

- **HOST-003** (`test_vertical_chain`, `test_preferred_channel`, `test_assign_rank`,
  `test_trajectory_group`): reserva del canal para todo el rango de la cadena, huecos no
  cedidos, canal preferido, prioridad de asignación y grupos con trayectoria (corrida
  contigua o entera a BOB).
- **HOST-072** (`test_compose_template_chain`, `test_compose_group_and_rank`): dos placements
  en el mismo canal con gap ≥1, DATA de cada segmento, evento de paleta, degradado único a
  BOB; fijo con canal preferido y grupo con trayectoria desde `ActorDesc` (grupo + *attached*
  se rechaza).
- **HOST-428**: rearme por franja y paleta intercalada en orden de línea (COLOR17 antes del
  rearme de la franja).
- **Rendimiento**: `measure-fps` da **49,87 fps** emulados y 142 244 ciclos/frame
  (`fieldsPerFrame` 1,003); `detail = 0x000400` (canal 0, 4 franjas, 0 degradados). **Perfil por
  frame** (`.amigaprofile`): `profileCycles` idéntico en los 8 frames (sin frames de 2 VBlanks ni
  picos de procesamiento), DMA estable (±120 ciclos) e idle ≈57 %.
- **Secuencia** (8 frames, 150 ms): un único objeto presente en todos los frames con
  **3840 px de paleta de sprite** (4×24 rojo + 16×24 naranja + 16×24 verde + 4×24 cian), anchos
  y colores exactos, posición cambiando **por toda la pantalla** (diagonal); `frame-diff`
  confina los cambios a la caja del objeto (SSIM ≈0,98; fondo estable).
- **Visión local (Ollama, qwen3-vl)**: por frame, las cuatro franjas completas con los colores
  correctos (rojo, naranja, verde, cian de arriba abajo), sin cortes, parpadeos, huecos ni
  basura, y posición distinta en cada frame; veredicto: «no hay anomalías».

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/217_sprite_template_actor --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/217_sprite_template_actor
```

## Referencias

- AHRM 3.ª cap. 4 (posición/control de sprite, `VSTOP`, reuso vertical).
- `docs/engine/architecture/OBJECT_SYSTEM.md` §2 (franjas de sprite y rearme).
- `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md` §8–§9.
- `tests/host/graphics/003_sprite_allocator`, `tests/host/scene/072_actor`,
  `tests/host/graphics/428_sprite_object_arm`.
