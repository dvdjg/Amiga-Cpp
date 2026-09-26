#pragma once

/// \file domains.hpp
/// Tags y alias de dominio del **sistema de tipos internos**
/// (`docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md`). Cada buffer interno usa
/// su alias (`Pattern`, `PaletteWords`, `AudioSample`…) y no es intercambiable con
/// otro dominio sin conversión explícita.
///
/// Un tag es un `struct` vacío; los alias son `Bytes`/`Words` sobre él (coste cero).
/// La frontera a crudo es `raw()`; solo el backend/`BlitJob` la cruzan.

#include <eng/core/types/typed.hpp>

namespace eng {

// Tags de dominio (vacíos, sin coste).
struct PatternTag {};
struct PlaneTag {};
struct PaletteTag {};
struct SpriteTag {};
struct CopperTag {};
struct TileBankTag {};
struct ChunkyTag {};
struct TextureTag {};      // textura indexada 1 B/texel (muestreo de efectos)
struct MaskTag {};
struct AudioTag {};
struct MusicTag {};
struct IndexedTilesTag {};
struct UafTag {};
struct MapCellsTag {};     // celdas de un mapa (indices de tile, u16)
struct BobTag {};          // BOB planar: planos de color + mascara de cookie-cut
struct MixerBufferTag {};  // buffer de trabajo del mezclador (consumido por asm)

// Aliases de dominio.
using Pattern = ByteView<PatternTag>;           // patrón de fondo (bytes)
using PatternWords = WordView<PatternTag>;      // patrón de fondo (words)
using PlaneBytes = Bytes<PlaneTag>;             // buffer de un plano (mutable)
using PlaneViewBytes = ByteView<PlaneTag>;      // vista de plano (solo lectura)
using ChipPlaneView = ChipView<PlaneTag>;       // plano **certificado en Chip** (DMA: BPLxPT)
using PaletteWords = WordView<PaletteTag>;      // paleta RGB444
using SpriteWords = WordView<SpriteTag>;        // palabras de sprite hardware
using SpriteBuffer = Words<SpriteTag>;          // buffer de sprite (escritura)
using CopperWords = WordView<CopperTag>;        // lista de Copper (WAIT/MOVE)
using TileBankBytes = ByteView<TileBankTag>;    // tilebank crudo (1 B/píxel)
using TileBankWords = WordView<TileBankTag>;    // banco de bloques interleaved (lectura)
using TileBankBuffer = Words<TileBankTag>;      // celdas de chunk residentes (escritura)
using ChunkyBuffer = Bytes<ChunkyTag>;          // buffer chunky (C2P)
using ChunkyView = ByteView<ChunkyTag>;         // vista chunky (solo lectura)
using IndexedTexture = ByteView<TextureTag>;    // textura indexada 1 B/texel
using MaskBytes = ByteView<MaskTag>;            // máscara 1-bit de cookie-cut
using MaskBuffer = Bytes<MaskTag>;
using AudioSample = ByteView<AudioTag>;         // muestra 8-bit con signo
using MusicBytes = ByteView<MusicTag>;          // módulo de tracker (bytes; sin confundir con `eng::audio::MusicModule`)
using IndexedTiles = ByteView<IndexedTilesTag>; // tilebank indexado del pipeline
using UafPayload = ByteView<UafTag>;            // payload UAF-R
using MapCells = Words<MapCellsTag>;            // celdas de mapa (escritura)
using MapCellsView = WordView<MapCellsTag>;     // celdas de mapa (lectura)
using BobBytes = Bytes<BobTag>;                 // BOB planar (planos + mascara)
using BobView = ByteView<BobTag>;               // BOB planar (solo lectura)
using MixerBuffer = Bytes<MixerBufferTag>;      // buffer del mezclador (escritura)
using MixerBufferView = ByteView<MixerBufferTag>; // buffer del mezclador (lectura)

} // namespace eng
