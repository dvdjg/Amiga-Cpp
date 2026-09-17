# Perfilar desde el agente (sin VSCode)

La extensión `bartmanabyss.amiga-debug` perfila con **F5 → "Frame Profiler"**, que internamente hace tres cosas: genera una **tabla de unwind** desde el ELF, arranca WinUAE con GDB y envía `monitor profile N "<unwind>" "<bin>"`. Todo eso es reproducible por línea de comandos, así que **el agente puede perfilar sin abrir VSCode**.

```
   agente                          WinUAE + GDB                     ficheros
┌───────────────────┐   monitor    ┌──────────────┐   escribe   ┌──────────────────────┐
│ winuae-profile.mjs│─────────────▶│  profile N   │────────────▶│ out/tmp/wprof-*.bin  │
│  · conecta GDB    │   profile N  │  ".unwind"   │             │  out/tmp/wprof-*.unwind
│  · arranca demo   │              └──────────────┘             └──────────┬───────────┘
│  · bateria UNWIND │                                                       │
└───────────────────┘                                                       ▼
                                                            ┌───────────────────────────┐
                                                            │ profile-samples.mjs       │
                                                            │  parsea bin + resuelve PC │
                                                            │  con el .map -> top rutina│
                                                            └───────────────────────────┘
```

## Comandos

```bash
# 1) capturar (ciclos ocupada/libre, DMA por tipo y, si hay unwind, muestras de CPU)
node tools/debug/winuae-profile.mjs <demo> [CONFIG] [frames]

# 2) resolver las muestras a rutinas
node tools/analyze/profile-samples.mjs out/tmp/wprof-<demo>-<CONFIG>.bin <demo> [CONFIG] [--top N] [--json]

# 3) secciones instrumentadas del engine (sin emulador de por medio)
node tools/debug/profile.mjs <demo> [CONFIG]
```

Antes de capturar, matar instancias huérfanas de WinUAE (bloquean los puertos GDB 2345/2346) y asegurarse de que el binario y su `.map`/`.elf` existen en `out/demos/<demo>/<CONFIG>/`.

## La tabla de unwind

El comando `monitor profile N "" bin` produce **0 muestras de CPU**: WinUAE necesita un `.unwind` que le diga cómo desenrollar la pila para atribuir la muestra a la función. `tools/debug/winuae-profile.mjs` lo construye portando la `UnwindTable` del plugin:

1. `objdump --dwarf=frames-interp <elf>` (m68k-amiga-elf-objdump del toolchain).
2. Parsear CIE/FDE y, para cada 2 bytes de `.text`, derivar `{ (cfaReg<<12)|cfaOfs, r13, ra }` (3 int16).
3. Escribir ese array al fichero `.unwind` que se pasa al comando `profile`.

Sin errores, el log imprime `[wprof] unwind: … (.text N bytes)`. Si aun con la tabla el binario sigue con 0 muestras, el problema está en la configuración de muestreo de WinUAE (opción del emulador), no en la tabla: es el item abierto registrado en `METODOLOGIA_PROFILING.md` §5.

## Por qué no se reutiliza directamente el parser del MCP

`parseProfile` del MCP lee del binario **ciclos y DMA pero descarta las muestras** (avanzaba `profileCount*4` sin leer el array). Ya está corregido en el MCP: `ProfileFrame.profileArray` conserva los PCs y `profile-ollama` los resume por sección/PC caliente. Para nombres de función hace falta además el `.map` (o el ELF), que el MCP no conoce: por eso `tools/analyze/profile-samples.mjs` resuelve los PCs con el `.map` del build y da la tabla por rutina que se le puede pasar a Ollama en modo texto.

## Alternativa: perfiles de VSCode

Si el perfil se tomó con el depurador del plugin y existe como `.amigaprofile` (JSON con árbol de llamadas y `hitCount`, una entrada por frame), analizarlo con:

```bash
node tools/analyze/profile-report.mjs <perfil.amigaprofile> [--top N] [--json]
```

Ojo: los perfiles tomados con el depurador incluyen sus IRQs y no reflejan un run limpio; para medidas de rendimiento, usar el camino de línea de comandos.
