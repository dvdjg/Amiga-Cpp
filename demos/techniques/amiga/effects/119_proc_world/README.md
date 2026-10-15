# Demo 119 — PROC WORLD (terreno procedural de `eng::gfx::proc`)

Monta y muestra en un Amiga 500 un **mundo de terreno generado proceduralmente** por
`engine/include/eng/graphics/proc/` (la librería de bitmaps procedurales; ficha canónica:
[`PROCEDURAL_BITMAPS.md`](../../../../../docs/engine/architecture/PROCEDURAL_BITMAPS.md)):

- **Atlas de materiales** (22 materiales × 3 variantes, paleta de **32 colores** — 8 familias
  de tono de 4 niveles — y **paso de halftone**: las texturas se cuantizan a `2·levels−1`
  pasos y los intermedios se pintan como tableros 2×2 entre colores vecinos, así una familia
  de 4 colores muestra 7 tonos aparentes). Set de **69 piezas** cocinadas (catálogo de 45 +
  rotaciones de relleno) y **78 en runtime** (el colocador deriva 9 orientaciones de
  río/caminos con `add_variant`). Todo se cocina en el host con la misma librería
  (`MiniFloat16`, determinista byte a byte; lo ancla el golden `HOST-443`).
- **Colocador** (`terrain::place_world`, con `PlaceParams`): río de norte a sur con meandros
  y **afluente** que desemboca en T desde el borde superior; camino con **puente** donde
  cruza el cauce y **sendero en L hasta la puerta** del castillo (cruce en T + fondo de
  saco); castillo 3×3 con **torre** y **puerta**; aldea, cueva, sierra y **lago junto al
  cauce**; todas las uniones se validan con `layout_is_connected`.
- **Composición y display**: el mapa 10×7 tiles se compone en un framebuffer indexado y se
  publica UNA vez con `eng::IndexedDisplay<5>` (32 colores, 5 planos; C2P por el engine en
  carga). El frame es estático: el coste por frame es solo la telemetría (ver abajo).

## Generación EN TIEMPO DE COMPILACIÓN (C++23 `constexpr`)

El atlas (66 texturas de 16×16 con ruido escalar) y el mundo completo (campos, gramática,
máscaras y **pintado de las ~90 piezas**) se evalúan **con el compilador** al construir la
demo: `make_world_atlas()`/`make_world()` en `src/main.cpp` son `constexpr` y el binario
m68k incrusta el resultado (rodata). El coste es de build (`build.args` sube los límites
`constexpr`; ~1 min extra) y **cero en la máquina**: la carga medida bajó de ≈103 M de
ciclos (generación runtime en el 68000) a **≈7,9 M** (componer + C2P + verificar).

```text
  build (g++ m68k):  kWorldAtlas = generar atlas constexpr   (~30 s de compilador)
                     kWorld      = generar mundo constexpr   (~30 s)
  amiga (carga):     verificar contrato → componer 20×14 → present (C2P 5 planos) → VBlank
```

La misma fuente corre en runtime en el host (`HOST-445`, dumps de visión) y el patrón de
cómo habilitarlo (`if consteval` en las cachés, límites, `DEMO_FLAGS`) está en
[`PROCEDURAL_BITMAPS.md`](../../../../../docs/engine/architecture/PROCEDURAL_BITMAPS.md) §9.

### Vía OFFLINE (misma librería, mismas semillas)

`tools/gen_assets.cpp` emite los MISMOS assets como header C++ (paleta, texturas, piezas
pintadas, especificaciones y layout) para consumidores que no puedan (o no quieran) pagar
la evaluación `constexpr` del compilador, otros targets, o inspección sin arrancar la
demo. Determinista byte a byte (mismas semillas que la vía `constexpr` y que `HOST-445`;
imprime huellas FNV comparables):

```bash
D=demos/techniques/amiga/effects/119_proc_world
g++ -std=gnu++23 -O2 -Iengine/include $D/tools/gen_assets.cpp -o out/tmp/gen_assets_119
./out/tmp/gen_assets_119 out/assets/proc-world/proc_world_assets.h
```

**Latido bajo un SO vivo**: el bucle usa el **modo de sondeo validado del doc 212**
(`run_frames_polling`: sin secuestrar el vector de nivel 3 ni depender de IRQs/`Wait`) —
estable a **49,8 fps**. Durante la validación se reconfirmó el fallo `a5=0` del 212 para el
bucle por IRQ (evidencia y alcance en [`VALIDATION.md`](VALIDATION.md) §Latido).

## Franja de versión y telemetría viva

La franja inferior de 16 px (norma del repo) muestra `PROC WORLD v<major>.<minor>.<build>`
(el **build lo inyecta** `build-demo.sh` con el contador persistente `out/.build-number`),
`LOAD <NNNN>KC PIECES 78` y un **contador de frames** (`F nnnnnn`) que se refresca cada
frame por **parcheo planar mínimo** (`IndexedDisplay::patch_text`: solo se reescriben por
planos los dígitos que cambian, sin C2P) — ~1-2 celdas de 8×8 por frame.

**Bajo un SO vivo la espera de VBlank es ACTIVA** (`set_vblank_sleep(false)`, variante 2 del
212): con `Wait()` el libro de display del SO reescribe `INTENA` y borra el `VERTB` del
engine → la tarea queda dormida para siempre (frame congelado a los pocos frames, PC en el
`STOP` de exec). Ver
[`212-vblank-irq-apagada-por-el-so.md`](../../../../../docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md).

## Comandos

```bash
# El build tarda ~1 min de más: el compilador evalúa la generación completa (ver build.args).
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/119_proc_world --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/119_proc_world --warp
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/effects/119_proc_world
```

## Contrato que ilustra

- El **mismo** contenido que los dumps host (`out/playground/proc-bitmaps/`): semillas
  compartidas (atlas `0x7E5A1`, set `0xBEE5`, mapa `0xBEE5`) y generación determinista
  (el set de la demo añade además las rotaciones de relleno del generador host).
- Las piezas encajan por construcción (`can_place`/`layout_is_connected`): el test host
  `HOST-442` verifica las uniones y `HOST-443` congela el render (paleta, texturas, piezas
  y mapa con dos semillas).
- Display verificado en hardware: captura + zonas evaluadas con el modelo de visión local
  (Ollama) y regresión con gate de parpadeo — ver [`VALIDATION.md`](VALIDATION.md).
