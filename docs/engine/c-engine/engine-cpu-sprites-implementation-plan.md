# Plan de implementacion: sprites CPU y capa retained

Plan ejecutable para convertir la propuesta de [engine-cpu-sprites-api-proposal.md](engine-cpu-sprites-api-proposal.md) en trabajo real del repo sin mezclar demasiados frentes a la vez.

## Objetivo

Introducir un subsistema de sprites CPU que:

- mantenga primitivas low-level parametricas;
- permita especializacion por bitplanes y mascara;
- tenga una capa retained minima para anclaje mundo/pantalla y persistencia por frame;
- se valide con bateria antes de promocion completa.

## Principios

1. No implementar la capa retained completa antes de tener una primitive low-level fiable.
2. No mezclar world-space, HUD fijo y restore-under en la misma primera fase.
3. No abrir C++ antes de medir una primera familia especializada en C.
4. No cerrar ningun paso sin evidencia viva.

## Fase 1: primitive low-level 1bpl sin mascara

### Objetivo

Primera ruta minima:

- `engine_cpu_sprite_blit_1bpl`
- sin mascara
- clipping basico
- sin retained

### Entregables

- cabecera nueva del engine
- implementacion C
- caso de bateria `CS01_cpu_sprite_1bpl`

### Evidencia de cierre

- build OK
- sprite visible en posicion esperada
- clipping correcto
- `assert_failures == 0`

## Fase 2: primitive low-level 4bpl masked

### Objetivo

Segunda ruta low-level:

- `engine_cpu_sprite_blit_4bpl`
- soporte de mascara
- clipping
- reutilizacion prioritaria de primitivas ya presentes en `engine_sprite_*`

### Entregables

- variante 4bpl o wrapper limpio sobre `engine_sprite_cpu_draw_masked_interleaved(...)`
- definicion de `EngineCpuSpriteBitmap`
- caso `CS02_cpu_sprite_4bpl_masked`

### Evidencia de cierre

- fondo 4bpl estable
- sprite masked correcto
- sin corrimiento ni basura visible
- `assert_failures == 0`

### Regla de implementacion

Antes de escribir otra rutina completa, comprobar si el caso queda cubierto limpiamente por:

- `engine_sprite_cpu_draw_masked_interleaved(...)`
- `engine_sprite_blit_cookie_cut_clipped(...)`

La nueva familia `engine_cpu_sprite_*` debe priorizar encapsular y validar bien estas rutas antes de duplicarlas.

## Fase 3: retained minima world-space

### Objetivo

Introducir una capa retained minima que:

- calcule posicion final con scroll
- mantenga `prev_x/prev_y`
- marque dirty rect actual y previo
- redibuje por frame

### Entregables

- `EngineSceneCpuSprite`
- `EngineSceneLayer`
- `engine_scene_begin_frame`
- `engine_scene_cpu_sprite_submit`
- `engine_scene_present`
- caso `CS03_scene_sprite_scroll`

### Evidencia de cierre

- sprite world-space se mueve con viewport
- no deriva al cambiar scroll
- politica por frame documentada

## Fase 4: overlay fijo / HUD

### Objetivo

Probar un caso de pantalla fija:

- `anchor = SCREEN`
- overlay no arrastrado por scroll
- dirty rect o redibujado controlado

### Entregables

- soporte retained para `ENGINE_CPU_SPRITE_ANCHOR_SCREEN`
- caso `CS04_scene_overlay_fixed`

### Evidencia de cierre

- overlay permanece fijo al mover viewport
- no destruye permanentemente el fondo
- politica de persistencia explicita

## Fase 5: save-under / restore-under

### Objetivo

Introducir la politica opcional mas delicada:

- `engine_cpu_sprite_save_under`
- `engine_cpu_sprite_restore_under`
- retained con `uses_restore_under`

### Entregables

- API low-level de save/restore
- uso opcional en retained
- caso de bateria adicional recomendado `CS05_scene_restore_under`

### Evidencia de cierre

- fondo restaurado sin rastros
- orden por frame estable
- sin corrupcion al mover sprite repetidamente

## Fase 6: despacho especializado y evaluacion C vs C++

### Objetivo

Medir si compensa una especializacion mas agresiva.

### Opcion A

Seguir en C con:

- variantes `_1bpl`, `_2bpl`, `_4bpl`
- wrapper `generic`
- posible `__builtin_constant_p`

### Opcion B

Abrir un piloto C++ aislado para:

- wrapper template
- comparacion de assembly
- comparacion de tamano

### Entregables

- nota comparativa
- decision documentada

## Casos de bateria sugeridos

| Caso | Objetivo | Fase |
|------|----------|------|
| `CS01_cpu_sprite_1bpl` | primitive low-level 1bpl | 1 |
| `CS02_cpu_sprite_4bpl_masked` | primitive low-level masked 4bpl | 2 |
| `CS03_scene_sprite_scroll` | retained world-space con scroll | 3 |
| `CS04_scene_overlay_fixed` | overlay fijo / HUD | 4 |
| `CS05_scene_restore_under` | save-under / restore-under | 5 |

## Estado actual

- Fase 1 cerrada: `CS01_cpu_sprite_1bpl` ya valida la primitive minima 1bpl en vivo.
- Fase 2 cerrada: `CS02_cpu_sprite_4bpl_masked` ya valida la ruta masked 4bpl en vivo sin acoplar `undraw`.
- Fase 3 cerrada en su version minima: `CS03_scene_sprite_scroll` ya valida world-space con scroll, `dirty_rect` minimo y mantenimiento por frame sobre retained ligera.
- Fase 4 cerrada en su primer nivel: `CS04_scene_overlay_fixed` ya valida `anchor = SCREEN` y separa overlay fijo de sprite anclado al mundo.
- Variante avanzada ya validada: `CS06_scene_overlay_fixed_32c` demuestra que la retained minima tambien puede sostener un overlay fixed 5bpl masked sobre un playfield 32 colores con scroll ciclico y evidencia animada.
- Siguiente escalon recomendado: `CS05_scene_restore_under`, para introducir restauracion opcional sin mezclarla con el modelo base de redraw completo por frame.

## Preguntas que deben hacerse siempre

1. La primitive pertenece al nivel low-level o retained?
2. El numero de bitplanes cambia el hot path?
3. El anclaje mundo/pantalla cambia la politica por frame?
4. Hace falta redraw o restore-under?
5. Este caso se puede cerrar sin mezclar tambien copper o blitter?

## Riesgos a evitar

- meter save-under demasiado pronto
- esconder coste real tras una API plana
- tratar HUD y sprite de mundo como el mismo caso
- abrir C++ antes de tener baseline en C

## Referencias

- [engine-cpu-sprites-api-proposal.md](engine-cpu-sprites-api-proposal.md)
- [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md)
- [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md)
- [ace-reuse-notes.md](ace-reuse-notes.md)
