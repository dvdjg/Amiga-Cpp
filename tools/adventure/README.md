# Herramientas del pipeline de aventuras (`tools/adventure/`)

Infraestructura **host** (PC) del pipeline de conversión de aventuras clásicas al engine: el plan y las decisiones están en [`ROADMAP_ADVENTURE_IMPORT.md`](../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md) (hitos I0–I5) y el diseño de la capa destino en [`ADVENTURE_LAYER.md`](../../docs/engine/architecture/ADVENTURE_LAYER.md).

## Estado

- **Hecho**:
  - Oráculo compilado en local (`build-scummvm-tools.sh`: `descumm`) y toolchain de discos Amiga verificado (§Herramientas de disco).
  - **Workspace por juego** ([`WORKSPACE.md`](WORKSPACE.md) + `workspace.mjs`): estructura canónica en `out/adventure/<juego>/` (raw/decoded/edited/cook/preview/reports) con descriptor `game.json`.
  - **Frontend SCUMM V5** (`frontends/scumm_v5/extract.mjs`, con su [README](frontends/scumm_v5/README.md)): índice, digest de salas, `dump-game`, `decode-boxes` (walkboxes BOXD/BOXM) y descompilado de guiones. Verificado sobre **MI1 Ultimate Talkie Edition** (86 salas, **748/748 guiones**, 150 costumes, 179 sonidos, 7 charsets, 1000 objetos) y MI2 UE (110 salas, **1006/1006 guiones**).
  - **Decodificador** (`decode/scumm_v5-decode.mjs` + `decode/decoders.mjs`): construye el **SIM v2** semántico (`decoded/sim.json`) — tamaño de sala, walkboxes con adyacencia, paleta (CLUT/EPAL), objetos (CDHD/IMHD con hotspots) y guiones por sala. MI1: 792 walkboxes y 1027 objetos; MI2: 966 walkboxes y 1145 objetos.
  - **Normalización a formatos estándar** (`decode/image.mjs`, `lib/png.mjs`, `decode/sof.mjs`): las imágenes (`RMIM` de sala y `IMxx` de objeto) se decodifican con los códecs del original (Major/Minor `gfx.cpp:5040`, zig-zag `gfx.cpp:4122/4154`, RAW256) y se exportan a **PNG indexado sin pérdidas** (`decoded/png/`, con tRNS en objetos) + manifiesto `decoded/images.json`. Verificado: **MI1 86/86 salas + 697 objetos** y **MI2 110/110 salas + 596 objetos**, con validación de visión local (escenas coherentes, sin artefactos). El audio de las ediciones Talkie (`.sof`, contenedor FLAC; `sound.cpp:1180-1200`) se extrae **sin recodificar** a `*.flac` (`decoded/audio/` + `manifest.json`); MI1: 4393 pistas (ffprobe: FLAC 44.1 kHz mono).
  - **Generador de contenido** (`codegen/emit-content.mjs`): SIM v2 → header C++ `constexpr` para el engine (`cook/cpp/<juego>_content.hpp`) con `eng::adv::RoomContent`/`find_room`. Verificado end-to-end compilando el header de MI1 contra el engine host (hit-test y walkbox reales de la sala 4).
  - **SIM** ([`sim/SCHEMA.md`](sim/SCHEMA.md), validador `sim/sim.mjs`): v1 (inventario) y v2 (semántico) con fixture sintético propio y **golden tests** (`sim/`, `decode/` y `codegen/`).
  - **Análisis**: `analyze/opcode-usage.mjs` → informe de uso de opcodes con nombres del oráculo (`reports/opcode-usage.json`).
  - **I1 (1/2) hecho**: parser `translate/parse.mjs` descumm → **AST JSON** (`adv-ast/1`; ficha en [translate/README.md](translate/README.md)) con cobertura **92.9 % (MI1)** y **91.5 % (MI2)**; el resto son líneas mal formadas de descumm, registradas con su motivo.
  - **I1 (2/2) hecho**: traductor `translate/emit.mjs` AST → **C++ de `eng::adv`** (`cook/cpp/scripts/`, contrato [`eng/adv/script_hooks.hpp`](../../engine/include/eng/adv/script_hooks.hpp)): **748/748 guiones MI1** y 1004/1006 MI2 generados (goto/unless → etiquetas C++; `raw` → primitiva no soportada contabilizada). Semántica mapeada: **17 864 llamadas** MI1 (9 341 no soportadas). Compilado y ejecutado contra `NullScriptWorld` (sala 4 y 10). Informe: `reports/translate-report.json`.
  - **Formato nativo Amiga ampliado** (`formats/STAGE_FORMAT.md`): **`adv-stage/1`** — display single/DPF/EHB/HAM, capas con scroll por hardware, parallax 8.8, transparencias por truco de color, **Copper Score** (cues con disparadores y keyframes), canales de sprite, BOBs, HUD, streaming y presupuesto A500; validador `formats/stage.mjs` + andamios en `edited/stages/` (86 + 110 salas generadas).
  - **A0 (núcleo) en el engine**: `eng/adv/navigation.hpp` (ruta por walkboxes con adyacencia geométrica y suavizado) y `eng/adv/camera.hpp` (dead-zone, bounds, paneo con easing) — **HOST-475** con `s16`/`float`/`Fixed`.
  - **Audio↔guiones**: `analyze/audio-cues.mjs` → `reports/audio-cues.json` (MI1: 326 cues/80 ids; MI2: 2593/153) con sala, guion y dirección; el enlace id→pista FLAC queda pendiente del parser `SOUN`.
  - **Análisis**: `analyze/opcode-usage.mjs` → informe de uso de opcodes con nombres del oráculo (`reports/opcode-usage.json`).
  - **Documentación local de artefactos**: cada carpeta del workspace lleva un **`README.md` generado** (ficha: qué es, quién lo produce/consume y relaciones); el mapa canónico está en [`WORKSPACE.md`](WORKSPACE.md) §«Relaciones entre artefactos».
- **Planificado (I0–I5)**: costumes (`COST` → frames PNG) para la cocina de assets (I2), frontend V3/V4 `.LFL` (copias Amiga), validador de presupuesto (I4) y la revisión semántica/IA opcional del traductor (I1).
- **Juego piloto**: **MI1** *Ultimate Talkie Edition* (V5) es el primer objetivo completo (workspace `out/adventure/mi1/`); MI2 UE segundo; Fate of Atlantis CD tercero (ver «Fuentes locales» del roadmap).
- **Referencia**: la VM del motor original está descrita en [`docs/reference/scumm/vm-v5.md`](../../docs/reference/scumm/vm-v5.md); la capa de aventura del engine, en [`docs/engine/architecture/ADVENTURE_LAYER.md`](../../docs/engine/architecture/ADVENTURE_LAYER.md).

## `build-scummvm-tools.sh`

Compila herramientas de **scummvm-tools** (GPL-3.0) desde las fuentes locales (`../SCUMM/scummvm-tools-2.9.0/`, ver «Fuentes locales» del roadmap) con el toolchain del host. Motivos frente a alternativas: los binarios oficiales de Windows dan **falsos positivos de antivirus**; el árbol completo `configure`+`make` arrastra todas las tools y sus librerías (wx, png, zlib, freetype); y una receta propia es **modificable** (parchear una fuente y reconstruir es inmediato).

```bash
# Desde la raíz del repo (Git Bash): compila descumm en ../SCUMM/scummvm-tools-2.9.0/descumm.exe
bash tools/adventure/build-scummvm-tools.sh

# Opciones: --src <dir> (fuentes), --out <dir> (binario), --help; entorno: CXX, CXXFLAGS
```

Notas:

- El binario se escribe **junto a las fuentes** (nunca en el repositorio; no hay binarios en git).
- La receta fija `-std=gnu++17` y enlaza estático (`-static`), para un ejecutable portable que no dependa de DLLs del toolchain.
- La herramienta se usa como **proceso externo** (los scripts del pipeline la invocan): nada de GPL se enlaza en `engine/` ni en el binario Amiga.

## Uso esperado en el pipeline

`descumm -5 -u script.bin` (o la versión correspondiente: `-3`, `-4`, `-6`, `-gNNN` para HE) descompila scripts SCUMM a texto legible; el frontend de I0/I1 lo invoca y parsea su salida hacia el SIM. Smoke test sin juego: `printf '\xde\xad\xbe\xef\x80\xa0' > t.bin && descumm -5 -u t.bin` debe imprimir `breakHere();` y `stopObjectCode();`.

## Herramientas de disco Amiga (ADF/HDF/WHDLoad)

El pipeline necesita **leer** las copias Amiga (ADF/WHDLoad) y, más adelante, **producir** discos para el Amiga. Todo lo necesario ya está en el entorno; se usa siempre como **proceso externo** (nada se enlaza en el engine ni en el binario Amiga):

| Necesidad | Herramienta | Comandos verificados |
|---|---|---|
| Leer/extraer/escribir ADF y HDF | **`xdftool`** (amitools, GPL-2.0-or-later; instalado vía `pip install amitools`; invocación `python -m amitools.tools.xdftool` o el shim `%APPDATA%\Python\Python3xx\Scripts\xdftool.exe`) | `xdftool disco.adf list` · `xdftool disco.adf type "Rooms/000.LFL"` · `xdftool disco.adf read "Rooms/000.LFL" <hostdir>` · `xdftool disco.adf unpack <dir>` (extrae todo) · crear/volcar: `xdftool nuevo.adf create + format "LABEL" + write <host> <amiga>` y `pack <dir>` |
| Crear ADF bootable desde un ejecutable | **`exe2adf.exe`** (plugin Bartman, GPL-3.0; `../vscode-amiga-debug/bin/win32/`) | `exe2adf -i juego.exe -l etiqueta -a juego.adf [-d <dir extra>] [-t readme] [-k]`; el plugin convierte antes ELF→hunk con `elf2hunk.exe` |
| Abrir `.zip` y `.lha` (paquetes WHDLoad) | **7-Zip** (`C:\Program Files\7-Zip\7z.exe`, LGPL) | `7z x pack.lha -o<dir>` · `7z x juego.zip -o<dir>` |
| Montar y ejecutar el resultado | **WinUAE** (ya en el entorno) | ADF/ADZ/DMS/IPF/ZIP/LHA como disquete |

Verificado (smoke): `xdftool` `list`/`type`/`unpack` sobre un ADF real (MI1 Amiga, disco 1) y ciclo completo `exe2adf` (desde un `.exe` de demo) → `xdftool list` del ADF bootable resultante.

**Datos de entrada de trabajo**: copias Amiga en ADF/WHDLoad (fuera del repositorio) y copias PC en `../SCUMM/Games/PC/` — MI1/MI2 *Ultimate Talkie Edition* y Fate of Atlantis CD son datos **SCUMM V5** (con habla añadida), Indy3 FM-Towns es V3 (`.LFL`). Dato de formato para I0: las copias Amiga V3/V4 guardan las salas en contenedores **`.LFL`** (`Rooms/000.LFL`, con índice y cifrado ligero) y las PC V5 en **`.000/.001`**; el parser de referencia es `engines/scumm/resource*.cpp` del oráculo.
