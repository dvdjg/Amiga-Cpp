#pragma once

/// \file bus_budget.hpp
/// **Presupuesto de bus DMA del Amiga 500 (OCS)** — herramienta de **planificación**, no de medida.
///
/// El A500 reparte un número fijo de *slots* de bus por línea de raster entre los consumidores de
/// Chip RAM. Los slots **impares (odd)** están reservados a DMA crítico de tiempo real (refresh,
/// disco, audio, sprites); los **pares (even)** los comparten **Copper > Blitter > CPU**. El
/// bitplane DMA (y el fetch de sprites) roban slots odd, y con **>4 planos lores** o **≥4 hires**
/// también roban pares a Copper/Blitter/CPU (por eso los juegos clásicos bajaban a 288×240 o
/// partían los FPS a la mitad). Base: AHRM 3.ª (cap. de DMA/timing) + práctica de la época.
///
/// ```cpp
/// eng::hw::BusBudgetInput in {};
/// in.bands_count = 1u;
/// in.bands[0] = eng::hw::BusBand {.height = 256u, .width = 320u, .bitplanes = 4u};
/// in.blitter_words = 8000u;
/// const auto r = eng::hw::amiga500_bus_budget(in);
/// // r.remaining_pct10 (décimas de %), r.bottleneck, r.hints
/// ```
///
/// El modelo es **acumulativo**: da unos recursos y devuelve el margen que queda para los demás y
/// el **cuello de botella**. Iterando (subir planos, acortar Copper, bajar FPS, activar Fast RAM…)
/// hasta acercarse a 0 % se deja la máquina al 100 %. Ver `docs/engine/architecture/BUS_BUDGET.md`.

#include <eng/core/types/types.hpp>

namespace eng::hw {

/// Recurso que compite por el bus de Chip RAM.
enum class BusResource : u8 {
	Refresh,
	Display, ///< bitplanes + sprites (roban odd; >4 planos/hires roban even)
	Copper,
	Blitter,
	Cpu,     ///< CPU ejecutando desde Chip RAM
	Count,
};

/// Slots de bus por línea de raster (OCS: 227.5 color clocks; se modela a 227).
inline constexpr u32 kBusSlotsPerLine = 227u;
inline constexpr u32 kPalLinesPerFrame = 312u; ///< PAL no entrelazado (aprox. práctica)
inline constexpr u32 kNtscLinesPerFrame = 262u;
inline constexpr u32 kPalRefreshHz = 50u;
inline constexpr u32 kNtscRefreshHz = 60u;

/// Costes fijos por línea (slots odd).
inline constexpr u32 kRefreshSlotsPerLine = 4u;
inline constexpr u32 kDiskSlotsPerLine = 3u;      ///< streaming de floppy (Paula)
inline constexpr u32 kAudioSlotsPerChannel = 1u;  ///< 1 palabra/canal/línea
inline constexpr u32 kSpriteSlotsPerChannel = 2u; ///< 2 palabras/canal/línea

/// Coste de Copper por palabra: `MOVE` = 2 (registro + dato), `WAIT` = 3.
inline constexpr u32 kCopperSlotsPerMove = 2u;
inline constexpr u32 kCopperSlotsPerWait = 3u;

/// Franja horizontal con su modo. Hasta `kMaxBusBands` franjas por frame.
struct BusBand {
	u16 height = 0u;        ///< líneas de esta franja
	u16 width = 320u;       ///< píxeles horizontales visibles
	u8 bitplanes = 4u;      ///< 1..6
	bool hires = false;     ///< true = 640 px (40 palabras/plano)
	u8 sprites_active = 0u; ///< 0..8 canales usados simultáneamente en la franja
	u16 copper_moves = 0u;  ///< MOVE locales de esta franja (por frame)
	u16 copper_waits = 0u;  ///< WAIT locales
	bool copper_every_frame = true; ///< false = se actualiza cada 2 frames (mitad de coste)
};

inline constexpr u32 kMaxBusBands = 4u;

/// Entrada del presupuesto. Los campos a 0 = no se usan.
struct BusBudgetInput {
	bool pal = true;
	u16 fps = 50u;              ///< 50/25 PAL, 60/30 NTSC (contabiliza VBlanks por frame lógico)
	bool disk_streaming = false;
	u8 audio_channels = 0u;     ///< 0..4 (Paula)
	u32 blitter_words = 0u;     ///< palabras totales del Blitter por frame (BOBs + fills + copias)
	u8 blitter_channels = 2u;   ///< 1..4 lecturas de canal por palabra (cookie-cut = 4/5)
	bool use_fastram = false;   ///< CPU desde Fast RAM: alivia el bus de Chip
	u32 cpu_chip_cycles = 0u;   ///< ciclos 68000 por frame que **tocan Chip RAM** (0 si Fast RAM)
	u32 bands_count = 0u;
	BusBand bands[kMaxBusBands] {};
};

/// Pistas (bitset) para la iteración del diseñador.
enum BusBudgetHint : u16 {
	kHintOverBudget = 1u << 0,      ///< `remaining < 0`
	kHintTight = 1u << 1,          ///< queda < 8 %
	kHintSixPlanesWide = 1u << 2,  ///< 6 planos con width > 288 (truco clásico 288×240)
	kHintHiresHeavy = 1u << 3,     ///< ≥4 planos hires (roba casi todo el even)
	kHintCpuNeedsFast = 1u << 4,   ///< la CPU en Chip se come > 50 % del presupuesto
	kHintLowerFps = 1u << 5,       ///< bajar a 25/30 fps casi duplica el presupuesto lógico
};

/// Resultado del presupuesto (slots de bus por frame lógico).
struct BusBudgetResult {
	u32 total_slots = 0u;
	u32 display_slots = 0u;
	u32 copper_slots = 0u;
	u32 blitter_slots = 0u;
	u32 cpu_slots = 0u;
	u32 used_slots = 0u;
	s32 remaining_slots = 0;   ///< puede ser negativo (exceso)
	u16 remaining_pct10 = 0u;  ///< décimas de % (p. ej. 1234 = 123,4 %; 0 si >1000 %)
	u8 bottleneck = 0u;        ///< índice de `BusResource` más cargado
	u16 hints = 0u;            ///< bitset de `BusBudgetHint`
};

/// Palabras de bitplane por plano y por línea: lores = `ceil(width/16)`, hires = 40.
[[nodiscard]] constexpr u32 bp_words_per_plane(const BusBand& b) noexcept {
	return b.hires ? 40u : (static_cast<u32>(b.width) + 15u) / 16u;
}

/// VBlanks que cubre un frame lógico de `fps` (50/25→1/2 en PAL; 30→2 en NTSC).
[[nodiscard]] constexpr u32 vblanks_per_frame(bool pal, u16 fps) noexcept {
	const u32 hz = pal ? kPalRefreshHz : kNtscRefreshHz;
	if (fps == 0u) return 1u;
	const u32 v = (hz + fps - 1u) / fps; // ceil
	return v == 0u ? 1u : v;
}

/// **Presupuesto de bus del A500** (estimación de planificación). Ver cabecera del fichero.
[[nodiscard]] constexpr BusBudgetResult amiga500_bus_budget(const BusBudgetInput& in) noexcept {
	BusBudgetResult r {};
	const u32 lines = in.pal ? kPalLinesPerFrame : kNtscLinesPerFrame;
	r.total_slots = kBusSlotsPerLine * lines * vblanks_per_frame(in.pal, in.fps);

	// Coste fijo global por línea (odd): refresh + disco + audio.
	u32 fixed_per_line = kRefreshSlotsPerLine;
	if (in.disk_streaming) fixed_per_line += kDiskSlotsPerLine;
	fixed_per_line += static_cast<u32>(in.audio_channels) * kAudioSlotsPerChannel;

	// Display por franja (bitplanes + sprites; fixed_per_line cuenta una vez por línea visible).
	u32 display = 0u;
	u32 visible_lines = 0u;
	bool six_wide = false;
	bool hires_heavy = false;
	for (u32 i = 0u; i < in.bands_count && i < kMaxBusBands; ++i) {
		const BusBand& b = in.bands[i];
		if (b.height == 0u) continue;
		visible_lines += b.height;
		const u32 sprites = static_cast<u32>(b.sprites_active <= 8u ? b.sprites_active : 8u) *
				    kSpriteSlotsPerChannel;
		const u32 bp = bp_words_per_plane(b) * static_cast<u32>(b.bitplanes);
		display += (fixed_per_line + sprites + bp) * static_cast<u32>(b.height);
		if (b.bitplanes >= 6u && b.width > 288u) six_wide = true;
		if (b.hires && b.bitplanes >= 4u) hires_heavy = true;
	}
	(void)visible_lines;

	// Copper (even, prioridad sobre Blitter/CPU).
	u32 copper = 0u;
	for (u32 i = 0u; i < in.bands_count && i < kMaxBusBands; ++i) {
		const BusBand& b = in.bands[i];
		if (b.height == 0u) continue;
		u32 c = static_cast<u32>(b.copper_moves) * kCopperSlotsPerMove +
			static_cast<u32>(b.copper_waits) * kCopperSlotsPerWait;
		if (!b.copper_every_frame) c /= 2u;
		copper += c;
	}

	// Blitter (even): lecturas de canal por palabra.
	const u32 channels = in.blitter_channels < 1u ? 1u : (in.blitter_channels > 4u ? 4u : in.blitter_channels);
	const u32 blitter = in.blitter_words * channels;

	// CPU en Chip (even): 0 si corre desde Fast RAM.
	const u32 cpu = in.use_fastram ? 0u : in.cpu_chip_cycles;

	r.display_slots = display;
	r.copper_slots = copper;
	r.blitter_slots = blitter;
	r.cpu_slots = cpu;
	r.used_slots = display + copper + blitter + cpu;
	r.remaining_slots = static_cast<s32>(r.total_slots) - static_cast<s32>(r.used_slots);

	// Porcentaje restante en décimas (0 si negativo o >1000 %).
	if (r.total_slots != 0u && r.remaining_slots > 0) {
		const u64 pct10 = (static_cast<u64>(r.remaining_slots) * 1000u) / r.total_slots;
		r.remaining_pct10 = pct10 > 1000u ? 1000u : static_cast<u16>(pct10);
	}

	// Cuello de botella: categoría con mayor coste.
	const u32 costs[static_cast<u32>(BusResource::Count)] = {kRefreshSlotsPerLine * lines, display,
								 copper, blitter, cpu};
	u8 best = 1u; // ignora Refresh (fijo)
	for (u8 i = 2u; i < static_cast<u8>(BusResource::Count); ++i) {
		if (costs[i] > costs[best]) best = i;
	}
	r.bottleneck = best;

	// Pistas para iterar.
	if (r.remaining_slots < 0) r.hints |= kHintOverBudget;
	if (r.remaining_pct10 < 80u) r.hints |= kHintTight;
	if (six_wide) r.hints |= kHintSixPlanesWide;
	if (hires_heavy) r.hints |= kHintHiresHeavy;
	if (r.total_slots != 0u && static_cast<u64>(cpu) * 2u > r.total_slots) r.hints |= kHintCpuNeedsFast;
	if (r.total_slots != 0u && static_cast<u64>(r.used_slots) * 2u > r.total_slots) r.hints |= kHintLowerFps;
	return r;
}

} // namespace eng::hw
