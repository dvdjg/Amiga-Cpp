# Audio streaming digital desde disquete (`eng::audio`)

Cómo reproducir una **grabación digital** (música o voz) desde disquete en un A500, con
**pseudo-streaming**: el disco llena buffers pequeños, la CPU los descomprime y Paula los
reproduce por DMA. Encaja con la E/S asíncrona del mini-SO
([MINI_OS_IO.md](MINI_OS_IO.md)) y con la capa de audio ([GAME_AUDIO.md](GAME_AUDIO.md)).

## 1. Restricción y objetivo

Paula solo lee audio desde **Chip RAM** por DMA, y el disquete es lento (~30 KB/s útiles). El
objetivo es un flujo continuo con **footprint = N buffers pequeños**, no el archivo entero: mientras
Paula reproduce un buffer, el disco llena el siguiente y la CPU descomprime el anterior. Como la
CPU queda libre, se prioriza el **ratio de compresión** sobre la velocidad de descompresión.

## 2. Compresión y formato

Se distinguen los **códecs de audio** (explotan la correlación entre muestras) de los
**compresores de propósito general** (LZ sobre cadenas repetidas). Un LZ puro sobre PCM crudo
rinde mal (las muestras varían de forma continua y no hay repeticiones exactas de bytes); por eso
el LZ se aplica **tras** un preprocesado delta, o se usa un códec específico de audio.

| Prioridad | Método | Ratio (audio 8-bit) | Descompresión 68000 | Pérdida | Código `compression` |
|---|---|---|---|---|---|
| 1.º | **Delta + ZX0** | ~40–60 % | media (bloques) | no | `5` |
| 2.º | **ZX0** sobre PCM crudo | bueno | media | no | `0` |
| 3.º | **aPLib** sobre PCM/delta-PCM | excelente | buena | no | `1` (reservado) |
| — | **Delta + RLE (ByteRun1)** | medio | muy rápida | no | `2` |
| — | **Fibonacci Delta (IFF 8SVX)** | 50 % (2:1) | **muy rápida** | sí | `4` |
| — | **IMA ADPCM 4-bit** | 50–75 % | alta | sí | `6` (planificado) |
| — | PCM crudo | 0 % | — | no | `3` |

Recomendación: **delta-PCM + ZX0** (o aPLib). El *delta encoding* (diferencias entre muestras
consecutivas) mejora mucho el ratio antes de comprimir. El engine incorpora el **Delta + RLE** como
codec propio (`eng/audio/pcm_codec.hpp`, §7); ZX0/aPLib se integran como descompresores externos
(ver roadmap).

### Formato de archivo

```text
Header (32–48 bytes)
├── Magic ("AUZX")
├── sample_rate   (11025 / 14000 / 16000 / 22050)
├── channels      (1 = mono)
├── bits          (8)
├── total_samples (sin comprimir)
├── compression   (códigos de pcm_codec::Codec, §7.1)
├── num_chunks
├── chunk_samples (potencia de 2: 4 KB / 8 KB)
└── (opcional) checksum
Chunk 0 (comprimido) · Chunk 1 · …
```

Preparación del audio: **mono 8-bit con signo**, delta antes de comprimir, chunks de **4–8 KB**
descomprimidos (cómodos en Chip y con buen margen de streaming).

## 3. Arquitectura de pseudo-streaming

```text
  Floppy (pista/sector)
        │  trackdisk DMA / CPU
        ▼
  Buffer comprimido (Chip o Fast)
        │  descompresión (68000)
        ▼
  Buffer PCM A  ←→  Buffer PCM B   (Chip RAM)
        │                │
     Paula DMA        Paula DMA
```

Mientras Paula reproduce el buffer A, la CPU descomprime el siguiente chunk en B; cuando Paula
termina A, la **IRQ de audio** (nivel 4) cambia el puntero a B y libera A para el chunk siguiente.
Con **triple buffer** hay margen si un chunk tarda más (seek o descompresión irregular).

## 4. Sincronización

La IRQ de Paula **solo** cambia el puntero, recarga la longitud y marca un flag; **no** descomprime
(sería demasiado tiempo dentro de la IRQ). La descompresión pesada se hace en el bucle principal o
como **tarea cooperativa** de `eng::task::BackgroundQueue`, drenada en los huecos de VBlank.

```text
  IRQ audio (nivel 4) ──► cambia buffer + flag "falta chunk"
                              │
  bucle/tarea de fondo ───────┘ ──► lee chunk + descomprime en el buffer libre
```

## 5. Esqueleto

```cpp
constexpr eng::u16 kChunkSamples = 4096;   // 4 KB de audio 8-bit
constexpr eng::u8  kNumBuffers   = 2;      // 2 o 3 (triple buffer = más margen)

struct AudioHeader {                       // cabecera del archivo (§2)
	char magic[4]; eng::u16 sample_rate; eng::u32 total_samples;
	eng::u16 chunk_samples, num_chunks; eng::u8 compression;
};

struct PcmStream {
	AudioHeader header {};
	eng::u8* pcm[kNumBuffers] {};           ///< buffers en Chip RAM (Paula los lee por DMA)
	eng::u8  compressed[8192] {};           ///< chunk comprimido (puede ir en Fast)
	volatile eng::u8  play_buf = 0;         ///< buffer que suena
	eng::u16 next_chunk = 0;
	volatile eng::u8  need_chunk = 0;       ///< lo pone la IRQ de audio
	bool eof = false, underrun = false;
};

/// IRQ de audio (nivel 4): cambia de buffer y pide el siguiente chunk. No descomprime.
void paula_audio_isr(PcmStream& s) {
	s.play_buf ^= 1u;
	paula::set_buffer(s.pcm[s.play_buf], kChunkSamples / 2u); // AUDxLCH/LCL + AUDxLEN (en words)
	s.need_chunk = 1u;
}

/// Tarea de fondo / bucle: llena el buffer libre. Vuelve al momento si no hay trabajo.
bool pcm_stream_service(PcmStream& s) {
	if (s.need_chunk == 0u) { return true; }
	s.need_chunk = 0u;
	const eng::u8 free_buf = static_cast<eng::u8>(s.play_buf ^ 1u);
	if (s.next_chunk >= s.header.num_chunks) { s.eof = true; return false; }

	// 1) Leer el chunk comprimido (E/S asíncrona del mini-SO; ver MINI_OS_IO.md).
	const eng::u32 n = read_chunk(s.next_chunk, eng::Span<eng::u8>{s.compressed});
	// 2) Descomprimir directo al buffer PCM de Chip.
	const eng::s32 got = pcm_codec::decode(
		eng::Span<const eng::u8>{s.compressed, n},
		eng::Span<eng::u8>{s.pcm[free_buf], kChunkSamples}, s.header.compression);
	if (got < 0) { s.underrun = true; return false; }
	++s.next_chunk;
	return true;
}
```

Al arrancar se descomprimen los `kNumBuffers` primeros chunks, se programa Paula
(`AUDxLCH/LCL`, `AUDxLEN = samples/2` porque cuenta en *words*, `AUDxPER = period_for_hz(rate)`,
`AUDxVOL`) y se activa el DMA de audio + su IRQ.

## 6. Integración con el mini-SO

- La **lectura del chunk** usa la E/S asíncrona (`file_read_async`); el `FileDone` **no** trae un
  mensaje por buffer: la tarea de fondo comprueba `need_chunk` y lee.
- El `underrun` (buffer de reproducción no listo) puede postear `MsgType::AudioUnderrun`
  (opcional, `post_underrun_msg`) para que el juego baje calidad o repita el último buffer.
- El streaming **no** comparte el tick de VBlank: Paula va por su IRQ; el servicio de descompresión
  se reparte como tarea de fondo.

## 7. Decodificadores

El engine despacha por el campo `compression` con un contrato único
`pcm_codec::decode(comprimido, destino, compression)`, freestanding y sin heap: decodifica directo
al buffer Chip. Implementados: **ZX0** (`eng/audio/zx0.hpp`, port de `dzx0.c`), **Delta + RLE**
propio, **Fibonacci Delta** (`eng/audio/fib_delta.hpp`, IFF 8SVX) y **Delta + ZX0**. Cada uno tiene
test host (HOST-271, 242, 323, 324). Pendiente: **aPLib** (`Codec::APLib`) e **IMA ADPCM 4-bit**.

### 7.1 Formatos exactos (compatibilidad PC → Amiga)

Los valores del campo `compression` son los de `eng::audio::pcm_codec::Codec` y **no se reordenan**
(los históricos 0..3 se conservan); los nuevos se añaden al final. Un compresor en PC debe emitir
exactamente estos flujos para que el fichero sea compatible.

**Fibonacci Delta — `4`** (IFF 8SVX, `sCompression = 1`; EA 1985, Apéndice C, con pérdida):

```text
cuerpo = [pad = 0x00] [semilla x (s8)] [pares de nibbles: primero el alto, luego el bajo]
muestra[k] = muestra[k-1] + TABLA[nibble]      muestra[-1] = semilla
salida     = 2 * (tamaño_cuerpo - 2) muestras s8
TABLA[16]  = {-34,-21,-13,-8,-5,-3,-2,-1, 0,1,2,3,5,8,13,21}
```

El encoder del engine emite `pad = 0` y `semilla = 0` y codifica **todas** las muestras como
incrementos desde 0 (`2 + ceil(N/2)` bytes). Cualquier semilla es válida para el decodificador
(el estándar la pasa como parámetro a `D1Unpack`), así que un fichero 8SVX de cualquier herramienta
se decodifica correctamente.

**Delta + ZX0 — `5`** (sin pérdida):

```text
deltas[i] = PCM[i] - PCM[i-1]     (mod 256; PCM[-1] = 0)
cuerpo    = ZX0(deltas)            (formato ZX0 v2 estándar)
```

Decodificar = `integrate(zx0::decompress(cuerpo))`. El preprocesado delta concentra los valores
alrededor de cero y mejora el ratio del LZ.

**Delta + RLE — `2`** (ByteRun1 de PackBits): mismo preprocesado delta; control `0..127` copia
`control+1` literales, `129..255` repite el byte siguiente `257-control` veces, `128` es no-op.

**ZX0 — `0`**: ZX0 v2 (Einar Saukas) sobre PCM directo, sin delta.

**IMA ADPCM — `6`** (planificado; con pérdida, 4 bits/muestra, tablas del estándar IMA/DVI).

Para producir estos ficheros desde un PC: `tools/audio/prep-sample.ts` (WAV → PCM mono 8-bit con
signo) y, según el códec, el paso delta y/o la herramienta ZX0 de referencia (`zx0 -f`). Ver el
roadmap en [`ROADMAP_AUDIO.md`](../../guides/roadmap/ROADMAP_AUDIO.md).

## 8. Detalles de implementación

- **Periodo de Paula**: `AUDxPER = period_for_hz(sample_rate)` (PAL 3546895; ver `GAME_AUDIO.md` §7).
- **Chip RAM obligatorio** para los buffers PCM; el comprimido puede ir en Fast.
- **Motor on** (`TD_MOTOR`) entre chunks de un stream, para no pagar el *spin-up* cada vez.
- **Datos contiguos en disco** y en orden de reproducción, para minimizar *seeks*.
- **Triple buffer** si la descompresión o la lectura son irregulares.

## 9. Referencias

- [`MINI_OS_IO.md`](MINI_OS_IO.md) — E/S asíncrona y streaming (doble/triple buffer) del mini-SO.
- [`GAME_AUDIO.md`](GAME_AUDIO.md) — capa de audio, Paula y modos.
- [`STREAMING_LOADER.md`](STREAMING_LOADER.md) — loader de chunks y trackloader de hardware.
- Paula: `../amiga-bootcamp/01_hardware/ocs_a500/paula_audio.md` (canales DMA, `AUDxLEN` en words).
- Compresión: ZX0 (Einar Saukas) y aPLib (Jørgen Ibsen).
