# Consulta (castellano): KS 1.3 sigue borrando VERTB de INTENA bajo un proceso AmigaDOS vivo — ¿por qué y cuál es la secuencia correcta de takeover/restore?

> Registro local de la consulta que se envía a Grok en inglés (`consulta-224-intena-vertb-takeover-en.md`). Continuación de `consulta-ks13-trap-vertb.md` y `consulta-ks13-trap-vertb-seguimiento.md`. Demo: `demos/techniques/amiga/sprites/224_sprite_reuse_carousel` (A500 OCS PAL, 320x256, 5 planos, 8 canales de sprite reutilizados 5 veces en vertical). Emulador: WinUAE-DBG (fork BartmanAbyss), A500 ciclo-exacto, Kickstart 1.3, `quickstart=a500,1` (512K chip + 512K slow), `cpu_cycle_exact=true`, `cpu_memory_cycle_exact=true`, `blitter_cycle_exact=true`, `ntsc=false`.

## Contexto: cómo se ejecuta el programa

El runner arranca KS 1.3 y ejecuta esta startup-sequence:

```
stack 131072
cd dh1:
:a.exe
```

Así que `a.exe` es un **proceso AmigaDOS normal**: el Shell padre sigue vivo y bloqueado esperando al hijo, y exec/intuition/graphics/dos siguen residentes. No es un bootblock ni un trackloader; lo carga dos.library y después intenta tomar el control del display.

Bootstrap del engine (`eng::amiga::run_demo`):

1. `ensure_boot()` (SysBase desde `*(4)`), `configure_memory`.
2. `app.set_display(display)` + `app.start()`; el `init` de la demo reserva Chip RAM y llama a `app.device().takeover_copper(m_lists[0].mem_view())`.
3. `AmigaBackend::takeover_display(list)`: espera `(VPOSR & 0x1ff00) == (311 << 8)`; `INTENA = 0x7FFF`; `INTREQ = 0x7FFF`; `wait_blitter()`; `DMACON = 0x7FFF`; `COPCON = 0x0002`; `COP1LC = lista`; `DMACON = SETCLR|MASTER|COPPER`; `COPJMP1 = 0x7fff`.
4. `Engine::run_frames` instala el servicio de VBlank: `set_vblank_service(...)` -> `level3_sync()`, que sustituye el autovector de nivel 3 (`$6C`) por `level3_irq` (guardando el vector de la ROM) y escribe `INTENA = SETCLR|INTEN|VERTB` (`0xC020`).
5. Por frame, `wait_vblank_run()`: al entrar, si `g_vbl_task != nullptr && (INTENA & 0x20) == 0`, vuelve a llamar a `level3_sync()` (rearme); después sondea `VPOSR` hasta `(VPOSR & 0x1ff00) == (311 << 8)`, hace `debug_stop_idle()` y retorna; el bucle llama a `update()`/`render()`.

La demo usa `VblankWait::Active`: **no duerme en `Wait()`**, sondea VPOSR y confía en la IRQ de nivel 3 para el latido (`hb.frames`). El frame solo avanza cuando la IRQ incrementa ese contador.

### El código de takeover/restore existe, pero run_demo no lo llama

`AmigaBackend` tiene takeover de sistema y restauración:

```cpp
bool AmigaBackend::takeover_system() {
    if (g_sys.active) return true;
    Forbid();                                  // 1) congelar el planificador
    g_sys.forbidden = true;
    GfxBase = OpenLibrary("graphics.library", 0);
    if (GfxBase) {
        g_sys.old_view = GfxBase->ActiView;
        LoadView(nullptr);                     // 2) matar el display de Intuition
        WaitTOF(); WaitTOF();
        OwnBlitter(); WaitBlit();
    }
    g_sys.old_dmacon = LECTURA_DMACON | 0x8000; // 3) guardar estado hardware
    g_sys.old_intena = LECTURA_INTENA | 0x8000;
    g_sys.old_adkcon = LECTURA_ADKCON | 0x8000;
    g_sys.active = true;
    return true;
}

void AmigaBackend::restore_system() {
    // 1) wait_blitter; DMACON/INTENA/INTREQ/ADKCON = 0x7FFF
    // 2) restaurar vector de nivel 3; g_vbl_task = nullptr; g_blit_task = nullptr
    // 3) COP1LC = GfxBase->copinit; COPJMP1; restaurar DMACON/INTENA/ADKCON antiguos
    // 4) DisownBlitter; LoadView(old_view); WaitTOF x2; CloseLibrary; Permit()
}
```

Solo las demos `066_bob_showcase` y `119_proc_world` llaman manualmente a `takeover_system()`. **`run_demo_impl` nunca lo llama**, y esta demo tampoco: solo usa `takeover_display` (registros de display) y luego el engine habilita VERTB. `restore_system()` tampoco se ejercita en esta ejecución.

## Síntoma

Con 5 planos y cinco copperlists preconstruidas que rotan con un swap de COP1LC cada 50 frames, la demo se congela en el frame **198-200** del run-status (unos 4 s). Todo lo demás sigue en orden: DMACON `0x03A0` (DMAEN/BPLEN/COPEN/SPREN, sin BLTEN), BPLCON0 `0x5200` (5 planos + color, LPEN=0), COP1LC apunta a una lista válida. El contador de frames y el latido se paran, y la CPU aparece en el bucle de VPOSR de `wait_vblank_run`.

En el parón:

- `INTENAR = 0x2000` (bit 14 INTEN=0, bit 13 EXTER=1) o `0x7001`; el bit **VERTB (`0x20`) está a 0**.
- `INTREQR = 0x0020` (VERTB pendiente).
- `g_vbl_task` no es nulo (`0x00C15D30` / `0x00C15D62` según build), `g_vbl_task_user` es una dirección de pila válida, `g_level3_installed = 1`.
- `g_vblank_seq` atascado en ~199-201 (uno más que el frame congelado).
- La CPU ejecuta (PC y registros cambian) y VPOSR a veces lee valores vivos (VPOS=291, HPOS=0xD1); en otras muestras el mismo registro lee `0xA0000502` fijo (VPOS=5, HPOS=2).
- PC dentro del bucle de `AmigaBackend::wait_vblank_run` (`$DFF004`, comparación `#0x13700`).

### Evidencia previa relacionada (misma investigación de KS 1.3)

Un watchpoint de hardware sobre `INTENA` ya cazó código de la ROM de KS 1.3 borrando VERTB con un vector de nivel 3 instalado por el proceso:

```
$FCD5AC: MOVE.W #$0020,(a2,$009A)   ; a2 = $DFF000 -> INTENA, bit15=0 => BORRA VERTB
```

con cadena de llamadas de ROM `$FC63D4 <- $FE0496 <- $FDC7AC`; justo después se re-habilitan DMACON `$8100`/`$8020`. Una consulta de seguimiento (abierta) analizó la tabla de despacho de supervisor en `SysBase-$36` (`$C00240`) y un crash en el trampolín `$FC08E6` con `a5=0` desde el camino de `Wait()`; las entradas #0/#1 fuerzan `INTENA=0xC000`/usan `INTREQ=0x8004` para la contabilidad de Disable/Enable. El mecanismo exacto que reescribe INTENA (Disable/Enable de exec, Wait, contabilidad de graphics.library u otro) no quedó concluyente.

## Pruebas hechas (A/B, una variable)

1. **Rotación desactivada** (`K224_ROTATE_ART=0`, una sola copperlist, sin swap de COP1LC): estable 20 s, 49,97 fps, 141.962 ciclos/frame, 1,001 campos/frame.
2. **Cinco listas preconstruidas + swap de COP1LC cada 50 frames**: congelación determinista en el frame 198-200, en cada reinicio, con 5 planos.
3. **Commit de COP1LC a la misma dirección** (publicar la lista activa cada 50 frames): también se congela en 198-200. El contenido de la lista destino es irrelevante; basta la escritura periódica de COP1LC (o el trabajo extra asociado).
4. **3 planos + rotación**: 20 s a 49,92-49,97 fps en las ejecuciones buenas; la congelación depende de la carga/timing, no solo de rotar.
5. **Mover la publicación de COP1LC**: antes se hacía al final de `render()` (tras parchear 160 palabras, varios microsegundos después de despertar en VPOS=311, cayendo en VPOS 0-2). Se movió al inicio de `render()` (VPOS=311, una línea completa de margen antes del wrap) y se parchea después la lista siguiente ya inactiva. La congelación con Active persistió (una ejecución incluso se colgó en la espera de VPOS=311 de `takeover_display`). La carrera documentada del wrap (`WinUAE-DBG/custom.cpp:2921-2942`, `copper.md` §3) no es la causa raíz, aunque publicar con margen sigue siendo lo correcto.
6. **Auditoría de solapes de memoria** (la hipótesis inicial era que el engine pisaba bloques reservados): los punteros vivos en el parón dan planos `0x15070..0x21870` (5 x 0x2800), sprites `0x2394C..0x245F0`, copperlists `0x258A0..0x260A0`; **sin solapes**. `BlockPool` es first-fit, alinea cada reserva y devuelve inválido si no cabe. No hay evidencia de corrupción.
7. **Herramientas/depurador**: la congelación se reproduce con cliente GDB, sin GDB, solo con canal lateral y sin sesión MCP. No es un halt de GDB. (Aparte, el latch de lightpen de WinUAE puede congelar las lecturas de VPOSR: `GETVPOS()` devuelve `vpos_lpen` mientras `lightpen_triggered` está activo, `custom.cpp:2356-2364, 3613-3636`; una muestra de ratón absoluto movió el valor latcheado de VPOS 5 a 185. Es una trampa real del emulador, pero no la causa principal aquí, porque la demo también se congela sin entrada ni herramientas).
8. **Arreglo por sondeo (funciona)**: cambiar la demo a `VblankWait::Polling` (`run_frames_polling`, `wait_vblank()` sondeando VPOSR, sin depender del latido de VERTB) hace que 5 planos pasen del frame 950; la medida canónica da 49,92 fps, 142.102 ciclos/frame, 1,002 campos/frame, 1013 frames en 20,3 s, estable. La misma build con 3 planos da 49,97 fps, 141.962 ciclos/frame, 1,001 campos, estable. Por tanto, render, Copper, DMA de sprites, swap de COP1LC y memoria están sanos; el fallo está en la propiedad del latido VERTB/INTENA.

## Preguntas

1. **Confirmar o corregir el diagnóstico.** Bajo un proceso AmigaDOS vivo en KS 1.3, ¿qué borra exactamente el bit VERTB de `INTENA` a los 3-4 s, aunque el proceso haya sustituido el autovector de nivel 3 y escrito `INTENA = SETCLR|INTEN|VERTB`? ¿Es la contabilidad de Disable/Enable de exec, el camino `Wait()`/`Signal()`, un cambio de tarea que restaura una sombra de `INTENA`, el servidor de display de graphics.library, o la rutina de ROM de `$FCD5AC`? En otras demos el proceso duerme en `Wait()`, pero aquí sondea VPOSR y nunca duerme, y pasa lo mismo. ¿Cuál es la secuencia precisa en KS 1.3 (con `SysBase+$124/$126/$127` si aplica)?
2. **¿Por qué no lo salva el rearme?** `wait_vblank_run` rearma `INTENA` en cada llamada si VERTB está apagado, y la CPU está en el bucle de VPOSR. Si el haz llega a 311, el bucle retorna, el engine vuelve a llamar a `wait_vblank_run` y el rearme reescribe `INTENA = 0xC020`. ¿Por qué se sigue perdiendo VERTB? ¿La OS vuelve a borrar INTENA entre nuestra escritura y el siguiente VERTB (cada campo/VBlank o en cada cambio de tarea), de modo que la interrupción nunca se afirma? ¿O la CPU no retorna porque VPOSR está latcheado/congelado (lightpen u otro estado de Agnus)? ¿Qué observación distinguiría "la OS borra INTENA en cada VBlank" de "Agnus deja de generar VERTB"?
3. **¿Es `takeover_system()`/`restore_system()` el arreglo correcto y su orden es válido?** Como existe pero `run_demo` nunca lo llama: ¿debería `run_demo_impl` llamar a `backend.takeover_system()` antes de `app.set_display/start` (y restaurar al salir), o debería hacerlo cada demo? ¿Basta `Forbid()` para parar las reescrituras de INTENA, o hace falta `Disable()`/`Enable()` o un takeover completo de `graphics.library` (`LoadView(NULL)` + `OwnBlitter`)? ¿`Forbid()` impide que se cambie al Shell padre bloqueado? ¿El orden de `restore_system()` es seguro (apagar DMA/IRQ, restaurar vector, `copinit`, restaurar INTENA/DMACON/ADKCON, `DisownBlitter`, `LoadView(old)`, `WaitTOF` x2, `CloseLibrary`, `Permit`) y qué falta para KS 1.3?
4. **¿Alternativa soportada?** En vez de sustituir el vector crudo de nivel 3 y escribir INTENA, ¿debería el engine usar `AddIntServer(INTB_VERTB, ...)` (y `RemIntServer` al salir) para que la contabilidad de exec conozca nuestro servidor? Si mantenemos el vector crudo, ¿hay alguna forma soportada de mantener VERTB encendido con AmigaDOS vivo? ¿`Forbid()` + `OwnBlitter()` + vector crudo funciona en KS 1.3 o es intrínsecamente inseguro? ¿Qué hacen los trackloaders/demos clásicos de KS 1.3 lanzados desde Workbench/CLI que luego vuelven?
5. **Recomendación de diseño del engine.** Dado el engine actual, ¿cuál es el cambio mínimo y seguro para que las demos de `run_demo` sean robustas en KS 1.3: (a) llamar a `takeover_system()` en el bootstrap; (b) mantener el takeover de display pero añadir `Forbid()` y un rearme de VERTB fiable/periódico; (c) usar `AddIntServer` y dejar que exec gestione INTENA; o (d) dejar `VblankWait::Polling` como opción por defecto para demos lanzadas por el runner y reservar Active para builds bare-metal/trackloader? Necesitamos 50 fps, sin tearing, y que el flujo READY/GDB/screenshot del runner siga funcionando.
6. **Camino de salida.** Si una demo vuelve al runner/Workbench, ¿cuál es la secuencia completa y correcta de restauración en KS 1.3 (display, Copper, Blitter, INTENA, vectores, `Permit`), y cuáles son las formas típicas de bloquearse o petar al volver (p. ej. `LoadView` con DMA activo, cerrar graphics.library con el Blitter propio, restaurar INTENA con VERTB pendiente)? Tenemos `restore_system()` implementado pero sin probar en una demo; revisa su orden y di qué le falta.

## Ya descartado (no volver a sugerirlo)

- Solape/corrupción de memoria en las reservas Chip del engine (planos, sprites y copperlists auditados; sin solapes; `BlockPool` first-fit con alineación y límites).
- La carrera del wrap de COP1LC: mover el swap de después del wrap a VPOS=311 con una línea de margen no cambió la congelación con Active.
- Interferencia pura de herramientas/GDB: se reproduce sin cliente GDB y sin sesión MCP.
- Latch de lightpen puro: puede mover VPOSR, pero la congelación ocurre sin entrada de puntero; tratarlo como trampa agravante de WinUAE, no como causa raíz.
- Setup de display/Copper/DMA: DMACON=0x03A0, BPLCON0=0x5200, COP1LC válido; con Polling renderiza y mueve sprites a 50 fps.
- Corrupción de código: los bytes en runtime coinciden con el ELF; el PC del fallo es un `move.l $DFF004,d0` legal en `wait_vblank_run`.
- `Wait()`/bit de señal 31 (esta demo no llama a `Wait()`; sondea VPOSR). El crash anterior de `$FC08E6`/`SysBase-$36` era en un camino de `Wait()` y es otra firma.

## Respuesta de Grok y triaje

Grok confirmó el diagnóstico: VERTB es una interrupción de **cadena de servidores** propiedad de exec; sustituir el vector crudo `$6C` y escribir `INTENA` pelea con KS 1.3, y graphics.library/exec borran VERTB en la contabilidad de display y en Disable/Enable. `Forbid()` solo no basta: hace falta también `LoadView(NULL)` + `OwnBlitter`. El rearme pierde porque la OS puede borrar `INTENA` después de nuestra escritura y antes del siguiente VBlank. La pareja `takeover_system()`/`restore_system()` es la secuencia clásica correcta y debe llamarse en el bootstrap antes de `set_display`, restaurando al salir. La alternativa soportada y sin carreras es `AddIntServer(INTB_VERTB, ...)` (el handler debe retornar con Z activo) + `RemIntServer`. El vector crudo + `Forbid` + `OwnBlitter` funciona en demos clásicas pero sigue siendo racy si despierta otra tarea o el servidor de display. Grok confirmó el orden de restauración y recomendó limpiar `INTREQ` antes de restaurar `INTENA`; mantener `Polling` como fallback robusto y no confiar en un rearme periódico sin el takeover.

Aplicado: `run_demo_impl` llama a `takeover_system()` antes de `set_display` y garantiza `restore_system()` con un `scope_guard`; el takeover se limita a `Active`/`Polling` porque `Wait()` con `Forbid` puede bloquear la tarea (`Signal` queda como estaba). El servicio de VBlank/BLIT usa ahora `AddIntServer(INTB_VERTB/BLIT)` bajo AmigaDOS (stubs ASM que retornan con Z activo) y el vector crudo queda para bare-metal. Con ese camino, 3 y 5 planos corren 20 s estables con `Active` (49,87–49,92 fps, 1 campo). La build de **4 planos** sigue parándose hacia el frame 1150 con `INTENAR=0x77FF`, `INTREQR=0x20` pendiente y la CPU en el bucle de secuencia; no está aislado (cadena de servidores, interferencia del depurador/sesión o latch de lightpen de WinUAE). La demo se queda en `Polling` (cuatro perfiles: 49,87–49,92 fps, 1,001–1,003 campos, 20 s estable). `measure-fps` lee ahora el contador de ciclos por el canal lateral y tiene `--require-stable` y diagnóstico INTENA/INTREQ en parones. El camino `Active`+`AddIntServer` queda como experimental hasta cerrar el parón tardío de 4 planos.
