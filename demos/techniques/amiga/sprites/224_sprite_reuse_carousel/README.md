# 224_sprite_reuse_carousel — carrusel de reuso vertical (S1.1)

Sonda de estrés S1.1 para [stress-sprites-bobs-roadmap.md](../../../../../docs/guides/roadmap/STRESS_SPRITES_BOBS.md). Ocho canales Sprite HW sirven **40 placements** repartidos en cinco bandas raster: dos pares *attached* (canales 0/1 y 2/3, 7 colores + transparencia) y cuatro canales libres (4–7, 3 colores + transparencia). Cada canal se reutiliza cinco veces por frame y muestra un objeto distinto en cada banda. Cada placement tiene su propia X animada por todo el ancho (16..301 px) y su propia oscilación vertical dentro de la banda.

## Diseño: estructura estática + parcheo

Recomponer y reemitir la copperlist cada frame con decenas de actores cuesta cientos de miles de ciclos (medido con `SpriteScene`: ~670k para 30 actores). Un juego real deja la **estructura en Chip** y parchea solo las palabras de dato. Aquí:

- La lista se construye **una vez** en `init`: display, paleta, `DMACON` con `SPREN`, y por cada banda un `WAIT` + `POS/CTL/PTH/PTL` de los ocho canales.
- Por frame se parchean **160 words** (POS/CTL de 40 placements) desde `update`, justo después del VBlank, antes de que el haz alcance la primera banda.
- Las cinco rotaciones de arte se **preconstruyen en init** (una copperlist por rotacion, con sus PT ya cocidos): rotar es solo un swap de COP1LC, sin tocar la lista en curso. El par *attached* se cocina una vez por contenido.
- El DMA de sprites ejecuta el resto: CPU casi libre.

La demo usa `app.device()` para instalar su lista estática (`takeover_copper(ChipView)`) porque la fachada `SpriteScene` todavía no expone un camino incremental equivalente; el contrato y el hueco están documentados en [SPRITE_MATERIALIZATION_CONTRACT.md](../../../../../docs/engine/architecture/SPRITE_MATERIALIZATION_CONTRACT.md).

## Perfiles de planos

| Perfil | Planos | Fondo | Estado |
|---|---:|---|---|
| A | 3 | COLOR00..07 | medido |
| B | 4 | COLOR00..15 | pendiente de re-medida |
| C | 5 | COLOR00..15 | medido |
| D | 6 (EHB) | COLOR00..15 | pendiente de re-medida |

Selección con `EXTRA_DEFINES` (mecanismo admitido por `build-demo.sh`):

```bash
EXTRA_DEFINES="-DK224_PLANES=3" bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/224_sprite_reuse_carousel --release --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/224_sprite_reuse_carousel --config A500_k224_planes3_release
node tools/debug/measure-fps.mjs 224_sprite_reuse_carousel A500_k224_planes3_release --json
```

El fondo usa solo COLOR00..07/15 para no pisar `COLOR16..31`, que pertenecen a los sprites.

## Búsqueda previa

Se reutilizan `copper::Scheduler`/`MoveSlot`/`patch_data`, `encode_sprite_pos_ctl`, `cook_attached_pair`, `attached_pair_structure_words`, `kSpriteScreenXOrigin`, `kSpriteOffPos/Ctl` y el bootstrap `run_demo`. El planner de actores (`SpriteScene` + `SpriteAllocator`) se midió primero y su coste por frame motivó el camino estático; su telemetría de canales/reusos se añadió en esta pasada (HOST-391) para que la batería pueda verificar el reparto sin tocar hardware.

## Estado

**Parcialmente verificada.** 49,92 fps / 1,002 campos / 142.102 ciclos por frame en A y C (build previo al ajuste de rango X, mismo camino de emisión). Tras el ajuste de X a todo el ancho, la animación se comprobó por secuencia, pero la re-medida de fps quedó bloqueada por el arnés en esta sesión (ver `VALIDATION.md`). B y D quedan SIN VERIFICAR.
