# Canal lateral de depuracion para WinUAE-DBG

Este documento especifica y registra el canal lateral de WinUAE-DBG. El objetivo
es que la IA pueda observar una instancia viva sin competir por el socket GDB que
usa Cursor/VS Code, y que las pruebas automaticas no dependan de esperas largas ni
solo de capturas de pantalla.

## Estado actual

Hay un MVP operativo en `WinUAE-DBG/od-win32/barto_gdbserver.cpp`.

- Escucha en `127.0.0.1:2346`.
- El puerto puede cambiarse con `WINUAE_SIDE_CHANNEL_PORT`.
- El protocolo es texto, una orden por linea.
- Cada respuesta es una unica linea JSON.
- El runner `tools/run/run-demo.mjs` lo usa para esperar `g_amg_run_status` en
  memoria mientras el 68000 sigue ejecutando.
- La regresion `20260529-120303` confirma `side-channel READY` en las demos
  `000`, `010`, `020` y `030`.

Comandos implementados:

```text
hello
state
regs
mem <hex-address> <length>
runstatus <hex-address>
```

`state` devuelve, entre otros campos, `baseText`, `sections`, `pc`, `sr` y
`cycles`. `sections` es importante porque un simbolo como `g_amg_run_status` puede
vivir en `.data`, no en `.text`; el runner resuelve la direccion runtime usando el
mapa del linker y los hunks reales reportados por WinUAE-DBG.

## Problema que resuelve

El flujo actual permite que la IA lance WinUAE, conecte por GDB/monitor, lea y
escriba memoria, capture pantalla, use profiler e inyecte input. Eso sirve para
regresiones y pruebas automatizadas.

Lo que todavia no resuelve de forma robusta es el caso colaborativo:

1. David esta depurando desde Cursor/VS Code.
2. La sesion WinUAE ya esta viva y contiene el fallo.
3. La IA debe entrar en esa misma instancia, observar memoria/registros/perfiles,
   quizas pausar la CPU, cargar codigo de diagnostico o parchear memoria.
4. La sesion manual debe seguir siendo recuperable.

El GDB server de WinUAE se comporta como un canal de un solo dueno. Si el debugger
humano esta conectado, otro cliente no puede asumir que podra conectarse sin
interferir. Ademas, se ha observado que algunas instancias no vuelven a aceptar
una nueva conexion GDB despues de que el cliente se desconecte.

## Direccion recomendada

La opcion recomendada es anadir a WinUAE-DBG un canal lateral independiente del
servidor GDB usado por VS Code/Cursor.

```text
Cursor / VS Code debugger  --->  GDB RSP actual  --->  WinUAE-DBG

IA / MCP / tools           --->  canal lateral    --->  WinUAE-DBG
```

Ese canal lateral puede ser un socket TCP local, named pipe o servidor MCP embebido
en el proceso WinUAE. Al principio conviene mantenerlo como protocolo simple sobre
localhost y despues envolverlo con MCP.

## Capacidades minimas

El MVP actual ya cubre una parte de observacion. El objetivo completo del canal
lateral debe exponer:

- estado de la emulacion: running/stopped, frame, vpos/hpos si esta disponible;
- pausar y reanudar CPU sin competir con el GDB server;
- lectura de registros 68k;
- lectura y escritura de memoria Amiga;
- comandos monitor seguros: screenshot, disasm, memcfg, profiler, input;
- captura de pantalla a ruta solicitada;
- control de raton/teclado/joystick emulado;
- listado de breakpoints/watchpoints visibles si WinUAE puede exponerlos;
- identificador de sesion y metadatos de configuracion cargada.

## Capacidades avanzadas

Cuando el MVP sea estable, el canal lateral debe crecer hacia:

- snapshots/savestates antes de operaciones peligrosas;
- reserva de una zona scratch para diagnostico;
- carga de codigo maquina 68k en caliente;
- ejecucion controlada de rutinas de diagnostico;
- trampolines temporales y parches reversibles;
- busqueda de patrones en memoria;
- resolucion de simbolos desde ELF/map del programa cargado;
- volcados de Copper, bitplanes, sprites, blits pendientes y DMA;
- profiler por frame y presupuesto de raster/DMA;
- export de evidencias reproducibles para `out/debug-sessions`.

## Politica de seguridad

Las operaciones deben clasificarse por riesgo:

- Lectura segura: memoria, registros, disasm, screenshot, profiler.
- Control leve: input emulado, pausa/reanudar, warp mode.
- Escritura reversible: poke de memoria con copia previa.
- Escritura peligrosa: parche de codigo, cambio de registros, trampolines.
- Ejecucion inyectada: codigo maquina cargado en caliente.

Para las tres ultimas categorias el canal debe:

1. tomar un debug lock;
2. pausar CPU;
3. registrar PC/SR/registros relevantes;
4. crear savestate o copia de bytes afectados cuando sea posible;
5. aplicar el cambio;
6. verificar lectura posterior;
7. registrar una entrada de auditoria;
8. permitir rollback.

La IA no debe escribir memoria ni cargar codigo sin dejar rastro documental de
direccion, longitud, valor anterior, valor nuevo y motivo.

## Coordinacion con el debugger humano

El canal lateral debe tener una politica explicita de propiedad temporal:

- `observe`: la IA solo lee estado; no pausa ni modifica.
- `assist`: la IA puede pausar brevemente, capturar y perfilar.
- `takeover`: la IA toma el debug lock para modificar memoria o ejecutar codigo.

Si VS Code/Cursor esta en una operacion critica, el canal lateral debe poder
responder "ocupado" en lugar de forzar una pausa.

## Relacion con el GDB proxy

Hay una alternativa: crear un broker/proxy GDB al que se conecten tanto VS Code
como la IA, manteniendo una sola conexion real con WinUAE. Es viable, pero mas
delicado porque habria que arbitrar todos los paquetes RSP y los estados de
continue/step/breakpoint.

El canal lateral es preferible para este proyecto porque:

- no rompe el flujo normal de depuracion del plugin;
- permite exponer herramientas no-GDB como profiler, capturas y recursos DMA;
- evita que la IA compita por el mismo socket GDB;
- encaja mejor con futuras operaciones de inspeccion grafica y hot patching.

## Criterio de aceptacion

Parte ya demostrada:

- La IA se conecta al canal lateral mientras el runner mantiene la conexion GDB.
- La IA lee registros, PC/SR, ciclos y memoria sin detener el 68000.
- El runner espera `RunStatus::Ready` sin usar el timeout largo de captura.
- La regresion completa conserva las capturas y los analizadores visuales.

Pendiente para depuracion colaborativa profunda:

- David arranca una sesion normal de debug desde Cursor/VS Code.
- La IA se conecta al canal lateral de esa misma instancia.
- La IA captura pantalla, lee registros y memoria sin romper la sesion.
- La IA pausa, inspecciona y reanuda con un debug lock visible.
- La IA escribe un byte de memoria en una zona segura, verifica y revierte.
- La IA carga una rutina 68k pequena en zona scratch, la ejecuta y restaura estado.
- Todo queda documentado en un log de sesion.

El siguiente incremento natural es anadir modos `observe/assist/takeover`, debug
lock, auditoria de escrituras y comandos seguros para screenshot/profiler/input
sin pasar por GDB.
