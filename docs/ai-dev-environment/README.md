# Entorno de desarrollo asistido por IA

Este árbol documenta las piezas que permiten trabajar sobre el engine Amiga 500 con una IA que compila, lanza, observa y depura una instancia viva de WinUAE. La fuente operativa sigue estando en los repositorios de código enlazados; este documento es el mapa de navegación y el registro de decisiones del entorno.

## Árbol

```text
ai-dev-environment/
├── README.md                 # mapa, contratos y estado resumido
├── AMIGA_DEV_GUIDE.md        # guía transversal: toolchain, run, debug (GDB+canal), MCP, capturas, perfil, Ollama
├── DOC-MAP-PRINCIPAL.md      # navegación IA → documentación (leer primero)
├── project-map.md            # archivos y proyectos de alto valor
├── ollama-local.md           # Ollama, modelos, scripts y operación segura
├── PROMPTS_VISION_LOCAL.md   # registro de prompts para visión local (cómo pedir)
├── session-evidence.md       # ciclo de sesión WinUAE y evidencias mínimas
└── mcp-debug-tools-audit.md  # utilidad de las modificaciones locales del MCP DAP
```

## Flujo canónico

```text
editar código
  -> build-demo.sh
  -> run-demo.sh / demo-menu.sh
  -> WinUAE-DBG + GDB :2345
  -> canal lateral :2346
  -> memoria/registros/captura/postmortem
  -> análisis determinista
  -> Vision Review con Ollama (opcional)
  -> evidencia y actualización documental
```

## Contratos que no deben mezclarse

- El depurador GDB de VS Code/Bartman usa el puerto `2345` y es una conexión de un solo cliente.
- El canal lateral de WinUAE-DBG usa `127.0.0.1:2346` y permite observación concurrente sin robar el socket GDB.
- Un ejecutable AmigaDOS se carga por ADF/devfs/loader DOS; no debe tratarse como un payload bare-metal.
- Un payload `metal/direct` requiere una entrada, pila y contexto de registros controlados; para iteración gráfica es hoy la ruta más directa.
- La IA debe usar `observe` para leer, `assist` para entrada/perfilado y `takeover` solo para escrituras reversibles o pausa controlada.

## Estado de la máquina de referencia

El estado comprobado de una instalación operativa (versiones de Ollama, binarios activos de WinUAE-DBG con sus hashes y la ROM A500) se mantiene **verificado y fechado** en [`docs/debugging/system/setup-nuevo-equipo.md`](../debugging/system/setup-nuevo-equipo.md), junto a la guía de reinstalación. Aquí no se duplica: este README es el mapa del entorno, no su inventario.

## Lectura inicial para una nueva IA

El orden de lectura de arranque es **único** y vive en `AGENTS.md` («Cómo usar este documento»); su router y el `DOC-MAP-PRINCIPAL.md` remiten a todo lo demás. Recursos del **entorno** que un hilo nuevo debe tener localizados:

- `docs/emulation/mcp-live-coding-workflow.md` — flujo vivo de edición/depuración con MCP.
- `docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md` — canal lateral: contratos y comandos.
- `docs/ai-dev-environment/project-map.md` — archivos y proyectos de alto valor.
- `docs/ai-dev-environment/ollama-local.md` — visión local: modelos y operación segura.
- `docs/ai-dev-environment/session-evidence.md` — ciclo de sesión y evidencias mínimas.
- `docs/ai-dev-environment/AMIGA_DEV_GUIDE.md` — entorno completo (compilar por máquina, WinUAE, GDB/canal lateral, MCP, capturas, perfil, Ollama).

## Fuentes hermanas

- `../mcp-winuae-emu`: servidor MCP y cliente GDB RSP.
- `../WinUAE-DBG`: fork de WinUAE con stub GDB y comandos monitor.
- `../mcp-debug-tools`: diseños de MCP/DAP y herramientas de integración VS Code.
- `../Universal-Asset-Format`: perfiles OCS, documentación de assets y precedentes de evidencia reproducible.
- `../ACE`: técnicas y documentación de programación Amiga reusable como referencia.
- `D:/scripts`: patrón real de operación Ollama, reanudación, watchdog y panel.

La clasificación de vídeos de `D:/scripts` es un proyecto externo y no forma parte del engine. Aquí solo se reutiliza Ollama como servicio local de visión para describir capturas y secuencias de juegos sin enviar imágenes a un proveedor cloud.
