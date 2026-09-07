# Matriz: subsistema del engine ↔ batería de pruebas

Objetivo: **cada bloque reusable** del motor tenga al menos un caso en `tests/amiga-battery/` con evidencia real y un estado honesto.

**Plan unificado** (taxonomía U/F/T/I, trazas obligatorias, audio→vídeo, fases e IDs `E*`): [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md).

Al crear casos nuevos del motor:

- asignar **tier** en el `README.md` del caso (`U`, `F`, `T` o `I`);
- enlazar esta matriz y el roadmap unificado;
- no mantener como `PENDIENTE` un caso que ya existe en `tests/amiga-battery/`.

| Subsistema / API | Tier | Caso de batería | Estado | Evidencia esperada |
|------------------|------|-----------------|--------|--------------------|
| Blitter fill | F | B01 | HECHO | Rectángulo por blitter, bitplane limpio |
| Blitter copy | F | B02 | HECHO | Copia A→D |
| Blitter línea | F | B03 | HECHO | Vectores |
| Polígono / scan | F | B04 | HECHO | Fill estable |
| Minterm / máscara | F | B05 | HECHO | BOB con máscara |
| Blitter shift | F | B06 | HECHO | Desplazamiento |
| Copper / paleta por bandas | F | C01 | HECHO | Degradado o barras |
| Lores 32c / tile base | F | T02 | HECHO | Modo color profundo |
| Sprites `engine_sprite_*` | F | S01 | HECHO | Sprite hardware visible con COLOR16/17 y evidencia viva |
| CPU sprites `engine_cpu_sprite_*` | F | CS01 / CS02 / CS03 / CS04 / CS06 | HECHO | `CS01` valida `engine_cpu_sprite_blit_1bpl`, `CS02` valida `engine_cpu_sprite_blit_4bpl_masked`, `CS03` valida retained minima world-space con scroll, `CS04` valida overlay fijo en pantalla y `CS06` valida overlay fijo 5bpl masked con scroll ciclico y secuencia animada; todos pasan en vivo con `assert_failures=0` |
| Attached sprites `engine_sprite_attach_pair` | F | S02 | PARCIAL | Parejas adjuntas multicolor, transparencia y trayectorias cerradas con cierre visual aún conservador |
| Copper doble buffer `engine_copper_double_*` | F | C02 *(plan)* | PENDIENTE | Alternancia documentada entre dos listas |
| Joystick `engine_joy_*` | F | J01 *(plan)* | PENDIENTE | Lectura JOYxDAT + fire en overlay o traza |
| Viewport / scroll `engine_view_*` | F | T07 / V04 / ST01 / ST02 / ST03 / ST04 | PARCIAL | Fine scroll + MOD visible en caso engine-centric dedicado, mas familia externa de scroll XY con wrap/split en progreso. `ST02` ya existe como baseline vertical; `ST04` sigue abierto pero sin validacion visual suficiente como referencia retained reusable; `ST03` sigue pendiente. |
| Tilemap `engine_tilemap_*` | U/F | T03 *(plan)* | PENDIENTE | Mapa pequeño + atlas, una capa |
| Custom peek/poke `engine_custom_*` | F | H01 | HECHO | Escritura de paleta con `engine_custom_write_uword` y lectura de registros legibles (`DMACONR/VPOSR/VHPOSR`) con huella visual estable |
| Fuente `engine_font_*` | U/F | EU01 / F01 | HECHO | `F01_font_digits` valida en vivo dos filas de dígitos con `stage_id=0xE405` y `assert_failures=0` |
| DOS `engine_dos_*` | F | M00 / M04 / M05 / D01 *(plan)* | PARCIAL | Slurp o listado con ADF DOS |
| Audio Paula / tracker | F/T | A01 *(plan)* | PENDIENTE | Silencio tras mute o un tono breve documentado con huella visual |
| Input flancos / suite | F | I01 | HECHO | `engine_input_edges` + automatización de ratón y test geométrico de puntero con `stage_id=0xE205` y `assert_failures=0` |
| Fixmath / reloj | U | EU01 / M01 *(plan)* | PARCIAL | `EU01_clock_counter` ya corre en vivo por ADF (`stage_id=0xE005`, `assert_failures=0`); falta robustecer evidencia visual secuencial y crear un caso dedicado de fixmath |
| Mem trace / DIAG | F | integración / EU03-EU04 *(plan)* | PARCIAL | Build con `ENGINE_MEM_TRACE` / `ENGINE_DIAG` y huella visual o trazas dedicadas |

## Reglas

1. **Promoción**: si un caso madura, su lógica debe consumir API de `engine/` y no repetir setup hardware innecesario.
2. **Evidencia**: cada caso engine-centric debe dejar `evidence/README.md` y artefactos de validación reales cuando WinUAE esté disponible.
3. **Prioridad**: cubrir primero huecos pequeños y reutilizables del engine antes de abrir técnicas compuestas nuevas.
4. **Estado real antes que plan**: si un caso ya existe con código y evidencia, esta matriz debe reflejarlo como `PARCIAL` o `HECHO`, nunca como si no existiera.
5. **Cierre visual fuerte**: un caso gráfico no pasa a referencia reusable si la imagen o la animación no son claramente coherentes con el objetivo, aunque `assert_failures=0`.
6. **Secuencia honesta**: si la métrica visual de secuencia no detecta cambio real, no se puede declarar movimiento correcto apoyándose solo en runtime markers.
7. **Continuidad mínima por caso**: casos de scroll/cámara/redraw retained pueden declarar umbrales cuantitativos mínimos de continuidad e intensidad visual; si no se alcanzan, el caso sigue abierto aunque exista algo de delta de píxel.

## Documentos padre

- [Spec batería](amiga-test-battery-spec.md)
- [Roadmap engine](engine-roadmap.md)
- [Subsistemas](engine-subsystems.md)
- [Auditoría inicial](engine-test-audit-2026-04-06.md)

