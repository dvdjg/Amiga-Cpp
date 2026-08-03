# Script de compilación

El script `scripts/build.sh` compila el proyecto desde la línea de comandos con distintos modos de optimización.

## Uso

```bash
./scripts/build.sh [opciones]
```

En Windows (CMD), usando Git Bash:
```cmd
scripts\build.bat [opciones]
```

## Opciones

| Opción | Descripción |
|--------|-------------|
| `--debug` | Depuración. Usa `-O0` y desactiva LTO. Los breakpoints en GDB funcionan correctamente. |
| `--size` | Optimiza para tamaño mínimo (`-Os` con LTO). Ideal para intros/demos donde el tamaño importa. |
| `--speed` | Optimiza para velocidad (`-Ofast` con LTO). Modo por defecto. |
| `--clean` | Ejecuta `make clean` antes de compilar. |
| `--program=P` | Nombre del binario sin extensión (por defecto: `out/a`). |
| `--machine=X` | `a500` (68000, por defecto), `a1200` o `cd32` (68020). Ver `config/winuae/README.md`. |
| `-j N` | Número de trabajos en paralelo (por defecto: 4). |
| `-h`, `--help` | Muestra la ayuda. |

## Ejemplos

```bash
./scripts/build.sh --debug              # Compilar para depurar con breakpoints
./scripts/build.sh --size               # Binario lo más pequeño posible
./scripts/build.sh --speed              # Binario lo más rápido posible (por defecto)
./scripts/build.sh --size --clean       # Limpiar y recompilar para tamaño
./scripts/build.sh --debug -j 8         # Depuración con 8 trabajos en paralelo
./scripts/build.sh --program=out/demo   # Salida en out/demo.elf, out/demo.exe
./scripts/build.sh --debug --machine=a1200
```

## Requisitos

- **AMIGA_BIN_PATH**: ruta del toolchain m68k-amiga-elf (gnumake, gcc, vasmm68k, elf2hunk).

  Si no está definido, el script intenta detectarlo en:
  - `%USERPROFILE%\.cursor\extensions\bartmanabyss.amiga-debug-*\bin\win32` (Windows)
  - `$HOME/.cursor/extensions/bartmanabyss.amiga-debug-*/bin/linux` o `bin/darwin` (Linux/macOS)

- **Bash**: en Windows se usa Git Bash (invocado por `build.bat`).

## Relación con el Makefile

El script llama a `make` con variables:

- `program=out/a` — destino
- `CFLAGS_OPT=-O0` (debug) | `-Os` (size) | `-Ofast` (speed)
- `LTO=0` (debug) | `1` (size, speed)

Equivalente manual:
```bash
make -j4 program=out/a CFLAGS_OPT=-O0 LTO=0    # debug
make -j4 program=out/a CFLAGS_OPT=-Os LTO=1    # size
make -j4 program=out/a CFLAGS_OPT=-Ofast LTO=1 # speed
```

## Depuración con F5

Para que los breakpoints detengan la ejecución en la extensión amiga-debug, compila con `--debug` o usa la configuración **"AROS (debug, breakpoints fiables)"**, que ejecuta la tarea `compile (debug)` antes de lanzar.

Véase [doc/diagnóstico-depurador-f5.md](diagnóstico-depurador-f5.md) y [docs/WINUAE-MCP-DEBUG.md](../docs/WINUAE-MCP-DEBUG.md).
