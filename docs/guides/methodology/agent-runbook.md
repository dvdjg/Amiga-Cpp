# Runbook para el agente (desarrollo Amiga en Cursor-Amiga-C)

Guía para que el agente sea autónomo en desarrollo y depuración. Solo escalar al usuario en bloqueos o decisiones (ver `development-methodology.md` §5.1).

## Responsabilidad del agente

- **No parar el desarrollo** por cada pasito ni por pequeños detalles; seguir hasta dejar el objetivo hecho. Solo avisar al usuario en bloqueos o decisiones (ver abajo).
- Implementar y corregir código en fases verificables.
- Asegurar que el proyecto compila (la compilación la lanza el usuario con la tarea "compile" o el agente puede sugerirla; si el usuario pega salida de error, el agente corrige).
- Depurar de forma autónoma cuando algo falle: usar MCP Debug Tools, scripts de análisis, overlay, etc.
- Crear scripts o herramientas cuando faciliten el trabajo (parsing de logs, comprobaciones, generación de datos de prueba).
- Documentar en `doc/engine-migration-log.md` (o similar) las fases, lo aprendido y el porqué de cambios relevantes.

## Roadmap Amiga (MCP, batería, engine): sistema de agentes

- **Roles, supervisión, Definition of Done y plantillas de prompt:** [agent-system-roadmap.md](agent-system-roadmap.md) (Orquestador G0, MCP G1, infra batería G2, efectos G3, engine G4, QA G5).
- **Estado PENDIENTE / PARCIAL / HECHO por ID:** [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md). No dar por cerrada una tarea del roadmap sin actualizar esa tabla en el mismo cambio.
- **Especificación de pruebas y visión IA:** [amiga-test-battery-spec.md](amiga-test-battery-spec.md).
- **Prompt maestro y contrato close-to-the-metal:** [amiga-lowlevel-agent-prompt.md](amiga-lowlevel-agent-prompt.md) y [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md). Para tareas que mezclen display, copper, blitter, sprites o timings, rellenar antes el contrato tecnico y ejecutar por fases.

## Compilación y verificación

- **En el IDE:** tarea "compile" (ejecuta `run-make.sh` con AMIGA_BIN_PATH inyectado por la extensión Amiga).
- **Valor de AMIGA_BIN_PATH:** Lo proporciona el comando `amiga.bin-path` de la extensión **Amiga C/C++ Compile, Debug & Profile** (BartmanAbyss). Ese comando devuelve `extensionPath/bin/<platform>`. En Windows 11 la extensión está en `%USERPROFILE%\.cursor\extensions\bartmanabyss.amiga-debug-*\bin\win32`. El script `scripts/verify-build.sh` intenta detectar esa ruta si `AMIGA_BIN_PATH` no está definido; el agente puede ejecutar `bash scripts/verify-build.sh` para compilar sin configurar nada.
- **Tras cambiar código:** el agente deja el código listo; si hay errores de compilación (usuario los pega o aparecen en terminal), el agente los corrige sin pedir más pasos de depuración manual.
- **Verificación obligatoria:** El agente debe **ejecutar la compilación** cuando pueda (`bash scripts/verify-build.sh` o tarea "compile"). No dar por válido un cambio sin compilar. Además, tras cambios que afecten al runtime debe **lanzar la aplicación** (no solo compilar): en Windows, `powershell -ExecutionPolicy Bypass -File run-and-capture.ps1 -Seconds 6` (compila, abre WinUAE unos segundos, cierra) o `run-without-debug.ps1`; así se comprueba que el ejecutable arranca. Para cambios en **copper, DMA, VBL o display**, el usuario debe confirmar en WinUAE que se ven gráficos y se oye la música; si el agente no puede ver la ventana, dejarlo documentado (ver `doc/engine-migration-log.md` Fase 4 revertida).

## Lanzar la aplicación y depurar (WinUAE)

- **Depuración con MCP winuae-emu (recomendado en este proyecto):** usar `winuae_connect` para lanzar WinUAE y conectar por GDB RSP; luego `winuae_load <ruta/out/a.exe>` para cargar el binario. Requiere tener **winuae-gdb.exe** instalado (descarga: https://github.com/BartmanAbyss/vscode-amiga-debug/tree/master/bin/win32; colocar en la ruta que indica el MCP al fallar `winuae_connect`). Con eso se pueden usar breakpoints, lectura de memoria/registros y paso a paso. Ver regla `amiga-verification-flow.mdc`.
- **Alternativa F5 (extensión Amiga):** si el usuario prefiere, puede lanzar con F5 usando la extensión Amiga (config "Amiga 500"); eso usa su propio cliente GDB contra WinUAE. No usar a la vez MCP winuae-emu y F5 (solo una conexión GDB).
- **Ver que la app está en el bucle:** con la sesión en marcha, usar `pause` → `get-call-stack`; debe verse algo como `main` → `demo_run_loop` → `demo_do_one_frame` → `Wait10` → `engine_wait_line`. Luego `continue` para seguir.
- **Capturas de pantalla / perfil:** La extensión tiene **Frame Profiler**: durante la sesión de debug, en la barra del depurador se puede pulsar **Profile** (1 frame) o **Profile (Multi)** (50 frames). La extensión genera un `.amigaprofile` en `%TEMP%` (p. ej. `amiga-profile-YYYY.MM.DD-HH.MM.SS.amigaprofile`) con perfiles y, en multi-frame, **capturas embebidas** (JPEG base64). El agente puede:
  - Analizar el perfil: `scripts/parse-amigaprofile.sh <ruta>` o `scripts/parse-latest-amigaprofile.sh`.
  - Extraer capturas a PNG: `scripts/parse-amigaprofile.sh <ruta> screenshots` (segundo argumento = directorio de salida); así el agente puede **ver** los frames capturados.
- Si el agente no puede invocar el botón Profile por UI, puede pedir al usuario que pulse Profile tras haber lanzado con `start-debug`; después el agente usa `parse-latest-amigaprofile.sh` y opcionalmente extrae capturas para comprobar gráficos.

## Depuración (autónoma)

- El depurador se puede **pausar/parar cuando convenga** para inspeccionar o depurar. Sin problema.
- **MCP winuae-emu (depuración Amiga):** usar para depurar programas Amiga en WinUAE:
   - `winuae_connect` (requiere winuae-gdb.exe instalado; ver arriba).
   - `winuae_load <ruta/out/a.exe>` para cargar el binario.
   - `winuae_breakpoint_set <dirección>`, `winuae_continue`, `winuae_pause`, `winuae_step`, `winuae_registers_get`, `winuae_memory_read`, `winuae_custom_registers`, etc.
   - Flujo completo en regla `amiga-verification-flow.mdc`. (dap-proxy está desactivado en este proyecto para depuración Amiga.)
2. **Overlay WinUAE:** usar `engine_debug_*` para mostrar datos útiles en pantalla (frame, estado, variables); el usuario los ve en WinUAE sin depurar paso a paso. Ver `doc/demoscene-effects-integration.md` § Overlay.
3. **Perfil (.amigaprofile):** ver sección anterior (Profile en barra de depuración; parse-amigaprofile.sh y extracción de capturas).
4. **Plan B (Coppenheimer):** si el depurador falla, usar browser MCP para abrir Coppenheimer, inspección memoria/DMA; ver `doc/debug-with-ai.md` y `doc/coppenheimer-ui.md`.
5. **Mismo poder que la extensión (imagen, Custom, memoria):** Este proyecto usa una **copia local** de mcp-debug-tools (en `../mcp-debug-tools` respecto a Cursor-Amiga-C) con herramientas extra para el adaptador Amiga: `read-memory` (dirección + longitud), `read-registers`, `read-register-list`, `execute-command` (comando GDB/monitor). Tras cambiar el fuente: `npm run compile` en mcp-debug-tools. Para que Cursor use la extensión con estas herramientas: **Instalar desde VSIX**: en mcp-debug-tools ejecutar (con package.json temporalmente sustituido por package-vscode.json) `npx @vscode/vsce package --no-dependencies`, luego en Cursor *Extensions → ⋮ → Install from VSIX* y elegir `mcp-debug-tools-0.2.1.vsix`; recargar la ventana. `.cursor/mcp.json` apunta al CLI local. Ver `doc/winuae-extension-internals.md`.

## Cuándo avisar al usuario

- **Bloqueo:** no se puede seguir sin que el usuario haga algo en su entorno (ej. ejecutar compilación y compartir error, instalar algo, subir una captura concreta).
- **Decisión:** habilitar un MCP, instalar software, elegir una opción de diseño con impacto (ej. cambiar estructura del engine).

## Herramientas propias

El agente puede añadir scripts en `scripts/` (o similar) para:
- Parsear salida de compilación o logs.
- Analizar `.amigaprofile` (ya existen `parse-amigaprofile.sh`, `parse-latest-amigaprofile.sh`).
- Generar datos de prueba o fixtures.
- Comprobar coherencia del código (incluye, símbolos, etc.) cuando no tenga make en PATH.

No es necesario pedir permiso para crear estos auxiliares; forman parte del desarrollo autónomo.

---

## Chat vs Agent y botón "New Agent"

En Cursor tienes **Chat** (conversación por mensajes) y **Agent** (el mismo chat pero en modo agente). El botón **"New Agent"** a la derecha abre una **nueva conversación** en la interfaz de Agent.

### Qué es el modo Agent (y Composer)

- **Agent** es el modo por defecto para tareas de código complejas. Según la documentación de Cursor:
  - El agente **explora el código de forma autónoma**, edita varios archivos, ejecuta comandos y corrige errores para completar lo que pides.
  - Tiene **todas las herramientas habilitadas** (búsqueda, edición, terminal, etc.).
  - Es el modo adecuado para desarrollo: le das un objetivo y él intenta llevarlo a cabo en una o varias “vueltas” (cada respuesta puede incluir muchos pasos).

- **Composer** es el nombre del modelo/entorno de Cursor para este tipo de trabajo (agente que hace muchas acciones por turno). En la práctica, cuando usas el panel de Agent (o "New Agent") sueles estar usando Composer/Agent.

- **Modos dentro del Agent:** en el desplegable del Agent puedes elegir:
  - **Agent:** tareas complejas, refactors; exploración autónoma y edición multiarchivo.
  - **Ask:** solo lectura; para aprender o planificar sin que cambie nada.
  - **Plan:** para features muy grandes; primero crea un plan que tú revisas y luego “Build” para que lo ejecute.
  - **Debug:** para bugs difíciles; genera hipótesis, añade logs y usa información en tiempo de ejecución.

### Cómo usarlo para que “no pare en nimiedades”

1. **Usa el Agent** (el panel que abres con "New Agent" o la pestaña Agent), no solo un chat rápido.
2. **Da un solo objetivo claro** por “tanda”, por ejemplo: *“Completar el engine según doc/engine-architecture.md en fases; no parar hasta que esté hecho salvo bloqueo o decisión.”*
3. El agente aplica la regla **máximo trabajo por turno** (`.cursor/rules/max-work-per-turn.mdc`): en cada respuesta hace muchas acciones y solo para cuando realmente termina ese turno o hay bloqueo/decisión.
4. Si en una respuesta no se ha llegado al final del objetivo, **escribes “sigue”** (o “continúa con lo que falta”) y el agente sigue en el siguiente turno sin parar por detalles.

Así, “no parar” significa: el agente no interrumpe el desarrollo por pasos pequeños; hace todo lo que puede en cada turno y tú solo das otro turno con “sigue” cuando haga falta.

---

## Modo continuo y sistema jerárquico (Roo Code vs Cursor)

### ¿Cursor tiene trabajo continuo nativo?

**No.** Cursor no tiene un modo “ejecuta hasta que el objetivo esté completo” en un solo run. Cada respuesta del agente es un turno; cuando termina, la conversación se detiene hasta que haya un nuevo mensaje. Por eso “sigue” tiene que venir de fuera (tú o una integración).

### ¿Se puede montar algo tipo Roo Code (planificador + tareítas)?

En **Roo Code** hay un **Planner** (descompone en plan) y **Coder** (ejecuta); el orquestador reparte tareas hasta terminar. En Cursor:

- **Subagents:** Cursor tiene **subagentes**: un agente padre puede delegar partes del trabajo en agentes especializados (con prompts y herramientas propias), que se ejecutan en paralelo. No es un “planner” explícito como en Roo, pero sí una jerarquía padre → subagentes. Se configura desde la documentación de Cursor (Context → Subagents).
- **Plan mode:** El modo **Plan** hace que el agente primero genere un plan (que tú revisas) y luego “Build” para ejecutarlo. Es parecido a “planificador + una ronda de ejecución”, pero no lanza “tareítas” en bucle hasta terminar; es una sola ejecución del plan.
- **Conclusión:** Puedes acercarte a un flujo “plan → ejecución por partes” con **Plan mode** + **subagents**, pero un bucle automático “planificador lanza tareítas hasta terminar” como en Roo no está preconstruido. Lo más cercano es usar la **Background Agents API** (ver abajo) y que un orquestador externo envíe muchos “turns” (siguientes tareas o “sigue”) hasta que el trabajo esté hecho.

### Automatizar "sigue" con integración externa (LM Studio u otro)

**Sí.** Cursor expone la **Background Agents API** (agentes en la nube, no en el IDE):

1. **API key:** En [Cursor Dashboard](https://cursor.com/dashboard) → pestaña **Background Agents** obtienes una API key.
2. **Lanzar agente:** `POST https://api.cursor.com/v0/agents` con el prompt inicial (objetivo del desarrollo), repositorio, etc.
3. **Webhooks:** Al crear el agente puedes registrar una URL; Cursor envía **POST** a esa URL en cambios de estado (por ejemplo cuando el agente termina un turno). Documentación: [Background Agent API – Webhooks](https://docs.cursor.com/en/background-agent/api/webhooks).
4. **Follow-up:** La API tiene un endpoint para **añadir un mensaje de seguimiento** al agente (equivalente a escribir “sigue” en el chat). Documentación: [Add follow-up](https://docs.cursor.com/en/background-agent/api/add-followup).

Flujo posible con **LM Studio haciendo tu rol**:

1. Un **servidor local** (Python, Node, etc.) que tú ejecutas:
   - Lanza el agente con `POST /v0/agents` (objetivo: “Completar el engine según doc/…; no parar salvo bloqueo o decisión”).
   - Registra un webhook que apunte a tu servidor.
2. Cuando Cursor envía el webhook “agent finished turn” (o estado equivalente), tu servidor:
   - Obtiene la última respuesta del agente (según lo que exponga la API, p. ej. resumen o transcript).
   - Envía ese contenido a **LM Studio** (tu modelo local) con un prompt tipo: *“Eres el supervisor. El agente de Cursor ha respondido con: [respuesta]. ¿El trabajo está completo? Si no, responde solo: sigue. Si hay bloqueo o decisión para el humano, dilo.”*
   - Si LM Studio responde “sigue” (o similar), tu servidor llama al endpoint **add follow-up** con el texto “sigue” (o una variante).
3. El agente de Cursor recibe el follow-up, hace otro turno, y el ciclo se repite hasta que LM Studio considere que está hecho o que hay bloqueo/decisión.

Así **sí puedes automatizar el “sigue”** y que una IA en LM Studio actúe como supervisor que decide cuándo seguir y cuándo parar. Necesitas:

- Cuenta Cursor con acceso a Background Agents API.
- Un pequeño servicio que reciba webhooks, hable con LM Studio (HTTP o el SDK que uses) y llame a la API de Cursor para follow-ups.
- Revisar en la [documentación oficial](https://docs.cursor.com/en/background-agent/api) los nombres exactos de los endpoints (launch agent, add follow-up, webhook payload) porque pueden cambiar.

Resumen: **no hay modo continuo nativo en Cursor**, pero **sí puedes montar un orquestador externo** (por ejemplo con LM Studio) que reciba eventos del agente y envíe “sigue” por API hasta que el desarrollo esté hecho o haya que avisar al humano.

### Programa nativo en Windows que use la notificación y ponga "sigue"

Cuando el agente termina un turno **en el IDE**, Windows puede mostrar una notificación. La idea: un programa en Windows que detecte esa notificación, analice tu resultado y envíe “sigue”.

Hay **dos escenarios**:

#### 1. Agente en la nube (Background Agents API) — recomendado

Aquí **no hace falta** reaccionar a la notificación de Windows. El desencadenante es el **webhook** que Cursor envía a tu servidor cuando el agente termina un turno.

- Un **programa nativo en Windows** (o un servicio en Python/Node/C#) que:
  - Exponga una **URL local** (p. ej. con ngrok o un túnel) para que Cursor pueda llamar al webhook, **o** que use un servidor con URL pública.
  - Al recibir el POST del webhook (agent finished turn), lea el payload (o pida por API la última respuesta del agente).
  - Analice el contenido (reglas fijas o **LM Studio**: “¿Está completo? Si no → sigue”).
  - Si debe continuar, llame al endpoint **add follow-up** de la API de Cursor con “sigue”.
- Así el “cuando terminas” lo marca **Cursor vía webhook**, no la notificación de Windows. El programa solo escucha HTTP y habla con la API y con LM Studio.

#### 2. Agente local en el IDE (el que usas con “New Agent”)

En este caso **no existe una API pública** para “enviar un mensaje al chat actual” desde fuera. Las opciones son:

- **Opción A (frágil):** Un programa nativo en Windows que:
  1. **Detecte la notificación** de “agent finished” (p. ej. leyendo toasts de Windows 10/11 con APIs de notificaciones o comprobando si aparece un texto concreto).
  2. Cuando la detecte, **simule entrada de teclado**: llevar el foco a la ventana de Cursor (UI Automation o FindWindow + SetForegroundWindow) y enviar “sigue” + Enter (SendInput / SendKeys).
  - Inconvenientes: depende del layout de Cursor, del foco, del idioma del teclado; se rompe si Cursor cambia de UI. Solo tiene sentido si quieres seguir usando **solo** el agente del IDE y no la API.

- **Opción B (recomendada):** Usar **Background Agents API** (agente en la nube) y el programa en Windows que recibe webhooks y pone “sigue” como en el apartado 1. No usas la notificación de Windows como trigger; el trigger es el webhook. El programa puede ser el mismo “servicio local” que recibe el POST y llama a LM Studio + add follow-up.

En resumen: **sí es posible** tener un programa en Windows que analice resultados y envíe “sigue”; la forma robusta es que ese programa hable con la **Background Agents API** (webhook al terminar turno + add follow-up) y opcionalmente use LM Studio para decidir. Usar la notificación de Windows como disparador solo tiene sentido para el agente local del IDE y es más frágil (simular teclado).
