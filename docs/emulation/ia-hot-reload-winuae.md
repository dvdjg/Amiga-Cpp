# IA, compilación en C y despliegue en caliente a WinUAE

## Documentos padre

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Indice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Spec de batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md)
- [Roadmap de implementación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-implementation-roadmap.md)
- [Workflow MCP + WinUAE](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/mcp-live-coding-workflow.md)
- [Cargador de desarrollo Amiga](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-dev-harness-loader.md)
- [Kernel y loader del Amiga 500](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-kernel-loader-notes.md)
- [68000: pila, ABI y llamadas](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/m68k-stack-and-calling-notes.md)
- [Formatos Amiga y artefactos del toolchain](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-binary-and-disk-formats.md)
- [Indice de la batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/README.md)

## Resumen ejecutivo

El objetivo que se persigue en este proyecto es:

1. pedirle a una IA una técnica o una pieza de software en C para Amiga;
2. hacer que la IA genere o modifique el código;
3. compilarlo con el toolchain GCC/m68k del proyecto;
4. desplegarlo en caliente o casi en caliente sobre WinUAE;
5. observar el resultado real en la pantalla del Amiga emulado;
6. extraer evidencia, postmortem y documentación técnica;
7. convertir lo válido en capacidad reusable del engine.

La vision no es simplemente "automatizar builds". La vision es un **bucle cerrado IA -> C -> build -> despliegue -> verificación -> iteracion** sobre hardware Amiga, con dos destinos principales:

- desarrollar el engine reusable;
- demostrar técnicas concretas de videojuegos y hardware en la batería de tests.

## Lo que se pretende exactamente

### Flujo deseado

El flujo ideal que se quiere alcanzar es:

1. la IA recibe una peticion como:
   - "haz un efecto de bandas raster"
   - "prueba un scroll por copper"
   - "demuestra que esta técnica de sprites es viable"
2. la IA crea o modifica el código C y ensamblador necesario;
3. el repositorio compila el artefacto adecuado:
   - ejecutable DOS normal;
   - payload `metal/direct`;
   - o un futuro payload cargado por el harness DOS;
4. WinUAE carga ese artefacto sin friccion:
   - por `ADF`
   - por volumen montado `devfs`
   - por `LoadSeg`/`RunCommand`
   - o por carga directa `winuae_load`
5. la IA observa la salida:
   - leyendo memoria y registros;
   - capturando pantalla;
   - usando LM Studio para describir o confirmar lo que se ve;
6. si hay fallo:
   - se captura postmortem;
   - se documenta;
   - se corrige;
7. si funciona:
   - se registra la técnica;
   - se deja test reproducible;
   - y se extrae al engine cuando sea reusable.

### Vision final

La vision final no es solo "cargar programas". Es llegar a poder hacer cosas como:

- pedir una técnica nueva y verla en WinUAE en pocos minutos;
- iterar con cargas calientes sin reiniciar todo el entorno;
- comparar variantes de una técnica;
- autopsiar crashes de forma repetible;
- y construir poco a poco un catálogo de técnicas de videojuego Amiga demostradas.

## Para que sirven los tests y por qué no son un fin en si mismos

La batería `tests/amiga-battery/` no existe para acumular ejemplos sueltos. Su función es:

- aislar una técnica de hardware concreta;
- demostrar que es viable en Amiga 500;
- capturar procedimiento, evidencias y limites;
- y empujar despues esa técnica hacia `engine/`.

En otras palabras:

- el **test** es la demostracion controlada;
- el **engine** es la reutilizacion de lo aprendido.

Por eso cada caso de batería debería acabar sirviendo para una de estas dos cosas:

1. demostrar una técnica de videojuego:
   - raster
   - copper
   - blitter
   - sprites
   - audio
   - sincronizacion por IRQ/VBL
2. o demostrar infraestructura necesaria para poder desarrollar esas técnicas:
   - launcher DOS
   - hot-load
   - postmortem
   - snapshot
   - decode de bitmaps

Documentos que ya explican esta idea:

- [Spec de batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md)
- [Roadmap de implementación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-implementation-roadmap.md)
- [Indice de batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/README.md)

## Arquitectura practica del bucle IA -> WinUAE

## 1. Generacion y compilación

La IA trabaja sobre este repo y genera/modifica:

- código C de `engine/`, `app/` o `tests/amiga-battery/`
- stubs ensamblador para payloads `metal`
- scripts de build y despliegue
- documentación y evidencias

La compilación cruza a 68000 usando el toolchain del proyecto y genera, según el caso:

- `.elf`
- `.exe` AmigaHunk
- `.adf`

Documentos relacionados:

- [Formatos Amiga y artefactos](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-binary-and-disk-formats.md)
- [68000: pila y ABI](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/m68k-stack-and-calling-notes.md)

## 2. Rutas de despliegue usadas

Hoy hay cuatro rutas reales de despliegue relevantes:

### Tabla rápida de estado por ruta

| Ruta | Artefacto típico | Estado actual | Uso recomendado |
|------|-------------------|---------------|-----------------|
| `ADF / OS loader` | `dos_hunk_exe` en floppy | PARCIAL | Ruta conservadora para validaciones cercanas al flujo real de AmigaDOS. |
| `devfs` | `a.exe` DOS en `dh1:` | HECHO para binarios mínimos; PARCIAL para casos complejos | Mejor ruta DOS base para iteracion rápida y diagnóstico del loader. |
| `hot-load DOS` sobre harness | payload DOS + `LoadSeg` / `RunCommand` | PARCIAL | Objetivo de medio plazo para despliegue DOS en caliente sin rehacer ADF cada vez. |
| `metal/direct` | payload con `battery_metal_entry` | HECHO para `M10`, `V01`, `V02`, `V03`, `C01`; PARCIAL para `T02` | Ruta más madura para abrir ya técnicas gráficas y depuración de bajo nivel. |

### A. ADF / OS loader

La más parecida a un flujo usuario normal:

- se genera un `ADF`
- WinUAE arranca desde ese disco
- AmigaDOS carga el programa

Ventaja:

- se respeta el contrato normal del SO

Inconveniente:

- hay friccion de arranque
- puede interferir el estado DOS/disco
- no siempre es ideal para iteracion rápida

### B. `devfs` DOS-friendly

Ruta ya funcional para ejecutables DOS normales:

- `dh0`/`dh1`
- staging de binario en `out/devfs/a.exe`
- lanzamiento por `debugging_trigger=:a.exe`

Esta ruta ya ha demostrado algo clave:

- **si podemos cargar y ejecutar binarios GCC DOS 68000 normales en A500/Kickstart 1.3**

Evidencia base:

- [experimentos DOS standalone](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/experiments/README.md)

### C. Hot-load DOS desde harness

Es la ruta deseada para ejecutables DOS "de verdad" sin depender siempre de reiniciar con un ADF nuevo.

La idea es:

- tener un harness residente;
- enviarle comandos;
- usar `LoadSeg()` / `RunCommand()` sobre un payload staged;
- recoger resultado y postmortem.

Esta ruta esta en progreso en:

- [M03_dev_harness_disk](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M03_dev_harness_disk/README.md)

### D. `metal/direct`

Ruta bare-metal pensada para payloads que no son ejecutables DOS normales:

- `winuae_load`
- salto a `battery_metal_entry`
- pila privada
- control y excepciones propias

Esta es la ruta más madura para iterar técnicas visuales ahora mismo.

Casos ya demostrados:

- [M10_smoke_metal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M10_smoke_metal/README.md)
- [V01_raster_bars_metal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V01_raster_bars_metal/README.md)
- [V02_palette_pulse_metal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V02_palette_pulse_metal/README.md)

## 3. Verificacion y observabilidad

La IA no solo compila y lanza. También observa:

- registros CPU
- memoria
- custom regs
- capturas de pantalla
- snapshots de maquina
- postmortem
- análisis visual con LM Studio

Esto ya está soportado por:

- `mcp-winuae-emu`
- scripts de captura del repo
- y el modelo local `qwen2.5-vl-7b-instruct` en LM Studio

Documentos relacionados:

- [Workflow MCP + WinUAE](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/mcp-live-coding-workflow.md)
- [Verificacion del display por IA](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/verificación-display-por-ia.md)

## Estado real de lo conseguido

## Cronologia resumida de hitos y bloqueos

| Fase | Hito | Resultado | Leccion |
|------|------|-----------|---------|
| 1 | Captura host fiable de WinUAE | Resuelto | Antes de juzgar un binario, hay que asegurar que la captura refleja la pantalla real. |
| 2 | Ejecutables DOS GCC mínimos por `devfs` | Resuelto | El toolchain y AmigaDOS base en A500/Kickstart 1.3 si funcionan. |
| 3 | `M03` con `LoadSeg()` sobre `dh1:payload.exe` | Resuelto parcialmente | La carga en caliente DOS ya es real; el hueco restante esta en el hot-run limpio. |
| 4 | Ruta `metal/direct` con `M10` | Resuelto | Ya existe un baseline bare-metal útil para iterar técnica gráfica sin depender de DOS. |
| 5 | Bug de `PC/SR/A7` en MCP | Resuelto | El arranque metal depende de escribir el contexto CPU de forma coherente, no registro a registro. |
| 6 | `V01` como primer vector visual | Resuelto | La primera técnica visual simple ya puede validarse en vivo con handshake observable. |
| 7 | `V02` y sensibilidad `.elf -> .exe` | Resuelto | Algunas variantes más complejas pueden romper el runtime aunque el `.elf` parezca sano; hay que validar el Hunk real. |
| 8 | `V03` scanline bands | Resuelto | La validación conjunta ya pasa con handshake reforzado y evidencia separada por comando (`SET_MODE`/`EXIT`), con fallback trazable a `fresh-launch` cuando `connect_existing` falla. |

## Evidencias clave y donde mirarlas

### 1. DOS base: binario GCC normal funcionando

- [experimentos DOS standalone](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/experiments/README.md)
- [captura `DOS_MIN_CLI_OK`](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/experiments/evidence/dos_min_cli/devfs-cli-live-screen.png)
- [análisis visual LM Studio](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/experiments/evidence/dos_min_cli/devfs-cli-vision-live-screen.md)
- [resumen de validación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/experiments/evidence/dos_min_cli/devfs-cli-live-validation-summary.json)

### 2. Hot-load DOS parcial vía harness

- [README de M03](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M03_dev_harness_disk/README.md)
- [resumen de `LOADSEG_RUN` con `dh1:payload.exe`](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M03_dev_harness_disk/evidence/dev-harness-command-summary.json)

### 3. Baseline metal y postmortem

- [README de M10](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M10_smoke_metal/README.md)
- [resumen de comandos metal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M10_smoke_metal/evidence/metal-command-summary.json)
- [crash metal / excepcion](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M10_smoke_metal/evidence/metal-command-crash.json)

### 4. Tecnicas visuales ya cerradas

- [V01 README](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V01_raster_bars_metal/README.md)
- [V01 validation summary](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V01_raster_bars_metal/evidence/vector-validation-summary.json)
- [V02 README](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V02_palette_pulse_metal/README.md)
- [V02 validation summary](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V02_palette_pulse_metal/evidence/vector-validation-summary.json)

### 5. Tecnica viva cerrada con fallback documentado

- [V03 README](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V03_scanline_bands_metal/README.md)
- [V03 `SET_MODE` summary](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V03_scanline_bands_metal/evidence/metal-command-set_mode-summary.json)
- [V03 validation summary](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V03_scanline_bands_metal/evidence/vector-validation-summary.json)
- [V03 `EXIT` summary (fallback)](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V03_scanline_bands_metal/evidence/metal-command-exit-fallback-summary.json)
- [C01 README](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/C01_copper_vertical_gradient/README.md)
- [C01 validation summary](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/C01_copper_vertical_gradient/evidence/vector-validation-summary.json)

## 1. Lo que ya funciona

### A. Ver la pantalla real de WinUAE

Ya no dependemos de capturas negras falsas.

Se corrigio la captura de ventana para usar `screen_copy` y recorte de area cliente, de forma que la IA puede ver la salida real de WinUAE.

Esto era critico, porque durante un tiempo parecia que la app principal no arrancaba cuando en realidad el problema era de captura, no de ejecución.

### B. Carga DOS normal con GCC

Ya está demostrado que:

- un binario DOS 68000 generado por GCC puede compilar;
- puede cargarse bajo **Amiga 500 + Kickstart 1.3**;
- y puede ejecutarse correctamente.

La prueba más importante es:

- `dos_min_cli.exe` mostrando `DOS_MIN_CLI_OK`

Referencia:

- [experimentos DOS standalone](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/experiments/README.md)

### C. Carga en caliente DOS parcial

En `M03` ya se ha demostrado:

- `LoadSeg()` correcto sobre payloads staged;
- entrada en la fase de ejecución DOS hot-run;
- mailbox y control del harness en RAM.

Todavía no está cerrada la ejecución limpia de todos los payloads, pero la parte "cargar en caliente" ya no es hipotética.

Referencia:

- [M03_dev_harness_disk](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M03_dev_harness_disk/README.md)

### D. Carga directa metal

Es la vía más fiable hoy para iterar técnica visual.

Ya están demostrados:

- stack privada
- comandos en RAM (`SET_MODE`, `EXIT`, `TRIGGER_ILLEGAL`)
- postmortem
- parcheo simbolico en caliente

Referencias:

- [M10_smoke_metal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M10_smoke_metal/README.md)
- [V01](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V01_raster_bars_metal/README.md)
- [V02](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V02_palette_pulse_metal/README.md)

### E. Primeras técnicas de videojuego ya demostradas

Hoy ya hay pruebas vivas de:

- pulso de paleta
- cambio visual por comandos MCP
- bandas/raster simples
- control de loop por frame

No es aún un juego, pero ya es técnica gráfica real, no solo infraestructura.

## 2. Problemas ya resueltos

Estos son errores reales que bloquearon el progreso y ya quedaron entendidos o corregidos.

### A. Confundir ejecutables DOS con payloads metal

Fue uno de los errores conceptuales más caros.

Un `dos_hunk_exe`:

- espera loader DOS
- proceso
- pila
- contexto de sistema

No debe tratarse como si fuera un blob de código bare-metal.

Esto llevo a una division de arquitectura más clara:

- ruta DOS para binarios DOS
- ruta metal para payloads metal

### B. `PROGDIR:` incorrecto en `devfs`

En la ruta `devfs`, el payload correcto era:

- `dh1:payload.exe`

No:

- `PROGDIR:payload.exe`

Ese error provocaba requesters de volumen inexistente y falseaba el diagnóstico del hot-load.

### C. Inicializacion global incompleta en runtime DOS

Hubo fallos reales por:

- `SysBase` no inicializado;
- `GfxBase` no copiado antes de `WaitTOF()`;
- y telemetría temprana (`battery_runtime_mark`) que reintroducia requesters.

Eso explico parte del `Software error - task held` que parecia misterioso.

### D. Falso negro en capturas

Durante bastante tiempo parecia que programas correctos mostraban pantalla negra, pero el error estaba en la estrategia de captura del host.

### E. Escritura incoherente de registros en MCP

Otro bug importante estuvo en `winuae_registers_set` del repo `mcp-winuae-emu`.

Escribir `PC`, `SR` y `A7` uno a uno dejaba el contexto incoherente para payloads `metal/direct`.

La correccion fue escribir el banco completo de registros de forma coherente. Eso recupero el baseline metal.

### F. `V02` y sensibilidad del artefacto `.elf -> .exe`

`V02` mostro un problema fino pero muy importante:

- una variante más compleja tenia un `.elf` razonable;
- pero el `.exe` Hunk cargado en caliente no quedaba consistente en runtime.

La solución fue reducirlo a una version mucho más cercana a `M10`, cerrandolo primero como baseline estable.

## 3. Problemas que siguen abiertos

### A. Hot-run DOS completamente limpio

La carga DOS caliente está muy avanzada, pero no todas las ejecuciones sobre el harness DOS retornan limpias.

El foco sigue estando en:

- `M03`
- convivencia launcher DOS / payload DOS
- y endurecimiento de `RunCommand()`

### B. Handshake inestable en algunos vectores metal

`V01` y `V02` ya pasan.

`V03` ya queda cerrado como vector baseline:

- validación conjunta `SET_MODE` + `EXIT` pasando en vivo
- evidencias separadas por comando para evitar sobreescritura
- fallback automático documentado para `EXIT` cuando falla la reconexión

## Estado especifico de V03

`V03_scanline_bands_metal` sigue siendo un buen ejemplo de técnica viva y ahora también de cierre con instrumentacion robusta.

### Que ya hace bien

- compila y carga por `metal/direct`
- entra en el payload
- consume `SET_MODE(1)` en validación conjunta
- consume `EXIT` en validación conjunta
- forma bandas visibles por scanline cambiando `COLOR00` en varias alturas del cuadro

Evidencias directas:

- [V03 `SET_MODE` summary](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V03_scanline_bands_metal/evidence/metal-command-set_mode-summary.json)
- [V03 `EXIT` summary fallback](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/V03_scanline_bands_metal/evidence/metal-command-exit-fallback-summary.json)

### Que sigue mejorable

- la reconexión `connect_existing` sigue siendo sensible en algunas tandas largas
- el pipeline ya lo absorbe con fallback `fresh-launch` en `EXIT`, pero conviene estabilizarlo sin fallback

### Interpretacion correcta

No significa que la técnica raster este en riesgo. Significa que:

- el vector ya corre;
- el canal de control ya funciona;
- y el **handshake automatizado** ya permite cerrar baseline con evidencia robusta.

Por eso `V03` debe leerse como:

- **técnica cerrada**
- **instrumentacion utilizable, con reconexión aun mejorable**

### C. Persistencia de sesión GDB entre turnos

Se ha avanzado, pero la reconexión perfecta entre sesiones largas sigue siendo parcial.

### D. Automatizacion visual aun mejorable

La IA ya puede ver WinUAE, pero:

- algunas capturas del host siguen metiendo UI alrededor;
- y en técnicas raster conviene afinar más la forma de capturar solo el area útil.

## Errores y lecciones aprendidas

## 1. Error de enfoque

El mayor error no fue técnico, sino de modelo mental:

- intentar usar una sola estrategia de carga para artefactos que no son equivalentes

La correccion conceptual es:

- **DOS -> loader DOS**
- **metal -> entry directa**

## 2. Error de observabilidad

No se debe concluir que algo "no funciona" solo porque la captura automática parezca negra. Hay que distinguir:

- ejecución real
- y captura fiable

## 3. Error de telemetría

Instrumentar demasiado pronto o demasiado agresivamente puede romper el propio runtime que se quiere observar.

## 4. Error de timing

Buena parte de los problemas restantes ya no son de compilación ni de carga, sino de:

- cuando leer memoria
- cuando pausar
- cuando dar por consumido un comando

## Incidentes relevantes

### Incidente 1: parecia que la app principal no arrancaba

Diagnostico final:

- la app si arrancaba;
- el error estaba en la captura de ventana del host;
- `screen_copy` + recorte de cliente resolvio el problema.

### Incidente 2: parecia que no podiamos ejecutar binarios GCC DOS

Diagnostico final:

- la ruta DOS base si funcionaba;
- el caso `dos_min_cli.exe` lo demostro;
- el problema estaba en casos concretos y en su runtime, no en GCC ni en AmigaDOS base.

### Incidente 3: el launcher DOS parecia roto por `PROGDIR:payload.exe`

Diagnostico final:

- en `devfs`, la ruta correcta era `dh1:payload.exe`;
- `PROGDIR:` introducia requesters y ruido diagnóstico.

### Incidente 4: el baseline metal parecio romperse de golpe

Diagnostico final:

- `PC`, `SR` y `A7` se estaban escribiendo de forma incoherente en el MCP;
- la correccion del write coherente de registros recupero `M10` y estabilizo `V01`.

### Incidente 5: `V02` parecia un vector roto

Diagnostico final:

- no era el concepto del vector, sino la sensibilidad del artefacto `.elf -> .exe`;
- la version reducida y alineada con `M10` cerro el baseline;
- desde ahi ya se pudo seguir avanzando a `V03`.

## Documentacion usada como base de conocimiento

## 1. Documentacion interna del proyecto

Esta ha sido la base operativa principal:

- [Spec de batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md)
- [Roadmap de implementación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-implementation-roadmap.md)
- [Workflow MCP + WinUAE](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/mcp-live-coding-workflow.md)
- [Cargador de desarrollo Amiga](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-dev-harness-loader.md)
- [Kernel y loader del Amiga 500](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-kernel-loader-notes.md)
- [68000: pila y ABI](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/m68k-stack-and-calling-notes.md)
- [Formatos Amiga y artefactos](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-binary-and-disk-formats.md)
- [Indice de batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/README.md)

## 2. Documentacion externa y manuales

Estas fuentes han servido para cimentar la parte de loader, DOS, ABI y excepciones:

- [Amiga ROM Kernel Reference Manual: DOS](https://developer.amigaos3.net/sites/default/files/downloads/2024-10/Amiga_ROM_Kernel_Reference_Manual_DOS.pdf)
- [Autodoc de `AddTask`](https://developer.amigaos3.net/autodocs/exec.library/AddTask.html)
- [XGCC Manual](https://gendev.spritesmind.net/files/xgcc/xgcc.pdf)

### Como se han usado esas fuentes

- **RKM DOS**: para entender `LoadSeg`, procesos, DOS list, carga de ejecutables y por qué un binario DOS no debe tratarse como metal puro.
- **Autodoc de `AddTask`**: para reforzar la separacion entre `Task` de bajo nivel y `Process` apto para DOS.
- **XGCC Manual**: para ABI, stack, excepciones, startup y relacion C/ensamblador.

## 3. Conocimiento empirico extraido del propio proyecto

Una parte importante del conocimiento no venia de manuales, sino de:

- builds reales
- crashes reales
- requesters reales
- postmortems
- y comparacion de artefactos `.elf`, `.exe`, `.map`, capturas y snapshots

Eso incluye hallazgos como:

- el bug de `PC/SR/A7` en el MCP
- el problema de `PROGDIR:` en `devfs`
- la sensibilidad de `V02` al layout `.elf -> .exe`
- y los problemas de telemetría que reintroducian requesters

## Relación entre esto y las técnicas para videojuegos

Todo este esfuerzo de carga en caliente no es infraestructura por gusto. Su valor para videojuegos es directo:

- permite probar técnicas visuales más deprisa;
- permite aislar si una técnica falla por la técnica misma o por el loader;
- permite documentar efectos uno a uno;
- y permite convertir esos efectos en piezas del engine.

Ejemplos de dirección:

- `V01`: baseline visual simple
- `V02`: pulso de paleta estable
- `V03`: bandas por scanline
- más adelante:
  - copper degradados
  - copper bars
  - blitter
  - sprites
  - audio Paula

## Estado resumido hoy

### Lo que ya se puede decir honestamente

- la IA ya puede generar código C y ensamblador en este repo;
- ese código ya puede compilarse para 68000;
- ya existen rutas funcionales de despliegue a WinUAE;
- ya se puede observar de forma real lo que aparece en pantalla;
- ya hay técnicas visuales demostradas en vivo;
- y ya existe una ruta de hot-load DOS parcial y una ruta `metal/direct` claramente útil.

### Lo que todavia no se puede vender como resuelto

- que cualquier binario DOS complejo se ejecute en caliente y limpio sin friccion;
- que toda la validación automática de vectores raster sea estable;
- que la persistencia GDB entre turnos sea perfecta.

## Ruta recomendada a partir de aqui

La estrategia más razonable ya no es abrir más infraestructura de golpe, sino avanzar en dos carriles en paralelo:

1. **Carril gráfico inmediato**
   - seguir con vectores `metal/direct`
   - demostrar técnicas de videojuego
   - extraer helpers al engine

2. **Carril de launcher DOS**
   - terminar de cerrar el harness `M03`
   - usar `LoadSeg`/`RunCommand` de forma cada vez más robusta
   - poder ejecutar payloads DOS en caliente con menos friccion

Esta combinacion es la que mejor encaja con la vision del proyecto:

- no bloquear técnica gráfica esperando a la perfeccion del launcher DOS;
- pero tampoco renunciar a una ruta DOS caliente más comoda a medio plazo.
