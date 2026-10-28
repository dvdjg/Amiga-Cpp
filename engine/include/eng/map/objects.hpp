#pragma once

/// \file objects.hpp
/// **Vista de los objetos de runtime de un mapa Tiled** (`eng::map`): registros
/// `foreground`/`occluder` ya ordenados por clave de profundidad y el catálogo de
/// imágenes planares que dibuja el Blitter.
///
/// ```text
/// objects.h generado por tools/tiled/cook-objects.mjs
///   kMapObjects[]      ──► MapObjectsView::object(i)  (foot_y / sort_key)
///   kMapObjectImages[] ──► MapObjectsView::image(i)   (offset en object_sheet.bin)
///   object_sheet.bin   ──► arte planar con guarda por fila (shift fino)
///   object_masks.bin   ──► máscara 1bpp compartida por todos los planos
/// ```
///
/// El runtime **no** parsea Tiled ni ordena por `y` de pantalla: la tabla llega
/// ordenada (primero `foreground`, luego `occluder`) y el juego inserta a sus
/// actores con `upper_bound_foreground(foot_y)`. Un `occluder` se dibuja después
/// de todos los actores (copa, alero, arco).
///
/// El arte es el nativo del objeto (escala 1:1); el cooker rechaza objetos
/// escalados o rotados. La fila de cada plano mide `row_words` palabras e incluye
/// una palabra de guarda al final para que el Blitter pueda desplazar hasta 15 px
/// sin arrastrar bits de la fila siguiente.
///
/// No reserva memoria y no usa heap; los arrays generados deben sobrevivir a la
/// vista (`.rodata`/Chip, según el asset).

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/sprite_asset.hpp>

namespace eng::map {

/// Rol de un objeto de runtime.
enum class MapObjectKind : u8 {
	Foreground = 0, ///< participa en el orden por profundidad con los actores
	Occluder = 1,   ///< se dibuja después de todos los actores (copa/alero)
};

/// Clave de profundidad declarada por el artista.
enum class MapObjectSort : u8 {
	Base = 0, ///< clave = `foot_y` (pie del objeto; el caso normal)
	Top = 1,  ///< clave = borde superior (`y`), para toldos/arcos elevados
};

/// Un objeto de runtime. `x/y` son el rectángulo del arte en píxeles de mundo
/// (`y` es el borde superior; el pie es `foot_y = y + h`). `image` indexa
/// `MapObjectsView::image()`. `id` es el id estable de Tiled.
struct MapObjectView {
	s16 x = 0;          ///< borde izquierdo en píxeles de mundo.
	s16 y = 0;          ///< borde superior en píxeles de mundo.
	s16 foot_y = 0;     ///< pie real del objeto (`y + h`); base del orden por Y.
	u16 w = 0u;         ///< ancho nativo del arte en píxeles.
	u16 h = 0u;         ///< alto nativo del arte en píxeles.
	u16 id = 0u;        ///< id estable del objeto en Tiled.
	u16 image = 0u;     ///< índice en el catálogo de imágenes.
	MapObjectKind kind = MapObjectKind::Foreground; ///< primer plano u occluder.
	MapObjectSort sort = MapObjectSort::Base;       ///< clave de profundidad.

	/// Clave usada para ordenar contra los actores.
	/// \returns `foot_y` si `sort == Base`; `y` si `sort == Top`.
	[[nodiscard]] constexpr s16 sort_key() const noexcept {
		return sort == MapObjectSort::Top ? y : foot_y;
	}
};

static_assert(sizeof(MapObjectView) == 16u, "MapObjectView empaquetado para el bundle");

/// Imagen planar de un objeto cocinado. Los offsets están en **palabras** dentro
/// de `object_sheet.bin` / `object_masks.bin`. Un plano ocupa
/// `row_words * height` palabras; el plano `p` empieza en
/// `sheet_word_offset + p * row_words * height`. La última palabra de cada fila
/// es la guarda a cero para el shift fino.
struct MapObjectImageView {
	u32 sheet_word_offset = 0u; ///< inicio de la imagen (plano 0) en el sheet.
	u32 mask_word_offset = 0u;  ///< inicio de la máscara 1bpp.
	u16 width = 0u;             ///< ancho del arte, múltiplo de 16 (padding transparente).
	u16 height = 0u;            ///< alto del arte en filas.
	u16 row_words = 0u;         ///< palabras por fila por plano, incluida la guarda.
	u16 reserved = 0u;          ///< reservado para evolución del formato.
};

static_assert(sizeof(MapObjectImageView) == 16u, "MapObjectImageView empaquetado");

/// Animación de un objeto de mapa con arte planar. Los frames están contiguos
/// en `object_sheet.bin`/`object_masks.bin`, separados por
/// `frame_stride_words` palabras (imagen) y `row_words*height` (máscara). El
/// objeto puede estar en cualquier píxel de mundo: lo coloca `MapSprite`/
/// `MapSpriteCache`, no una rejilla de tiles.
inline constexpr u8 kMapObjectMaxFrames = 8u; ///< máximo de frames por objeto.

struct MapObjectAnimationView {
	u16 frame_stride_words = 0u; ///< palabras entre frames consecutivos del sheet.
	u8 frame_count = 0u;         ///< frames válidos (1 = estático).
	u8 reserved = 0u;            ///< reservado.
	u8 durations[kMapObjectMaxFrames] {}; ///< ticks de juego por frame (50 Hz).
};
static_assert(sizeof(MapObjectAnimationView) == 12u, "MapObjectAnimationView empaquetado");

/// Frame visible de una animación para el tick de juego `tick` (0..).
/// Si `frame_count <= 1` o no hay duraciones válidas, devuelve 0.
[[nodiscard]] constexpr u8 object_frame_at_tick(const MapObjectAnimationView& anim,
						u16 tick) noexcept {
	if (anim.frame_count <= 1u) return 0u;
	u32 cycle = 0u;
	for (u8 i = 0u; i < anim.frame_count && i < kMapObjectMaxFrames; ++i) {
		cycle += anim.durations[i];
	}
	if (cycle == 0u) return 0u;
	u32 t = tick % cycle;
	for (u8 i = 0u; i < anim.frame_count && i < kMapObjectMaxFrames; ++i) {
		if (t < anim.durations[i]) return i;
		t -= anim.durations[i];
	}
	return static_cast<u8>(anim.frame_count - 1u);
}

/// Construye el `Sprite` que dibuja un objeto de mapa con `MapSprite`/
/// `MapSpriteCache`. El objeto puede estar en cualquier píxel de mundo; el
/// `Bob` usa el layout planar con guarda del cooker y el frame actual se elige
/// con `object_frame_at_tick`.
/// \param image   imagen del objeto (offset y geometría en el sheet).
/// \param anim    animación del objeto (frame_count, stride y duraciones).
/// \param planes  planos del sheet (1..6).
/// \param sheet   banco Chip de imágenes (p. ej. `object_sheet.bin`).
/// \param mask    banco Chip de máscaras (p. ej. `object_masks.bin`).
/// \return sprite no propietario; vacío si la geometría no es válida.
[[nodiscard]] inline graphics::Sprite map_object_sprite(
	const MapObjectImageView& image, const MapObjectAnimationView& anim,
	u8 planes, ChipView<BobTag> sheet, ChipView<BobTag> mask) noexcept {
	if (planes == 0u || image.width == 0u || image.height == 0u ||
	    image.row_words == 0u || anim.frame_count == 0u ||
	    anim.frame_stride_words == 0u) {
		return {};
	}
	const u32 frame_bytes = static_cast<u32>(anim.frame_stride_words) * 2u;
	const u32 mask_frame_bytes = static_cast<u32>(image.row_words) * 2u * image.height;
	const u32 sheet_bytes = frame_bytes * anim.frame_count;
	const u32 mask_bytes = mask_frame_bytes * anim.frame_count;
	if (image.sheet_word_offset * 2u + sheet_bytes > sheet.size() ||
	    image.mask_word_offset * 2u + mask_bytes > mask.size()) {
		return {};
	}
	graphics::Bob bob {};
	bob.sheet = sheet.subview(image.sheet_word_offset * 2u, sheet_bytes);
	bob.mask = mask.subview(image.mask_word_offset * 2u, mask_bytes);
	bob.width = image.width;
	bob.height = image.height;
	bob.planes = planes;
	bob.frame_count = anim.frame_count;
	bob.frame_stride = frame_bytes;
	bob.layout = graphics::BobLayout::Planar;
	bob.draw = graphics::BobDraw::CookieCut;
	bob.mask_pack = graphics::BobMaskPack::SeparatePlane;
	return graphics::Sprite {bob, sheet_bytes, mask_bytes};
}

/// Vista validada de la tabla de objetos y su catálogo. Los `objects` deben
/// llegar **ordenados**: primero los `Foreground` por `sort_key` ascendente y,
/// a igualdad, por `id`; después los `Occluder` con la misma regla.
class MapObjectsView {
public:
	constexpr MapObjectsView() noexcept = default;
	/// \param objects          registros generados (`kMapObjects`).
	/// \param images           catálogo generado (`kMapObjectImages`).
	/// \param foreground_count objetos `Foreground` al inicio de `objects`.
	/// \param animations       tabla opcional `kMapObjectAnimations` (paralela a objects).
	constexpr MapObjectsView(Span<const MapObjectView> objects,
				 Span<const MapObjectImageView> images,
				 u16 foreground_count,
				 Span<const MapObjectAnimationView> animations = {}) noexcept
		: m_objects(objects), m_images(images), m_animations(animations),
		  m_foreground(foreground_count) {}

	/// `true` si los spans son coherentes (el conteo de foreground cabe).
	[[nodiscard]] constexpr bool valid() const noexcept {
		return m_foreground <= m_objects.size() &&
		       (m_animations.empty() || m_animations.size() == m_objects.size());
	}
	[[nodiscard]] constexpr u16 count() const noexcept {
		return static_cast<u16>(m_objects.size());
	}
	[[nodiscard]] constexpr u16 foreground_count() const noexcept { return m_foreground; }
	[[nodiscard]] constexpr u16 occluder_count() const noexcept {
		return static_cast<u16>(m_objects.size() - m_foreground);
	}
	[[nodiscard]] constexpr u16 image_count() const noexcept {
		return static_cast<u16>(m_images.size());
	}
	/// Objeto `index` (`0 < count()`); sin comprobación de rango.
	[[nodiscard]] constexpr const MapObjectView& object(u16 index) const noexcept {
		return m_objects[index];
	}
	/// Imagen `index` (`0 < image_count()`); sin comprobación de rango.
	[[nodiscard]] constexpr const MapObjectImageView& image(u16 index) const noexcept {
		return m_images[index];
	}
	/// Imagen de `object`.
	[[nodiscard]] constexpr const MapObjectImageView& image_of(const MapObjectView& object) const noexcept {
		return m_images[object.image];
	}
	/// ¿El objeto `index` tiene animación con más de un frame?
	[[nodiscard]] constexpr bool animated(u16 index) const noexcept {
		return index < m_animations.size() && m_animations[index].frame_count > 1u;
	}
	/// Animación de `index`; requiere `animated(index)`.
	[[nodiscard]] constexpr const MapObjectAnimationView& animation(u16 index) const noexcept {
		return m_animations[index];
	}
	/// Número de entradas de la tabla de animación (0 = sin tabla).
	[[nodiscard]] constexpr u16 animation_count() const noexcept {
		return static_cast<u16>(m_animations.size());
	}

	/// Primer índice de `foreground` con `sort_key() > key`.
	///
	/// El juego dibuja `[0, split)` antes de un actor y `[split, foreground_count)`
	/// después; a igualdad de clave el actor queda delante del prop, que es la
	/// lectura habitual de un sprite que se apoya en el mismo pie.
	/// \param key `foot_y` (o clave equivalente) del actor.
	/// \returns índice en `[0, foreground_count]`.
	[[nodiscard]] constexpr u16 upper_bound_foreground(s16 key) const noexcept {
		u16 lo = 0u;
		u16 hi = m_foreground;
		while (lo < hi) {
			const u16 mid = static_cast<u16>(lo + (hi - lo) / 2u);
			if (m_objects[mid].sort_key() <= key) lo = static_cast<u16>(mid + 1u);
			else hi = mid;
		}
		return lo;
	}

private:
	Span<const MapObjectView> m_objects {};
	Span<const MapObjectImageView> m_images {};
	Span<const MapObjectAnimationView> m_animations {};
	u16 m_foreground = 0u;
};

} // namespace eng::map
