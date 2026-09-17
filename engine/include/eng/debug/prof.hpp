#pragma once

/// \file prof.hpp
/// Contadores de ciclos por SECCION dentro del frame, para localizar donde se va el
/// tiempo (el contador total entre frames lo da `tools/debug/measure-fps.mjs`).
///
/// Uso (coste ~2 lecturas del contador por seccion y frame):
///
///   ENG_PROF_INIT(kProfCount);                 // una vez, en init
///   ENG_PROF_FRAME();                          // una vez por frame
///   ENG_PROF_BEGIN(kProfBlits); ... ENG_PROF_END(kProfBlits);
///
/// El host lee el bloque desde el simbolo `g_eng_prof` del `.map`, sin tocar el MCP:
///
///   node tools/debug/profile.mjs <demo> [config]
///
/// El contador es el periferico de depuracion (0xB7E928), el mismo que usa
/// `measure-fps.mjs`: ciclos emulados a 7,09379 MHz. Un campo PAL son ~141.876 ciclos.
///
/// La seccion NO instrumentada (espera de VBlank, `render`, sobrecarga del bucle) se
/// obtiene por resta: total/frame (measure-fps) menos la suma de secciones.

#include <eng/core/types.hpp>

namespace eng::debug {

inline constexpr eng::u32 prof_magic = 0x50524f46u; ///< 'PROF'
inline constexpr eng::u8 prof_max_sections = 16u;

/// Bloque de profiling (se lee de golpe por el canal lateral/GDB).
struct ProfBlock {
	eng::u32 magic;
	eng::u8 sections;
	eng::u8 pad0[3];
	eng::u32 frames;
	eng::u32 cycles[prof_max_sections];
	eng::u32 calls[prof_max_sections];
};

/// Instancia unica del bloque, `volatile` y `inline`: una sola definicion por programa
/// (sin errores de enlace en host ni en Amiga) y accesos ordenados respecto al contador,
/// que es lo que hace fiable la medida tambien en builds optimizados.
///
/// El host lo localiza por el simbolo del `.map`; con enlace C++ el nombre va manglado
/// (`_ZN3eng6debug10g_eng_profE`), asi que `tools/debug/profile.mjs` busca por subcadena.
inline volatile ProfBlock g_eng_prof {};

/// Marcas de inicio (la escribe `ENG_PROF_BEGIN`).
inline volatile eng::u32 g_prof_start[prof_max_sections] {};

/// Secciones que instrumenta el PROPIO engine (indices altos, para no chocar con las de la
/// demo). El coste es ~2 lecturas del contador por seccion y frame: despreciable, por eso
/// van siempre activas y permiten perfilar el interior del engine sin recompilar nada.
inline constexpr eng::u8 prof_sort_lines = 7;  ///< `Plan::sort_by_top`
inline constexpr eng::u8 prof_sort_prio = 8;   ///< `Plan::sort_priority_within_lines`
inline constexpr eng::u8 prof_emit = 9;        ///< `Plan::materialize` (emision al copper)

/// Reloj de ciclos del periferico de depuracion (7,09379 MHz en A500).
inline eng::u32 prof_clock() {
	return *reinterpret_cast<volatile eng::u32*>(0xb7e928UL);
}

} // namespace eng::debug

// Fuera del target (host) el perfilado es NO-OP: el contador es un periferico del emulador
// y leerlo en un test host seria un acceso invalido. En m68k instrumenta (~2 lecturas por
// seccion y frame).
#if defined(__m68k__)

#define ENG_PROF_INIT(n)                                                                          \
	do {                                                                                       \
		::eng::debug::g_eng_prof.magic = ::eng::debug::prof_magic;                         \
		::eng::debug::g_eng_prof.sections = static_cast<::eng::u8>(n);                     \
	} while (0)
#define ENG_PROF_FRAME()                                                                          \
	do {                                                                                       \
		::eng::debug::g_eng_prof.frames =                                                  \
			static_cast<::eng::u32>(::eng::debug::g_eng_prof.frames + 1u);             \
	} while (0)
#define ENG_PROF_BEGIN(s)                                                                         \
	do {                                                                                       \
		::eng::debug::g_prof_start[(s)] = ::eng::debug::prof_clock();                      \
	} while (0)
#define ENG_PROF_END(s)                                                                           \
	do {                                                                                       \
		const ::eng::u32 _eng_prof_e = ::eng::debug::prof_clock();                         \
		::eng::debug::g_eng_prof.cycles[(s)] =                                             \
			static_cast<::eng::u32>(::eng::debug::g_eng_prof.cycles[(s)] +             \
						(_eng_prof_e - ::eng::debug::g_prof_start[(s)]));  \
		::eng::debug::g_eng_prof.calls[(s)] =                                              \
			static_cast<::eng::u32>(::eng::debug::g_eng_prof.calls[(s)] + 1u);         \
	} while (0)

#else

#define ENG_PROF_INIT(n) do { (void)sizeof(n); } while (0)
#define ENG_PROF_FRAME() do {} while (0)
#define ENG_PROF_BEGIN(s) do { (void)sizeof(s); } while (0)
#define ENG_PROF_END(s) do { (void)sizeof(s); } while (0)

#endif
