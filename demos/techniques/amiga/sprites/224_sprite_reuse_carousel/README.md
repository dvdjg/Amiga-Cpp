# 224_sprite_reuse_carousel — carrusel de reuso vertical (S1.1)

Sonda de estrés S1.1 para [stress-sprites-bobs-roadmap.md](../../../../../docs/guides/roadmap/STRESS_SPRITES_BOBS.md). Ocho canales Sprite HW sirven **40 placements** repartidos en cinco bandas raster: dos pares *attached* (canales 0/1 y 2/3, 7 colores + transparencia) y cuatro canales libres (4–7, 3 colores + transparencia). Cada canal se reutiliza cinco veces por frame y muestra un objeto distinto en cada banda. Cada placement tiene su propia X animada por todo el ancho (16..301 px) y su propia oscilación vertical dentro de la banda.

## Diseño: estructura estática + parcheo

Recomponer y reemitir la copperlist cada frame con decenas de actores cuesta cientos de miles de ciclos (medido con `SpriteScene`: ~670k para 30 actores). Un juego real deja la **estructura en Chip** y parchea solo las palabras de dato. Aquí:

- La lista se construye **una vez** en `init`: display, paleta, `DMACON` con `SPREN`, y por cada banda un `WAIT` + `POS/CTL/PTH/PTL` de los ocho canales.
- Por frame se parchean **160 words** (POS/CTL de 40 placements) desde `render`, justo después del VBlank y antes de que el haz alcance la primera banda.
- Las cinco rotaciones de arte se **preconstruyen en init** (una copperlist por rotacion, con sus PT ya cocidos): rotar es solo un swap de COP1LC, sin tocar la lista en curso. El par *attached* se cocina una vez por contenido.
- El DMA de sprites ejecuta el resto: CPU casi libre.

La demo usa `app.device()` para instalar su lista estática (`takeover_copper(ChipView)`) porque la fachada `SpriteScene` todavía no expone un camino incremental equivalente; el contrato y el hueco están documentados en [SPRITE_MATERIALIZATION_CONTRACT.md](../../../../../docs/engine/architecture/SPRITE_MATERIALIZATION_CONTRACT.md).

## Perfiles de planos

| Perfil | Planos | Fondo | Estado |
|---|---|---:|---|---|
| A | 3 | COLOR00..07 | verificado (20 s, 49,95 fps; flicker OK) |
| B | 4 | COLOR00..15 | verificado (20 s, 49,97 fps) |
| C | 5 | COLOR00..15 | verificado (20 s, 49,95 fps) |
| D | 6 (EHB) | COLOR00..15 | verificado (20 s, 49,96 fps) |

Selección con `EXTRA_DEFINES` (mecanismo admitido por `build-demo.sh`):

```bash
EXTRA_DEFINES="-DK224_PLANES=3" bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/224_sprite_reuse_carousel --release --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/224_sprite_reuse_carousel --config A500_k224_planes3_release
node tools/debug/measure-fps.mjs 224_sprite_reuse_carousel A500_k224_planes3_release --json
```

El fondo usa solo COLOR00..07/15 para no pisar `COLOR16..31`, que pertenecen a los sprites.

## Búsqueda previa

Se reutilizan `copper::Scheduler`/`MoveSlot`/`patch_data`, `encode_sprite_pos_ctl`, `cook_attached_pair`, `attached_pair_structure_words`, `kSpriteScreenXOrigin`, `kSpriteOffPos/Ctl` y el bootstrap `run_demo`. El planner de actores (`SpriteScene` + `SpriteAllocator`) se midió primero y su coste por frame motivó el camino estático; su telemetría de canales/reusos se añadió en esta pasada (HOST-391) para que la batería pueda verificar el reparto sin tocar hardware.

La emisión de sprites HW se contrastó con `sprite_chain.hpp`/`arm_dma_chain`, `SpriteManager` (armado/rearme), `SpriteAllocator` (reparto/gap) y `actor_to_sprite_intent` (proyección de actor); la lista estática es una optimización local sobre ese contrato, no un camino paralelo.

## Estado

**Verificada en A (3 planos), B (4), C (5) y D (6 EHB)**: 49,95–49,97 fps, 1 campo/frame, 20 s estables; secuencias revisadas y visión canónica en A; flicker formal OK en A. La demo usa `VblankWait::Active` (IRQ de VBlank + espera a VPOS 311) por defecto: está validado en **3/4/5/6 planos** con rotación. El parón ~198-200 era el commit de COP1LC en la ventana 0-2 (el `Active` despertaba en VPOS 0; `wait_vblank_run` ahora espera VPOS 311) y no la IRQ/SO. `K224_WAIT_ACTIVE=0` deja el fallback de sondeo `Polling`. El contador de frames y los FPS se muestran en el overlay de depuración del host; la franja inferior reserva nombre y versión. Detalle y evidencia en `VALIDATION.md`.
