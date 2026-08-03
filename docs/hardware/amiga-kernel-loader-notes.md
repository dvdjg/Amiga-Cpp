# Kernel y Loader del Amiga 500

## Documentos padre

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Indice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Cargador de desarrollo Amiga](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-dev-harness-loader.md)
- [Spec de batería](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md)

## Objetivo

Reunir la información mínima que necesitamos para dejar de tratar el arranque de binarios Amiga como una caja negra.

La idea central es sencilla:

- `exec.library` gobierna tareas, memoria, señales, interrupciones y librerias base.
- `dos.library` gobierna procesos, path resolution, ficheros, handlers y carga de ejecutables.
- un `dos_hunk_exe` normal no debería arrancarse como si fuera un bloque de código crudo; lo correcto es dejar que AmigaDOS le prepare su contexto.

## Alcance por maquina y ROM

Este documento esta escrito primero para **Amiga 500 con Kickstart 1.3 / AmigaOS 1.3**, que es la base conservadora del proyecto.

Regla editorial:

- si una API o comportamiento es seguro para A500 + Kickstart 1.3, se describe como ruta base;
- si algo es más propio de ROMs o DOS posteriores, se marca como posterior o como opcion menos conservadora.

### Matriz rápida

| Entorno | Prioridad en este proyecto | Nota |
|--------|-----------------------------|------|
| **Amiga 500 + Kickstart 1.3** | **Base principal** | Todas las decisiones del loader deben ser validas aqui salvo que se indique lo contrario. |
| **Amiga 600** | Compatible en gran parte a nivel OS loader/DOS | Puede compartir muchas rutas DOS, pero no es la referencia principal. |
| **Amiga 1200** | Secundario / ampliado | Admite más memoria, AGA y APIs más modernas; no debe dictar la ruta base del loader. |

## Capas del sistema relevantes

### Exec

`exec.library` es el microkernel del sistema:

- scheduler y listas de tareas
- memoria (`AllocMem`, `FreeMem`)
- señales, puertos y mensajes
- bibliotecas, dispositivos y recursos
- interrupciones y vectores de bajo nivel

Punto importante para este proyecto:

- una `Task` pura es un bloque de bajo nivel;
- puede servir para utilidades muy cercanas al hardware;
- pero no es el vehiculo correcto para llamar a DOS con comodidad.

La propia autodoc de `AddTask()` advierte que las tasks son un bloque de construcción de bajo nivel y que, en general, no deben llamar a `dos.library`; para eso existen los `Process` de AmigaDOS.

### AmigaDOS

`dos.library` añade el entorno de proceso:

- current directory y `PROGDIR:`
- `Input()` / `Output()`
- lista DOS de devices, volumes y assigns
- shell/CLI
- carga de binarios (`LoadSeg`, `RunCommand`, `CreateProc`, `CreateNewProc`)

Para nuestro problema, esta es la capa decisiva.

Advertencia de compatibilidad:

- para A500/Kickstart 1.3 debemos pensar primero en el conjunto clasico `LoadSeg`, `UnLoadSeg`, `CreateProc` y flujo DOS tradicional;
- APIs con tags o extensiones más nuevas deben tratarse como posteriores salvo verificación explicita.

## Como carga un ejecutable AmigaDOS

Ruta conceptual:

1. Resolver un path como `DF0:prog`, `SYS:Utilities/Foo` o `PROGDIR:payload.exe`.
2. Pedir a DOS que abra y cargue el ejecutable Hunk.
3. Obtener una `seglist` (`BPTR`) con `LoadSeg()`.
4. Ejecutar esa `seglist` en un contexto de proceso válido, normalmente via `RunCommand()`, `CreateProc()` o una ruta equivalente.
5. Liberar la `seglist` con `UnLoadSeg()` cuando ya no se use.

Consecuencia directa:

- si MCP escribe el binario en RAM y pone `PC` a mano, eso no equivale a `LoadSeg() + proceso DOS`.
- por eso un binario GCC puede estar bien escrito en memoria y aún así morir antes de `main`.

## `LoadSeg`, `RunCommand`, `CreateProc`

### `LoadSeg`

Es la API base para cargar un ejecutable AmigaDOS desde fichero y obtener una `seglist`.

Uso mental:

- entrada: path DOS
- salida: `BPTR` a la lista de segmentos
- en error: `0`, con detalle en `IoErr()`

Es la piedra angular del método robusto que nos interesa.

### `RunCommand`

Ejecuta una `seglist` ya cargada.

Nos interesa porque:

- evita que el harness tenga que inventar un parser Hunk propio en la ruta DOS;
- deja el flujo de carga dentro del contrato del sistema operativo;
- se adapta mejor a payloads generados por GCC que un salto bruto a una dirección.

Precaucion:

- conceptualmente es una vía de shell/proceso, no un sustituto de `PC=entry`.
- si el harness DOS no esta sano, `RunCommand` tampoco va a salvar la situación.

### `CreateProc` / `CreateNewProc`

Sirven para crear procesos de AmigaDOS.

Implicacion practica:

- para A500/Kick 1.3 la opcion conservadora es apoyarse primero en `LoadSeg` y el flujo DOS más clasico;
- las APIs más modernas o basadas en tags pueden reservarse para perfiles nuevos o builds posteriores.

Regla explicita por version:

- **A500 / Kick 1.3**: pensar primero en `LoadSeg` + launcher DOS clasico
- **A600**: normalmente compatible con la ruta anterior
- **A1200 / ROMs más nuevas**: aqui si tiene más sentido abrir la puerta a `CreateNewProc` y variantes de tags

## Process vs Task

Regla de oro para este repo:

- si el código necesita DOS, usa proceso DOS;
- si el código quiere control casi total del hardware y postmortem muy bajo nivel, usa `metal/direct`.

Traducido a nuestra arquitectura:

- `M03_dev_harness_disk` debe ser DOS-friendly.
- `M10_smoke_metal` debe seguir siendo el sitio para excepciones 68000, pila privada y payloads “sin SO”.

## Memoria en A500: chip, slow, fast

Para el método de carga tenemos que distinguir:

- **Chip RAM**: visible por Agnus y coprocesadores; obligatoria para bitplanes, audio DMA, sprites, copper lists, etc.
- **Slow RAM**: expansion conectada al bus del sistema, accesible por CPU pero no equivalente a fast real para el chipset.
- **Fast RAM**: solo CPU; mejor candidata para cargas, tool payloads y binarios de depuración fuera de paths estrictamente DOS.

Regla practica:

- datos gráficos/audio DMA y estructuras del display en chip RAM;
- cargadores, payloads de depuración y binarios experimentales en fast RAM cuando no dependan del loader del sistema;
- si el código es AmigaDOS normal, preferir `LoadSeg` y dejar que el sistema gestione memoria/segmentos.

## Librerias, devices y handlers

Tres conceptos que no conviene mezclar:

- **Library**: API compartida en memoria, abierta con `OpenLibrary()`.
- **Device**: endpoint de I/O, normalmente abierto con `OpenDevice()`.
- **Handler / file system**: proceso que atiende paths y packets DOS.

Esto importa porque:

- montar `DF0:` o un hardfile no es “abrir un fichero normal”: hay devices, file systems y la DOS list de por medio;
- el fallback por ADF o hardfile es válido precisamente porque le devuelve el control al stack normal de DOS.

## DosList, devices, volumes y assigns

AmigaDOS resuelve nombres a traves de la DOS list:

- devices
- volumes
- assigns
- file systems / handlers

Para un método “infalible”, `PROGDIR:` es especialmente útil:

- el harness puede arrancar desde disco
- y luego cargar `PROGDIR:payload.exe` sin depender de rutas absolutas

Eso simplifica mucho un dev disk o un hardfile de desarrollo.

## Conclusiones para el proyecto

### Lo que no debemos seguir haciendo

- asumir que `dos_hunk_exe` y `payload metal` son la misma clase de artefacto
- usar `winuae_load + PC` como ruta “normal” para binarios DOS

### Lo que si debemos hacer

1. Mantener una ruta **DOS/loader** para ejecutables GCC normales:
   - ADF o hardfile
   - harness DOS
   - `LoadSeg` / `RunCommand`

2. Mantener una ruta **metal/direct** separada:
   - stack privada
   - excepciones 68000
   - parcheo directo en RAM
   - payloads pensados para ello

3. Unir ambas rutas con un **harness residente**:
   - arranca desde disco
   - publica mailbox en RAM
   - recibe evento/comando
   - carga `PROGDIR:payload.exe`
   - o delega a un payload metal conocido

## Nota de compatibilidad final

Siempre que en este repo hablemos de "kernel" o "loader" sin más calificativos, debe entenderse:

- **primero: Amiga 500 + Kickstart 1.3**
- **despues: extensiones compatibles con A600/A1200 si no rompen la ruta base**

## Fuentes consultadas

- [Amiga ROM Kernel Reference Manual: DOS (Thomas Richter, 2024)](https://developer.amigaos3.net/sites/default/files/downloads/2024-10/Amiga_ROM_Kernel_Reference_Manual_DOS.pdf)
- [Autodocs de `AddTask`](https://developer.amigaos3.net/autodocs/exec.library/AddTask.html)
- [Indice de autodocs Amiga](https://d0.se/autodocs)
