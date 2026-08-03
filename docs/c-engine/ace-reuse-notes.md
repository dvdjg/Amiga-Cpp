# Notas de reutilizacion desde ACE

Resumen practico de que merece la pena reutilizar conceptualmente desde ACE para este engine, y que no conviene copiar tal cual.

## Conclusiones rapidas

- ACE si contiene bloques maduros de trabajo para objetos dibujados por blitter o CPU.
- ACE no intenta resolver todo esto con un sistema general de "sprite management" retained.
- Lo mas valioso de ACE para este repo no es una API unica, sino su disciplina operativa:
  - mascara separada para cookie-cut;
  - `undraw -> update -> save background -> draw -> end frame`;
  - opcion de buffer pristino frente a save-under;
  - guardrails de debug en operaciones de blit.

## Evidencia base

- [README ACE](C:/Users/dvdjg/Documents/programa/AI/ACE/README.md): declara explicitamente que ACE no ofrece "elaborate sprite management".
- [blits_with_mask.md](C:/Users/dvdjg/Documents/programa/AI/ACE/docs/programming/blits_with_mask.md): explica el flujo de mascara separada y cookie-cut.
- [blit_undraw.md](C:/Users/dvdjg/Documents/programa/AI/ACE/docs/programming/blit_undraw.md): muestra el ciclo de restauracion de fondo por frame.
- [using_bobs.md](C:/Users/dvdjg/Documents/programa/AI/ACE/docs/programming/using_bobs.md): formaliza una capa de gestion para objetos blitteados, pero centrada en BOBs del blitter y no en un retained universal.
- [blit.c](C:/Users/dvdjg/Documents/programa/AI/ACE/src/ace/managers/blit.c): aporta checks de debug y variantes segura/rapida.
- [bob.c](C:/Users/dvdjg/Documents/programa/AI/ACE/src/ace/managers/bob.c): muestra una implementacion madura de colas, undraw y doble buffer para BOBs.

## Que si merece la pena traer

### 1. Guardrails de debug para superficies y rangos

ACE valida varias cosas antes de un blit en debug:

- memoria CHIP en origen y destino;
- coordenadas y tamanos dentro de rango;
- limites adicionales en blits interleaved.

Esto encaja muy bien con el objetivo del repo de evitar iteraciones a ciegas. Conviene copiar la idea, aunque no necesariamente la API exacta.

### 2. Politica explicita de ciclo por frame

ACE deja muy claro que un objeto blitteado no "vive solo" en pantalla. Hay que decidir:

- si se restaura fondo previo;
- si se usa pristine buffer;
- en que momento exacto se dibuja respecto al resto de la escena;
- cuando se puede y no se puede tocar el blitter.

Esto es especialmente valioso para el skill y para futuros subsistemas retained del engine.

### 3. Distincion entre primitive y manager

ACE separa bien:

- primitive de blit;
- tecnica masked;
- undraw/save-under;
- manager mas alto nivel para BOBs.

Ese desacoplamiento es justo la direccion que queremos para `engine_cpu_sprite_*`.

## Que no conviene copiar sin mas

### 1. El manager BOB completo

`bob.c` es interesante, pero esta muy ligado a:

- bitmaps interleaved;
- doble buffer;
- flujo del blitter;
- decisiones internas de ACE;
- macros y tipos propios del framework.

Copiarlo entero ahora meteria demasiada semantica externa de golpe. Es mejor inspirarse en su modelo para futuras capas retained del engine.

### 2. Las APIs de juego tutorial de ACE

Los tutoriales de ACE resuelven casos concretos muy bien, pero muchas veces asumen:

- estructura de gamestate propia;
- managers y tipos concretos de ACE;
- recursos preparados segun su pipeline.

Lo util ahi es el patron, no la firma literal.

## Implicacion para este repo

1. `CS02_cpu_sprite_4bpl_masked` no deberia empezar con una reescritura completa.
2. Debe reutilizar primero las primitivas ya presentes en [engine_sprite.h](C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/engine/include/engine_sprite.h) y [sprite.c](C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/engine/src/sprite.c):
   - `engine_sprite_cpu_draw_masked_interleaved(...)`
   - `engine_sprite_cpu_save_rect_interleaved(...)`
   - `engine_sprite_cpu_restore_rect_interleaved(...)`
   - `engine_sprite_blit_cookie_cut_clipped(...)`
3. La nueva familia `engine_cpu_sprite_*` debe funcionar como frente limpio y verificable, no como duplicacion innecesaria de toda la logica previa.
4. Si mas adelante queremos retained serio para muchos objetos, conviene estudiar ACE `bob.c` como referencia de arquitectura y no como codigo para importar literalmente.

## Decision actual

- Reutilizar ideas y, donde compense, checks de debug inspirados en ACE.
- No importar aun el subsistema BOB de ACE.
- Empujar `CS02` sobre las primitivas ya existentes del engine antes de abrir mas variantes duplicadas.
