# Herramientas del pipeline de aventuras (`tools/adventure/`)

Infraestructura **host** (PC) del pipeline de conversión de aventuras clásicas al engine: el plan y las decisiones están en [`ROADMAP_ADVENTURE_IMPORT.md`](../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md) (hitos I0–I5) y el diseño de la capa destino en [`ADVENTURE_LAYER.md`](../../docs/engine/architecture/ADVENTURE_LAYER.md).

## Estado

- **Hecho**:
  - Oráculo compilado en local (`build-scummvm-tools.sh`: `descumm`) y toolchain de discos Amiga verificado (§Herramientas de disco).
  - **Frontend SCUMM V5** (`frontends/scumm_v5/extract.mjs`, con su [README](frontends/scumm_v5/README.md)): índice, digest de salas, volcado por sala y descompilado de guiones. Extracción completa verificada de **MI2 Ultimate Talkie Edition**: 127 salas en el índice (110 con datos), **1006 guiones descompilados al 100 %** con `descumm`, 163 costumes, 229 sonidos, 9 charsets, 1200 objetos globales.
  - **SIM v1** ([`sim/SCHEMA.md`](sim/SCHEMA.md) + validador `sim/sim.mjs`) con **fixture sintético** (`sim/make-fixture.mjs`) y golden test: `node --test tools/adventure/sim/sim.test.mjs`.
- **Planificado (I0–I5)**: decodificación semántica (walkboxes/costumes/imagen — I0/I2), frontend V3/V4 `.LFL` (copias Amiga), traductor de guiones (`translate/`, I1) y validador de presupuesto (`report/`, I4).
- **Juego piloto**: MI2 *Ultimate Talkie Edition* (V5) como primer objetivo completo; MI1 UE segundo; Fate of Atlantis CD tercero (ver «Fuentes locales» del roadmap).

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
