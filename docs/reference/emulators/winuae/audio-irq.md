# WinUAE — IRQ de audio de Paula (`AUD0..3`, nivel 4)

Cómo implementa WinUAE la **interrupción de audio** (canales `AUD0..AUD3`, autovector de nivel 4),
leído de su fuente (`../WinUAE-DBG/audio.cpp`) y contrastado con el AHRM. Es la referencia de facto
para el *pseudo-streaming* del engine (ver `AGENTS.md` §1.11 y
[`AUDIO_STREAMING.md`](../../../engine/architecture/AUDIO_STREAMING.md)).

## 1. Cuándo se levanta (una por bloque, en el wrap)

La IRQ se pide cuando el contador de palabras del canal llega al final del buffer:

| Paso | Código | Fuente |
|---|---|---|
| Wrap: al llegar `wlen == 1`, recarga `wlen = len` y marca `intreq2` | `audio.cpp:2523-2528` | `event_audxdat_func` |
| Pasa `intreq2` a `INTREQ` en la transición de estado 2→3 | `audio.cpp:1920-1923` | `audiostream_state_channel` |
| Arranque de la DMA (estado 0→1) también pide IRQ | `audio.cpp:1854` | idem |
| `setirq` → `INTREQ_INT(nr + 7, …)` (bits 7..10, con 1 CCK de retardo) | `audio.cpp:1556-1565` | `setirq` |

El DMA **auto-loopea** el buffer: al agotar `len` palabras recarga el contador y sigue, de modo que
sin reprogramar se dispara **una IRQ por vuelta** (no hay tormenta por sí sola). Coincide con el
AHRM: una interrupción por bloque (`...cat.md:4378`, `:6526`), tras leer location/length a los
back-up registers.

## 2. Escribir los registros en la IRQ **no** re-arma la IRQ

- `AUDxLEN` (`audio.cpp:2722-2732`): asigna `cdp->len = v`. No toca `wlen` ni `intreq2`.
- `AUDxLCH` (`:2631-2658`) y `AUDxLCL` (`:2660-2684`): fijan `cdp->lc` (el puntero latcheado).
- `AUDxPER` (`:2686-2720`): fija el periodo (acotado a `PERIOD_MIN`); periodo **0** → `65536`.

Por tanto, reescribir el puntero/longitud en el servicio (la técnica del «join tones» del AHRM)
**no** genera una IRQ extra inmediata: el bloque actual termina con el valor latcheado y el nuevo
valor se usa en el **siguiente** bloque. (Observado; hay excepciones con los *hacks* de
`usehacks()`/`ptx_written`, `:2639`/`:2665`.)

## 3. Los registros `AUDx` son de **solo escritura**

En el hardware real (y en WinUAE) `AUDxLCH/LCL/LEN/PER/VOL` solo tienen handler de escritura; leer
esas direcciones devuelve el valor de bus (`0xFFFF`). **No** se pueden volcar para depurar los
valores efectivos: hay que instrumentarlos con `write_log` (`DEBUG_AUDIO`, `audio.cpp:1558-1562`) o
seguirlos con el depurador.

## 4. Implicación para el engine

- La IRQ de nivel 4 (`support/level4_irq.s`, `install_audio_service`) es el sitio del **cambio de
  buffer**: el servicio llama a `PcmStream::advance()` y reprograma `AUDxLCH/LCL/LEN`
  (`PaulaAudio::set_buffer`), sin descomprimir.
- `INTREQ` a limpiar = `0x0780` (AUD0..3); con **una** voz basta con armar su bit.
- El ritmo esperado es `1 / (AUDxPER * AUDxLEN)` (en ciclos); si se observa mucho mayor hay que
  instrumentar la longitud/el puntero en el emulador (no basta con releer los registros, §3). Caso
  abierto en [`audio-stream-irq-rate.md`](../../../debugging/audio-stream-irq-rate.md).

## Referencias

- AHRM 3.ª: `docs/reference/ahrm/...cat.md:4374`, `:4378`, `:4533`, `:6522-6526`.
- Fuente: `../WinUAE-DBG/audio.cpp` (`setirq`, `event_audxdat_func`, `AUDxLEN`/`AUDxLCH`/`AUDxLCL`/`AUDxPER`).
- Doc del engine: `docs/engine/architecture/AUDIO_STREAMING.md`, `docs/engine/architecture/GAME_AUDIO.md`.
