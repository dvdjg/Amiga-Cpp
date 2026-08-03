# Cargador de Desarrollo Amiga

## Documentos padre

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Indice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Spec de batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md)
- [Roadmap de implementación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-implementation-roadmap.md)
- [Workflow MCP + WinUAE](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/mcp-live-coding-workflow.md)
- [Kernel y loader del Amiga 500](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-kernel-loader-notes.md)
- [68000: pila, ABI y llamadas](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/m68k-stack-and-calling-notes.md)
- [Formatos Amiga y artefactos del toolchain](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-binary-and-disk-formats.md)

## Objetivo

Definir una ruta de carga alternativa para desarrollo automatizado cuando:

- un ejecutable AmigaDOS normal no sea un buen candidato para carga directa;
- el arranque por ADF/Workbench interfiera con el takeover del test;
- necesitemos utilidades residentes para depuración, postmortem, control de interrupciones o ejecución de binarios experimentales.

La idea no es sustituir AmigaOS para siempre, sino disponer de un **harness de desarrollo** controlado por nosotros.

## Alcance de compatibilidad

La ruta base de este documento debe leerse como:

- **objetivo principal:** Amiga 500 con Kickstart 1.3
- **compatible en gran parte:** Amiga 600, siempre que no exija APIs más nuevas
- **extensiones posteriores:** Amiga 1200 y ROMs/DOS más nuevos

## Por qué la carga directa esta dando problemas

Un binario `dos_hunk_exe` no es solo "código y datos". Normalmente espera que el loader del sistema le prepare:

- contexto de proceso;
- pila valida;
- segmento/entry según convenciones AmigaDOS;
- librerias y entorno basico de arranque;
- y, a veces, que el sistema no este en mitad de actividad de disco o requesters.

Cuando lo cargamos "a pelo" por GDB y fijamos `PC`, estamos saltandonos parte de ese contrato. Por eso puede ocurrir que:

- el transporte sea correcto y aún así el programa muera antes de su `main`;
- un test que funciona como ejecutable DOS no sea automáticamente válido como carga directa;
- y un takeover temprano con `TakeSystem()` choque con actividad DOS/disco si el programa arranca desde ADF.

## Estrategias de carga soportadas por el proyecto

### 1. Loader del sistema operativo

Ruta estable para `dos_hunk_exe`.

- ADF o launcher DOS
- el sistema prepara proceso y pila
- el test entra como programa normal

Es la vía preferida para casos tipo `T01`.

### 2. Carga directa diagnostica

Ruta útil para depurar transporte, memoria y postmortem temprano.

- MCP escribe el Hunk o binario en una dirección fija
- fija `PC` y `A7`
- observa crash, requester o estado de runtime

Es útil, pero no debe confundirse con "equivalente al loader del sistema".

### 3. Harness / kernel de desarrollo

Ruta recomendada cuando queremos controlar mejor el entorno.

La propuesta es arrancar un disco o cargador propio que:

- reserve memoria y stack de forma predecible;
- ofrezca utilidades de depuración residentes;
- cargue payloads de prueba con un contrato conocido;
- capture excepciones e interrupciones;
- y deje un canal simple de control desde MCP.

## Propuesta concreta de harness

### Fase 1. Dev disk DOS-friendly

Disco de desarrollo que arranca un programa nuestro en vez de lanzar directamente el test final.

Ese programa puede:

- esperar ordenes o payloads;
- lanzar tests DOS desde un entorno más controlado;
- drenar actividad de disco antes del takeover;
- exponer estructuras de telemetría y postmortem en memoria;
- y servir de "launcher" para los casos de la batería.

Ventaja:

- reusa parte del ecosistema DOS/Workbench;
- menos agresivo que un metal puro;
- muy bueno para automatización temprana.

### Fase 2. Harness metal directo

A partir del stub ya propuesto en:

- [common/metal/README.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/common/metal/README.md)
- [_template-metal/README.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/_template-metal/README.md)

El harness metal debería:

- instalar stack propio;
- registrar boot state;
- opcionalmente capturar excepciones 68000;
- cargar payloads con formato sencillo;
- y entrar en un `battery_metal_main()` conocido.

Hoy ya existe la base de ese contrato: `g_battery_metal_control`, validado por [M10_smoke_metal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/M10_smoke_metal/README.md), con `stage`, `heartbeat`, `command` y `command_arg` en RAM.

Ventaja:

- control maximo;
- ideal para probes y tests de hardware crudos;
- evita ambiguedades de AmigaDOS.

## Metodo recomendado “infalible”

Para este proyecto, la secuencia más robusta no es una sola ruta, sino dos rutas explicitas y un puente entre ellas:

1. **Ruta DOS normal**
   - el harness arranca desde `ADF` o hardfile
   - espera orden en un mailbox de RAM
   - usa `LoadSeg(PROGDIR:payload.exe)` y la ejecución DOS más conservadora validada en A500/Kick 1.3
   - libera con `UnLoadSeg()`

2. **Ruta metal**
   - stubs con pila privada y excepciones propias
   - sin depender del contrato DOS
   - ideal para probes, postmortem y payloads muy bajos

3. **Fallback de transporte**
   - si el mailbox DOS no esta listo, generar ADF nuevo e insertarlo en `DF0:`
   - si el ciclo de iteracion lo pide, migrar a hardfile/HDF persistente

## Ruta `devfs` disponible hoy

Sin llegar aún a un `HDF` real, el repo ya tiene una vía intermedia útil para binarios DOS del toolchain:

- config [mcp-amiga-battery-devfs.uae](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/.vscode/mcp-amiga-battery-devfs.uae)
- staging de `a.exe` en `out/devfs/` con [scripts/stage-devfs-binary.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/stage-devfs-binary.mjs)
- captura/evidencia con [scripts/capture-devfs-battery-evidence.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/capture-devfs-battery-evidence.mjs)

Esta ruta reutiliza el mecanismo que ya funcionaba para la app principal:

- `dh0:` contiene el entorno de arranque de la extensión de Amiga Debug
- `dh1:` apunta a `out/devfs/`
- `debugging_trigger=:a.exe` lanza el ejecutable staged

No sustituye al objetivo final del harness DOS o del hardfile, pero ya da una carga DOS más repetible que el floppy puro y un mejor punto de observación para `qOffsets`.

Además, ya existe una validación positiva con ejecutables DOS standalone mínimos en:

- [experimentos DOS standalone](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/experiments/README.md)

La evidencia más fuerte es `dos_min_cli.exe`, que arranca por `devfs` en A500/Kickstart 1.3 y deja `DOS_MIN_CLI_OK` visible en pantalla. Eso confirma que la base `dh0/dh1 + debugging_trigger=:a.exe` es valida para ejecutables DOS sencillos y que el bloqueo actual de `M00`/`M03` no debe seguir leyendose como "no se pueden cargar binarios GCC DOS".

## Capacidades deseables del dev harness

- tabla publica de estado de arranque;
- log circular simple en memoria;
- hooks de excepcion 68000 (`ILLEGAL`, `BUS ERROR`, `ADDRESS ERROR`);
- handshake con MCP mediante estructura de memoria conocida;
- carga de payload por nombre/ruta DOS (`PROGDIR:payload.exe`) mediante `LoadSeg`;
- ejecución del payload mediante `RunCommand`;
- opcion de delegar a payload metal con stack privada;
- restauración parcial o reset controlado;
- utilidades de captura de registros/custom/dumps.

## Relacion con el engine

Este harness no es el engine del juego.

Su papel es ser infraestructura de desarrollo:

- el engine implementa capacidades gráficas/audio reutilizables;
- la batería valida técnicas;
- el dev harness facilita cargar, ejecutar y autopsiar pruebas de forma repetible.

## Estado actual

- La ruta `ADF` de `T01` sigue chocando con actividad de disco/requester.
- La ruta `direct` ya carga/verifica, pero el binario no alcanza ni la primera marca del harness común.
- La ruta `metal/direct` ya tiene una prueba viva reproducible en `tests/amiga-battery/M10_smoke_metal/`: entra por `battery_metal_entry`, captura pantalla visible y permite parchear simbolos en RAM (`g_m10_palette_mode`) sin depender de AmigaDOS.
- Esa misma ruta ya expone `g_battery_metal_control`, que es el contrato candidato para que el futuro dev harness DOS-friendly delegue sobre payloads metal sin inventar otro protocolo distinto.
- `M03_dev_harness_disk` ya reserva también un mailbox de loader (`payload_path`, `payload_stack_size`, `loader_result`, `loader_ioerr`, `payload_seglist`) para avanzar hacia `LoadSeg`/`RunCommand`.
- M03 ya empaqueta también un payload DOS auxiliar (`PROGDIR:payload.exe`) para que la primera validación del loader no dependa de un binario grande o ambiguo.
- Ya existe una base mínima para la vía metal en `tests/amiga-battery/common/metal/`.

Por tanto, un **dev harness disk** ya no es una idea exotica: es una siguiente pieza bastante razonable del sistema.

## Regla de compatibilidad para implementaciones futuras

Cuando se documente o implemente una API del harness:

- marcar si esta pensada para **A500/Kick 1.3**
- marcar si es **compatible también con A600**
- y marcar como **extensión posterior** cualquier dependencia real de OS/ROM más modernos, especialmente en la parte DOS/procesos
- En la ruta `devfs` usada para desarrollo rápido (`filesystem2=rw,dh1:.../out/devfs`), los payloads DOS deben cargarse por `dh1:payload.exe`. `PROGDIR:` no es una suposición segura en este flujo y puede disparar requesters de volumen inexistente aunque el binario y `LoadSeg()` sean correctos.
