# Consulta (castellano): KS 1.3 — bucle de mensajes VBlank cooperativo (`Wait`) frente a takeover exclusivo del display (`Forbid`); la cadena VERTB de exec se detiene a los ~21 frames

> Registro local de la consulta enviada a Grok en inglés (`consulta-ks13-cooperative-display-wait-en.md`). Continuación de `consulta-ks13-trap-vertb.md`, `consulta-ks13-trap-vertb-seguimiento.md` y `consulta-224-intena-vertb-takeover.md`. Emulador WinUAE-DBG, A500 ciclo-exacto, Kickstart 1.3, proceso AmigaDOS normal lanzado desde `:a.exe`.

## Dos modos y el conflicto

Queremos (1) un bucle de juego **dirigido por mensajes**: el engine registra un servidor de VBlank, publica `MsgType::VBlank` o levanta una señal Exec cada frame, y el bucle duerme en `Wait()`/`WaitPort` en vez de sondear; y (2) un **display fullscreen propio** (Copper, sprites, DMA). En KS 1.3 chocan.

**Modo exclusivo (funciona, pero no puede dormir).** `takeover_system()`: `Forbid()` + `OpenLibrary(graphics)` + `LoadView(NULL)` + 2×`WaitTOF()` + `OwnBlitter()` + `WaitBlit()`; guarda DMACON/INTENA/ADKCON; la demo toma el Copper. El engine instala VBlank/BLIT con `AddIntServer(INTB_VERTB/BLIT)`. Con `VblankWait::Active` (sondeo de VPOSR, sin `Wait`) corre **35 s a ~50 fps sin depurador**; los contadores dan `adds=1`, `calls≈frames` y `seq` avanzando. `Wait()` con `Forbid` es ilegal (RKM), así que este modo solo puede sondear.

**Modo cooperativo (falla).** Sin `Forbid()`, pero con `LoadView(NULL)` + 2×`WaitTOF()` + `OwnBlitter()`/`WaitBlit()`; `AddIntServer(INTB_VERTB)`; la demo toma el Copper; el bucle arma una señal Exec y duerme en `Wait(1<<sig)`; el servidor llama al heartbeat y levanta la señal; **no escribimos INTENA** (lo posee exec). Resultado: READY en el frame ~18 y el contador se queda en 18/19/26; no avanza.

- Contadores de diagnóstico leídos por canal lateral **sin GDB**: `adds=0x2001` (un AddIntServer), `calls=0x1015` (21 llamadas), `seq=0x15` (21): el servidor se llamó 21 veces y dejó de llamarse.
- En el parón: `INTENAR=0x602C` (INTEN=1, VERTB=1), `INTREQR=0x1060` (DSKSYN+BLIT+VERTB pendiente), `SR=0x0004` (IPL 0), `PC=0x00FDFF76` (ROM). VERTB está **habilitada y pendiente**, pero la cadena de nivel 3 ya no ejecuta nuestro servidor.
- También probamos escribir INTENA tras AddIntServer (26 frames), limpiar INTENA en el takeover (18) y prioridad 100 del servidor (sin cambio). Sin GDB el fallo persiste, así que no es el depurador.

## Preguntas

1. **¿Qué detiene la cadena a los ~21 frames?** Con VERTB habilitada y pendiente y la CPU a IPL 0, ¿por qué no se despacha el nivel 3? ¿graphics.library/Intuition recupera el vector de nivel 3 con `SetIntVector` (saltándose la cadena de servidores de exec) al llamar a `LoadView(NULL)`/`OwnBlitter` **sin Forbid**? ¿Una tarea de display/bookkeeping restaura después su propio vector y elimina nuestra cadena? ¿Se deshace el AddIntServer o se sustituye el vector? ¿Qué rutina de ROM lo hace (en la 212 vimos `$FCD5AC` limpiar VERTB; aquí VERTB sigue habilitada pero pendiente)?
2. **¿Es legal/soportado `LoadView(NULL)` + `OwnBlitter()` sin `Forbid()`?** ¿Cuál es el orden canónico en cooperativo: AddIntServer antes o después de LoadView/WaitTOF/OwnBlitter? ¿Hay que evitar LoadView/OwnBlitter si queremos conservar la cadena de exec y `Wait()`? ¿Es compatible `OwnBlitter` con el SO vivo? ¿Hace falta `Disable()`/`Enable()` alrededor de la instalación, o un camino con `SetIntVector` en vez de AddIntServer?
3. **¿Puede un proceso de KS 1.3 tener a la vez** display fullscreen propio (Copper/DMA) y bucle `Wait()`/`WaitPort()` con mensajes de VBlank? ¿O solo son válidas (a) display exclusivo + `Forbid` + polling/activo (sin Wait) y (b) display del SO/Intuition + `AddIntServer` + Wait/mensajes? ¿Qué hacen los juegos/demos clásicos de KS 1.3 que quieren fullscreen y volver al SO o usar DOS? ¿Hay referencias (RKM, Aminet, código) de ese patrón?
4. **Si el fullscreen cooperativo es imposible**, ¿cuál es el API de alto nivel recomendado? Queremos ocultar la elección al developer: dos modos explícitos, p. ej. `Exclusive` (display propio, sin Wait, polling/activo) y `Cooperative` (display del SO/ventana, mensajes VBlank, WaitPort), con la misma lógica de juego. ¿O hay un tercer patrón (tarea de hardware que no hace Forbid, puerto de mensajes y tarea principal que espera) que dé ambos? `Forbid()` es global: una tarea de hardware que lo mantenga impide correr a la principal; ¿hay alguna forma soportada en KS 1.3?
5. **Confirmar la regla Forbid/Wait**: `Wait()`/`WaitPort()` con `Forbid()` activo es ilegal/bloquea. ¿Basta `Disable()`/`Enable()` (interrupciones) para las secciones críticas cortas (AddIntServer/LoadView) sin prohibir tareas? ¿`Disable()` alrededor de LoadView/AddIntServer evita que el display/exec recupere el vector de VBlank y permite luego `Wait()`? ¿Ventajas y riesgos?
6. **Detección/diagnóstico en el engine**: ¿cómo distinguir en runtime "nuestro servidor fue eliminado/sustituido" de "VERTB deshabilitada en INTENA" de "la CPU no puede tomar la IRQ"? Tenemos INTENAR/INTREQR y contadores de llamadas; ¿hay algún campo de Exec/ROM o técnica de monitor para ver el vector de nivel 3 instalado y si el nodo sigue en la cadena? ¿Qué debe comprobar el engine antes de declarar usable el modo cooperativo?

## Ya descartado (no volver a sugerirlo)

- Perturbación de GDB/depurador: el fallo cooperativo se reproduce solo con canal lateral, sin cliente.
- Solape/corrupción de memoria (rangos auditados; la build exclusiva con las mismas reservas es estable).
- VERTB deshabilitada en INTENA: `INTENAR=0x602C` la tiene habilitada y `INTREQR=0x1060` pendiente.
- Prioridad/orden del servidor: probada prioridad 100; el servidor se llama 21 veces antes de parar.
- Escritura manual de INTENA: probado no escribirla, escribirla tras AddIntServer y limpiarla en el takeover; todo falla.
- Que falte una señal de Wait: el contador de llamadas del servidor se para en 21; el servidor ya no se invoca.
- El camino exclusivo con Forbid + AddIntServer + Active funciona (35 s, 50 fps, sin GDB).

## Anexo (2026-10-22): el cooperativo limpio sin Copper también se para tras un VBlank

Tras la respuesta de los dos modos, probamos el camino cooperativo realmente limpio en una app **CLI/overlay** (demo 212): **sin** `LoadView`, **sin** `OwnBlitter`, **sin** escribir Copper/DMACON, sin takeover de display y sin teclado/CD32 (solo sondeo de joystick en el latido). La app solo llama a `AddIntServer(INTB_VERTB)`, el engine levanta una señal Exec en el servidor y la tarea principal duerme en `Wait()`. **No** llega al frame 60; el run-status se queda en frame 0 con `detail=0x10010000` (pack de diagnóstico: palabra baja de `g_irq_diag_vbl_calls` = 0x1001, es decir **una** llamada al servidor, frame = 0). El primer frame se entrega y el servidor no vuelve a llamarse. Es un fallo distinto del de 21 frames: aquí no hay takeover de Copper en absoluto.

El engine no escribe `INTENA` para instalar el servidor; solo **setea** VERTB (`INTENA = SETCLR|VERTB`) si `INTENAR` la muestra apagada antes de esperar. La app corre desde la CLI de AmigaDOS con solo el overlay de depuración del host; no abre ninguna pantalla/ventana de Intuition.

Preguntas nuevas para este caso limpio:

1. En un proceso **CLI** de KS 1.3 (sin pantalla/ventana de Intuition abierta por la app), ¿VERTB está normalmente habilitada y graphics/Intuition la mantienen? Si `AddIntServer` no es el primer servidor, no habilita la interrupción; ¿la contabilidad de `INTENA`/`Wait`/`Switch` de exec apaga VERTB tras el primer frame porque la pantalla del SO está estática? ¿Cómo mantiene VERTB un proceso CLI que duerme en `Wait()` sin tomar el display?
2. ¿Hace falta abrir una pantalla/ventana mínima de Intuition (o llamar a `WaitTOF`) para que graphics/Intuition mantenga vivo el servidor/interrupción de VBlank? ¿Es ese el patrón cooperativo canónico, o un CLI + overlay del host + `Wait()` es imposible?
3. Si `AddIntServer` no basta en este entorno, ¿la alternativa soportada es `SetIntVector`/`Disable`+`Enable` o instalar el servidor de VBlank antes que graphics? ¿Cuál es la app mínima soportada que recibe una señal de VBlank con el SO/display vivo?


## Anexo 2 (2026-10-22): aplicadas las correcciones del checklist; sigue una llamada

Aplicamos el checklist de la última respuesta: el servidor es un stub ASM que salva D2-D7/A2-A6, llama al cuerpo C, los restaura, deja **A0=** y termina con MOVEQ #0,D0; RTS (sin RTE); la prioridad del servidor VERTB se probó a **100** y a **-60**; el camino cooperativo **no** sustituye el vector ni toca el Copper, y solo setea INTENA = SETCLR|INTEN|VERTB si falta alguno de esos bits antes de esperar. La señal es AllocSignal(-1) + FindTask(NULL) + Signal(task, 1<<sig) desde el servidor; la tarea principal espera por el mismo bit. El resultado sigue siendo exactamente **una** llamada al servidor (frame 0, g_irq_diag_vbl_calls=0x1001); el primer frame se entrega y luego silencio. Igual con teclado/CD32 habilitados o deshabilitados.

Preguntas pendientes: ¿cómo inspeccionar, sin GDB, si el nodo sigue en la cadena VERTB y si el chain walker lo alcanza? ¿Puede el servidor de graphics/Intuition posterior al nuestro seguir rompiendo la cadena a pesar de A0/Z? ¿Es compatible Signal() en cada VBlank desde el servidor con el patrón CLI del RKM, o provoca un Wait/Switch que restaura INTENA y apaga VERTB? ¿Cuál es el servidor+bucle mínimo exacto de KS 1.3 (prioridad, A0, Z, Signal, INTENA) que incrementa un contador **y** despierta una tarea dormida cada frame desde la CLI?


## Anexo 3 (2026-10-22): la sonda contador-only también se para a las dos llamadas

Instalamos un servidor VERTB mínimo en el camino **polling** de la 212: struct Interrupt estático, prioridad -60, el stub ASM (A0=, Z, RTS), cuerpo que solo incrementa un contador (**sin Signal, sin INTENA, sin INTREQ, sin camino Wait**). La captura del runner 0,5 s después de READY muestra bl: 0x1002 (dos llamadas) y rames: 0; a 50 Hz deberían ser ~25. Por tanto la cadena/VERTB se para a los dos campos incluso sin el Wait/Signal cooperativo. Esto cambia el diagnóstico: la llamada única no se explica solo por el Wait/INTENA; hay que inspeccionar nodo/cuerpo/cadena/lado del SO. Siguiente: sonda **independiente** (sin engine/demo) con breakpoint en el servidor y watchpoint en INTENA/vector de nivel 3, más PC/INTENAR/INTREQR por canal lateral en el parón; después perfil.


## Resolución (2026-10-22): desbordamiento del HUD, no la cadena del SO

La cadena cooperativa nunca fue el problema. Las sondas mínimas en la máquina alcanzaron 60 VBlanks en todas las capas: servidor directo contador-only (006), servicio cooperativo del `AmigaBackend` (007), `Engine` (008), `MessagePumpGame` (009), `os::init` con joystick/toda la entrada (010/011), tareas (012) y overlay/input/timer tipo demo (013). El fallo real era el buffer crudo `char line[64]` del HUD con los `append` locales: la línea de contadores mide >64 bytes y desbordaba la pila. El polling lo toleraba; el bucle cooperativo corrompía la pila y se paraba tras el primer frame. El HUD ahora usa `eng::util::TextLine<160>` + `operator<<`/`hex`; la 212 corre en cooperativo (frames 57, msgs 30, t 29, bg 29) con el display del SO y sin takeover de Copper. No hacen falta pantalla de Intuition, `WaitTOF`, cambio de vector, escritura de `INTENA` ni más preguntas a Grok. Los fallos de 21 frames y de una llamada eran el mismo camino de corrupción con variantes de overlay, no una prueba de que CLI + `AddIntServer` + `Wait()` sea imposible.
