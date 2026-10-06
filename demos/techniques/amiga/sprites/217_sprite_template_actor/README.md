# 217_sprite_template_actor - plantilla de franjas por el camino de actores

**Objetivo:** un objeto vertical de **tres franjas** servido por **un solo canal** de sprite
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

- Un **tótem** de tres tramos de 24 líneas (barras de 16, 10 y 4 px, separadas por la línea de
  gap que exige el DMA) que se desplaza en horizontal dentro de la ventana de display.
- **Paleta por franja**: `COLOR17` se conmuta en la línea de cada tramo (rojo → verde → cian)
  con los switches de la plantilla, sin copias de bitmap.
- Un único `ActorDesc` con `visual` (contenido para el fallback a BOB) y `sprite_template`
  (bitmap + segmentos + switches); el juego no conoce canales ni escribe `SPRxPT`.

## Implementación (el contrato que ilustra)

```text
ActorDesc { sprite_template: bitmap + 3 segmentos + 3 switches }   ← el juego declara QUÉ
        │  SpriteScene::emit(plan, ctx)
        ▼
build_sprite_intents: 3 intents ENCADENADOS (chain_id; gap 1 línea)  ← franjas al mismo canal
        │  SpriteAllocator: reserva el canal para el rango completo de la cadena
        ▼
compose_sprites: 3 placements (mismo canal, vstart 48/73/98) + palette_events (líneas 48/73/98)
        │  SpriteManager::emit_placements_into(placements, arm_line, palette)
        ▼
Copper: WAIT 32 + armado franja 0 · WAIT 48 COLOR17 · WAIT 73 rearm+COLOR17 · WAIT 98 rearm+COLOR17
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

## Estado: validada

- **HOST-003** (`test_vertical_chain`): reserva del canal para todo el rango, hueco entre
  franjas no cedido, cadenas intercaladas y degradado entero si no cabe.
- **HOST-072** (`test_compose_template_chain`): dos placements en el mismo canal con gap ≥1,
  DATA de cada segmento y evento de paleta; degradado único a BOB sin canal.
- **HOST-428**: rearme por franja y paleta intercalada en orden de línea (COLOR17 antes del
  rearme de la franja).
- **Rendimiento**: `measure-fps` da **49,87 fps** emulados y 142 244 ciclos/frame
  (`fieldsPerFrame` 1,003).
- **Secuencia** (8 frames, 150 ms): un único objeto presente en todos los frames con
  **2880 px de paleta de sprite** (16×24 rojo + 10×24 verde + 4×24 cian), anchos y colores
  exactos, `x` monótona (rebote); `frame-diff` confina los cambios a la caja del objeto
  (SSIM ≈0,983; fondo estable).
- **Visión local (Ollama)**: barras completas, colores correctos, sin cortes ni basura; la
  "duplicación" señalada en un frame la refuta el gate determinista (un solo clúster por
  frame con idéntico recuento de píxeles).

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
