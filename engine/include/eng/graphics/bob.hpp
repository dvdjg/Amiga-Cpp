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

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/bitmap_view.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/plane_layout.hpp>

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
	/// Guarda el fondo y lo restaura al moverlo (save-under). `bob_erase` **no** lo
	/// ejecuta (solo atiende `ClearRect`): es una secuencia en la capa superior — la
	/// política `BackgroundPolicy::SaveUnder` del actor (`scene/actor_types.hpp`) y el
	/// camino de intención (`scene/layer.hpp`) emiten `RestoreRect` + `CopyRect` con
	/// `bob_save_box`/`bob_restore_box`; cubierto por HOST-072 y la demo 086.
	RestoreUnder,
};

/// Layout de los planos (alias del enum único `PlaneLayout`); `BobLayout::Planar` = contiguo.
using BobLayout = PlaneLayout;

/// Empaquetado de la **máscara** del cookie-cut dentro de la hoja.
enum class BobMaskPack : u8 {
	/// La máscara es un plano de 1 bit **aparte** (`Bob::mask`, misma rejilla). Es la forma
	/// del cookie-cut planar.
	SeparatePlane,
	/// La hoja intercala `[imagen][máscara]` por cada fila de cada plano (`width/16` palabras
	/// de imagen seguidas de `width/16` de máscara, sin guarda): cookie-cut con planos
	/// intercalados en **un solo** blit (minterm `$CA`). `Bob::mask` se ignora (la máscara
	/// va en la propia hoja). Contrato de `make_interleaved_masked_bob` (`blit_job.hpp`).
	InterleavedPair,
};

/// Descripción de un objeto de bitmap. No posee memoria (apunta a bloques del
/// llamador, en Chip RAM: el Blitter solo lee Chip).
struct Bob {
	ChipView<BobTag> sheet {}; ///< frames del objeto (Chip: el Blitter solo lee Chip)
	ChipView<BobTag> mask {};  ///< cookie-cut: un plano de 1 bit (misma rejilla)
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
	BobMaskPack mask_pack = BobMaskPack::SeparatePlane;
};

/// Geometría del bitmap destino (los planos del playfield): una **`BitmapView`** con el **banco
/// Chip en el tipo** (el Blitter solo escribe Chip).
using BobTarget = BitmapView<PlaneTag, eng::MemoryKind::Chip>;

/// Construye la geometría de destino a partir de la memoria **Chip** de la zona y su geometría.
/// `plane_bytes` (separación entre planos) se **deriva** de `layout`/`row_bytes`/`height` en la
/// propia vista (`plane_pointer_step`), así que no se pasa.
/// \param planes       memoria Chip del plano 0 (base).
/// \param row_bytes    bytes por fila de un plano.
/// \param height       alto en filas.
/// \param plane_count  nº de planos.
/// \param layout       `Interleaved` o `Planar` (contiguo).
/// \param plane_step   separación entre planos (`0` = derivada del layout).
/// \return la geometría de destino (`BobTarget`) lista para `bob_draw`/`Sprite::draw`.
[[nodiscard]] inline BobTarget make_bob_target(eng::MemView<PlaneTag, eng::MemoryKind::Chip> planes,
					       eng::u16 row_bytes, eng::u16 height,
					       eng::u8 plane_count,
					       BobLayout layout = BobLayout::Interleaved,
					       eng::u32 plane_step = 0u) noexcept {
	BobTarget t {};
	t.planes = planes;
	t.row_bytes = row_bytes;
	t.height = height;
	t.plane_count = plane_count;
	t.layout = layout;
	t.plane_step = plane_step;
	return t;
}

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
	return !bob.sheet.empty() && !t.planes.empty() && bob.width != 0u && bob.height != 0u &&
	       bob.planes != 0u && bob.planes <= 6u && bob.planes <= t.plane_count && t.row_bytes != 0u;
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
	const u32 start_row = inter ? static_cast<u32>(t.row_bytes) * t.plane_count
				    : t.row_bytes; // fila del bitmap en la que empieza la caja
	// **Construcción IN SITU**: ranura del plan, sin `BlitJob` local ni copia; se fijan todos los
	// campos que el encoder lee (ver `ZERO_COST_FRAME_PATH.md`).
	BlitJob& job = plan.begin_blit_job(BlitJobKind::ClearRect);
	job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
		t.data() + static_cast<u32>(y) * start_row + (static_cast<u32>(wx) >> 3u)));
	job.words_per_row = words;
	job.source_words_per_row = 0u;
	job.source_modulo_bytes = 0;
	job.descending = false;
	job.source_shift = 0u;
	if (inter && bob.planes == t.plane_count) {
		// Si el BOB cubre todos los planos del destino, sus filas físicas son contiguas.
		job.height = static_cast<u16>(h * bob.planes);
		job.destination_modulo_bytes = mod16(static_cast<s32>(t.row_bytes) - static_cast<s32>(words) * 2);
		job.bitplane_count = 1u;
		job.destination_plane_stride_bytes = 0u;
		job.interleaved = true;
		job.minterm = 0x00u; // D = 0
		return plan.commit_blit_job();
	} else if (inter) {
		// Si el BOB ocupa menos planos que el destino, conservar la rejilla interleaved
		// mediante un blit por plano lógico; no recorrer filas físicas consecutivas.
		job.height = h;
		job.destination_modulo_bytes = mod16(
			static_cast<s32>(t.row_bytes) * t.plane_count - static_cast<s32>(words) * 2);
		job.bitplane_count = bob.planes;
		job.destination_plane_stride_bytes = t.plane_pointer_step();
	} else {
		job.height = h;
		job.destination_modulo_bytes = mod16(static_cast<s32>(t.row_bytes) - static_cast<s32>(words) * 2);
		job.bitplane_count = bob.planes;
		job.destination_plane_stride_bytes = t.plane_pointer_step();
	}
	job.interleaved = inter;
	job.minterm = 0x00u; // D = 0
	return plan.commit_blit_job();
}

/// **Save-under (guardar)**: copia la caja de `w x h` en `(x,y)` del destino (Chip) al buffer
/// `save`; `false` si el buffer no da (rechazo controlado). El cálculo de palabras cubre la
/// palabra extra del barrel shifter (desplazamiento fino), como el borrado. Único sitio del
/// algoritmo: lo usan el camino de actor (`BackgroundPolicy::SaveUnder`, vía
/// `scene::actor_detail::emit_save`) y el de intención (`BobErase::RestoreUnder`). El destino
/// debe ser **planar** (planos contiguos).
inline bool bob_save_box(FramePlan& plan, const Bob& bob, u16 w, u16 h, s16 x, s16 y,
			 const BobTarget& t, eng::Span<eng::u16> save, u16 save_words_per_row,
			 u16 save_height) {
	const u16 words = static_cast<u16>((w + 15u) / 16u + ((x & 15) != 0 ? 1u : 0u));
	if (save.empty() || words > save_words_per_row || h > save_height) {
		return false; // sin buffer o buffer insuficiente
	}
	const u32 save_row_bytes = static_cast<u32>(save_words_per_row) * 2u;
	BlitJob& job = plan.begin_blit_job(BlitJobKind::CopyRect);
	job.destination = BlitPtr::from_storage(save.data());
	job.source = BlitPtr::from_storage(reinterpret_cast<const u16*>(
		t.data() + static_cast<u32>(y) * t.row_bytes + (static_cast<u32>(x & ~15) >> 3u)));
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = bob.planes;
	job.source_modulo_bytes = mod16(static_cast<s32>(t.row_bytes) - static_cast<s32>(words) * 2);
	job.destination_modulo_bytes = mod16(static_cast<s32>(save_row_bytes) - static_cast<s32>(words) * 2);
	job.source_plane_stride_bytes = t.plane_pointer_step();
	job.destination_plane_stride_bytes = save_row_bytes * save_height;
	job.source_words_per_row = 0u;
	job.descending = false;
	job.source_shift = 0u;
	job.interleaved = false;
	return plan.commit_blit_job();
}

/// **Save-under (restaurar)**: devuelve el buffer `save` a la caja de `w x h` en `(x,y)` del
/// destino. Inverso de `bob_save_box`; mismo cálculo de palabras y misma guarda de capacidad.
inline bool bob_restore_box(FramePlan& plan, const Bob& bob, u16 w, u16 h, s16 x, s16 y,
			    const BobTarget& t, eng::Span<eng::u16> save, u16 save_words_per_row,
			    u16 save_height) {
	const u16 words = static_cast<u16>((w + 15u) / 16u + ((x & 15) != 0 ? 1u : 0u));
	if (save.empty() || words > save_words_per_row || h > save_height) {
		return false;
	}
	const u32 save_row_bytes = static_cast<u32>(save_words_per_row) * 2u;
	BlitJob& job = plan.begin_blit_job(BlitJobKind::RestoreRect);
	job.source = BlitPtr::from_storage(save.data());
	job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
		t.data() + static_cast<u32>(y) * t.row_bytes + (static_cast<u32>(x & ~15) >> 3u)));
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = bob.planes;
	job.source_modulo_bytes = mod16(static_cast<s32>(save_row_bytes) - static_cast<s32>(words) * 2);
	job.destination_modulo_bytes = mod16(static_cast<s32>(t.row_bytes) - static_cast<s32>(words) * 2);
	job.source_plane_stride_bytes = save_row_bytes * save_height;
	job.destination_plane_stride_bytes = t.plane_pointer_step();
	job.source_words_per_row = 0u;
	job.descending = false;
	job.source_shift = 0u;
	job.interleaved = false;
	return plan.commit_blit_job();
}

/// Borra la caja del objeto en `(x,y)` (`BobErase::ClearRect`; con otro algoritmo no
/// hace nada).
/// \param plan  plan del frame (recibe el blit de borrado).
/// \param bob   objeto (usa `width`/`height`).
/// \param x,y   posición del ancla (píxeles).
/// \param t     geometría de destino.
/// \return `false` si el blit no cupo; `true` si no había nada que borrar.
inline bool bob_erase(FramePlan& plan, const Bob& bob, s16 x, s16 y, const BobTarget& t) {
	if (bob.erase != BobErase::ClearRect) {
		return true;
	}
	return bob_erase_box(plan, bob, bob.width, bob.height, x, y, t);
}

/// Dibuja un objeto cookie-cut con hoja **par** (`BobMaskPack::InterleavedPair`): un único
/// blit `$CA` con el cableado explícito de la hoja interleaved. La hoja intercala,
/// por cada fila de cada plano, `width/16` palabras de imagen seguidas de `width/16` de máscara (sin
/// palabra de guarda). Es la geometría de `make_interleaved_masked_bob` (`blit_job.hpp`); el
/// bit de máscara de cada plano se materializa en el propio blit, sin copia expandida.
/// El encoder conecta `A` = máscara (segunda mitad) y `B` = imagen (primera mitad) con
/// `ASH = BSH = x & 15`, como el original de Bartman.
inline bool bob_draw_interleaved_pair(FramePlan& plan, const Bob& bob, u8 frame, s16 x, s16 y,
				      const BobTarget& t) {
	using namespace bob_detail;
	// Precondición: `bob_draw` (único llamador) ya validó `bob`/`t` y `frame < frame_count`.
	const u16 words = static_cast<u16>(bob.width / 16u);
	if (words == 0u) {
		return false;
	}
	const u8 shift = static_cast<u8>(x & 15);
	const s16 wx = static_cast<s16>(x & ~15);
	const s16 x_start = (wx < 0) ? 0 : wx;
	const u32 start_row = static_cast<u32>(t.row_bytes) * t.plane_count;
	const u32 frame_offset = static_cast<u32>(frame) * bob.frame_stride;
	const u16* src = reinterpret_cast<const u16*>(bob.sheet.address(frame_offset).cptr());
	// **Construccion IN SITU** (coste cero): se rellena la ranura del plan directamente, sin
	// `BlitJob` local (que en -O0 es un `memset`) ni copia (otro `memcpy`). Ver `ZERO_COST_FRAME_PATH.md`.
	BlitJob& job = plan.begin_blit_job(BlitJobKind::MaskedBobCookieCut);
	job.source = BlitPtr::from_storage(src);               // primera mitad: imagen
	job.mask = BlitPtr::from_storage(src + words);         // segunda mitad: máscara expandida
	job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
		t.data() + static_cast<u32>(y) * start_row + (static_cast<u32>(x_start) >> 3u)));
	job.words_per_row = words;
	job.height = static_cast<u16>(bob.height * bob.planes);
	job.source_modulo_bytes = mod16u(words * 2u);
	job.destination_modulo_bytes = mod16(static_cast<s32>(t.row_bytes) - static_cast<s32>(words) * 2);
	job.bitplane_count = 1u;
	// La máscara expandida y la imagen comparten shift; el engine traduce el layout empaquetado.
	job.source_shift = shift;
	job.minterm = 0x00cau;
	job.interleaved = true;
	// La ranura es **estable** (se reutiliza cada frame): hay que fijar TODO campo que el encoder
	// lea, no solo los que cambian. `source_words_per_row` (fuente compacta) y `descending` los
	// lee `blitter_job_from`; si quedaran con un valor previo, el blit saldría mal. Ver
	// `ZERO_COST_FRAME_PATH.md`.
	job.source_words_per_row = 0u;
	job.descending = false;
	job.source_plane_stride_bytes = 0u;
	job.destination_plane_stride_bytes = 0u;
	return plan.commit_blit_job();
}

/// Dibuja el `frame` del objeto en `(x,y)`.
///
/// Camino caliente (un BOB por objeto y frame): `always_inline` para que el contrato
/// de la hoja y la aritmetica de modulos queden dentro del bucle del llamador, sin
/// `jsr` por objeto. Medido en la 117 (bobs3d): `draw` ~2.660 ciclos/BOB sin inline.
/// \param plan   plan del frame (recibe el/los blit(s)).
/// \param bob    objeto (hoja, máscara, layout, modo de dibujo).
/// \param frame  índice de frame dentro de la hoja.
/// \param x,y    posición del ancla (píxeles; el `offset` del `Bob` se aplica).
/// \param t      geometría de destino (planos Chip, `row_bytes`, planos, layout).
/// \return `false` si el `bob`/`t` no son válidos, el frame está fuera de rango o el
///         cookie-cut interleaved no lo cubre el ejecutor.
__attribute__((always_inline)) inline bool bob_draw(FramePlan& plan, const Bob& bob, u8 frame, s16 x, s16 y, const BobTarget& t) {
	using namespace bob_detail;
	if (!valid(bob, t) || frame >= bob.frame_count) {
		return false;
	}
	if (bob.draw == BobDraw::CookieCut && bob.layout == BobLayout::Interleaved) {
		if (bob.mask_pack == BobMaskPack::InterleavedPair) {
			return bob_draw_interleaved_pair(plan, bob, frame, x, y, t);
		}
		// Con máscara en un plano suelto, el cookie-cut intercalado exigiría una máscara
		// "expandida" (una copia por plano) y modulos A/B distintos: no lo cubre el
		// ejecutor actual (documentado). Usar `BobMaskPack::InterleavedPair` para el
		// camino de un solo blit.
		return false;
	}
	const u8 shift = static_cast<u8>(x & 15);
	const bool inter = (t.layout == BobLayout::Interleaved);
	const u16 words = blit_words(bob, shift);
	const s16 wx = static_cast<s16>(x & ~15);
	const s16 x_start = (wx < 0) ? 0 : wx;
	const u32 start_row = inter ? static_cast<u32>(t.row_bytes) * t.plane_count : t.row_bytes;
	const bool masked = bob.draw == BobDraw::CookieCut;
	if (masked && bob.mask.empty()) {
		return false; // cookie-cut sin mascara
	}
	// **Construcción IN SITU**: ranura del plan, sin `BlitJob` local ni copia.
	BlitJob& job = plan.begin_blit_job(masked ? BlitJobKind::MaskedBobCookieCut
						  : BlitJobKind::OrBlob);
	job.source = BlitPtr::from_storage(reinterpret_cast<const u16*>(
		bob.sheet.address(static_cast<u32>(frame) * bob.frame_stride).cptr()));
	job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
		t.data() + static_cast<u32>(y) * start_row + (static_cast<u32>(x_start) >> 3u)));
	job.words_per_row = words;
	job.height = inter ? static_cast<u16>(bob.height * bob.planes) : bob.height;
	job.source_shift = shift;
	job.bitplane_count = inter ? 1u : bob.planes;
	job.source_modulo_bytes = mod16(static_cast<s32>(sheet_row_of(bob)) - static_cast<s32>(words) * 2);
	job.destination_modulo_bytes = mod16(static_cast<s32>(t.row_bytes) - static_cast<s32>(words) * 2);
	job.source_plane_stride_bytes = inter ? 0u : bob.height * sheet_row_of(bob);
	job.destination_plane_stride_bytes = inter ? 0u : t.plane_pointer_step();
	job.interleaved = inter;
	job.minterm = (bob.draw == BobDraw::Or) ? 0x00fcu
		    : (bob.draw == BobDraw::Opaque ? 0x00f0u
		    : (bob.draw == BobDraw::And ? 0x00c0u : 0x00cau));
	job.source_words_per_row = 0u;
	job.descending = false;
	if (masked) {
		job.mask = BlitPtr::from_storage(reinterpret_cast<const u16*>(bob.mask.data()));
	}
	return plan.commit_blit_job();
}

} // namespace eng::graphics
