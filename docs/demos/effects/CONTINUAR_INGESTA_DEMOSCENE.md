# Continuar la ingesta de demoscene-repo-orig (prompt de hilo nuevo)

Copia este contenido literalmente como primer mensaje de un hilo de IA **sin
historial** para que continúe la portación de las librerías de
`demoscene-repo-orig` al engine de Amiga-Cpp.

---

## Rol y proyecto

Trabajas en `C:\Users\dvdjg\Documents\programa\AI\Amiga\Amiga-Cpp`, un engine de
demos para Amiga 500 (OCS/ECS, Kickstart 1.3) en **C++23 freestanding** (sin
libstdc++, sin excepciones, sin RTTI, sin heap en gameplay) compilado con el
toolchain cruzado `m68k-amiga-elf` (extensión Bartman, GCC 15) y flujo de
validación `build -> run -> analyze` con WinUAE-DBG (GDB :2345 + canal lateral
:2346). El objetivo es **incorporar el conocimiento de las librerías de
`demoscene-repo-orig`** (proyecto Amiga en dialecto C, con 11 librerías) al
engine, adaptándolas a C++23 y validándolas en el pipeline real.

**Ambiente**: Windows nativo. **NO uses WSL** (mangla rutas y rompe
cc1plus/WinUAE). Usa binarios de Windows: `C:\Program Files\nodejs\node.exe`,
los `.exe` del toolchain (en `AMIGA_BIN_PATH` o en `.vscode\extensions\
bartmanabyss.amiga-debug-1.8.1\bin\win32`). El runner compilado está en
`dist/tools/run/run-demo.js`; las tools TS en `tools/`.

## Lectura obligatoria inicial

1. `AGENTS.md` (idioma, formato de docs, **buscar-antes-de-implementar**,
   **regla de tests unitarios**, regla de evidencia, regla de rendimiento 68000,
   no-WSL).
2. `docs/STRUCTURE.md` (dónde va cada archivo).
3. `docs/demos/effects/LIBRARIES-CPP23-IMPORT-ROADMAP.md` ← **EL documento
   rector de esta tarea**. Léelo entero: principios, destino por librería,
   oleadas, flujo por librería e índice de cobertura.
4. `docs/demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md` (no portar línea
   a línea; reconstruir con APIs limpias del engine).
5. `docs/demos/effects/demoscene-repo-coverage-index.md` (catálogo efecto a
   efecto 01-67 + Starfox, con efecto → técnica → API candidata).
6. `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` (restricciones del
   backend m68k; bitácora de descubrimientos).
7. `docs/reference/ahrm/amiga-hardware-manual-index.md` (AHRM 3.ª edición local;
   texto en el `.cat.md`) — úsalo ante cualquier duda de registros/timing del
   chipset.

## Fuentes

- **Origen (fuente de verdad)**: `C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo-orig`
  — `lib/` (11 librerías: lib2d, lib3d, libahx, libblit, libc, libctr, libgfx,
  libgui, libmisc, libp61, libpt), `include/` (~50 headers), `effects/` (~67
  efectos), `docs/` (tutoriales y catálogo).
- **Conversión intermedia (NO fiable)**: `C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo`
  — otra IA convirtió el dialecto a GCC; puede no compilar. Úsala solo para ver
  *cómo se encaró* cada pieza, nunca como fuente de verdad.

## Punto en el que nos quedamos (estado real)

**Oleada 0 (infraestructura sin hardware) — hecha y validada con test host:**

- `engine/include/eng/core/isqrt.hpp` — port fiel de `libmisc/fx.c` (`isqrt`
  con tabla + nlz, sin división/floats). El algoritmo original **NO es una raíz
  exacta** (subestima; p. ej. `isqrt(9)==2`, `isqrt(32768)==181`); se conserva
  ese comportamiento. Validado por equivalencia con el C original compilado.
- `engine/include/eng/core/sort.hpp` — `eng::quick_sort` (quicksort + inserción,
  genérico sobre `Span<T>` con comparador) y `eng::sort_items` (equivalente a
  `SortItemArray`).
- `engine/include/eng/core/crc32.hpp` — CRC-32 IEEE de `libmisc/crc32.c`
  (con máscara de 32 bits para host x64).
- `engine/include/eng/core/random.hpp` — xoroshiro64++ de `libc/stdlib/random.c`.
  El `rol` del original (por rangos + `swap16`) equivale a `rotl32` estándar
  (verificado); se expone la forma limpia.
- Infraestructura de **test unitario host**: `tests/host/000_eng_core_math` +
  `tools/run-host-tests.sh` (usa `g++` del entorno, sin WSL/MSVC). Los tests
  validan **equivalencia con el C original** (no propiedades inventadas).

**Decidido en la Oleada 0 (no reabrir salvo que surja necesidad real):** `qsort`
ya superado por `eng::core::quick_sort`; `string`/`stdio` (kvprintf/snprintf) NO
se portan ahora (el engine usa `Span`/builtins y `debug.text()` es
`const char*` fijo). `sintab`→`core::sinetable` (ya existe), `SIN/COS`→`sinetable`.

**Pendientes de Oleada 0 (opcional, bajo prioridad):** `console` (depende de
`libgfx` → pasa a Oleada 1), `sync`, `file`. No bloquear.

**Punto de continuación recomendado: Oleada 1 — `libgfx`** (display base:
bitmaps, copper lists, sprites, paletas, c2p). Sigue el flujo de la §4 del
roadmap. Alternativa previa si quieres completar validación visual de la Oleada
0: portar/mapear el resto de `libmisc`/`libc`.

## Cómo importar (patrón obligatorio, ya establecido)

Para cada librería/oleada, SIN saltarse pasos:

1. **Inventario**: lee el `.h` público y los `.c` de la lib; lista funciones,
   tipos y dependencias; enumera los efectos que la usan.
2. **Crosswalk** con `demoscene-repo` (solo pistas, no copiar).
3. **Mapeo con el engine**: para cada función, ¿ya existe en `eng::`? → mapear
   (no duplicar). ¿Vale la pena portarla? → portar. ¿No aporta? → descartar y
   anotar por qué. Regla: **solo la mejor información, no se mantienen
   duplicados**.
4. **Portación C++23 fiel al comportamiento, limpia en estilo**: sin
   excepciones/RTTI/heap; APIs paramétricas (no mágicos); el `.asm` se conserva
   como `support/`, no se traduce a C++; comentarios didácticos en español.
5. **Test obligatorio** (regla de AGENTS): toda API reutilizable lleva test.
   Preferido: demo paramétrica/por fases con `g_eng_run_status.detail`. Si no,
   **test unitario host** en `tests/host/` (compilado con g++ del entorno, sin
   WSL/MSVC). Los algoritmos puros llevan **test host**; el patrón para
   funciones numéricas es **equivalencia con el C original compilado** (autenticar
   muestras, no inventar propiedades).
6. **Validación con efecto**: al menos un efecto que use la librería, adaptado
   como demo `demos/amiga/NNN_...`, con `build -> run -> analyze` y evidencia
   real de hardware.
7. **Promoción**: dejar en `engine/` solo lo estable y validado; actualizar el
   índice de cobertura (§5 del roadmap) y este roadmap en la misma pasada.

## Reglas de rendimiento 68000 (releer OPTIMIZACION_GPP_68000.md)

- Preferir algoritmos **rápidos y exactos** a lentos y precisos (p. ej. el
  `isqrt` del origen subestima pero no paga división).
- Aritmética 16-bit nativa (`muls.w/divs.w`), bucles countdown (`dbra` con
  `-Os`), cero divisiones runtime (`fast_div<N>`), cero floats, cero STL.
- **Comprobar el asm generado** (`-S -fverbose-asm`) al portar: nuestra versión
  debe generar código al menos igual de eficiente que el asm original; anotar en
  la bitácora del doc de optimización.

## Cómo ejecutar (build/run/test)

```powershell
# Test host (rápido, sin emulador):
bash tools/run-host-tests.sh                        # todos
bash tools/run-host-tests.sh tests/host/000_eng_core_math

# Compilar una demo (toolchain Windows; el runner usa CONFIG A500_debug por defecto):
#  - construir el .exe en out/demos/<demo>/A500_debug/ con los .exe del toolchain
#    (o, con Git Bash nativo: bash tools/build/build-demo.sh demos/amiga/<demo> --clean)

# Correr + capturar + analizar:
& "C:\Program Files\nodejs\node.exe" "$PWD\dist\tools\run\run-demo.js" demos\amiga\<demo> --warp
```

Nota: el runner resuelve el `.exe` desde `out/demos/<demo>/A500_debug/` (o el
CONFIG que pidas). Verifica que corres la build de la fuente actual, no un
`.exe` viejo de otra config.

## Estado de pendientes transversales (no es tu misión, pero tenlo en cuenta)

- Hay un bug de arranque/display aún abierto (doble texto + banda azul
  intermitente) documentado en
  `docs/debugging/DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md`; afecta a la demo
  060 y a la 201. Si la Oleada 1 toca la toma de display, ten en cuenta ese bug.
- Regla del proyecto: persistir solo la mejor información; no duplicar; no
  introducir metainformación de proceso en docs de referencia (va al commit).

## Entregable al final de cada pieza

1. Inventario y mapeo (qué se portó, qué se mapeó, qué se descartó y por qué).
2. Código portado en `engine/` + su test (demo o host).
3. Evidencia: build + test host + (si aplica) captura/demo.
4. Índice de cobertura (§5 del roadmap) actualizado.
5. Mensaje de commit razonado (no commitear salvo que te lo pidan).