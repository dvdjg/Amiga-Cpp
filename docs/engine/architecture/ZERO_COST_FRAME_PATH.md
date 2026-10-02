# Camino de frame a coste cero

Regla de diseño del engine: **el bucle principal no hace trabajo evitable**. La abstracción
(fachada, `Scene`, `FramePlan`, capas de objetos) debe resolverse en **tiempo de compilación o en el
setup**, y el frame solo escribe los registros de hardware que cambian. Es la diferencia entre caber
en un campo PAL (50 Hz) o necesitar dos (25 Hz).

## La norma (obligatoria)

1. **Prohibido `memset`/`memcpy`/cimiento de buffers fuera del setup** (init de escena, carga de
   assets, reserva de memoria). En el camino de frame, cualquier cero o copia implícita es un fallo
   de diseño, no una micro-optimización.
2. **Prohibido construir y destruir objetos con estado grande por frame** (un `BlitJob` local
   value-inicializado, un `FramePlan` temporal, un `Screen`/`DrawTarget` copiado por valor). Se
   construyen **in situ** en su almacenamiento estable o se pasan por **referencia**.
3. **Prohibida la contabilidad por elemento en el encolado** (recorrer/recalcular agregados en cada
   `add`). Se **difiere** al cierre del plan.
4. **Prohibida la asignación dinámica** en el frame (no hay heap por diseño).

El objetivo no es «casi gratis»: es **coste cero** salvo las escrituras de registro que el hardware
exige. El símbolo de la operación (`std::memset`, `memcpy`, un `= {}`) en el árbol del frame es la
señal de alarma.

## Por qué (medido, demo 213)

Perfil de la 213 antes de aplicar la norma: `memset` 16,6 % + `memcpy` 12,3 % del frame
(≈ 83 000 ciclos/frame) más `rebuild_blit_budget_report` en cada job y el `mulu` del presupuesto
por objeto. Con el blitter **instantáneo** (sin robar ciclos al 68000) la demo ya daba **50 fps**:
el coste de CPU sí cabía en un campo; las copias y la contabilidad por objeto lo empujaban fuera.
Pasos aplicados y efecto (213, A/B con `measure-fps`):

| medida | debug | release |
|---|---|---|
| antes | 24,96 fps (2,0 campos) | 28,55 fps (1,75 campos) |
| `BlitJob` in situ + informe diferido | 31,9 | 37,6 |
| **contabilidad del presupuesto diferida entera** | 32,6 | **49,92 fps (1,00 campo)** |

Sin tocar el Blitter: la ristra de 16 BOBs y el clear son los mismos 17 blits que antes. La prueba
de que el CPU ya cabía es que con blitter instantáneo la demo daba 50 fps; el trabajo que sobraba
era todo de CPU (copia/construcción de `BlitJob` por objeto y aritmética de presupuesto por job).

## Técnicas (C++23, sin coste en runtime)

- **Construcción in situ**: el contenedor da una **ranura** (`FramePlan::begin_blit_job`) y el
  llamador rellena los campos directamente; cierra con `commit_blit_job`. Evita el local
  value-inicializado y la copia. Los grupos de campos no usados por ese `kind` conservan valores
  previos (no se leen): no hace falta inicializarlos.
- **Sin inicialización innecesaria**: no `Type t {}` para tipos grandes en el frame; usar agregados
  con los campos justos o escribir sobre la ranura.
- **Paso por referencia**: `const T&`/`T&` para `BobTarget`, `DrawTarget`, `Screen`, planes. Un
  parámetro **por valor** de un tipo con estado es una copia.
- **Contabilidad diferida**: acumular incrementos baratos al encolar; recalcular los informes
  agregados **una vez** (en `finalize()` o de forma perezosa al leerlos).
- **Tablas y decisiones en compilación**: `constexpr`, `consteval`, parámetros de plantilla y
  `if constexpr` para ramificar por tipo/plataforma sin coste. Geometría y offsets derivados del
  hardware, en constantes.
- **Expression templates** cuando el problema es de **composición de operaciones** (encadenar
  transformaciones/consultas sin materializar intermedios): ver `EXPRESSION_TEMPLATES.md`. No es la
  herramienta para copias de estructuras, pero sí para que una cadena de cálculos no genere
  temporales.
- **`always_inline` en el camino caliente** con criterio (encoders por objeto), y funciones de
  soporte fuera del bucle.

## Cómo verificar

- **Blitter instantáneo** (`run-demo --immediate-blits`) aísla el coste de CPU: si con él no se
  llega a 50 fps, el problema es CPU; si se llega y el real no, el problema es uso del bus/Blitter.
- **Perfil por secciones** (`tools/debug/profile.mjs`) y **perfil del plugin**: buscar `memset`/
  `memcpy`/`= {}` en las primeras posiciones del frame.
- **Test de no-regresión**: el camino `sprite()`/`clear_box()` no debe invocar `memset`/`memcpy` por
  objeto (ver HOST-388 y el camino de `bob_draw_interleaved_pair`).

## Diseño del plan de Blitter y de Copper

El sistema se organiza en dos piezas que **se construyen una vez** y por frame solo reciben escrituras:

```
  SETUP (una vez)                         FRAME (coste cero)
  ─────────────────                       ──────────────────
  Copper list  ──► patch slots (Patch32)   escribir BPLxPT/registros en la lista viva
  FramePlan    ──► ranuras estables        begin_blit_job() -> rellenar -> commit_blit_job()
  assets       ──► Chip (res.ChipView)     solo lecturas (vistas), nunca copias
```

- **Copper**: la lista se emite en setup y por frame se **parchea** (palabras de datos, `BPLxPT`,
  `BPLCON1`), no se reconstruye. Un modo que necesite re-emitir (copper-chunky) usa **doble bloque**
  (`flip_copper`) y parchea datos, sin `memset`/`memcpy` de la lista.
- **Blitter**: `FramePlan` es un array **estable** de jobs; por frame se rellenan **ranuras** in situ
  (`begin_blit_job`), el ejecutor encadena rachas de estado común y el informe se calcula una vez
  (`finalize`). La cadena async (IRQ de fin de blit) es el modo opt-in cuando el CPU necesita
  presupuesto; ver `BLITTER_INTENT_QUEUE.md`.
- **Composición de operaciones** (varias transformaciones/consultas encadenadas): usar
  *expression templates* (`EXPRESSION_TEMPLATES.md`) para no materializar temporales intermedios.

### Modo streaming: dibujar = emitir (sin plan)

El camino a coste cero **de verdad** es **emitir los blits en el momento de la llamada**, como el
`main.c` de referencia, en vez de **construir un plan y ejecutarlo después**. El engine expone una
**racha de estampado** en la fachada (el juego no ve planos ni registros):

```cpp
auto s = app.screen();
s.clear_now(band);                   // borrado inmediato (D = 0, bloque contiguo)
auto run = s.stamp(sheet);           // fija el estado comun UNA vez (op = politica del sprite)
for (/* cada objeto */) run.at(x, y, frame);  // escribe solo los registros que cambian y lanza
run.done();                          // espera al ultimo
app.present();                       // commit (doble buffer)
```

`stamp` toma la geometría del `Sprite` (interleaved, planos, máscara) y el destino de la escena, y
elige el `BlobOp` de la política del sprite (`Or`/`Opaque`/`CookieCut`); `at` emite un blit por
objeto. Detrás está `AmigaBackend::blitter_blob_run_begin/one/end` (racha genérica en streaming,
`BlobOp` de dominio), el mismo bucle que `blitter_or_bobs_*`/`main.c`. **No hay `FramePlan`, ni
array de jobs, ni pasada de ejecución, ni copia por objeto.** El `Screen` recibe la racha por una
factoría *type-erased* (`BlitStream`) que el `App` conecta a su backend, así que la fachada no
nombra tipos del backend.

Medición (demo 213, A500, imagen verificada):

| modo | debug | release |
|---|---|---|
| plan (construir + ejecutar) | 32,9 fps | 38,0 fps |
| **streaming (dibujar = emitir)** | **49,87 fps** | **49,87 fps** |

El plan sigue existiendo para lo que aporta (reordenar, lote, async con avisos, ejecución diferida
al blanking); el **streaming es el camino por defecto cuando el frame cabe y se quiere coste cero**.

**Cuándo cada uno.** Streaming si el frame es una **secuencia homogénea** de objetos que cabe en un
campo y no necesita reordenarse ni encadenarse. Plan si hay **estados del Blitter mezclados** (que
conviene agrupar), si se quiere **diferir/encadenar** la ejecución (async, avisos) o si el frame no
cabe y hay que repartirlo. No se mezclan en el mismo frame (el plan se ejecuta en `present`; el
streaming emite ya).

**Decisión: sin auto-selección.** El dev **elige explícitamente** (`screen().sprite(...)` vs
`screen().stamp(...)`) porque los caminos **no son intercambiables**: el streaming emite en orden y
no admite reordenar ni diferir, y el plan se ejecuta en `present`. Un «auto» ocultaría qué
semántica corre (orden, latencia, encadenado) y obligaría a un análisis que el dev ya sabe. La
fachada deja ambas y documenta la regla; el motor no adivina.

**Decisión: capas/mundo de momento sobre el plan.** Promover el streaming a `BobLayer`/`world`
exigiría darle acceso al backend (las capas son **agnósticas del backend** por diseño) y elegir el
camino por capa; el caso «escena homogénea» ya lo cubre la fachada con `stamp`. Se deja para cuando
haya un consumidor concreto (una capa que quiera streaming sin que el juego lo pida).

## Referencias

## Referencias

- `docs/engine/architecture/CODING_STYLE.md` (§Reglas obligatorias de diseño).
- `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` (plan de blits y cadena async).
- `docs/engine/architecture/EXPRESSION_TEMPLATES.md`.
- Perfiles de la 213 (2026-10): reparto `memset`/`memcpy` y A/B de fps.
