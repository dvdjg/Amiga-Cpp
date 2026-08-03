# Metodología de desarrollo incremental y verificable

Documento general que recoge principios, procedimientos y herramientas para desarrollar software de forma incremental, con pruebas parciales y capacidad de depuración y análisis en cada etapa. Aplicable a cualquier proyecto; las estrategias concretas varían según la plataforma (C/Amiga, Salesforce LWC, WASM, etc.).

---

## 1. Principio fundamental: no big-bang

**No implementar un desarrollo grande de una sola vez sin ir comprobando que lo construido funciona.**

- Es preferible **varias fases viables**, cada una ejecutable y verificable, que un bloque único sin pruebas.
- Un desarrollo grande sin pruebas parciales es **casi una garantía de fracaso**: los fallos se acumulan y se vuelve muy difícil localizar la causa.
- Por tanto, **siempre** que se afronte un desarrollo de cierto tamaño:
  1. Descomponer en **fases o hitos** que se puedan ejecutar y probar por separado.
  2. Asegurar que cada fase deja **código funcional** antes de pasar a la siguiente.
  3. Contar con un **procedimiento explícito** para pruebas parciales (ver sección 2).

---

## 2. Procedimiento para pruebas parciales

### 2.1 Entorno de pruebas controlado

- Poder **ejecutar los añadidos parciales** en un entorno conocido y reproducible.
- Poder **ver los resultados** de cada ejecución (salida, logs, pantalla, estado).
- Poder obtener **información de depuración y profiling**:
  - Breakpoints, paso a paso, inspección de variables y call stack.
  - Profiling (tiempo, memoria, hits) cuando aplique.
- Si la aplicación tiene **interfaz gráfica**: poder ver qué se muestra en cada etapa (capturas, vídeo, herramientas de UI automation).

### 2.2 Vectores de prueba sintéticos

- A menudo es necesario **datos de prueba similares a los reales** pero controlados y repetibles.
- Desarrollar **generadores de datos de prueba** (scripts, programas, fixtures) antes o en paralelo al código que los consumirá.
- Usar estos vectores para:
  - Verificar comportamiento esperado.
  - Reproducir regresiones.
  - Validar límites y casos raros.

### 2.3 Tests automatizados

- **Tests unitarios**: comprobar unidades de código (funciones, módulos) de forma aislada.
- **Tests funcionales / de integración**: comprobar flujos completos o subsistemas.
- Integrar la ejecución de tests en el flujo de desarrollo (local y, si aplica, CI).

---

## 3. Interfaz gráfica y “ver” lo que se muestra

- **Aplicaciones web**: usar herramientas de automatización (p. ej. **Playwright**) para abrir la app, interactuar y capturar estado o pantalla; en muchos casos es suficiente para ver todas las etapas.
- **Otras aplicaciones**: puede ser necesario **análisis de captura de pantalla o vídeo** y capacidad de **interactuar** con la aplicación (teclado, ratón). Aquí pueden ayudar:
  - Herramientas de captura y OCR.
  - IA local (p. ej. lm-studio) para análisis de imágenes/audio.
  - MCPs que expongan captura o control de ventanas si están disponibles.
- **Plataformas emuladas** (p. ej. Amiga con WinUAE): es necesario **acceso pleno a las herramientas de depuración del emulador** (breakpoints, memoria, registros, overlay de debug, profiling). Ver `doc/debug-with-ai.md` en este proyecto.

---

## 4. Acordar enfoque y preparar herramientas antes de empezar

- **Antes** de lanzar un desarrollo grande:
  1. Acordar con el usuario la **mejor forma de afrontarlo** (fases, orden, criterios de “listo” por fase).
  2. **Preparar las mejores herramientas posibles**:
     - Entorno de compilación/ejecución y depuración.
     - Generadores de datos de prueba y scripts de análisis.
     - Tests (marco, primeros casos).
     - Acceso a depurador, profiling y, si aplica, captura de pantalla/UI.
  3. **Cuando estemos listos**, comenzar el proceso de desarrollo fase a fase.

- El usuario puede contar con:
  - **IA local (lm-studio)** para tareas pesadas: parsing de logs, OCR, análisis de imágenes, análisis de audio, etc.
  - **MCPs** que se puedan habilitar para esos fines.
  - Aplicaciones adicionales (p. ej. herramientas NVIDIA u otras) que la IA pueda controlar o usar como apoyo.

---

## 5. Registro y documentación

- **Documento de desarrollo** (por proyecto o por iniciativa):
  - Objetivos y fases acordadas.
  - Decisiones tomadas y **cosas aprendidas** (qué falló, qué se corrigió, por qué).
  - Referencias a perfiles, capturas o logs relevantes.
- **Comentarios en el código**:
  - Explicar **por qué** se implementó de una forma concreta y no de otra.
  - Incluir referencias a pruebas o incidencias cuando un detalle se corrigió tras comprobar la ejecución (p. ej. “en ejecución se vio que X fallaba si no se hacía Y”).

---

## 5.1 Autonomía del agente

- **Objetivo:** El agente (IA) se ocupa del desarrollo y de los detalles; entrega el producto final funcionando sin pedir pasos intermedios al usuario.
- **El agente solo debe avisar al usuario cuando:**
  1. **No puede continuar** (bloqueo: falta un recurso que solo el usuario puede dar —ej. ejecutar algo en su máquina, un binario que no está instalado—).
  2. **Hay que tomar una decisión relevante** (p. ej. habilitar un MCP, instalar software, elegir entre alternativas de diseño con impacto).
- **En todo lo demás el agente es autónomo:** implementación, pruebas parciales, corrección de fallos, depuración (incluido uso de breakpoints, step, análisis de perfiles y logs). Puede **elaborar software auxiliar** (scripts de parsing, comprobaciones, generación de datos de prueba) para facilitar sus tareas.
- **Depuración:** El usuario prefiere no depurar a mano; el agente debe ser autónomo también en depuración, usando las herramientas disponibles (MCP Debug Tools, scripts de análisis de perfiles, overlay en emulador, etc.).
- **Máximo trabajo por turno:** En cada respuesta, el agente ejecuta el máximo de pasos posible hacia el objetivo y no para por detalles nimios; regla concreta en `.cursor/rules/max-work-per-turn.mdc`. Uso recomendado: panel Agent (botón "New Agent") + objetivo claro + "sigue" cuando quede trabajo; ver `doc/agent-runbook.md` § Chat vs Agent.

---

## 6. Reglas generales sugeridas (para cualquier proyecto)

1. **Desarrollo por fases**: todo desarrollo no trivial se descompone en fases ejecutables y verificables.
2. **Verificación por fase**: cada fase debe poder ejecutarse y probarse antes de dar por cerrada.
3. **Entorno controlado**: definir y usar un entorno de pruebas estable (build, run, debug, profile).
4. **Datos de prueba**: disponer de vectores sintéticos o fixtures cuando sea necesario para reproducir y validar.
5. **Tests**: mantener tests unitarios y funcionales donde sea viable.
6. **Visibilidad**: si hay UI, asegurar una forma de ver lo que se muestra (automation, capturas, overlay).
7. **Depuración y profiling**: tener acceso a las herramientas de depuración y profiling del stack (incluido emulador si aplica).
8. **Documentación**: mantener documento de desarrollo y comentarios en código que expliquen el “por qué”.

---

## 7. Estrategias por tipo de proyecto

Cada tipo de proyecto puede tener una regla o sección específica que detalle cómo se aplican estos principios.

### 7.1 C para Amiga (emulador WinUAE / FS-UAE)

- **Fases**: por módulo del engine (system, blitter, input, display, memory) o por características (init, VBL, un blit, overlay de debug). Cada fase = algo que compila, se ejecuta en el emulador y se puede inspeccionar. Ver roadmap en `doc/engine-architecture.md` (view/viewport, tilemaps, estados, 3D blitter).
- **Ejecución y depuración**: usar **MCP Debug Tools** (dap-proxy) con la extensión vscode-amiga-debug: breakpoints, step, variables, call stack, memoria/registros. Compilar en modo debug (`CFLAGS_OPT=-Og` o `-O0`) para breakpoints fiables; ver `doc/diagnostico-depurador-f5.md`.
- **Ver la pantalla**: overlay de debug en WinUAE (`debug_rect`, `debug_text`, etc.); el usuario puede compartir capturas si hace falta. Plan B: Coppenheimer + Playwright para inspección de memoria/DMA.
- **Profiling**: generación de `.amigaprofile` (botón Profile en la barra de depuración); la IA puede analizar con `scripts/parse-amigaprofile.sh` / `parse-latest-amigaprofile.sh`.
- **Build Debug vs Release**: en builds de depuración se pueden añadir comprobaciones de seguridad (p. ej. en blits: coordenadas y tamaños dentro de bitmap) y logging opcional a fichero; en release se omiten para máximo rendimiento (`ENGINE_DIAG`, `ENGINE_MEM_TRACE` en este proyecto).
- **Tests**: tests unitarios en host (C estándar) para lógica pura; tests funcionales = ejecutar en emulador y comprobar salida/overlay o perfil.
- **Documentación**: `doc/engine-architecture.md` (visión, API, roadmap), `doc/engine-docs-index.md` (índice), `doc/debug-with-ai.md`, `doc/diagnostico-depurador-f5.md`. Regla de Cursor: `.cursor/rules/amiga-debug-with-mcp.mdc`.

### 7.2 Salesforce (LWC, Service Cloud, APIs)

- **Fases**: por componente, flujo o integración; cada uno desplegable y probado en org de desarrollo/sandbox.
- **Logs**: poder **generar y analizar** logs (Debug Logs, Apex, LWC). La IA debe saber interpretar stack traces y mensajes de error de Salesforce.
- **Interfaz**: poder **abrir la página web** de Service Cloud y usarla como un agente (Playwright o Cursor Browser): navegar, rellenar formularios, comprobar datos.
- **Setup y login-as**: acceso a Setup y uso de **Login As** para probar con distintos perfiles/roles.
- **Depuración**: depurar componentes y flujos con **Playwright** o con la **consola de desarrollo del navegador (F12)**; breakpoints en JS, inspección de estado.
- **APIs**: acceso a **todas las API** que expone Salesforce (REST, SOAP, Tooling, Metadata, etc.) para automatizar y verificar.
- **Documentación**: documento de desarrollo del proyecto Salesforce; comentarios en Apex/LWC sobre decisiones y problemas encontrados en pruebas.

### 7.3 WASM (WebAssembly)

- **Fases**: incorporar funcionalidades **poco a poco**; cada paso debe ser ejecutable y depurable.
- **Depuración a bajo nivel**: poder depurar la aplicación **desde el navegador** (Chrome DevTools, breakpoints en WASM/JS, memoria) y **desde el escritorio** si la aplicación es desktop (p. ej. runtime WASM en escritorio).
- **Tests**: tests unitarios del código que se compila a WASM (en host o en runtime); tests funcionales en navegador o en la app final.
- **Documentación**: documento de desarrollo; comentarios que expliquen tramos delicados (FFI, memoria, hilos si aplica).

### 7.4 Otros (web estándar, backend, apps nativas)

- Ajustar **entorno de ejecución** (servidor local, emulador, dispositivo) y **herramientas de depuración** (F12, depurador del IDE, logs, profiling).
- Mantener los mismos principios: fases verificables, datos de prueba, tests, documentación y comentarios útiles.

---

## 8. Resumen

| Aspecto | Acción |
|--------|--------|
| Tamaño del desarrollo | Dividir en fases viables; no big-bang. |
| Cada fase | Ejecutable, probada, con código funcional antes de seguir. |
| Entorno | Controlado: build, run, debug, profile. |
| Datos | Vectores/fixtures sintéticos cuando haga falta. |
| Tests | Unitarios y funcionales. |
| UI | Ver en todas las etapas (Playwright, capturas, overlay, etc.). |
| Emuladores | Acceso pleno a depuración del emulador. |
| Antes de empezar | Acordar enfoque y preparar herramientas. |
| IA local / MCPs | Usar para parsing, OCR, imágenes, audio, etc. |
| Registro | Documento de desarrollo + comentarios en código (“por qué”). |
| Por proyecto | Definir regla o doc específica (Amiga, Salesforce, WASM, etc.). |

Este documento debe servir como referencia general; cada proyecto puede tener además su propio `doc/development-methodology.md` o sección en un README/AGENTS que adapte estos puntos al stack concreto.

---

## 9. Reglas de Cursor en este proyecto

- **General (siempre aplicada)**: `.cursor/rules/incremental-development.mdc` — desarrollo por fases, pruebas parciales, entorno controlado, documentación.
- **Máximo trabajo por turno (siempre aplicada)**: `.cursor/rules/max-work-per-turn.mdc` — en cada respuesta hacer el máximo de pasos; no parar por nimiedades; solo avisar en bloqueo o decisión.
- **Amiga (siempre aplicada)**: `.cursor/rules/amiga-debug-with-mcp.mdc` — uso de MCP Debug Tools para depurar y perfilar; overlay y Coppenheimer como plan B.

Para otros tipos de proyecto (Salesforce, WASM, etc.) se pueden añadir reglas en `.cursor/rules/` con `globs` o `alwaysApply` según convenga, siguiendo la sección 7 de este documento.
