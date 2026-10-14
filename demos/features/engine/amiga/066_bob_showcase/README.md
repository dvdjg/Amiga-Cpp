# 066: showcase de BOBs + Sprites HW

Vitrina técnica de **todos los usos de BOB** del motor (la matriz de HOST-439 en vivo) más
sprites hardware. Referencia por píxeles exactos:
`tests/host/graphics/439_bob_render_matrix`.

## Zonas, técnicas y sus **contratos**

| Zona | Técnica | API | Contrato |
|------|---------|-----|----------|
| A | orbes rebotando: Or ($FC) + borrado por caja, **dos pasadas** (borrar-todo → dibujar-todo) | `bob_draw(Or)` + `bob_erase_box` | con borrado intercalado, la caja de un objeto muerde los píxeles ya dibujados de otro cuando se cruzan; dos pasadas lo evitan |
| B | deslizadores con padding **FastCopy**: copia opaca ($F0) que tapa la posición previa sin borrado | `bob_draw(Opaque)` | **no solapar**: el padding se escribe opaco y borraría otro objeto — cada deslizador tiene su carril/corredor sin cruce |
| C | caminante **cookie-cut planar** ($CA con máscara en plano aparte) sobre damero | `bob_draw(CookieCut)` + máscara | la máscara preserva el fondo; el borrado previo limpia solo los planos 0-1 |
| D | patrullero cookie-cut + **save-under** del cofre | `bob_save_box`/`bob_restore_box` | la técnica que **sí** soporta solapes: restaura lo que había debajo byte a byte |
| E | barras que borran su caja y se redibujan | `bob_erase_box` | como A (un objeto por zona ⇒ sin solape) |
| F | joya multicolor de 4 planos con padding | `bob_draw(Opaque)` | como B (no solapa) |
| G | **dos naves HW** (cada una par adjunto DAT/DATB = 15 colores) armadas por Copper | `encode_sprite_pos_ctl` + POS/CTL/PTR por canal | el armado va tras las bandas y **antes del VSTART** |

Fondo: degradado de `COLOR00` por bandas de Copper (lista **estática**, se emite una vez y se
parchean solo POS/CTL del armado de sprites por frame) + arte estático (marcos, dameros,
iconos, estrellas) en los planos 2-3 dibujado una vez.

> La **copperlist** la usa la demo para lo que el Copper hace mejor: el display (BPLCON0/
**punteros/DIW/DDF), la paleta, el degradado por bandas (visible en pantalla) y el armado de
> los sprites HW en una línea concreta. Al ser todo estático salvo la posición de las naves,
> se emite una vez y se parchean 8 palabras por frame (véase `VALIDATION.md` §Perfil).

## Estado (2026-10)

- **37.5 fps async** (`execute_frame_plan_async` + `set_blit_service_enabled(false)`), 19
  jobs/frame; techo de frame = CPU de la demo + cadena de blits (perfil por secciones en
  `VALIDATION.md`). Objetivo 50 fps: palancas medidas (target interleaved, menos jobs).
- Render verificado con **visión** (secuencia) + **diff objetivo por zonas**: las 7 zonas y
  los sprites HW, sin artefactos. Informe y medidas: [VALIDATION.md](VALIDATION.md).
- **Coherencia de setup** activa: regiones sin solape, `BPLCON0` vs planos, orden de WAITs y
  combinación/formato de cada bob (`check_bob_plan(*b, m_target)`) — fallan con `0x7C0xx`.
- Franja inferior: nombre + versión + F/U/T/L/S (norma de versionado).

## Ejecución

```bash
bash tools/build/build-demo.sh demos/features/engine/amiga/066_bob_showcase --debug
bash tools/run/run-demo.sh demos/features/engine/amiga/066_bob_showcase --keep-running
node tools/debug/measure-fps.mjs engine_amiga_066_bob_showcase A500_debug --json
```
