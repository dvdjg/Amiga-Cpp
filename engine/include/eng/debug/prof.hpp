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

#include <eng/core/types/types.hpp>

namespace eng::debug {

inline constexpr eng::u32 prof_magic = 0x50524f46u; ///< 'PROF'
inline constexpr eng::u8 prof_max_sections = 16u;

/// Bloque de profiling (se lee de golpe por el canal lateral/GDB).
///
/// Por seccion guarda suma (`cycles`) y llamadas (`calls`) para la media, mas `min`/`max`
/// del ciclo individual, que es lo que revela el **peor frame** (la media sola lo esconde).
/// `min_cycles` arranca en 0 y se siembra en la primera muestra (si `calls == 0`).
struct ProfBlock {
	eng::u32 magic;
	eng::u8 sections;
	eng::u8 pad0[3];
	eng::u32 frames;
	eng::u32 cycles[prof_max_sections];
	eng::u32 calls[prof_max_sections];
	eng::u32 min_cycles[prof_max_sections];
	eng::u32 max_cycles[prof_max_sections];
};

/// Instancia unica del bloque, `volatile`: UNA definicion por programa (no `inline`, para que
/// el `.map` le de direccion final y `tools/debug/profile.mjs` la resuelva; con `inline` cae en
/// `.gnu.linkonce.b` y el mapa no publica su direccion). La define `amiga.cpp`. Enlace **C**
/// (como `g_eng_run_status`): el mapa publica el nombre sin manglar con su direccion.
extern "C" volatile ProfBlock g_eng_prof;

/// Marcas de inicio (la escribe `ENG_PROF_BEGIN`). Definida en `amiga.cpp`.
extern "C" volatile eng::u32 g_prof_start[prof_max_sections];

/// Secciones que instrumenta el PROPIO engine (indices altos, para no chocar con las de la
/// demo). El coste es ~2 lecturas del contador por seccion y frame: despreciable, por eso
/// van siempre activas y permiten perfilar el interior del engine sin recompilar nada.
inline constexpr eng::u8 prof_sort_lines = 7;  ///< `Plan::sort_by_top`
inline constexpr eng::u8 prof_sort_prio = 8;   ///< `Plan::sort_priority_within_lines`
inline constexpr eng::u8 prof_emit = 9;        ///< `Plan::materialize` (emision al copper)
/// Interior del relleno por plano del Blitter (`fill_polygons_by_plane_patterns`): clear del
/// rectangulo sucio, contorno (lineas ONEDOT) y area fill, con sus esperas incluidas. Sirven
/// para decidir que optimizar sin recompilar (el total de la seccion de la demo los contiene).
inline constexpr eng::u8 prof_fill_clear = 10; ///< clear por plano (blit + espera)
inline constexpr eng::u8 prof_fill_lines = 11; ///< contorno ONEDOT por plano (lineas + esperas)
inline constexpr eng::u8 prof_fill_area = 12;  ///< area fill FILL_XOR por plano (+ espera)

/// Reloj de ciclos del periferico de depuracion (7,09379 MHz en A500).
inline eng::u32 prof_clock() {
	return *reinterpret_cast<volatile eng::u32*>(0xb7e928UL);
}

/// Reloj PAL del contador de ciclos anterior: 7093790 Hz. Fuente unica del valor.
inline constexpr eng::u32 prof_clock_hz = 7093790u;

/// Ventana de 1 segundo del reloj de ciclos para el campo de **fps del HUD**. Se
/// expresa con **un decimal** (`fps10`) porque las demos actuales corren a 15-40 fps
/// (el entero de `VersionStrip` no distingue 18,1 de 18,9). Es la MEDIA DE LA ULTIMA
/// VENTANA, no acumulada: si la cadencia cambia (activar una seccion, un pico puntual),
/// el HUD lo ensena al segundo siguiente.
///
///   HudFpsWindow fps;
///   fps.reset(frame_index, prof_clock());           // init: arma, no emite
///   if (fps.update(frame_index, prof_clock(), out_fps10)) {
///       // ~1 vez/segundo: pintar `out_fps10` ("25.0" -> 250) por palabras
///   }
///
/// Coste por frame: una resta y una comparacion (~10-15 ciclos); la division se paga
/// UNICAMENTE en el frame que cierra la ventana (justificado por la disciplina del
/// repo: fuera de todo bucle caliente). En host (sin periferico) pasar `cycle_now = 0`
/// y queda inactivo.
struct HudFpsWindow {
	eng::u32 cycle_ref = 0u; ///< ciclos de la ultima actualizacion (referencia de ventana)
	eng::u32 frame_ref = 0u; ///< `frame_index` de la ultima actualizacion
	bool armed = false;      ///< falso hasta ver un reloj valido (host/arranque)

	/// Arma la ventana en init: fija referencias sin emitir fps (la primera ventana
	/// empieza a contar aqui, no en 0).
	void reset(eng::u32 frame_index, eng::u32 cycle_now) noexcept {
		armed = (cycle_now != 0u);
		cycle_ref = cycle_now;
		frame_ref = frame_index;
	}

	/// Cierra la ventana cuando han pasado >= 1 s de reloj desde la anterior y escribe
	/// `out_fps10` = fps x 10. Devuelve `true` SOLO en ese frame.
	bool update(eng::u32 frame_index, eng::u32 cycle_now, eng::u32& out_fps10) noexcept {
		if (!armed) {
			reset(frame_index, cycle_now);
			return false;
		}
		const eng::u32 elapsed = cycle_now - cycle_ref;
		if (elapsed < prof_clock_hz) {
			return false; // aun no ha pasado 1 segundo
		}
		eng::u32 frames = frame_index - frame_ref;
		// Tope de 50 Hz PAL: una ventana justo por encima de 1 s no puede traer mas
		// (y evita overflow u32 en `frames * prof_clock_hz * 10`, que cabe hasta 60).
		if (frames > 50u) {
			frames = 50u;
		}
		out_fps10 = (frames * prof_clock_hz * 10u) / elapsed;
		cycle_ref = cycle_now;
		frame_ref = frame_index;
		return true;
	}
};

} // namespace eng::debug

// Fuera del target Amiga (host u otro m68k) el perfilado es NO-OP: el contador es un periferico
// del emulador y leerlo en un test host (o en Mega Drive/Atari) seria un acceso invalido. En
// Amiga (`ENG_AMIGA`, lo define el build) instrumenta (~2 lecturas por seccion y frame).
#if defined(ENG_AMIGA)

#define ENG_PROF_INIT(n)                                                                          \
	do {                                                                                       \
		::eng::debug::g_eng_prof.magic = ::eng::debug::prof_magic;                         \
		::eng::debug::g_eng_prof.sections = static_cast<::eng::u8>(n);                     \
		::eng::debug::g_eng_prof.frames = 0u;                                              \
		for (::eng::u8 _eng_prof_i = 0u; _eng_prof_i < (n); ++_eng_prof_i) {               \
			::eng::debug::g_eng_prof.cycles[_eng_prof_i] = 0u;                         \
			::eng::debug::g_eng_prof.calls[_eng_prof_i] = 0u;                          \
			::eng::debug::g_eng_prof.min_cycles[_eng_prof_i] = 0u;                     \
			::eng::debug::g_eng_prof.max_cycles[_eng_prof_i] = 0u;                     \
		}                                                                                  \
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
		const ::eng::u32 _eng_prof_d = _eng_prof_e - ::eng::debug::g_prof_start[(s)];      \
		::eng::debug::g_eng_prof.cycles[(s)] =                                             \
			static_cast<::eng::u32>(::eng::debug::g_eng_prof.cycles[(s)] + _eng_prof_d); \
		if (::eng::debug::g_eng_prof.calls[(s)] == 0u) {                                   \
			/* primera muestra: siembra min (arranca en 0 y no vale como minimo) */     \
			::eng::debug::g_eng_prof.min_cycles[(s)] = _eng_prof_d;                    \
		} else if (_eng_prof_d < ::eng::debug::g_eng_prof.min_cycles[(s)]) {               \
			::eng::debug::g_eng_prof.min_cycles[(s)] = _eng_prof_d;                    \
		}                                                                                  \
		if (_eng_prof_d > ::eng::debug::g_eng_prof.max_cycles[(s)]) {                      \
			::eng::debug::g_eng_prof.max_cycles[(s)] = _eng_prof_d;                    \
		}                                                                                  \
		::eng::debug::g_eng_prof.calls[(s)] =                                              \
			static_cast<::eng::u32>(::eng::debug::g_eng_prof.calls[(s)] + 1u);         \
	} while (0)

#else

#define ENG_PROF_INIT(n) do { (void)sizeof(n); } while (0)
#define ENG_PROF_FRAME() do {} while (0)
#define ENG_PROF_BEGIN(s) do { (void)sizeof(s); } while (0)
#define ENG_PROF_END(s) do { (void)sizeof(s); } while (0)

#endif
