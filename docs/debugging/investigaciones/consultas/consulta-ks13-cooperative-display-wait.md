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
