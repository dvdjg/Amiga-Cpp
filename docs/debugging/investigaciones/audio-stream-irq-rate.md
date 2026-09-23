# Ritmo de la IRQ de audio en el streaming (A5) — 272_audio_stream

La demo `demos/amiga/272_audio_stream` reproduce PCM desde RAM con `PcmStream` y cambia de buffer
en la **IRQ de audio (nivel 4)**. El sintoma observable es que la demo **no alcanza el frame de
informe** (se queda en `state=2`) y acumula *underruns* (solo una fraccion de los bloques acaban en
*swap*). A5 **no queda verificado**.

## Medición (log del emulador)

Con el log de audio de WinUAE activado (ver «Observación» abajo), en una corrida de ~60 s:

```text
AUD3PER: 221                      (correcto: period_for_hz(16000))
AUD3LEN: 1024                     (correcto: kChunkSamples/2)
AUD3 looped    : 324              (vueltas de DMA, a 1024 words)
AUD3 near loop : 1297             (~4 por vuelta: coherencia)
SETIRQ3        : 325              (IRQs de audio levantadas)
AUD3LEN writes : 81               (ISR reprogramando: 80 swaps + 1 setup)
```

**Conclusion:** `SETIRQ3` ≈ `looped` ⇒ la IRQ de audio dispara **una vez por bloque**, como manda
el AHRM. **No hay tormenta de IRQ**: la hipotesis de «~34× mas rapido» del informe previo **no se
sostiene** (era un artefacto de suponer 50 fps: la demo corre muy por debajo, asi que el conteo por
frame parecia enorme). El problema real es que solo **80 de 324** bloques acaban en *swap*: el resto
son *underruns* porque el **feeder no repone a tiempo** (la demo es CPU-bound sintetizando/codificando
los chunks en el bucle principal).

## Medición previa (descartada)

Con contadores de 8 bits consistentes (`irq == swap + underrun`) y ventana de 20 frames:

```text
irq=207  swap=51  underrun=156      ->  ~517 IRQ/s   (esperado ~15/s con len=1024, per=221)
```

Esta lectura asumia 50 fps (20 frames = 0,4 s); el log del emulador muestra que la IRQ va a
**1 por bloque** (324 IRQ en ~20 s de audio), por lo que la tasa por segundo es la esperada y el
«~34×» era una mala estimacion de la ventana temporal.

## Mecanismo (referencia)

- **AHRM 3.ª** (`docs/reference/ahrm/...cat.md:4378`): las location/length se leen **antes** de
  empezar el bloque (a back-up registers) y la interrupción ocurre **justo después de esa lectura**;
  se pueden reescribir los registros para el siguiente segmento. Una IRQ **por bloque**.
- **WinUAE** (`../WinUAE-DBG/audio.cpp`): la IRQ se levanta en el **wrap** del DMA
  (`event_audxdat_func:2523-2527`: al llegar `wlen==1` recarga `wlen=len` y marca `intreq2` → el
  `setirq` ocurre en la transición 2→3, `:1920`). Escribir `AUDxLEN` solo fija `len`
  (`AUDxLEN:2722-2732`) y `AUDxLCH/LCL` fijan `lc` (`:2631-2684`): **no re-arman** la IRQ.

Conclusión: ni la técnica del *swap* ni una re-armadura explícita explican la tormenta.

## Descartado

1. **`ADKCON` (ATPER/ATVOL, bits 1..4)**: era una hipótesis del ritmo disparado (el periodo/volumen
   saldrían de los datos del canal anterior). Se limpian en `PaulaAudio::silence()`
   (`engine/include/eng/platform/audio_paula.hpp`) y **el ritmo no cambió**.
2. **INTREQ de otro canal**: armar solo `AUD3` en `INTENA` (en vez de AUD0..3) no lo arregló.
3. **`period_for_hz`**: `period_for_hz(16000) = 221` (correcto, `eng/audio/audio_mode.hpp:73`).
4. **Releer los registros**: `AUDxLEN`/`AUDxPER` son **write-only** (la lectura devuelve `0xFFFF`);
   no se pueden verificar los valores efectivos en runtime.

## Arreglado de paso (bugs reales)

1. **Estado compartido con la IRQ sin `volatile`**: `ChunkStream` (`eng/os/stream.hpp`) y los
   contadores de la demo. El compilador cacheaba `m_ready`/`m_eof` en registros (la IRQ no es
   visible para su modelo de datos) y el bucle no veía los buffers liberados → cuelgue. Ahora son
   `volatile`.
2. **Contadores de 32 bits**: en el 68000 un `u32` se parte en dos accesos y la lectura se
   **desgarra** (daba `swap > irq`, imposible). Los contadores de la IRQ son ahora `volatile u8`
   (un byte se lee/escribe de forma atómica).

## Hipótesis abierta (para retomar)

El registro y la IRQ están **bien** (el log del emulador lo confirma: `AUD3LEN=1024`, `AUD3PER=221`,
`SETIRQ3` ≈ `looped`). El problema es de **alimentación**: el feeder repone pocos buffers frente a los
bloques que Paula consume, así que la mayoría de IRQs no encuentran buffer y cuentan como *underrun*.
La causa próxima es que la demo es **CPU-bound** en `refill()`/`synth_chunk()` (sintetiza 2048
muestras y las codifica con Delta+RLE **por chunk**, en el bucle principal), de modo que su ritmo de
frame queda muy por debajo de los 50 Hz y no da tiempo a reponer. Verificado en código que los
registros se escriben bien: `PaulaAudio::set_pointer` (LCH/LCL), `set_length` (`AUDxLEN`),
`set_period` (`AUDxPER`), `start_channel` (`DMACON`), con stride `channel*8` words y orden
`[LCH, LCL, LEN, PER, VOL]` (`engine/include/eng/platform/audio_paula.hpp`), y que `AUDxLEN(nr, v)`
en WinUAE guarda `cdp->len = v` sin transformar (`audio.cpp:2722-2732`).

Siguientes pasos: (1) medir el ritmo real de frame de la demo (¿por qué no llega al frame 240?);
(2) aligerar el feeder (pre-sintetizar/codificar los chunks una vez, o sintetizar PCM directo sin
codec para aislar el coste); (3) comprobar `PcmStream::advance()`/`needs_data()` con el feeder
aligerado.

## Observación pendiente (para retomar)

El log de audio de WinUAE **está compilado fuera** en el binario actual (`#define DEBUG_AUDIO 0`,
`audio.cpp:53`; `debugchannel` y sus `write_log` van bajo `#if DEBUG_AUDIO > 0`). Para observar el
`len`/`per` efectivos hay que:

1. **Recompilar `../WinUAE-DBG`** con `DEBUG_AUDIO 1` (y `DEBUG_AUDIO2 1` para el detalle de la
   máquina de estados 2→3). `debugchannel(nr)` no necesita preferencias (`DEBUG_CHANNEL_MASK 15` →
   los 4 canales). El log imprime `AUD%dLEN`/`AUD%dPER`, los eventos `looped`/`near loop`
   (`:2520-2533`) y `SETIRQ` (`:1556`). Comparar el `LEN` efectivo con 1024 y el `per` con 221.
   **El rebuild es viable y barato**: `WinUAE-DBG/build.bat` usa VS 18 Community (`C:\Program Files\
   Microsoft Visual Studio\18\Community`), recompila **solo `audio.cpp`** de forma incremental y
   despliega a `bin/winuae-gdb.exe` **y** a las extensiones amiga-debug (`.cursor`/`.vscode`,
   `bin/win32/winuae-gdb.exe`) — que es lo que usa el runner. Verificado (build OK); se restauró el
   binario original tras la prueba.
2. **Plumbing del log: RESUELTO.** El conector `mcp-winuae-emu` redirige `stdout`/`stderr` al
   `winuae-*.log`, pero en Windows (app GUI sin consola) `write_log` no llega ahí. La cadena real es
   `write_log` → `barto_gdbserver::log_output` (`od-win32/writelog.cpp:735`, activo con
   `debugging_features=gdbserver`) → paquete GDB `O` (que el conector solo traza con `WINUAE_TRACE=1`).
   **Fix aplicado** (temporal, en el emulador): `barto_gdbserver::log_output`
   (`od-win32/barto_gdbserver.cpp:4844`) ahora **también** hace `fputs` a `log_file`, que el
   gdbserver autoabre en `%TEMP%\winuae-gdb.log` (`:1454`). Así el log de audio queda en un fichero
   legible. El binario y las fuentes del emulador se restauran tras la sesión de medida.
3. Alternativa sin depender del log: **GDB** (el runner ya conecta) con breakpoint en `AUDxLEN`
   (`audio.cpp:2722`) y en `level4_dispatch` (`amiga_minimal.cpp:224`), volcando `v` y el estado de
   la voz en cada IRQ para medir el intervalo real.

Descartado leer los registros desde el 68000 (write-only; la lectura devuelve `0xFFFF`).
