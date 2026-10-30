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
/// también con la guarda por fila. La hoja **par** (`BobMaskPack::InterleavedPair`) tiene
/// su propio contrato: `[imagen k][guarda k][máscara k][guarda k]` por fila física con
/// `width = 2·W` (ver el enum y `pack_interleaved_pair`). `frame_stride` separa frames
/// (0 = denso).
///
/// Contrato del **borrado** (`bob_erase`/`bob_erase_box`) y del **save-under**: la caja
/// procesa `base + (shift != 0)` palabras — las MISMAS que el dibujo — porque con
/// desplazamiento fino el blit escribe la palabra extra. Borrar solo `base` deja hasta
/// 15 px por fila sin limpiar (residuo en el borde derecho del objeto). La hoja par ya
/// lleva la guarda dentro de su ventana: su caja procesa `width/16` palabras, igual que
/// su dibujo (`bob_detail::box_words`).
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
	///
	/// ⚠️ Con `x` **no alineada a 16** y una hoja de 2+ palabras por fila, el blit escribe
	/// la ventana completa: la **guarda** (ceros) se pinta como **banda** del color 0 a la
	/// derecha del objeto (invisible sobre fondo negro, visible sobre fondos con color —
	/// defecto real de la demo 302: el actor "mordía" los bloques estáticos al pasar). Para
	/// objetos que se mueven sobre fondos no lisos usar **`CookieCut`** con hoja par
	/// (`BobMaskPack::InterleavedPair`): el minterm `$CA` preserva el fondo fuera del arte.
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
	/// La hoja intercala por cada fila física `[imagen k][guarda k][máscara k][guarda k]`
	/// (k = W/16 palabras; los bloques de guarda son del tamaño del arte, a cero): cookie-cut
	/// con planos intercalados en **un solo** blit (minterm `$CA`). El `Bob` declara
	/// `width = 2·W` (ventana: la mitad de imagen incluye su bloque de guarda) y el blit
	/// procesa la mitad de imagen completa. `Bob::mask` se ignora (la máscara va en la
	/// propia hoja). Contrato de `pack_interleaved_pair` (`blit_job.hpp`); la guarda aporta
	/// al shifter los bits del borde derecho y su máscara nula no toca el destino (AHRM 6:
	/// máscaras «anded» al dato crudo antes del shift; HOST-453, los 16 subpíxeles exactos).
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
	// Ancho lógico = píxeles que cubre una fila de la reserva; es el límite horizontal con
	// el que se validan los blits (si el llamador conoce un ancho visible menor, lo fija él).
	t.width = static_cast<eng::u16>(static_cast<eng::u32>(row_bytes) * 8u);
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
	if (bob.mask_pack == BobMaskPack::InterleavedPair &&
	    bob.layout == BobLayout::Interleaved) {
		// Fila FÍSICA de la hoja par: `[imagen k][guarda k][máscara k][guarda k]`
		// (k = width/32; la ventana `width` = 2·k·16) → `width/4` bytes.
		return static_cast<u32>(base_words(bob)) * 4u;
	}
	return (static_cast<u32>(base_words(bob)) + 1u) * 2u;
}

/// Palabras que la caja de borrado/save-under debe cubrir para igualar la **huella del
/// dibujo** (`bob_draw`): el camino planar/interleaved procesa `base + (shift != 0)`; la
/// hoja par procesa su ventana completa (`width/16`; la guarda ya va dentro) sin palabra
/// extra. Compartido por `bob_erase_box`/`bob_save_box`/`bob_restore_box`.
constexpr u16 box_words(const Bob& bob, u16 w, s16 x) {
	const bool pair = bob.draw == BobDraw::CookieCut &&
			  bob.mask_pack == BobMaskPack::InterleavedPair &&
			  bob.layout == BobLayout::Interleaved;
	return static_cast<u16>((w + 15u) / 16u + ((!pair && (x & 15) != 0) ? 1u : 0u));
}

/// Huella **real** de un BOB en el destino: palabras que procesa el Blitter,
/// posicion alineada y caja vertical recortada. Es la unica fuente para colision,
/// limites y metadatos beam; no usar `width`/`height` del arte como huella.
struct BobFootprint {
	s16 left = 0;      ///< x alineada a 16 px del inicio de la ventana
	u16 words = 0;     ///< palabras por fila que escribe el Blitter (incluye guarda)
	u16 width_px = 0;  ///< `words * 16`; incluye la guarda cuando `shift != 0`
	u16 top = 0;       ///< borde superior visible (pantalla)
	u16 bottom = 0;    ///< borde inferior exclusivo visible (pantalla)
};

/// Calcula la huella real de un BOB con desplazamiento fino. `x`/`y` son la
/// posicion del ancla; `t.height` recorta la caja vertical. No valida si el BOB
/// o el destino son validos (los llamadores ya lo hacen); devuelve una huella
/// vacia si no hay geometria.
[[nodiscard]] constexpr BobFootprint bob_footprint(const Bob& bob, s16 x, s16 y,
						   const BobTarget& t) noexcept {
	BobFootprint fp {};
	if (bob.width == 0u || bob.height == 0u || t.row_bytes == 0u) return fp;
	fp.left = static_cast<s16>(x & ~15);
	fp.words = bob_detail::blit_words(bob, static_cast<u8>(x & 15));
	fp.width_px = static_cast<u16>(fp.words * 16u);
	const s32 top = y < 0 ? 0 : y;
	s32 bottom = top + bob.height;
	if (bottom > t.height) bottom = t.height;
	fp.top = top < t.height ? static_cast<u16>(top) : t.height;
	fp.bottom = bottom < t.height ? static_cast<u16>(bottom) : t.height;
	return fp;
}

/// Marca un job como BOB de bitmap y anota su caja vertical **de pantalla** para
/// que el ejecutor agrupe y decida si el haz ya pasó o si el blit llega antes de
/// `bob_top`. `y`/`h` son la caja del dibujo; `target_height` recorta lo que cae
/// fuera del destino. El grupo lo asigna el productor con `FramePlan::tag_bob_group`.
constexpr BlitJob& mark_bob(BlitJob& job, s16 y, u16 h, u16 target_height) noexcept {
	const s32 top = y < 0 ? 0 : y;
	s32 bottom = top + h;
	if (bottom > target_height) bottom = target_height;
	job.bob = true;
	job.bob_top = top < target_height ? static_cast<u16>(top) : target_height;
	job.bob_bottom = static_cast<u16>(bottom);
	return job;
}

/// Bytes por fila que separan dos filas consecutivas de la hoja: el valor declarado
/// si lo hay, o el del contrato compacto. Es lo que hay que usar para el modulo de
/// origen y el stride entre planos de la hoja.
constexpr u32 sheet_row_of(const Bob& bob) {
	return bob.sheet_row_bytes != 0u ? bob.sheet_row_bytes : sheet_row_bytes(bob);
}

/// ¿La geometría del destino es utilizable como destino de blits? Además de tener fila y
/// planos, la **reserva debe cubrir la geometría completa** (`storage_bytes_used`): si no,
/// el blit escribiría en el buffer contiguo (la «contaminación» clásica entre recursos
/// Chip). Los destinos **de banda** pueden declarar `height == 0` (la altura la fija el
/// emisor de la banda): en ese caso no se valida el tamaño vertical.
constexpr bool target_valid(const BobTarget& t) {
	return !t.planes.empty() && t.row_bytes != 0u && t.plane_count != 0u &&
	       (t.height == 0u || t.planes.size() >= t.storage_bytes_used());
}

/// Bytes que la hoja necesita **por frame**: todas las filas físicas de sus planos con la
/// guarda por fila (`sheet_row_of`). Es la unidad del contrato de `frame_count`/`frame_stride`.
constexpr u32 frame_bytes(const Bob& bob) {
	return eng::math::mulu32x16(eng::math::mulu32x16(sheet_row_of(bob), bob.height),
				    bob.planes);
}

/// Bytes que la **máscara separable** necesita (un plano, `height` filas con guarda).
constexpr u32 mask_bytes(const Bob& bob) {
	return eng::math::mulu32x16(sheet_row_of(bob), bob.height);
}

constexpr bool valid(const Bob& bob, const BobTarget& t) {
	if (bob.sheet.empty() || !target_valid(t) || bob.width == 0u || bob.height == 0u ||
	    bob.planes == 0u || bob.planes > 6u || bob.planes > t.plane_count) {
		return false;
	}
	// El Blitter lee la hoja (y la máscara) con la geometría declarada: la vista debe
	// cubrir al menos un frame y, con `frame_stride` explícito, el último frame usado.
	const u32 per_frame = frame_bytes(bob);
	if (bob.sheet.size() < per_frame) {
		return false;
	}
	if (bob.frame_count > 1u && bob.frame_stride != 0u &&
	    bob.sheet.size() < eng::math::mulu32x16(bob.frame_stride,
						     static_cast<eng::u16>(bob.frame_count - 1u)) +
					per_frame) {
		return false;
	}
	if (bob.draw == BobDraw::CookieCut && bob.mask_pack == BobMaskPack::SeparatePlane &&
	    !bob.mask.empty() && bob.mask.size() < mask_bytes(bob)) {
		return false;
	}
	return true;
}

/// `true` si la caja en `(x,y)` con `words` palabras por fila y `h` filas **cabe entera en
/// la reserva**. El Blitter no recorta: fuera de aquí escribe fuera del bitmap. En bandas
/// (`t.height == 0`) solo se puede validar el eje horizontal.
constexpr bool box_fits(const BobTarget& t, s16 x, s16 y, u16 h, u16 words) {
	if (x < 0 || y < 0 || t.row_bytes == 0u) {
		return false;
	}
	const u32 right = static_cast<u32>(x & ~15) + static_cast<u32>(words) * 16u;
	if (right > static_cast<u32>(t.row_bytes) * 8u) {
		return false;
	}
	return t.height == 0u || static_cast<u32>(y) + h <= t.height;
}

} // namespace bob_detail

using bob_detail::BobFootprint;
using bob_detail::bob_footprint;

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
	if (y < 0) {
		// Fuera por arriba: no se borra (el recorte parcial está pendiente, ver
		// `OBJECT_SYSTEM.md`); sin esta guarda el offset negativo del destino envuelve y
		// el blit escribe FUERA del bitmap (corrupción de Chip RAM).
		return true;
	}
	const bool inter = (t.layout == BobLayout::Interleaved);
	// La caja debe cubrir el objeto TAL COMO SE DIBUJA: con desplazamiento fino el blit
	// procesa una palabra de más (`base + shift`), así que borrar `base` dejaría el borde
	// derecho del objeto sin limpiar (residuo de hasta 15 px por fila). La hoja par ya
	// trae la guarda en la ventana: su huella es `width/16` (`box_words`).
	const u16 words = box_words(bob, w, x);
	if (!box_fits(t, x, y, h, words)) {
		return true; // caja fuera de la geometría del destino: no se emite el blit
	}
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
		mark_bob(job, y, h, t.height);
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
	mark_bob(job, y, h, t.height);
	return plan.commit_blit_job();
}

/// Dibuja **un solo plano** de un BOB planar sobre un destino interleaved. Permite
/// objetos de 1..N planos en una escena de profundidad mayor sin gastar un blit por
/// plano del fondo. `bob.planes` debe ser 1; la hoja es planar (una fila por altura).
inline bool bob_draw_plane(FramePlan& plan, const Bob& bob, u8 frame, s16 x, s16 y,
                           const BobTarget& t, u8 target_plane) {
    using namespace bob_detail;
    if (bob.planes != 1u || target_plane >= t.plane_count || !t.interleaved() ||
        !valid(bob, t) || frame >= bob.frame_count || y < 0) {
        return false;
    }
    const s16 wx = static_cast<s16>(x & ~15);
    if (wx < 0) return true;
    const u8 shift = static_cast<u8>(x & 15u);
    const u16 words = blit_words(bob, shift);
    if (!box_fits(t, x, y, bob.height, words)) return false;
    const u32 sheet_row = sheet_row_of(bob);
    const u32 frame_off = static_cast<u32>(frame) * bob.frame_stride;
    const u32 dest_row = static_cast<u32>(y) * t.row_bytes * t.plane_count +
                         static_cast<u32>(target_plane) * t.row_bytes;
    BlitJob& job = plan.begin_blit_job(BlitJobKind::OrBlob);
    job.source = BlitPtr::from_storage(reinterpret_cast<const u16*>(
        bob.sheet.address(frame_off).cptr()));
    job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
        t.data() + dest_row + (static_cast<u32>(wx) >> 3u)));
    job.words_per_row = words;
    job.height = bob.height;
    job.source_modulo_bytes = mod16(static_cast<s32>(sheet_row) - static_cast<s32>(words) * 2);
    // Un plano de un destino interleaved avanza `row_bytes * planes` por fila; usar
    // `row_bytes` escribir?a en el plano siguiente (rayas entre planos).
    job.destination_modulo_bytes = mod16(
        static_cast<s32>(t.row_bytes) * static_cast<s32>(t.plane_count) -
        static_cast<s32>(words) * 2);
    job.bitplane_count = 1u;
    job.source_plane_stride_bytes = sheet_row;
    job.destination_plane_stride_bytes = t.row_bytes * t.plane_count;
    job.source_words_per_row = 0u;
    job.source_shift = shift;
    job.descending = false;
    job.interleaved = false;
    job.minterm = 0x00fcu;
    mark_bob(job, y, bob.height, t.height);
    return plan.commit_blit_job();
}

/// Dibuja **un solo plano opaco** (`D = A`) de una hoja planar de 1 plano sobre un
/// destino interleaved. La guarda de la hoja y el padding del arte son color 0, el
/// mismo del fondo plano, de modo que la copia borra la posicion anterior. Un blit.
inline bool bob_draw_plane_opaque(FramePlan& plan, const Bob& bob, u8 frame, s16 x, s16 y,
                                  const BobTarget& t, u8 target_plane) {
    using namespace bob_detail;
    if (bob.planes != 1u || target_plane >= t.plane_count || !t.interleaved() ||
        !valid(bob, t) || frame >= bob.frame_count || y < 0) {
        return false;
    }
    const s16 wx = static_cast<s16>(x & ~15);
    if (wx < 0) return true;
    const u8 shift = static_cast<u8>(x & 15u);
    const u16 words = blit_words(bob, shift);
    if (!box_fits(t, x, y, bob.height, words)) return false;
    const u32 sheet_row = sheet_row_of(bob);
    const u32 frame_off = static_cast<u32>(frame) * bob.frame_stride;
    const u32 dest_row = static_cast<u32>(y) * t.row_bytes * t.plane_count +
                         static_cast<u32>(target_plane) * t.row_bytes;
    BlitJob& job = plan.begin_blit_job(BlitJobKind::OrBlob);
    job.source = BlitPtr::from_storage(reinterpret_cast<const u16*>(
        bob.sheet.address(frame_off).cptr()));
    job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
        t.data() + dest_row + (static_cast<u32>(wx) >> 3u)));
    job.words_per_row = words;
    job.height = bob.height;
    job.source_modulo_bytes = mod16(static_cast<s32>(sheet_row) - static_cast<s32>(words) * 2);
    job.destination_modulo_bytes = mod16(
        static_cast<s32>(t.row_bytes) * static_cast<s32>(t.plane_count) -
        static_cast<s32>(words) * 2);
    job.bitplane_count = 1u;
    job.source_plane_stride_bytes = sheet_row;
    job.destination_plane_stride_bytes = t.row_bytes * t.plane_count;
    job.source_words_per_row = 0u;
    job.source_shift = shift;
    job.descending = false;
    job.interleaved = false;
    job.minterm = 0x00f0u;
    mark_bob(job, y, bob.height, t.height);
    return plan.commit_blit_job();
}

/// Borra (`D = 0`) una caja de **un solo plano** de un destino interleaved.
/// Es el complemento de `bob_draw_plane_opaque` para limpiar la huella previa de un
/// BOB que comparte plano con otros objetos; no toca los demas planos.
inline bool bob_erase_plane_box(FramePlan& plan, const Bob& bob, u16 w, u16 h, s16 x,
				s16 y, const BobTarget& t, u8 plane) {
	using namespace bob_detail;
	if (plane >= t.plane_count || !t.interleaved() || !valid(bob, t) || w == 0u ||
	    h == 0u || y < 0) {
		return true;
	}
	const s16 wx = static_cast<s16>(x & ~15);
	if (wx < 0) return true;
	const u16 words = box_words(bob, w, x);
	if (!box_fits(t, x, y, h, words)) return true;
	const u32 off = static_cast<u32>(y) * t.row_bytes * t.plane_count +
		       static_cast<u32>(plane) * t.row_bytes +
		       (static_cast<u32>(wx) >> 3u);
	BlitJob& job = plan.begin_blit_job(BlitJobKind::ClearRect);
	job.destination = BlitPtr::from_storage(
		reinterpret_cast<u16*>(t.data() + off));
	job.words_per_row = words;
	job.height = h;
	job.source_words_per_row = 0u;
	job.source_modulo_bytes = 0;
	job.source_shift = 0u;
	job.descending = false;
	job.destination_modulo_bytes = mod16(
		static_cast<s32>(t.row_bytes) * static_cast<s32>(t.plane_count) -
		static_cast<s32>(words) * 2);
	job.bitplane_count = 1u;
	job.destination_plane_stride_bytes = 0u;
	job.interleaved = true;
	job.minterm = 0x00u; // D = 0
	mark_bob(job, y, h, t.height);
	return plan.commit_blit_job();
}

/// Restaura **un solo plano** desde un bitmap pristino interleaved a otro de la misma
/// geometria. Un blit de `height` filas; no toca los demas planos del fondo.
inline bool bob_copy_plane(FramePlan& plan, const BobTarget& source,
                           const BobTarget& destination, u16 w, u16 h, s16 x, s16 y,
                           u8 plane) {
    using namespace bob_detail;
    if (!target_valid(source) || !target_valid(destination) || w == 0u || h == 0u ||
        !source.interleaved() || !destination.interleaved() ||
        source.row_bytes != destination.row_bytes ||
        source.plane_count != destination.plane_count || plane >= source.plane_count ||
        y < 0 || (x & ~15) < 0) {
        return false;
    }
    const u16 words = static_cast<u16>((w + 15u) / 16u);
    if (!box_fits(destination, x, y, h, words)) return false;
    const u32 row_step = static_cast<u32>(source.row_bytes) * source.plane_count;
    const u32 off = static_cast<u32>(y) * row_step + static_cast<u32>(plane) * source.row_bytes +
                    (static_cast<u32>(x & ~15) >> 3u);
    BlitJob& job = plan.begin_blit_job(BlitJobKind::CopyRect);
    job.source = BlitPtr::from_storage(reinterpret_cast<const u16*>(source.data() + off));
    job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(destination.data() + off));
    job.words_per_row = words;
    job.height = h;
    job.bitplane_count = 1u;
    job.source_modulo_bytes = mod16(static_cast<s32>(row_step) - static_cast<s32>(words) * 2);
    job.destination_modulo_bytes = job.source_modulo_bytes;
    job.source_plane_stride_bytes = row_step;
    job.destination_plane_stride_bytes = row_step;
    job.source_words_per_row = 0u;
    job.source_shift = 0u;
    job.descending = false;
    job.interleaved = false;
    job.minterm = 0u;
    mark_bob(job, y, h, destination.height);
    return plan.commit_blit_job();
}

/// **Copia de una caja entre dos bitmaps interleaved full-depth** (restore de fondo
/// pristino o staging): un solo blit recorre las `h * planes` filas fisicas, con el
/// mismo modulo de fila en origen y destino. Los dos targets deben tener la misma
/// geometria (`row_bytes`, `plane_count`, `layout` interleaved).
inline bool bob_copy_box_interleaved(FramePlan& plan, const BobTarget& source,
                                     const BobTarget& destination, u16 w, u16 h,
                                     s16 x, s16 y) {
    using namespace bob_detail;
    if (!target_valid(source) || !target_valid(destination) || w == 0u || h == 0u ||
        !source.interleaved() || !destination.interleaved() ||
        source.row_bytes != destination.row_bytes ||
        source.plane_count != destination.plane_count || y < 0 || (x & ~15) < 0) {
        return false;
    }
    const u16 words = static_cast<u16>((w + 15u) / 16u);
    if (!box_fits(destination, x, y, h, words)) return false;
    const u32 row_step = static_cast<u32>(source.row_bytes) * source.plane_count;
    const u32 off = static_cast<u32>(y) * row_step + (static_cast<u32>(x & ~15) >> 3u);
    BlitJob& job = plan.begin_blit_job(BlitJobKind::CopyRect);
    job.source = BlitPtr::from_storage(
        reinterpret_cast<const u16*>(source.data() + off));
    job.destination = BlitPtr::from_storage(
        reinterpret_cast<u16*>(destination.data() + off));
    job.words_per_row = words;
    job.height = static_cast<u16>(h * source.plane_count);
    job.bitplane_count = 1u;
    job.source_modulo_bytes = mod16(static_cast<s32>(source.row_bytes) - static_cast<s32>(words) * 2);
    job.destination_modulo_bytes = job.source_modulo_bytes;
    job.source_plane_stride_bytes = 0u;
    job.destination_plane_stride_bytes = 0u;
    job.source_words_per_row = 0u;
    job.source_shift = 0u;
    job.descending = false;
    job.interleaved = true;
    job.minterm = 0u;
    mark_bob(job, y, h, destination.height);
    return plan.commit_blit_job();
}

/// **Save-under (guardar)**: copia la caja de `w x h` en `(x,y)` del destino (Chip) al buffer
/// `save` (vista tipada en Chip, tag `BobTag` como los bloques que lo reservan); `false` si el
/// buffer no da (rechazo controlado). El cálculo de palabras cubre la palabra extra del barrel
/// shifter (desplazamiento fino), como el borrado. Único sitio del algoritmo: lo usan el camino
/// de actor (`BackgroundPolicy::SaveUnder`, vía `scene::actor_detail::emit_save`) y el de
/// intención (`BobErase::RestoreUnder`). El destino puede ser **planar** o **interleaved** (por
/// plano, con el paso de fila por plano).
inline bool bob_save_box(FramePlan& plan, const Bob& bob, u16 w, u16 h, s16 x, s16 y,
			 const BobTarget& t, eng::ChipView<eng::BobTag> save, u16 save_words_per_row,
			 u16 save_height) {
	const u16 words = bob_detail::box_words(bob, w, x);
	if (save.empty() || words > save_words_per_row || h > save_height || y < 0 ||
	    (x & ~15) < 0 || !bob_detail::box_fits(t, x, y, h, words)) {
		return false; // sin buffer, buffer insuficiente o caja fuera de la reserva destino
	}
	const u32 save_row_bytes = static_cast<u32>(save_words_per_row) * 2u;
	// Paso de fila DENTRO de un plano del destino: `row_bytes` (planar) o
	// `row_bytes × planos` (interleaved: cada fila física contiene los N planos).
	const u32 dest_row_step = t.interleaved()
		? static_cast<u32>(t.row_bytes) * t.plane_count
		: t.row_bytes;
	BlitJob& job = plan.begin_blit_job(BlitJobKind::CopyRect);
	job.destination = BlitPtr {save};
	job.source = BlitPtr::from_storage(reinterpret_cast<const u16*>(
		t.data() + static_cast<u32>(y) * (t.interleaved() ? dest_row_step : t.row_bytes) +
		(static_cast<u32>(x & ~15) >> 3u)));
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = bob.planes;
	job.source_modulo_bytes = mod16(static_cast<s32>(dest_row_step) - static_cast<s32>(words) * 2);
	job.destination_modulo_bytes = mod16(static_cast<s32>(save_row_bytes) - static_cast<s32>(words) * 2);
	job.source_plane_stride_bytes = t.plane_pointer_step();
	job.destination_plane_stride_bytes = save_row_bytes * save_height;
	job.source_words_per_row = 0u;
	job.descending = false;
	job.source_shift = 0u;
	job.interleaved = false;
	bob_detail::mark_bob(job, y, h, t.height);
	return plan.commit_blit_job();
}

/// **Save-under (restaurar)**: devuelve el buffer `save` a la caja de `w x h` en `(x,y)` del
/// destino. Inverso de `bob_save_box`; mismo cálculo de palabras y misma guarda de capacidad.
inline bool bob_restore_box(FramePlan& plan, const Bob& bob, u16 w, u16 h, s16 x, s16 y,
			    const BobTarget& t, eng::ChipView<eng::BobTag> save, u16 save_words_per_row,
			    u16 save_height) {
	const u16 words = bob_detail::box_words(bob, w, x);
	if (save.empty() || words > save_words_per_row || h > save_height || y < 0 ||
	    (x & ~15) < 0 || !bob_detail::box_fits(t, x, y, h, words)) {
		return false; // sin buffer, buffer insuficiente o caja fuera de la reserva destino
	}
	const u32 save_row_bytes = static_cast<u32>(save_words_per_row) * 2u;
	// Paso de fila DENTRO de un plano del destino (ver `bob_save_box`).
	const u32 dest_row_step = t.interleaved()
		? static_cast<u32>(t.row_bytes) * t.plane_count
		: t.row_bytes;
	BlitJob& job = plan.begin_blit_job(BlitJobKind::RestoreRect);
	job.source = BlitPtr {save};
	job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
		t.data() + static_cast<u32>(y) * (t.interleaved() ? dest_row_step : t.row_bytes) +
		(static_cast<u32>(x & ~15) >> 3u)));
	job.words_per_row = words;
	job.height = h;
	job.bitplane_count = bob.planes;
	job.source_modulo_bytes = mod16(static_cast<s32>(save_row_bytes) - static_cast<s32>(words) * 2);
	job.destination_modulo_bytes = mod16(static_cast<s32>(dest_row_step) - static_cast<s32>(words) * 2);
	job.source_plane_stride_bytes = save_row_bytes * save_height;
	job.destination_plane_stride_bytes = t.plane_pointer_step();
	job.source_words_per_row = 0u;
	job.descending = false;
	job.source_shift = 0u;
	job.interleaved = false;
	bob_detail::mark_bob(job, y, h, t.height);
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
/// por cada fila física, `[imagen k][guarda k][máscara k][guarda k]` (`k = W/16`, guardas a
/// cero del tamaño del arte): es la geometría de `pack_interleaved_pair` (`blit_job.hpp`)
/// con `bob.width = 2·W` (ventana). El bit de máscara de cada plano se materializa en el
/// propio blit, sin copia expandida.
/// El encoder conecta `A` = máscara (segunda mitad) y `B` = imagen (primera mitad) con
/// `ASH = BSH = x & 15`, como el original de Bartman.
/// `first_row`/`rows` limitan el RANGO de filas del arte (fila inicial y cuántas);
/// `bob_draw` pasa `(0, height)` y el reparto en la costura del anillo
/// (`bob_ring.hpp`) `(0, seg)` + `(seg, height − seg)`.
inline bool bob_draw_interleaved_pair(FramePlan& plan, const Bob& bob, u8 frame, s16 x, s16 y,
				      u16 first_row, u16 rows, const BobTarget& t) {
	using namespace bob_detail;
	// Precondición: `bob_draw` (único llamador) ya validó `bob`/`t` y `frame < frame_count`.
	// `bob.width` es la **ventana** del arte en píxeles e **incluye la palabra de guarda**
	// (arte + 16; la hoja pack la trae tras cada mitad: `[arte n][guarda][máscara n][guarda]`,
	// `pack_interleaved_pair`). Se procesan TODAS las palabras de la ventana: con el subpíxel
	// (`shift > 0`) el shifter `(previa << 16 | actual) >> shift` necesita la palabra
	// siguiente por fila para que no se recorten las `shift` columnas derechas del arte; la
	// guarda (cero) aporta esos bits y su máscara nula no altera el destino. `shift = 0`
	// procesa también la guarda, que resulta un no-op (reescribe el destino con su valor).
	// La máscara de cookie-cut vive en `src + words` (mitad derecha del par).
	const u16 words = static_cast<u16>(bob.width / 16u);
	if (words == 0u) {
		return false;
	}
	// Fuera por la izquierda: no se dibuja (mismo contrato que el camino normal; el recorte
	// parcial izquierdo no lo cubre el barrel del Blitter).
	const s16 wx = static_cast<s16>(x & ~15);
	if (wx < 0) {
		return true;
	}
	if (!box_fits(t, x, y, rows, words)) {
		return false; // no cabe en el destino: fuera de la reserva del bitmap
	}
	const u8 shift = static_cast<u8>(x & 15);
	const s16 x_start = wx;
	const u32 start_row = static_cast<u32>(t.row_bytes) * t.plane_count;
	const u32 frame_offset = static_cast<u32>(frame) * bob.frame_stride;
	// Rango de filas del arte: la fila `first_row` arranca en la fila FÍSICA
	// `first_row · planos` del pack (una fila física por plano del destino, paso
	// `sheet_row_of` = 4·k bytes con la guarda).
	const u32 first_off = static_cast<u32>(first_row) * static_cast<u32>(bob.planes) *
			      sheet_row_of(bob);
	const u16* src = reinterpret_cast<const u16*>(
		bob.sheet.address(frame_offset + first_off).cptr());
	// **Construccion IN SITU** (coste cero): se rellena la ranura del plan directamente, sin
	// `BlitJob` local (que en -O0 es un `memset`) ni copia (otro `memcpy`). Ver `ZERO_COST_FRAME_PATH.md`.
	BlitJob& job = plan.begin_blit_job(BlitJobKind::MaskedBobCookieCut);
	job.source = BlitPtr::from_storage(src);               // primera mitad: imagen
	job.mask = BlitPtr::from_storage(src + words);         // segunda mitad: máscara expandida
	job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
		t.data() + static_cast<u32>(y) * start_row + (static_cast<u32>(x_start) >> 3u)));
	job.words_per_row = words;
	job.height = static_cast<u16>(rows * bob.planes);
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
	mark_bob(job, static_cast<s16>(y + first_row), rows, t.height);
	return plan.commit_blit_job();
}

/// **Núcleo del dibujo por RANGO de filas del arte** — única fuente de todos los caminos:
/// `bob_draw` = `(0, bob.height)`; el reparto en la **costura del anillo** =
/// `(0, seg)` + `(seg, height − seg)` con la segunda parte en la línea 0 del anillo
/// (`eng/graphics/bob_ring.hpp`). `rows` limita las filas que recorre el blit; los **pasos**
/// (stride de plano de la hoja, módulos, stride de plano del destino) se calculan siempre
/// con la altura COMPLETA del objeto — por eso el rango viaja como parámetro y no como
/// `bob.height` mutada (mutarla rompería `source_plane_stride_bytes` en los layouts por
/// plano). Precondiciones, validadas por los llamadores: `valid(bob, t)`,
/// `frame < frame_count`, `y >= 0`.
__attribute__((always_inline)) inline bool draw_rows(FramePlan& plan, const Bob& bob, u8 frame,
						     s16 x, s16 y, u16 first_row, u16 rows,
						     const BobTarget& t) {
	using namespace bob_detail;
	if (bob.draw == BobDraw::CookieCut && bob.layout == BobLayout::Interleaved) {
		if (bob.mask_pack == BobMaskPack::InterleavedPair) {
			return bob_draw_interleaved_pair(plan, bob, frame, x, y, first_row, rows, t);
		}
		// Con máscara en un plano suelto, el cookie-cut intercalado exigiría una máscara
		// "expandida" (una copia por plano) y modulos A/B distintos: no lo cubre el
		// ejecutor actual (documentado). Usar `BobMaskPack::InterleavedPair` para el
		// camino de un solo blit.
		return false;
	}
	// **Fuera por la izquierda**: no se dibuja. El recorte parcial exigiría un
	// desplazamiento a la IZQUIERDA de la fuente, que el barrel del Blitter no puede hacer
	// (ASH solo desplaza a la derecha); mismo contrato que `y < 0`. Un bob a caballo del
	// borde izquierdo aparece entero al alcanzar x >= 0 (el llamador decide si lo mantiene
	// fuera con la lógica de juego). Antes se dibujaba en `x&15 + 16` (posición errónea).
	const s16 wx = static_cast<s16>(x & ~15);
	if (wx < 0) {
		return true;
	}
	const u8 shift = static_cast<u8>(x & 15);
	const bool inter = (t.layout == BobLayout::Interleaved);
	const bool masked = bob.draw == BobDraw::CookieCut;
	const bool separate_mask = masked && bob.mask_pack == BobMaskPack::SeparatePlane;
	// Hoja planar + destino interleaved: la IMAGEN sola cabe en UN blit (el orden físico de
	// filas coincide, con módulo `sheet_row − words*2`). Con máscara SEPARADA **no**: la
	// máscara tiene `height` filas, no `height*planos`, y el walk de `height*planos` filas se
	// saldría de su plano (leería Chip ajena como máscara — DT-012: planos 1+ con máscara
	// basura). Ese caso va **por plano** (misma máscara en cada uno: su walk usa `src_mod`,
	// su stride real) con el paso de fila FÍSICA del destino interleaved.
	const bool flat = inter && !separate_mask;
	const u16 words = blit_words(bob, shift);
	const s16 x_start = wx;
	const u32 start_row = inter ? static_cast<u32>(t.row_bytes) * t.plane_count : t.row_bytes;
	if (masked && bob.mask.empty()) {
		return false; // cookie-cut sin mascara
	}
	if (!box_fits(t, x, y, rows, words)) {
		return false; // no cabe en el destino: fuera de la reserva del bitmap
	}
	// Rango de filas del arte: en `flat` (hoja intercalada por los planos del destino)
	// cada fila de arte ocupa `planos` filas físicas; en la hoja apilada por plano, una.
	// `sheet_row_of` es el paso físico entre filas de la hoja (guarda incluida).
	const u32 row_adv = sheet_row_of(bob);
	const u32 first_off =
		flat ? static_cast<u32>(first_row) * static_cast<u32>(bob.planes) * row_adv
		     : static_cast<u32>(first_row) * row_adv;
	// **Construcción IN SITU**: ranura del plan, sin `BlitJob` local ni copia.
	BlitJob& job = plan.begin_blit_job(masked ? BlitJobKind::MaskedBobCookieCut
						  : BlitJobKind::OrBlob);
	job.source = BlitPtr::from_storage(reinterpret_cast<const u16*>(
		bob.sheet.address(static_cast<u32>(frame) * bob.frame_stride + first_off).cptr()));
	job.destination = BlitPtr::from_storage(reinterpret_cast<u16*>(
		t.data() + static_cast<u32>(y) * start_row + (static_cast<u32>(x_start) >> 3u)));
	job.words_per_row = words;
	job.height = flat ? static_cast<u16>(rows * bob.planes) : rows;
	job.source_shift = shift;
	job.bitplane_count = flat ? 1u : bob.planes;
	job.source_modulo_bytes = mod16(static_cast<s32>(sheet_row_of(bob)) - static_cast<s32>(words) * 2);
	job.destination_modulo_bytes = mod16(
		static_cast<s32>(flat ? t.row_bytes : start_row) - static_cast<s32>(words) * 2);
	// El stride entre planos de la hoja usa la altura COMPLETA (los planos distan
	// `height · fila` aunque se dibuje un rango de filas).
	job.source_plane_stride_bytes = flat ? 0u : bob.height * row_adv;
	job.destination_plane_stride_bytes = flat ? 0u : t.plane_pointer_step();
	job.interleaved = inter;
	job.minterm = (bob.draw == BobDraw::Or) ? 0x00fcu
		    : (bob.draw == BobDraw::Opaque ? 0x00f0u
		    : (bob.draw == BobDraw::And ? 0x00c0u : 0x00cau));
	job.source_words_per_row = 0u;
	job.descending = false;
	if (masked) {
		job.mask = BlitPtr::from_storage(reinterpret_cast<const u16*>(bob.mask.data()));
	}
	mark_bob(job, static_cast<s16>(y + first_row), rows, t.height);
	return plan.commit_blit_job();
}

/// Dibuja el `frame` del objeto COMPLETO en `(x,y)`.
///
/// Camino caliente (un BOB por objeto y frame): `always_inline` para que el contrato
/// de la hoja y la aritmetica de modulos queden dentro del bucle del llamador, sin
/// `jsr` por objeto. Medido en la 117 (bobs3d): `draw` ~2.660 ciclos/BOB sin inline.
/// Para bitmaps **cíclicos** (costura del anillo) usar `bob_draw_ring`
/// (`bob_ring.hpp`): parte el rect en la costura sin tocar este camino (coste cero
/// cuando no hay cruce más allá de la comparación del wrapper).
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
	if (y < 0) {
		// Objeto fuera por arriba: no se dibuja (recorte parcial pendiente). Sin esta
		// guarda el offset negativo del destino envuelve y el blit escribe FUERA del
		// bitmap, corrompiendo Chip RAM (caso visto en la demo 064: enemigos a y=-24).
		return true;
	}
	return draw_rows(plan, bob, frame, x, y, 0u, bob.height, t);
}

} // namespace eng::graphics
