# Propuesta de API: sprites CPU y scene retained

Propuesta concreta para aterrizar la politica de "low-level parametrico + retained" en un subsistema visual del engine.

Caso elegido: sprites CPU sobre bitplanes, con opcion de mascara, clipping y politicas de persistencia distintas segun el contexto.

## Objetivo

Evitar una API plana del estilo:

```c
void engine_draw_cpu_sprite(...);
```

porque esa firma no deja claro:

- cuantos bitplanes toca
- si el sprite es masked o unmasked
- si el destino pertenece al mundo o a un overlay fijo
- si hay restore-under
- si se requiere clipping
- si el hot path puede especializarse

La propuesta separa:

1. primitivas low-level explicitamente parametrizadas;
2. una capa retained para gestionar escena, dirty rects y persistencia por frame.

## Regla de diseno

### Lo que debe vivir en low-level

- copia de pixels a bitplanes concretos
- variantes por bitplanes
- variantes masked / unmasked
- clipping
- save-under / restore-under explicito
- blit CPU contra una superficie concreta

### Lo que debe vivir en retained

- lista de sprites de escena
- orden de dibujo
- anclaje a mundo o pantalla
- actualizacion por frame
- invalidacion / dirty rects
- politica de restauracion
- composicion con scroll o viewport

## Modelo propuesto

### 1. Tipos base low-level

```c
typedef enum EngineCpuSpriteMaskMode {
    ENGINE_CPU_SPRITE_MASK_NONE = 0,
    ENGINE_CPU_SPRITE_MASK_1BPL = 1
} EngineCpuSpriteMaskMode;

typedef enum EngineCpuSpriteAnchor {
    ENGINE_CPU_SPRITE_ANCHOR_WORLD = 0,
    ENGINE_CPU_SPRITE_ANCHOR_SCREEN = 1
} EngineCpuSpriteAnchor;

typedef struct EngineCpuSurface {
    UBYTE *planes;
    UWORD width;
    UWORD height;
    UWORD row_bytes;
    UWORD bitplanes;
} EngineCpuSurface;

typedef struct EngineCpuSpriteBitmap {
    const UBYTE *planes;
    const UBYTE *mask;
    UWORD width;
    UWORD height;
    UWORD row_bytes;
    UWORD bitplanes;
} EngineCpuSpriteBitmap;

typedef struct EngineRect {
    WORD x;
    WORD y;
    WORD w;
    WORD h;
} EngineRect;
```

## 2. Primitivas low-level propuestas

### Ruta generica

```c
void engine_cpu_sprite_blit_generic(
    EngineCpuSurface *dst,
    WORD dst_x,
    WORD dst_y,
    const EngineCpuSpriteBitmap *src,
    EngineCpuSpriteMaskMode mask_mode,
    const EngineRect *clip);
```

Uso:

- fallback para casos no especializados
- debug o primeras integraciones
- pruebas de correccion
- posible frente limpio sobre primitivas heredadas de `engine_sprite_*`

### Rutas especializadas explicitas

```c
void engine_cpu_sprite_blit_1bpl(
    EngineCpuSurface *dst,
    WORD dst_x,
    WORD dst_y,
    const EngineCpuSpriteBitmap *src,
    EngineCpuSpriteMaskMode mask_mode,
    const EngineRect *clip);

void engine_cpu_sprite_blit_2bpl(
    EngineCpuSurface *dst,
    WORD dst_x,
    WORD dst_y,
    const EngineCpuSpriteBitmap *src,
    EngineCpuSpriteMaskMode mask_mode,
    const EngineRect *clip);

void engine_cpu_sprite_blit_4bpl(
    EngineCpuSurface *dst,
    WORD dst_x,
    WORD dst_y,
    const EngineCpuSpriteBitmap *src,
    EngineCpuSpriteMaskMode mask_mode,
    const EngineRect *clip);
```

### Restore-under y save-under

```c
ULONG engine_cpu_sprite_save_under(
    const EngineCpuSurface *src_surface,
    WORD x,
    WORD y,
    WORD w,
    WORD h,
    UBYTE *save_buffer,
    ULONG save_buffer_size);

void engine_cpu_sprite_restore_under(
    EngineCpuSurface *dst_surface,
    WORD x,
    WORD y,
    WORD w,
    WORD h,
    const UBYTE *save_buffer,
    ULONG save_buffer_size);
```

Estas funciones deben ser explicitas porque no todos los contextos necesitan ni toleran save-under.

## Relacion con codigo existente

Esta propuesta no implica desechar las primitivas ya presentes en el engine. Hoy ya existen rutas utiles en:

- [engine_sprite.h](C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/engine/include/engine_sprite.h)
- [sprite.c](C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/engine/src/sprite.c)

En especial:

- `engine_sprite_cpu_draw_masked_interleaved(...)`
- `engine_sprite_cpu_save_rect_interleaved(...)`
- `engine_sprite_cpu_restore_rect_interleaved(...)`
- `engine_sprite_blit_cookie_cut_clipped(...)`

La familia `engine_cpu_sprite_*` debe nacer, siempre que sea viable, como una capa publica mas clara y testeable sobre esas rutas ya depuradas. Solo conviene abrir una implementacion nueva cuando:

- haya un hot path claramente distinto;
- la firma actual impida una especializacion real;
- o la ruta heredada no pueda validarse o encapsularse limpiamente.

### Despacho para parametros fijos

En C:

- wrapper con `__builtin_constant_p(bitplanes)`
- o seleccion explicita por API

En C++:

- wrapper template encima de ABI C o implementacion C++

## 3. Capa retained propuesta

### Tipos retained

```c
typedef struct EngineSceneCpuSprite {
    const EngineCpuSpriteBitmap *bitmap;
    WORD x;
    WORD y;
    WORD prev_x;
    WORD prev_y;
    UBYTE visible;
    UBYTE needs_redraw;
    UBYTE uses_restore_under;
    UBYTE z_order;
    EngineCpuSpriteAnchor anchor;
    EngineRect dirty_rect;
    UBYTE *save_under;
    ULONG save_under_size;
} EngineSceneCpuSprite;

typedef struct EngineSceneLayer {
    EngineCpuSurface *surface;
    EngineRect viewport;
    WORD scroll_x;
    WORD scroll_y;
} EngineSceneLayer;
```

### Operaciones retained

```c
void engine_scene_cpu_sprite_init(
    EngineSceneCpuSprite *sprite,
    const EngineCpuSpriteBitmap *bitmap);

void engine_scene_cpu_sprite_set_pos(
    EngineSceneCpuSprite *sprite,
    WORD x,
    WORD y);

void engine_scene_cpu_sprite_set_anchor(
    EngineSceneCpuSprite *sprite,
    EngineCpuSpriteAnchor anchor);

void engine_scene_cpu_sprite_enable_restore_under(
    EngineSceneCpuSprite *sprite,
    UBYTE *save_under,
    ULONG save_under_size);

void engine_scene_cpu_sprite_submit(
    EngineSceneLayer *layer,
    EngineSceneCpuSprite *sprite);

void engine_scene_begin_frame(EngineSceneLayer *layer);
void engine_scene_present(EngineSceneLayer *layer);
```

## 4. Politica por frame

La capa retained debe asumir al menos este ciclo:

1. `engine_scene_begin_frame`
   - limpiar lista de sprites visibles de ese frame
   - preparar politica de dirty rects
2. `engine_scene_cpu_sprite_submit`
   - registrar sprite visible
   - calcular posicion final segun anchor y scroll
   - marcar dirty rect actual y previo
3. `engine_scene_present`
   - restaurar fondos previos si aplica
   - redibujar sprites en orden
   - actualizar `prev_x` / `prev_y`

Esto evita el error clasico de "dibuje una vez el HUD o sprite y supuse que se mantiene solo".

## 5. Anchors y viewport

### Sprites anclados al mundo

- su posicion final depende de `scroll_x` / `scroll_y`
- al cambiar el viewport cambian sus coords en pantalla
- si comparten superficie con el fondo, requieren redraw o restore-under

### Sprites anclados a pantalla

- ignoran scroll del mundo
- tipico para HUD, debug o cursor fijo
- no deben derivar al mover el viewport

## 6. Casos de uso recomendados

### Caso A: sprite CPU simple en mundo

- low-level especializado por bitplanes
- retained con redraw por frame
- clipping contra viewport

### Caso B: HUD de debug fijo

- retained con `anchor = SCREEN`
- preferible sobre superficie dedicada o dirty region controlada
- si comparte bitplanes con el mundo, requiere politica explicita de restauracion

### Caso C: BOB con mascara y restore-under

- low-level masked + save-under / restore-under
- retained decide cuando guardar/restaurar y en que orden

## 7. Tests de bateria sugeridos

### `CS01_cpu_sprite_1bpl`

- un sprite CPU en 1 bitplane
- ruta low-level directa
- evidencia: posicion, clipping, markers

### `CS02_cpu_sprite_4bpl_masked`

- sprite masked sobre fondo 4bpl
- evidencia visual y `assert_failures == 0`

### `CS03_scene_sprite_scroll`

- sprite world-space con viewport movil
- debe moverse correctamente con scroll

### `CS04_scene_overlay_fixed`

- overlay o HUD fijo sobre escena con scroll
- no debe derivar con el viewport

## 8. Encaje con C vs C++

### En C puro

Se puede empezar con:

- ABI C
- variantes `_1bpl`, `_2bpl`, `_4bpl`
- fallback `generic`
- wrappers retained normales

### En C++ controlado

Se puede anadir despues:

```cpp
template<int Bitplanes, bool Masked>
inline void engine_cpu_sprite_blit(...);
```

pero idealmente como capa de especializacion o wrapper sobre una base C estable, no como migracion masiva obligatoria.

## 9. Decision recomendada para el repo

1. Empezar en C con familia low-level especializada + retained minima.
2. Medir assembly y complejidad en una rutina critica.
3. Solo despues decidir si conviene un modulo piloto en C++.

## 10. Referencias relacionadas

- [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md)
- [engine-architecture.md](engine-architecture.md)
- [engine-subsystems.md](engine-subsystems.md)
- [engine-test-battery-matrix.md](engine-test-battery-matrix.md)
- [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md)
