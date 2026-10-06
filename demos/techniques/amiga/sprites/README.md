# Técnicas: Amiga · sprites

Demos de **sprites** (técnicas de hardware Amiga). Índice: [../../../README.md](../../../README.md).
Estructura y numeración: [../../../../docs/STRUCTURE.md](../../../../docs/STRUCTURE.md) §4 y
[../../../../docs/ai-dev-environment/NUMBERING.md](../../../../docs/ai-dev-environment/NUMBERING.md).

## Catálogo

| Demo |
|---|
| [053_sprite_multiplex](053_sprite_multiplex/README.md) |
| [054_sprite_allocator](054_sprite_allocator/README.md) |
| [206_sprite_collision](206_sprite_collision/README.md) |
| [207_sprite_layer](207_sprite_layer/README.md) |
| [208_risky_woods](208_risky_woods/README.md) |
| [211_risky_woods_layer](211_risky_woods_layer/README.md) |
| [212_free_scroll_layer](212_free_scroll_layer/README.md) — **NO VERIFICADA (rota)** |
| [213_spr_layer](213_spr_layer/README.md) |
| [214_attached_object](214_attached_object/README.md) — objeto de 15 colores (*attached*) |
| [215_jim_power](215_jim_power/README.md) — **NO VERIFICADA**: fondo por DATA por línea (pacing abierto) |
| [216_attached_actors](216_attached_actors/README.md) — pares *attached* end-to-end por el camino de actores |
| [217_sprite_template_actor](217_sprite_template_actor/README.md) — plantilla de franjas (rearme + paleta por franja) por el camino de actores |

Técnicas del [catálogo](../../../../docs/reference/amiga/techniques/sprite-techniques-catalog.md)
**aún sin demo** (candidatas): **multiplexado vertical**
(4 canales → N balas), **combinación con BOBs**, **bending** (onda por línea con tabla de seno),
**palette splitting** (misma DATA, paletas por franja), **HUD de sprites** y prioridad dinámica
por franjas. Lista viva y priorizada:
[`ROADMAP_UNIFICADO.md` §Sprites hardware](../../../../docs/guides/roadmap/ROADMAP_UNIFICADO.md).

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/<NNN>_<tema> --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/<NNN>_<tema>
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/sprites/<NNN>_<tema>
```

El número `NNN` es único **dentro de este ámbito** (`demos/techniques/amiga/sprites`).
