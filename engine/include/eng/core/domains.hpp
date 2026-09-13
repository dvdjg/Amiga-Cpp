#pragma once

/// \file domains.hpp
/// Tags y alias de dominio del **sistema de tipos internos**
/// (`docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md`). Cada buffer interno usa
/// su alias (`Pattern`, `PaletteWords`, `AudioSample`…) y no es intercambiable con
/// otro dominio sin conversión explícita.
///
/// Un tag es un `struct` vacío; los alias son `Bytes`/`Words` sobre él (coste cero).
/// La frontera a crudo es `raw()`; solo el backend/`BlitJob` la cruzan.

#include <eng/core/typed.hpp>

namespace eng {

// Tags de dominio (vacíos, sin coste).
struct PatternTag {};
struct PlaneTag {};
struct PaletteTag {};
struct SpriteTag {};
struct CopperTag {};
struct TileBankTag {};
struct ChunkyTag {};
struct AudioTag {};
struct MusicTag {};
struct IndexedTilesTag {};
struct UafTag {};

// Aliases de dominio.
using Pattern = ByteView<PatternTag>;           // patrón de fondo (bytes)
using PatternWords = WordView<PatternTag>;      // patrón de fondo (words)
using PlaneBytes = Bytes<PlaneTag>;             // buffer de un plano (mutable)
using PlaneViewBytes = ByteView<PlaneTag>;      // vista de plano (solo lectura)
using PaletteWords = WordView<PaletteTag>;      // paleta RGB444
using SpriteWords = WordView<SpriteTag>;        // palabras de sprite hardware
using CopperWords = WordView<CopperTag>;        // lista de Copper (WAIT/MOVE)
using TileBankBytes = ByteView<TileBankTag>;    // tilebank crudo (1 B/píxel)
using TileBankWords = WordView<TileBankTag>;    // banco de bloques interleaved (lectura)
using TileBankBuffer = Words<TileBankTag>;      // celdas de chunk residentes (escritura)
using ChunkyBuffer = Bytes<ChunkyTag>;          // buffer chunky (C2P)
using AudioSample = ByteView<AudioTag>;         // muestra 8-bit con signo
using MusicModule = ByteView<MusicTag>;         // módulo de tracker
using IndexedTiles = ByteView<IndexedTilesTag>; // tilebank indexado del pipeline
using UafPayload = ByteView<UafTag>;            // payload UAF-R

} // namespace eng
