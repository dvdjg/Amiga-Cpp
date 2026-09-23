#pragma once

/// \file bob.hpp
/// **BOB**: objeto de bitmap (**copia** de planos + máscara opcional) con
/// desplazamiento fino por barrel shifter, parametrizable por profundidad, layout y
/// algoritmo de dibujo/borrado.
///
/// Regla del repo («BOB ≠ polígono», `AGENTS.md`): un BOB se dibuja **copiando** un
/// bitmap pre-renderizado; el Blitter poligonal (line-draw + area-fill) es para relleno
/// vectorial/3D, no para objetos. Referencias: AHRM 3.ª (Blitter),
/// `amiga-bootcamp/08_graphics/blitter_programming.md` (tabla de minterms y *Use Case 4:
/// interleaved bitplane BOBs*) y `demoscene-repo-orig/effects/bobs3d/bobs3d.c` (OR-bobs
/// intercalados, un blit por objeto).
///
/// Un BOB **no posee memoria**: describe una hoja de frames y se materializa como
/// `BlitJob`s en un `FramePlan`. Con planos **intercalados** el objeto es **UN blit**
/// (altura = alto × planos); en **planar** son N (uno por plano). El borrado es otro
/// blit (`ClearRect`) o ninguno (objetos aditivos).
///
/// Contrato de la **hoja** (`sheet`): por cada fila de cada plano, `base + 1` palabras,
/// donde `base = (width+15)/16` y la última palabra es **guarda** (absorbe la lectura
/// desplazada del barrel shifter). Planar: `planes · height` filas contiguas y una
/// palabra extra al final. Interleaved: `height · planes` filas alternando planos,
/// también con la guarda por fila. `frame_stride` separa frames (0 = denso).
///
/// Contrato del **borrado** (`bob_erase`/`bob_erase_box`) y del **save-under**: la caja
/// procesa `base + (shift != 0)` palabras — las MISMAS que el dibujo — porque con
/// desplazamiento fino el blit escribe la palabra extra. Borrar solo `base` deja hasta
/// 15 px por fila sin limpiar (residuo en el borde derecho del objeto).
///
/// Uso por frame (el estado — posición, frame — lo lleva el llamador o un manager):
///
///   bob_erase(plan, bob, prev_x, prev_y, target);   // si bob.erase == ClearRect
///   bob_draw(plan, bob, frame, x, y, target);
///
/// Verificación: geometría de los `BlitJob`s cubierta por el test host `072_actor`
/// (matriz dibujo × layout × borrado × 3..6 planos) y el camino Blitter en hardware por la
/// demo `086_bob_objects` (gate visual: cookie-cut/OR/opaco, borrado por caja y save-under,
/// con desplazamiento fino). El residuo de borde que apareció al montarla —borrar `base`
/// palabras en vez de `base + shift`— está corregido y cubierto por `072_actor`.

#include <eng/core/types/types.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::graphics {

/// Algoritmo de dibujo del objeto.
enum class BobDraw : u8 {
	/// `D = A | D` (minterm `$FC`): **aditivo**/glow, sin máscara; los ceros del
	/// objeto dejan el fondo. Camino de `bobs3d.c`.
	Or,
	/// `D = A` (minterm `$F0`): **opaco**, sin máscara; sobrescribe el fondo.
	Opaque,
	/// `D = A & D` (minterm `$C0`, `B = D`): **sombra**/sustractivo, sin máscara; los
	/// unos del objeto oscurecen el fondo.
	And,
	/// `D = A·B + ¬A·C` (minterm `$CA`): **cookie-cut** con máscara (transparencia).
	CookieCut,
};

/// Algoritmo de borrado.
enum class BobErase : u8 {
	/// No borra: objeto aditivo o fondo que se repinta entero.
	None,
	/// Borra la caja del objeto (`D = 0`) antes de dibujar el nuevo. Barato; válido
	/// cuando el fondo bajo el objeto es liso (p. ej. un cielo por copper).
	ClearRect,
	/// Guarda el fondo y lo restaura al moverlo (save-under). **PENDIENTE**: exige
	/// buffer de guardado y 2 jobs (`RestoreRect` + `CopyRect`), como la demo 050.
	RestoreUnder,
};

/// Layout de los planos (hoja del objeto y bitmap destino).
enum class BobLayout : u8 {
	Planar,      ///< N planos contiguos: N blits por objeto.
	Interleaved, ///< filas de planos alternadas: 1 blit por objeto.
};

/// Descripción de un objeto de bitmap. No posee memoria (apunta a bloques del
/// llamador, en Chip RAM: el Blitter solo lee Chip).
struct Bob {
	const u8* sheet = nullptr; ///< frames del objeto (ver contrato de la hoja)
	const u8* mask = nullptr;  ///< cookie-cut: un plano de 1 bit (misma rejilla)
	u16 width = 0;             ///< ancho en píxeles
	u16 height = 0;            ///< alto en píxeles
	u8 planes = 0;             ///< profundidad (3..6)
	u8 frame_count = 1;        ///< frames en la hoja
	u32 frame_stride = 0;      ///< bytes entre frames (0 = denso)
	/// Bytes por fila de la HOJA (0 = contrato compacto: `(base+1)*2`). Hay que
	/// declararlo cuando la hoja tiene filas mas anchas que el frame (varios frames
	/// por fila, atlas) o cuando el frame es un sub-rectangulo de la hoja.
	u32 sheet_row_bytes = 0;
	BobLayout layout = BobLayout::Interleaved;
	BobDraw draw = BobDraw::Or;
	BobErase erase = BobErase::None;
};

/// Geometría del bitmap destino (los planos del playfield).
struct BobTarget {
	u8* base = nullptr;    ///< inicio del plano 0 (fila 0)
	u16 row_bytes = 0;     ///< bytes por fila de UN plano
	u32 plane_bytes = 0;   ///< separación entre planos (solo `Planar`)
	u8 planes = 0;         ///< planos del destino
	BobLayout layout = BobLayout::Interleaved;
};

namespace bob_detail {

/// Palabras base por fila y plano (sin la guarda).
constexpr u16 base_words(const Bob& bob) {
	return static_cast<u16>((bob.width + 15u) / 16u);
}

/// Palabras procesadas por fila incluyendo el desplazamiento fino (la guarda absorbe
/// la palabra extra).
constexpr u16 blit_words(const Bob& bob, u8 shift) {
	return static_cast<u16>(base_words(bob) + (shift != 0u ? 1u : 0u));
}

/// Bytes por fila de la HOJA (base + guarda).
constexpr u32 sheet_row_bytes(const Bob& bob) {
	return (static_cast<u32>(base_words(bob)) + 1u) * 2u;
}

/// Bytes por fila que separan dos filas consecutivas de la hoja: el valor declarado
/// si lo hay, o el del contrato compacto. Es lo que hay que usar para el modulo de
/// origen y el stride entre planos de la hoja.
constexpr u32 sheet_row_of(const Bob& bob) {
	return bob.sheet_row_bytes != 0u ? bob.sheet_row_bytes : sheet_row_bytes(bob);
}

constexpr bool valid(const Bob& bob, const BobTarget& t) {
	return bob.sheet != nullptr && t.base != nullptr && bob.width != 0u && bob.height != 0u &&
	       bob.planes != 0u && bob.planes <= 6u && bob.planes <= t.planes && t.row_bytes != 0u;
}

} // namespace bob_detail

/// Borra una caja de `w x h` en `(x,y)` (`D = 0`). 1 job si el bitmap es intercalado.
/// Se separa de `bob_erase` para poder borrar el rectangulo PREVIO del objeto, que
/// puede tener otro tamaño que el frame actual (animacion con formas distintas).
inline bool bob_erase_box(FramePlan& plan, const Bob& bob, u16 w, u16 h, s16 x, s16 y,
			  const BobTarget& t) {
	using namespace bob_detail;
	if (!valid(bob, t) || w == 0u || h == 0u) {
		return true;
	}
	const s16 wx = static_cast<s16>(x & ~15);
	if (wx < 0) {
		return true; // caja fuera por la izquierda
	}
	const bool inter = (t.layout == BobLayout::Interleaved);
	// La caja debe cubrir el objeto TAL COMO SE DIBUJA: con desplazamiento fino el blit
	// procesa una palabra de más (`base + shift`), así que borrar `base` dejaría el borde
	// derecho del objeto sin limpiar (residuo de hasta 15 px por fila).
	const u16 words = static_cast<u16>((w + 15u) / 16u + ((x & 15) != 0 ? 1u : 0u));
	const u32 start_row = inter ? static_cast<u32>(t.row_bytes) * t.planes
				    : t.row_bytes; // fila del bitmap en la que empieza la caja
	BlitJob job {};
	job.destination = {reinterpret_cast<u16*>(t.base + static_cast<u32>(y) * start_row +
						  (static_cast<u32>(wx) >> 3u))};
	job.words_per_row = words;
	job.height = inter ? static_cast<u16>(h * bob.planes) : h;
	// El puntero avanza una fila de plano por fila de blit: modulo = fila − procesado.
	job.destination_modulo_bytes = static_cast<s16>(t.row_bytes - static_cast<u32>(words) * 2u);
	job.bitplane_count = inter ? 1u : bob.planes;
	job.destination_plane_stride_bytes = inter ? 0u : t.plane_bytes;
	job.interleaved = inter;
	job.minterm = 0x00u; // D = 0
	return plan.add_clear_rect(job);
}

/// Borra la caja del objeto en `(x,y)` (`BobErase::ClearRect`; con otro algoritmo no
/// hace nada).
inline bool bob_erase(FramePlan& plan, const Bob& bob, s16 x, s16 y, const BobTarget& t) {
	if (bob.erase != BobErase::ClearRect) {
		return true;
	}
	return bob_erase_box(plan, bob, bob.width, bob.height, x, y, t);
}

/// Dibuja el `frame` del objeto en `(x,y)`.
///
/// Camino caliente (un BOB por objeto y frame): `always_inline` para que el contrato
/// de la hoja y la aritmetica de modulos queden dentro del bucle del llamador, sin
/// `jsr` por objeto. Medido en la 117 (bobs3d): `draw` ~2.660 ciclos/BOB sin inline.
__attribute__((always_inline)) inline bool bob_draw(FramePlan& plan, const Bob& bob, u8 frame, s16 x, s16 y, const BobTarget& t) {
	using namespace bob_detail;
	if (!valid(bob, t) || frame >= bob.frame_count) {
		return false;
	}
	if (bob.draw == BobDraw::CookieCut && bob.layout == BobLayout::Interleaved) {
		// Cookie-cut con planos intercalados exige máscara "expandida" (una copia por
		// plano) y modulos A/B distintos: no lo cubre el ejecutor actual (documentado).
		return false;
	}
	const u8 shift = static_cast<u8>(x & 15);
	const bool inter = (t.layout == BobLayout::Interleaved);
	const u16 words = blit_words(bob, shift);
	const s16 wx = static_cast<s16>(x & ~15);
	const s16 x_start = (wx < 0) ? 0 : wx;
	const u32 start_row = inter ? static_cast<u32>(t.row_bytes) * t.planes : t.row_bytes;

	BlitJob job {};
	job.source = {reinterpret_cast<const u16*>(bob.sheet +
						   static_cast<u32>(frame) * bob.frame_stride)};
	job.destination = {reinterpret_cast<u16*>(t.base + static_cast<u32>(y) * start_row +
						  (static_cast<u32>(x_start) >> 3u))};
	job.words_per_row = words;
	job.height = inter ? static_cast<u16>(bob.height * bob.planes) : bob.height;
	job.source_shift = shift;
	job.bitplane_count = inter ? 1u : bob.planes;
	// La hoja lleva guarda por fila (`sheet_row_bytes`): el blit consume `words` y la
	// fila avanza `sheet_row_bytes`, asi que el modulo es la diferencia (2 B si no hay
	// desplazamiento; 0 si el barrel shifter lee la guarda).
	job.source_modulo_bytes = static_cast<s16>(sheet_row_of(bob) - static_cast<u32>(words) * 2u);
	job.destination_modulo_bytes = static_cast<s16>(t.row_bytes - static_cast<u32>(words) * 2u);
	job.source_plane_stride_bytes = inter ? 0u : bob.height * sheet_row_of(bob);
	job.destination_plane_stride_bytes = inter ? 0u : t.plane_bytes;
	job.interleaved = inter;
	job.minterm = (bob.draw == BobDraw::Or) ? 0x00fcu
		    : (bob.draw == BobDraw::Opaque ? 0x00f0u
		    : (bob.draw == BobDraw::And ? 0x00c0u : 0x00cau));
	if (bob.draw == BobDraw::CookieCut) {
		if (bob.mask == nullptr) {
			return false; // cookie-cut sin mascara
		}
		job.mask = {reinterpret_cast<const u16*>(bob.mask)};
		return plan.add_masked_bob(job);
	}
	return plan.add_or_blob(job);
}

} // namespace eng::graphics
