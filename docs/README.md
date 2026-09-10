# Documentación de Amiga-Cpp

Índice maestro del árbol documental del proyecto. Este árbol está organizado por
**tema y rol** (no por origen) para que pueda crecer sin reordenarse: cada
documento nuevo debe ir a la carpeta de su tema. La organización del repositorio
completo (fuentes, demos, assets, tools, out/, etc.) está especificada en
[STRUCTURE.md](STRUCTURE.md) — **léela antes de crear cualquier archivo**.

El proyecto construye un **engine de juegos retro con conectores para distintas
plataformas** (Amiga, Atari ST, Megadrive), empezando por Amiga 500. El
repositorio mantiene dos flujos claramente separados:

- **Engine C++23 (`engine/`, `demos/`, `tools/`)**: flujo activo, con
  build/run/analyze orquestados por bash + Node y validación determinista
  (canal lateral, pixel assertions, secuencias, FrameScope, Vision Review).
- **Proyecto C legado (`legacy/`)**: demo Amiga en C con música ProTracker, con
  su propio `legacy/Makefile` y config de depuración F5 en `legacy/.vscode/`. No
  mezclar con el engine.

## Cómo empezar

1. **Organización del repo (obligatorio)**: [STRUCTURE.md](STRUCTURE.md).
2. **Navegación IA → documentación (leer primero como IA)**: [ai-dev-environment/DOC-MAP-PRINCIPAL.md](ai-dev-environment/DOC-MAP-PRINCIPAL.md).
3. **Para continuar el trabajo**: [CONTINUATION_CONTEXT.md](CONTINUATION_CONTEXT.md)
   (estado del proyecto y orden de lectura).
4. **Para operar build/run/analyze**: [build/BUILD_AND_RUN.md](build/BUILD_AND_RUN.md).
5. **Para entender el engine**: [engine/README.md](engine/README.md).

## Estructura del árbol

| Carpeta | Contenido |
|---------|-----------|
| [STRUCTURE.md](STRUCTURE.md) | **Especificación canónica de organización del repositorio** (dónde va cada cosa). |
| [engine/](engine/README.md) | Diseño del engine C++ actual: estilo, drivers gráficos, memoria, política de hardware/ROM, benchmarks de API y roadmap (en `engine/architecture/`) + histórico del engine C (`engine/c-engine/`). |
| [demos/](demos/README.md) | Efectos demoscene (`demos/effects/`) y pipeline de tiles/EHB (`demos/tile-pipeline/`, con informes IA en `ai-reports/`). |
| [tools/](tools/README.md) | Documentación de las herramientas de desarrollo (build/run/analyze/debug/profile/verify/pipeline). |
| [reference/](reference/README.md) | Referencia por plataforma: `reference/amiga/` (hardware + técnicas), `reference/ahrm/` (AHRM 3.ª edición), `reference/amc-wrobel/`, y áreas futuras `atarist/` y `megadrive/`. |
| [guides/](guides/README.md) | Roadmaps vigentes (`guides/roadmap/`), guías de optimización (`guides/optimization/`) y metodología/agentes (`guides/methodology/`). |
| [build/](build/README.md) | Toolchain, build CLI, artefactos y formatos de disco. |
| [emulation/](emulation/README.md) | WinUAE, extensión amiga-debug, MCP, canal lateral, perfiles y automatización del emulador. |
| [debugging/](debugging/README.md) | Sistema de depuración WinUAE-DBG (arquitectura, arreglos) y guías de depuración con IA. |
| [testing/](testing/README.md) | Validación: pixel assertions, secuencias de frames, FrameScope y Vision Review. |
| [ai-dev-environment/](ai-dev-environment/README.md) | Mapa del entorno IA: MCP/WinUAE, canal lateral, evidencias, Ollama local y **doc-map principal de navegación IA → documentación** ([DOC-MAP-PRINCIPAL.md](ai-dev-environment/DOC-MAP-PRINCIPAL.md)). |
| [legacy/](legacy/README.md) | Notas históricas de troubleshooting con ortografía irregular, en cuarentena. |

## Procedencia del contenido

Parte de esta documentación procede del repo hermano **`Cursor-Amiga-C`** (engine
en C con batería de pruebas y sistema de depuración WinUAE-DBG). Ese material se
incorporó **por tema**:

- Conocimiento reutilizable (hardware, técnicas, depuración, WinUAE/MCP,
  metodología) se fusionó con los documentos de este repo en las carpetas
  correspondientes.
- Los documentos de **arquitectura del engine C** (diseño, roadmaps, migraciones,
  batería de pruebas C) quedaron en [engine/c-engine/](engine/c-engine/README.md)
  como historial y prior art.
- Las notas de troubleshooting antiguas con ortografía irregular quedaron en
  [legacy/](legacy/README.md).

> Los enlaces internos de los documentos importados pueden apuntar a la estructura
> del repo original (p. ej. `app/`, `tests/amiga-battery/`, `scripts/`). Trátalos
> como referencia histórica; la fuente de verdad operativa es este repositorio.

## Créditos y atribución

Código e ideas tomadas de terceros, con su licencia. Si incorporas código de una
fuente nueva, añádela aquí en la misma pasada.

### Audio

- **Jeroen Knoester (Photon)** — *Audio Mixer 3.7* (motor SFX por software,
  `support/audio_mixer/`; powerprograms.nl). Integrado de forma nativa. Ver
  `engine/architecture/AUDIO_MIXER.md`.
- **Photon/Scoopex** — *P61* (formato y playroutine 6.1), `demoscene-repo-orig/lib/libp61`.
- **Frank Wille (phx)** — *ProTracker playroutine 6.4* (`ptplayer`), dominio público.
- **Arnaud Carré (Leonard/Oxygene)** — *LightSpeedPlayer (LSP)*.
- **agermose** — división sin signo 32/32 usada por los plugins del mixer.
- **h0ffman, nivrig, KaiN, Jobbo, McGeezer** — AmigaGameDev Discord (apoyo al mixer).

### Input

- **ACE (Amiga C Engine)** — referencia de decodificación del joystick
  (`eng/platform/input_poll.hpp`).
- **Sevgi_Engine** — referencia de entrada/CD32 (protocolo `POTGO`).

### Gráficos / efectos demoscene

- **demoscene-repo / demoscene-repo-orig** — rutinas asm portadas (`support/`:
  c2p, fuego, depacker) y librerías de referencia. Procedencia detallada en
  `demos/effects/`.

### Ejemplos y assets del mixer original

- **Roald Strauss** — música de los ejemplos del mixer.
- **freesound.org** — efectos de sonido de ejemplo.
- **Henrik Erlandsson** — código de arranque de los ejemplos.

### Referencia técnica

- **Commodore Amiga Hardware Reference Manual** (3.ª ed., `reference/ahrm/`).
- **amiga-bootcamp** (técnicas y hardware, repo hermano).

## Dónde va cada documento nuevo

| Si el documento trata de... | va en... |
|-----------------------------|----------|
| Diseño, API, drivers, memoria del engine C++ | `engine/architecture/` |
| Arquitectura o roadmaps del engine en C (histórico) | `engine/c-engine/` |
| Efectos demoscene y qué enseña cada demo | `demos/effects/` y README de la demo |
| Pipeline de tiles/EHB e informes IA | `demos/tile-pipeline/` |
| Documentación de herramientas | `tools/` |
| Registros, DMA, chipset, ABI, loader del Amiga | `reference/amiga/hardware/` |
| Una técnica concreta de composición/efecto | `reference/amiga/techniques/` |
| Atari ST (futuro) / Megadrive (futuro) | `reference/atarist/` / `reference/megadrive/` |
| Compilar, toolchain, artefactos, ADF/HDF | `build/` |
| WinUAE, MCP, canal lateral, perfiles, hot-reload | `emulation/` |
| Depuración (gdbserver, breakpoints, DAP/MI/RSP) | `debugging/` |
| Validación automática de frames/píxeles | `testing/` |
| Procesos, agentes IA, runbooks | `guides/methodology/` |
| Roadmaps de desarrollo activos | `guides/roadmap/` |
| Optimización 68000/C++ | `guides/optimization/` |
| Fuentes externas (manuales, cursos) | `reference/` |
| Notas históricas sin limpiar | `legacy/` |
| Operación IA, Ollama, evidencias y mapa de proyectos | `ai-dev-environment/` |
| Navegación IA → documentación (qué leer antes de cada tarea) | `ai-dev-environment/DOC-MAP-PRINCIPAL.md` |

> **Documentos nuevos → enlazarlos SIEMPRE aquí (o en AGENTS.md).** Un documento
> sin enlace desde este índice o desde `AGENTS.md` se pierde para futuras sesiones.