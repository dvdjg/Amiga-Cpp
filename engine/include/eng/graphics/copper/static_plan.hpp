#pragma once

/// \file static_plan.hpp
/// **Compilador de copperlist en tiempo de compilación**, genérico sobre el mismo
/// vocabulario que el camino dinámico: `graphics::CopperIntent`.
///
/// El camino dinámico (`Plan` + `Scheduler`) recibe intenciones y las ordena y materializa
/// cada frame: es versátil, pero en la 086 ese trabajo se llevaba el 84 % del frame
/// (medido con `tools/debug/profile.mjs`). Lo que NO cambia de forma se puede resolver con
/// `constexpr`: la lista resultante se copia a Chip una vez y por frame solo se parchean
/// las palabras de dato.
///
/// Este compilador **no** limita los tipos: materializa los mismos `CopperIntentKind` que
/// `Scheduler::emit_single_intent` y con la misma codificación (WAIT de línea con el par de
/// overflow de VPOS, WAIT a posición, MOVEs de COLOR/punteros de bitplane/puntero de sprite/
/// BPLCON2), de modo que la lista compilada es **palabra a palabra** la del camino
/// dinámico para la misma escena (`tests/host/070_copper_plan` lo verifica).
///
/// Los valores que dependen de DIRECCIONES (punteros de plano o de sprite) no existen en
/// tiempo de compilación: el compilador los emite a 0 y anota su ranura en
/// `value_word[]`, para que `init` escriba la dirección real (una palabra por plano/par).
///
/// Uso:
///
///   static constexpr copper::CopperIntent kScene[] = { ... };   // o generado con constexpr
///   static constexpr auto kList = copper::compile_intents(kScene, {40u, 4u, 0x2cu});
///   static_assert(kList.words[0] == copper::wait_word(0x2c));   // gate en compilación
///   // init:  copiar kList.words[0..word_count) a los bloques de copperlist + parchear
///   //        las ranuras de punteros con las direcciones reales
///   // frame: kList.words[kList.value_word[i]] = nuevo_valor;     // parche de dato

#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::copper {

/// Geometría del display que necesita el compilador para las intenciones de layout
/// (`BitplaneSplit`/`ShiftLines`). Es constante en compilación: la BASE de los planos no lo
/// es (vive en la arena) y por eso sus palabras van a `value_word[]` para parchearlas.
struct StaticDisplayLayout {
	eng::u32 plane_bytes = 0;
	eng::u8 planes = 0;
	eng::u16 first_line = 0; ///< línea absoluta del arranque del display
};

/// Lista de copper compilada: words + ranuras de parcheo de las palabras de DATO.
template <eng::u16 MaxWords>
struct StaticCopperList {
	eng::util::Array<eng::u16, MaxWords> words {};
	/// Índice, dentro de `words`, del SEGUNDO word de cada MOVE (su dato). Parchear por
	/// frame es escribir `words[value_word[i]]`, sin tocar el resto de la lista.
	eng::util::Array<eng::u16, (MaxWords / 2u) + 1u> value_word {};
	/// `true` para las ranuras cuyo valor es una DIRECCIÓN (punteros de plano/sprite), que
	/// el llamador debe escribir en `init` porque no existe en tiempo de compilación.
	eng::util::Array<bool, (MaxWords / 2u) + 1u> value_is_address {};
	eng::u16 word_count = 0;
	eng::u16 value_count = 0;
	bool ok = false;

	/// Escribe el dato de una ranura (parche de un valor que ya existía).
	constexpr void patch(eng::u16 slot, eng::u16 value) {
		if (slot < value_count) {
			words[value_word[slot]] = value;
		}
	}
};

namespace static_detail {

/// Añade un WAIT de línea completo (0..311), con el mismo par de overflow que
/// `CopperBuilder::wait_line_pal` (`0xffdf/0xfffe` la primera vez que se cruza la 255) y
/// la misma máscara (`0xff00` dentro del campo, `0xfffe` al cruzar).
template <eng::u16 MaxWords>
constexpr void emit_wait_line(StaticCopperList<MaxWords>& out, eng::u16 vpos,
			      bool& overflow_sent) {
	if (vpos <= 255u) {
		out.words[out.word_count++] = wait_word(static_cast<eng::u8>(vpos & 0xffu));
		out.words[out.word_count++] = 0xff00u;
		return;
	}
	if (!overflow_sent) {
		overflow_sent = true;
		out.words[out.word_count++] = 0xffdfu;
		out.words[out.word_count++] = 0xfffeu;
	}
	out.words[out.word_count++] = wait_word(static_cast<eng::u8>(vpos & 0xffu));
	out.words[out.word_count++] = 0xfffeu;
}

/// MOVE con registro + dato, anotando la ranura del dato.
template <eng::u16 MaxWords>
constexpr void emit_move(StaticCopperList<MaxWords>& out, eng::u16 reg, eng::u16 value,
			 bool is_address = false) {
	out.words[out.word_count++] = reg;
	out.words[out.word_count++] = value;
	out.value_word[out.value_count] = static_cast<eng::u16>(out.word_count - 1u);
	out.value_is_address[out.value_count] = is_address;
	++out.value_count;
}

/// Materializa un MOVE de puntero de bitplane (PTH + PTL), con las MISMAS ranuras de
/// registro que `move_bitplane_pointer`.
template <eng::u16 MaxWords>
constexpr void emit_plane_pointer(StaticCopperList<MaxWords>& out, eng::u8 plane,
				  eng::u32 address) {
	emit_move(out, bitplane_pointer_high_register(plane),
		  static_cast<eng::u16>(address >> 16), true);
	emit_move(out, bitplane_pointer_low_register(plane),
		  static_cast<eng::u16>(address & 0xffffu), true);
}

} // namespace static_detail

/// Compila `N` intenciones (el MISMO tipo que consume el `Plan` dinámico) a words de
/// copperlist. Reproduce exactamente la materialización de `Scheduler::emit_single_intent`
/// (mismos WAIT, mismos registros, mismo orden y el mismo recorte de `count` en las
/// paletas). Las intenciones que el scheduler cuenta como `unhandled` (layout sin display o
/// rearm sin puntero) se omiten igual.
template <eng::u16 MaxWords, eng::u16 N>
constexpr StaticCopperList<MaxWords> compile_intents(const graphics::CopperIntent (&intents)[N],
						     StaticDisplayLayout layout) {
	using namespace static_detail;
	StaticCopperList<MaxWords> out {};
	bool overflow_sent = false;
	for (eng::u16 i = 0; i < N; ++i) {
		const graphics::CopperIntent& it = intents[i];
		switch (it.kind) {
			case graphics::CopperIntentKind::PaletteLine:
				emit_wait_line(out, it.top, overflow_sent);
				for (eng::u8 c = 0; c < it.count; ++c) {
					emit_move(out, color_register(static_cast<eng::u8>(it.first + c)),
						  it.colors[static_cast<eng::u8>(it.first + c)]);
				}
				break;
			case graphics::CopperIntentKind::PaletteSpan:
				out.words[out.word_count++] =
					wait_word(static_cast<eng::u8>(it.top & 0xffu),
						  static_cast<eng::u8>(it.hpos & 0xfeu));
				out.words[out.word_count++] = 0xfffeu;
				for (eng::u8 c = 0; c < it.count; ++c) {
					emit_move(out, color_register(static_cast<eng::u8>(it.first + c)),
						  it.colors[static_cast<eng::u8>(it.first + c)]);
				}
				break;
			case graphics::CopperIntentKind::BitplaneSplit:
				if (it.bitplanes.empty()) {
					break;
				}
				emit_wait_line(out, it.top, overflow_sent);
				for (eng::u8 p = 0; p < layout.planes; ++p) {
					emit_plane_pointer(out, p, static_cast<eng::u32>(p) * layout.plane_bytes);
				}
				break;
			case graphics::CopperIntentKind::ShiftLines:
				emit_wait_line(out, it.top, overflow_sent);
				for (eng::u8 p = 0; p < layout.planes; ++p) {
					const eng::s32 offset = static_cast<eng::s32>(
						static_cast<eng::u32>(p) * layout.plane_bytes +
						static_cast<eng::u32>(it.shift_x));
					emit_plane_pointer(out, p, static_cast<eng::u32>(offset));
				}
				break;
			case graphics::CopperIntentKind::SpriteRearm:
				if (it.sprite_ptr == nullptr) {
					break;
				}
				emit_wait_line(out, it.top, overflow_sent);
				emit_move(out, static_cast<eng::u16>(0x120u + it.sprite_channel * 4u), 0u, true);
				emit_move(out, static_cast<eng::u16>(0x122u + it.sprite_channel * 4u), 0u, true);
				break;
			case graphics::CopperIntentKind::Priority:
				emit_wait_line(out, it.top, overflow_sent);
				emit_move(out, static_cast<eng::u16>(Register::BPLCON2),
					  static_cast<eng::u16>(it.shift_x));
				break;
		}
		if (out.word_count + 8u > MaxWords) {
			break; // capacidad: el static_assert del llamador lo detecta en compilación
		}
	}
	out.ok = true;
	return out;
}

} // namespace eng::copper
