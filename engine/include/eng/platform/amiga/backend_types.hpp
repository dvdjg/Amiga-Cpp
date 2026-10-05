#pragma once

/// \file backend_types.hpp
/// Tipos de plataforma del backend Amiga: perfil de hardware, overlay de debug y triangulo
/// plano. Separados de `backend.hpp` (la clase `AmigaBackend`) por tamano/tema.

#include <eng/core/types/types.hpp>

namespace eng::amiga {

/// Perfil fisico/logico de la maquina objetivo.
///
/// Este perfil describe nuestras expectativas de diseno, no una deteccion dinamica
/// completa. `A500_1MB_Slow` significa 512 KB Chip + 512 KB trapdoor/bogo.
struct HardwareProfile {
	const char* id; ///< nombre del perfil (etiqueta legible)
	u16 chip_kb;    ///< Chip RAM en KB (la que ve el chipset por DMA)
	u16 slow_kb;    ///< Slow RAM en KB (trapdoor/bogo)
	u16 fast_kb;    ///< Fast RAM en KB (solo CPU)
	bool pal;       ///< `true` = timing PAL (50 Hz), `false` = NTSC
};

/// Perfil inicial realista para la maquina del proyecto.
constexpr HardwareProfile a500_1mb_slow {
	"A500_1MB_Slow",
	512,
	512,
	0,
	true,
};

/// Envoltorio del overlay de debug de WinUAE-DBG.
///
/// No dibuja en bitplanes Amiga. Es una herramienta de pruebas en host que permite
/// validar las primeras demos antes de escribir drivers graficos reales.
struct DebugOverlay {
	void clear();
	void text(s16 x, s16 y, const char* value, u32 rgb);
	void rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb);
	void filled_rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb);

	/// **Vista de recursos** del depurador gráfico de WinUAE: registra un bitmap, una paleta o
	/// una copperlist para que el gfx debugger los muestre como recursos nombrados (equivalente
	/// a `debug_register_*` de la demo original). `addr` debe permanecer válido mientras el
	/// recurso esté registrado; `debug_unregister(addr)` lo retira.
	void register_bitmap(const void* addr, const char* name, u16 width, u16 height,
			     u16 num_planes, bool interleaved, bool masked) noexcept;
	void register_palette(const void* addr, const char* name, u16 num_entries) noexcept;
	void register_copperlist(const void* addr, const char* name, u32 size) noexcept;
	void unregister(const void* addr) noexcept;
};

/// Triangulo plano (coordenadas de pantalla) para el relleno por Blitter.
struct FlatTriangle {
	s16 x0 = 0; ///< X del vértice 0 (pantalla, píxeles)
	s16 y0 = 0; ///< Y del vértice 0
	s16 x1 = 0; ///< X del vértice 1
	s16 y1 = 0; ///< Y del vértice 1
	s16 x2 = 0; ///< X del vértice 2
	s16 y2 = 0; ///< Y del vértice 2
	u8 color = 0; ///< índice de color (paleta)
};

} // namespace eng::amiga
