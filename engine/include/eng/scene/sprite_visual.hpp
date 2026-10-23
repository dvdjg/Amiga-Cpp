#pragma once

/// \file sprite_visual.hpp
/// **Contenido y presupuesto de la fachada de sprites por intención** (`SpriteDirector`).
///
/// Reúne lo que el juego declara y lo que el engine reporta:
///
///   - `VisualStore`/`CompositeStore`/`SceneAssets`: contenido retenido con handles
///     generacionales (`VisualId`, `CompositeId`). El contenido son vistas (`Visual`,
///     `CompositeVisual`) que el llamador mantiene vivas; el store no copia píxeles.
///   - `SpriteVisual`/`SpriteVisualRef`: una referencia por valor a contenido simple o
///     compuesto, para que el director tenga un solo camino público.
///   - `SpriteHint`: la intención del juego (política, attached, reuso, gap, canal,
///     rango, grupo y tira horizontal).
///   - `SpriteBudget`/`SpriteLoadReport`: el contrato único de contabilidad (runtime y
///     preflight estático). Los campos de hardware se calculan con el `SpriteAllocator`
///     real; el contador grueso de `RepresentationAllocator` no se usa en esta fachada.
///   - `BandPalette`/`CopperCue`: paletas por banda con `top` móvil (`scroll_y`).
///   - `SpriteStaticDesc`/`evaluate_static`: escenas estáticas evaluables en compilación
///     (`constexpr`/`static_assert`), con el mismo gap de 1 línea que el runtime.
///
/// No toca hardware ni registros: las vistas de contenido y los buffers DMA los aporta
/// el llamador, y la composición se puede probar en host.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/pool.hpp>
#include <eng/graphics/animation.hpp>
#include <eng/graphics/composite_visual.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_limits.hpp>
#include <eng/graphics/sprite.hpp>

namespace eng::scene {

/// Identificador estable de contenido simple en un `VisualStore`.
struct VisualId {
	eng::u16 index = 0xffffu;
	eng::u16 generation = 0u;
	[[nodiscard]] constexpr bool valid() const noexcept { return index != 0xffffu; }
	[[nodiscard]] constexpr bool operator==(const VisualId& o) const noexcept {
		return index == o.index && generation == o.generation;
	}
	[[nodiscard]] constexpr bool operator!=(const VisualId& o) const noexcept {
		return !(*this == o);
	}
};

/// Identificador estable de contenido compuesto en un `CompositeStore`.
struct CompositeId {
	eng::u16 index = 0xffffu;
	eng::u16 generation = 0u;
	[[nodiscard]] constexpr bool valid() const noexcept { return index != 0xffffu; }
	[[nodiscard]] constexpr bool operator==(const CompositeId& o) const noexcept {
		return index == o.index && generation == o.generation;
	}
	[[nodiscard]] constexpr bool operator!=(const CompositeId& o) const noexcept {
		return !(*this == o);
	}
};

/// Almacén generacional de `Visual` (sin copiar los píxeles: solo la vista descriptora).
/// \tparam Max Capacidad de visuales vivos (1..65534); fija el array del `Pool` interno.
///         Un `add` con el store lleno devuelve un id inválido.
template <eng::u16 Max>
class VisualStore {
	using PoolType = eng::util::Pool<eng::graphics::Visual, Max>;

public:
	/// Capacidad máxima de visuales (la `Max` de la plantilla).
	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return Max; }
	/// Número de visuales vivos ahora mismo (0..`capacity()`).
	[[nodiscard]] constexpr eng::u16 count() const noexcept {
		return static_cast<eng::u16>(m_pool.size());
	}
	/// `true` si no hay visuales vivos.
	[[nodiscard]] constexpr bool empty() const noexcept { return m_pool.empty(); }

	/// Registra una vista descriptora y devuelve su handle generacional.
	/// \param v `Visual` a copiar (spans incluidos); la memoria de píxeles no se copia.
	/// \return id válido, o inválido si el store está lleno.
	[[nodiscard]] VisualId add(const eng::graphics::Visual& v) noexcept {
		const auto h = m_pool.add();
		if (!h.valid()) {
			return {};
		}
		*m_pool.get(h) = v;
		return VisualId {h.index, h.generation};
	}

	/// Libera un visual; un handle con la generación anterior deja de ser válido.
	/// \param id handle devuelto por `add`.
	/// \return `true` si el id era válido y se liberó.
	[[nodiscard]] constexpr bool remove(VisualId id) noexcept {
		return m_pool.remove({id.index, id.generation});
	}

	/// Acceso de solo lectura a un visual vivo.
	/// \param id handle a consultar.
	/// \return vista no propietaria, o vacía si el id no es válido.
	[[nodiscard]] constexpr eng::Ref<const eng::graphics::Visual> get(
		VisualId id) const noexcept {
		return m_pool.get({id.index, id.generation});
	}

	/// Acceso mutable a un visual vivo (para ajustar su vista descriptora).
	/// \param id handle a consultar.
	/// \return vista no propietaria, o vacía si el id no es válido.
	[[nodiscard]] constexpr eng::Ref<eng::graphics::Visual> get(VisualId id) noexcept {
		return m_pool.get({id.index, id.generation});
	}

	/// ¿El handle (índice + generación) sigue siendo válido?
	/// \param id handle a comprobar.
	/// \return `true` si el slot está vivo y la generación coincide.
	[[nodiscard]] constexpr bool valid(VisualId id) const noexcept {
		return m_pool.valid({id.index, id.generation});
	}

	/// Vacía el store y sube la generación de todos los slots (invalida handles previos).
	constexpr void reset() noexcept { m_pool.reset(); }

private:
	PoolType m_pool {}; ///< pool fijo de descriptores; no copia píxeles
};

/// Almacén generacional de `CompositeVisual` (las partes referencian sus propios `Visual`).
/// \tparam Max Capacidad de compuestos vivos (1..65534); fija el array del `Pool` interno.
///         Un `add` con el store lleno devuelve un id inválido.
template <eng::u16 Max>
class CompositeStore {
	using PoolType = eng::util::Pool<eng::graphics::CompositeVisual, Max>;

public:
	/// Capacidad máxima de compuestos (la `Max` de la plantilla).
	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return Max; }
	/// Número de compuestos vivos ahora mismo (0..`capacity()`).
	[[nodiscard]] constexpr eng::u16 count() const noexcept {
		return static_cast<eng::u16>(m_pool.size());
	}
	/// `true` si no hay compuestos vivos.
	[[nodiscard]] constexpr bool empty() const noexcept { return m_pool.empty(); }

	/// Registra una vista compuesta y devuelve su handle generacional.
	/// \param v `CompositeVisual` a copiar (spans de partes/secuencias incluidos).
	/// \return id válido, o inválido si el store está lleno.
	[[nodiscard]] CompositeId add(const eng::graphics::CompositeVisual& v) noexcept {
		const auto h = m_pool.add();
		if (!h.valid()) {
			return {};
		}
		*m_pool.get(h) = v;
		return CompositeId {h.index, h.generation};
	}

	/// Libera un compuesto; un handle con la generación anterior deja de ser válido.
	/// \param id handle devuelto por `add`.
	/// \return `true` si el id era válido y se liberó.
	[[nodiscard]] constexpr bool remove(CompositeId id) noexcept {
		return m_pool.remove({id.index, id.generation});
	}

	/// Acceso de solo lectura a un compuesto vivo.
	/// \param id handle a consultar.
	/// \return vista no propietaria, o vacía si el id no es válido.
	[[nodiscard]] constexpr eng::Ref<const eng::graphics::CompositeVisual> get(
		CompositeId id) const noexcept {
		return m_pool.get({id.index, id.generation});
	}

	/// Acceso mutable a un compuesto vivo.
	/// \param id handle a consultar.
	/// \return vista no propietaria, o vacía si el id no es válido.
	[[nodiscard]] constexpr eng::Ref<eng::graphics::CompositeVisual> get(
		CompositeId id) noexcept {
		return m_pool.get({id.index, id.generation});
	}

	/// ¿El handle (índice + generación) sigue siendo válido?
	/// \param id handle a comprobar.
	/// \return `true` si el slot está vivo y la generación coincide.
	[[nodiscard]] constexpr bool valid(CompositeId id) const noexcept {
		return m_pool.valid({id.index, id.generation});
	}

	/// Vacía el store y sube la generación de todos los slots (invalida handles previos).
	constexpr void reset() noexcept { m_pool.reset(); }

private:
	PoolType m_pool {}; ///< pool fijo de vistas compuestas
};

/// Tipo de contenido de un sprite: simple (`Visual`) o compuesto (`CompositeVisual`).
enum class SpriteVisualKind : eng::u8 {
	Simple = 0u,
	Composite = 1u,
};

/// Referencia por valor al contenido de un sprite (discriminante + handle).
struct SpriteVisualRef {
	/// Discriminante: solo el campo correspondiente (`visual` o `composite`) es válido.
	SpriteVisualKind kind = SpriteVisualKind::Simple;
	/// Handle de `VisualStore` cuando `kind == Simple`.
	VisualId visual {};
	/// Handle de `CompositeStore` cuando `kind == Composite`.
	CompositeId composite {};

	/// ¿El handle del tipo discriminado es válido? No comprueba que el store siga vivo.
	[[nodiscard]] constexpr bool valid() const noexcept {
		return kind == SpriteVisualKind::Simple ? visual.valid() : composite.valid();
	}
	/// Construye una referencia simple.
	/// \param id handle de `VisualStore`.
	[[nodiscard]] static constexpr SpriteVisualRef simple(VisualId id) noexcept {
		return SpriteVisualRef {SpriteVisualKind::Simple, id, {}};
	}
	/// Construye una referencia compuesta.
	/// \param id handle de `CompositeStore`.
	[[nodiscard]] static constexpr SpriteVisualRef composite_visual(CompositeId id) noexcept {
		return SpriteVisualRef {SpriteVisualKind::Composite, {}, id};
	}
};

/// Vista resuelta del contenido: `simple` o `composite` está poblado según `ref.kind`.
struct SpriteVisualView {
	/// Referencia original (discriminante + handle).
	SpriteVisualRef ref {};
	/// Vista al `Visual` si `ref.kind == Simple`; vacía en otro caso.
	eng::Ref<const eng::graphics::Visual> simple {};
	/// Vista al `CompositeVisual` si `ref.kind == Composite`; vacía en otro caso.
	eng::Ref<const eng::graphics::CompositeVisual> composite {};

	/// ¿La vista corresponde a un contenido existente y del tipo correcto?
	[[nodiscard]] constexpr bool valid() const noexcept {
		return ref.kind == SpriteVisualKind::Simple ? simple.valid() : composite.valid();
	}
	/// Tipo de contenido resuelto (simple o compuesto).
	[[nodiscard]] constexpr SpriteVisualKind kind() const noexcept { return ref.kind; }
};

/// **Concepto común de contenido**: la vista resuelta de un simple o un compuesto. El
/// director no necesita dos caminos públicos (`SpriteVisualRef` es la referencia por valor).
using SpriteVisual = SpriteVisualView;

/// Contenido retenido de una escena: visuales simples y compuestos con handles separados.
/// \tparam MaxVisuals Capacidad de `VisualStore` (contenido simple). Rango 1..65534;
///         por defecto 128. Un alta con el store lleno devuelve `VisualId` inválido.
/// \tparam MaxComposites Capacidad de `CompositeStore` (contenido compuesto). Rango
///         1..65534; por defecto 32. Un alta con el store lleno devuelve id inválido.
template <eng::u16 MaxVisuals = 128u, eng::u16 MaxComposites = 32u>
class SceneAssets {
public:
	/// Registra contenido simple.
	/// \param v vista `Visual` (spans incluidos); el store copia la vista, no los píxeles.
	/// \return handle generacional, o inválido si `MaxVisuals` está lleno.
	[[nodiscard]] VisualId add_visual(const eng::graphics::Visual& v) noexcept {
		return m_visuals.add(v);
	}
	/// Registra contenido compuesto.
	/// \param v vista `CompositeVisual`; el store copia la vista.
	/// \return handle generacional, o inválido si `MaxComposites` está lleno.
	[[nodiscard]] CompositeId add_composite(const eng::graphics::CompositeVisual& v) noexcept {
		return m_composites.add(v);
	}
	/// Baja contenido simple; los ids con la generación anterior quedan inválidos.
	/// \param id handle devuelto por `add_visual`.
	/// \return `true` si era válido y se liberó.
	[[nodiscard]] bool remove_visual(VisualId id) noexcept { return m_visuals.remove(id); }
	/// Baja contenido compuesto; los ids con la generación anterior quedan inválidos.
	/// \param id handle devuelto por `add_composite`.
	/// \return `true` si era válido y se liberó.
	[[nodiscard]] bool remove_composite(CompositeId id) noexcept {
		return m_composites.remove(id);
	}
	/// ¿El handle simple sigue vivo?
	/// \param id handle a comprobar.
	[[nodiscard]] constexpr bool valid(VisualId id) const noexcept { return m_visuals.valid(id); }
	/// ¿El handle compuesto sigue vivo?
	/// \param id handle a comprobar.
	[[nodiscard]] constexpr bool valid(CompositeId id) const noexcept {
		return m_composites.valid(id);
	}
	/// Vista de solo lectura al `Visual` simple.
	/// \param id handle; vacío si no existe.
	[[nodiscard]] eng::Ref<const eng::graphics::Visual> visual(VisualId id) const noexcept {
		return m_visuals.get(id);
	}
	/// Vista de solo lectura al `CompositeVisual`.
	/// \param id handle; vacío si no existe.
	[[nodiscard]] eng::Ref<const eng::graphics::CompositeVisual> composite(
		CompositeId id) const noexcept {
		return m_composites.get(id);
	}
	/// Resuelve una referencia por valor al contenido correspondiente.
	/// \param ref referencia simple/compuesta (puede ser inválida).
	/// \return vista con el campo del tipo poblado; `valid()` indica si existe.
	[[nodiscard]] SpriteVisualView view(SpriteVisualRef ref) const noexcept {
		SpriteVisualView v {};
		v.ref = ref;
		if (ref.kind == SpriteVisualKind::Simple) {
			v.simple = m_visuals.get(ref.visual);
		} else {
			v.composite = m_composites.get(ref.composite);
		}
		return v;
	}
	/// Número de visuales simples vivos.
	[[nodiscard]] constexpr eng::u16 visual_count() const noexcept { return m_visuals.count(); }
	/// Número de compuestos vivos.
	[[nodiscard]] constexpr eng::u16 composite_count() const noexcept {
		return m_composites.count();
	}
	/// Vacía ambos stores (invalida todos los handles de contenido).
	void reset() noexcept {
		m_visuals.reset();
		m_composites.reset();
	}

private:
	VisualStore<MaxVisuals> m_visuals {};         ///< store de contenido simple
	CompositeStore<MaxComposites> m_composites {}; ///< store de contenido compuesto
};

/// Política de materialización pedida por el juego.
enum class SpritePolicy : eng::u8 {
	Auto = 0u,   ///< sprite HW si cabe; BOB si no.
	Sprite = 1u, ///< intenta sprite HW; el allocator puede degradar.
	Bob = 2u,    ///< dibuja siempre por Blitter.
	Cpu = 3u,    ///< reservado para objetos de CPU (no usa sprite/BOB).
	Layer = 4u,  ///< reservado para una capa de fondo propia.
};

/// Modo de par *attached* (15 colores).
enum class SpriteAttach : eng::u8 {
	Auto = 0u, ///< deriva del contenido (4 planos y 16 px).
	Off = 1u,  ///< no forma par; si el contenido exige 4 planos, cae a BOB.
	On = 2u,   ///< exige par; si no cabe, cae a BOB.
};

/// Estrategia de reuso/emisión pedida.
enum class SpriteReuse : eng::u8 {
	Auto = 0u,
	VerticalDma = 1u, ///< reuso vertical del canal por DMA/rearme.
	Chain = 2u,       ///< cadena DMA por canal (F3).
	PerLine = 3u,     ///< rearme por línea (capa avanzada `SpriteLineLayer`).
	Horizontal = 4u,  ///< tira horizontal / canales contiguos.
	None = 5u,        ///< sin sprite HW para esta entidad.
};

/// Emisor efectivo elegido por el director.
enum class SpriteEmitter : eng::u8 {
	Rearm = 0u,
	DmaChain = 1u,
	PerLine = 2u,
	Horizontal = 3u,
	Bob = 4u,
	Cpu = 5u,
};

/// Intención completa de un sprite.
struct SpriteHint {
	SpritePolicy policy = SpritePolicy::Auto;
	SpriteAttach attach = SpriteAttach::Auto;
	SpriteReuse reuse = SpriteReuse::Auto;
	/// Gap vertical pedido. El allocator aplica `kSpriteVerticalGapLines`; un valor menor
	/// se reporta como `GapViolation` porque no es representable en hardware.
	eng::u8 gap_lines = eng::graphics::kSpriteVerticalGapLines;
	eng::u8 channel = 0xffu; ///< 0..7 preferido; 0xff = cualquiera.
	eng::u8 rank = 0u;       ///< 0 = libre; 1..3 = se asigna antes.
	eng::u8 sprite_priority = 0u; ///< prioridad frente a playfields (`BPLCON2`).
	eng::u8 group = 0u;
	eng::u8 group_index = 0u;
	eng::u8 group_span = 1u;
	eng::u8 strip_id = 0u;
	eng::u8 strip_index = 0u;
	eng::u8 strip_span = 1u;
};

/// Elige el emisor a partir de los hints y de la infraestructura disponible.
[[nodiscard]] constexpr SpriteEmitter choose_sprite_emitter(const SpriteHint& h,
							    bool has_template,
							    bool has_line_layer,
							    bool has_strip) noexcept {
	if (h.policy == SpritePolicy::Bob || h.policy == SpritePolicy::Cpu) {
		return h.policy == SpritePolicy::Cpu ? SpriteEmitter::Cpu : SpriteEmitter::Bob;
	}
	switch (h.reuse) {
		case SpriteReuse::None:
			return SpriteEmitter::Bob;
		case SpriteReuse::Chain:
		case SpriteReuse::VerticalDma:
			return has_template ? SpriteEmitter::DmaChain : SpriteEmitter::Rearm;
		case SpriteReuse::PerLine:
			return has_line_layer ? SpriteEmitter::PerLine : SpriteEmitter::Rearm;
		case SpriteReuse::Horizontal:
			return has_strip ? SpriteEmitter::Horizontal : SpriteEmitter::Rearm;
		case SpriteReuse::Auto:
			break;
	}
	if (has_strip) return SpriteEmitter::Horizontal;
	if (has_template) return SpriteEmitter::DmaChain;
	return SpriteEmitter::Rearm;
}

/// Tope de palabras de Blitter por frame que usa la fachada si el juego no fija otro.
/// Es un presupuesto de **planificación** (no un límite de hardware): 60k palabras deja
/// margen para Copper/CPU en un A500 con playfield activo; medir y ajustar al juego.
inline constexpr eng::u16 kSpriteBlitWordsDefault = 60000u;
/// Número de intenciones de Copper que reserva copper::Plan por frame.
inline constexpr eng::u16 kSpriteCopperIntentsDefault = 320u;
/// Eventos de paleta por frame (dos juegos de 16 registros: sprites + playfield).
inline constexpr eng::u16 kSpritePaletteEventsDefault = 32u;

/// Presupuesto de un frame o de un preflight estático. Los valores por defecto son
/// cotas de planificación; el juego puede ajustarlos por campo.
struct SpriteBudget {
	eng::u8 channels = eng::graphics::kSpriteChannels; ///< canales HW disponibles (0..8).
	eng::u16 scanline = eng::graphics::kSpriteMaxPerLine; ///< sprites simultáneos por scanline.
	eng::u16 copper_intents = kSpriteCopperIntentsDefault; ///< intenciones de Copper.
	eng::u16 palette_events = kSpritePaletteEventsDefault; ///< eventos de paleta.
	eng::u16 gap_violations = 0u; ///< violaciones toleradas (0 = cualquiera falla).
	eng::u16 band_overlaps = 0u; ///< solapes tolerados (0 = cualquiera falla).
	eng::u16 blit_words = kSpriteBlitWordsDefault; ///< palabras de Blitter del frame.
};

/// Avisos del reporte; máscara de bits para acumularlos en `SpriteLoadReport::warnings`.
enum class SpriteWarning : eng::u32 {
	None = 0u,
	Gap = 1u << 0u,
	Scanline = 1u << 1u,
	Channels = 1u << 2u,
	BlitBudget = 1u << 3u,
	CopperBudget = 1u << 4u,
	PaletteBudget = 1u << 5u,
	BandOutOfRange = 1u << 6u,
	BandOverlap = 1u << 7u,
	StripShort = 1u << 8u,
	AttachedMalformed = 1u << 9u,
	BlitDeadline = 1u << 10u,
	PerLineMissing = 1u << 11u,
	MissingCookedPool = 1u << 12u,
	Truncated = 1u << 13u,
	CompositeUnsupported = 1u << 14u,
};

/// Convierte un aviso tipado a su bit de máscara (`SpriteLoadReport::warnings`).
/// \param w aviso a convertir.
/// \return bit correspondiente (0 para `None`).
[[nodiscard]] constexpr eng::u32 warning_bit(SpriteWarning w) noexcept {
	return static_cast<eng::u32>(w);
}

/// **Contrato único de contabilidad** de sprites (runtime y preflight).
struct SpriteLoadReport {
	eng::u16 entities = 0u;
	eng::u16 parts = 0u;
	eng::u16 hw = 0u;
	eng::u16 bob = 0u;
	eng::u16 degraded = 0u;
	eng::u16 attached_pairs = 0u;
	eng::u16 strips = 0u;
	eng::u16 bands = 0u;
	eng::u16 band_lines = 0u;
	eng::u8 channels_used = 0u;
	eng::u8 max_reuse = 0u;
	eng::u16 scanline_peak = 0u;
	eng::u16 gap_violations = 0u;
	eng::u16 band_overlaps = 0u;
	eng::u16 band_out_of_range = 0u;
	eng::u16 copper_intents = 0u;
	eng::u16 copper_words_estimate = 0u;
	eng::u16 palette_events = 0u;
	eng::u16 bob_jobs = 0u;
	eng::u32 blit_words = 0u;
	eng::u16 emitter_rearm = 0u;
	eng::u16 emitter_chain = 0u;
	eng::u16 emitter_per_line = 0u;
	eng::u16 emitter_horizontal = 0u;
	eng::u32 warnings = 0u;
	bool ok = true;

	/// ¿Está activo el aviso `w` en este reporte?
	/// \param w aviso a consultar.
	[[nodiscard]] constexpr bool has(SpriteWarning w) const noexcept {
		return (warnings & warning_bit(w)) != 0u;
	}
	/// Activa el aviso `w` sin cambiar `ok` (el llamador decide si es bloqueante).
	/// \param w aviso a activar.
	constexpr void warn(SpriteWarning w) noexcept { warnings |= warning_bit(w); }
	/// Acumula otro reporte: suma contadores, OR de canales/avisos, máximos y `ok` AND.
	/// \param o reporte a fusionar; no se modifica.
	constexpr void merge(const SpriteLoadReport& o) noexcept {
		entities = static_cast<eng::u16>(entities + o.entities);
		parts = static_cast<eng::u16>(parts + o.parts);
		hw = static_cast<eng::u16>(hw + o.hw);
		bob = static_cast<eng::u16>(bob + o.bob);
		degraded = static_cast<eng::u16>(degraded + o.degraded);
		attached_pairs = static_cast<eng::u16>(attached_pairs + o.attached_pairs);
		strips = static_cast<eng::u16>(strips + o.strips);
		bands = static_cast<eng::u16>(bands + o.bands);
		band_lines = static_cast<eng::u16>(band_lines + o.band_lines);
		channels_used = static_cast<eng::u8>(channels_used | o.channels_used);
		if (o.max_reuse > max_reuse) max_reuse = o.max_reuse;
		if (o.scanline_peak > scanline_peak) scanline_peak = o.scanline_peak;
		gap_violations = static_cast<eng::u16>(gap_violations + o.gap_violations);
		band_overlaps = static_cast<eng::u16>(band_overlaps + o.band_overlaps);
		band_out_of_range = static_cast<eng::u16>(band_out_of_range + o.band_out_of_range);
		copper_intents = static_cast<eng::u16>(copper_intents + o.copper_intents);
		copper_words_estimate = static_cast<eng::u16>(copper_words_estimate + o.copper_words_estimate);
		palette_events = static_cast<eng::u16>(palette_events + o.palette_events);
		bob_jobs = static_cast<eng::u16>(bob_jobs + o.bob_jobs);
		blit_words += o.blit_words;
		emitter_rearm = static_cast<eng::u16>(emitter_rearm + o.emitter_rearm);
		emitter_chain = static_cast<eng::u16>(emitter_chain + o.emitter_chain);
		emitter_per_line = static_cast<eng::u16>(emitter_per_line + o.emitter_per_line);
		emitter_horizontal = static_cast<eng::u16>(emitter_horizontal + o.emitter_horizontal);
		warnings |= o.warnings;
		ok = ok && o.ok;
	}
};

/// Aplica el presupuesto al reporte (marca avisos y `ok`). Función pura.
constexpr void apply_budget(SpriteLoadReport& r, const SpriteBudget& b) noexcept {
	if (r.channels_used != 0u) {
		eng::u8 n = 0u;
		eng::u8 mask = r.channels_used;
		while (mask != 0u) {
			n = static_cast<eng::u8>(n + (mask & 1u));
			mask = static_cast<eng::u8>(mask >> 1u);
		}
		if (n > b.channels) {
			r.warn(SpriteWarning::Channels);
			r.ok = false;
		}
	}
	if (r.scanline_peak > b.scanline) {
		r.warn(SpriteWarning::Scanline);
		r.ok = false;
	}
	if (r.blit_words > b.blit_words) {
		r.warn(SpriteWarning::BlitBudget);
		r.ok = false;
	}
	if (r.copper_intents > b.copper_intents) {
		r.warn(SpriteWarning::CopperBudget);
		r.ok = false;
	}
	if (r.palette_events > b.palette_events) {
		r.warn(SpriteWarning::PaletteBudget);
		r.ok = false;
	}
	if (r.gap_violations > b.gap_violations) {
		r.warn(SpriteWarning::Gap);
		r.ok = false;
	}
	if (r.band_overlaps > b.band_overlaps) {
		r.warn(SpriteWarning::BandOverlap);
		r.ok = false;
	}
	if (r.blit_words > b.blit_words) {
		r.warn(SpriteWarning::BlitDeadline);
		r.ok = false;
	}
}

/// Descripción estática de un sprite para `evaluate_static` (sin contenido ni punteros).
struct SpriteStaticDesc {
	eng::u16 top = 0u;
	eng::u16 bottom = 0u;
	eng::u8 channels = 1u; ///< 1 normal; 2 par attached.
	eng::u8 rank = 0u;
	eng::u8 strip_span = 1u;
	SpritePolicy policy = SpritePolicy::Auto;
	SpriteReuse reuse = SpriteReuse::Auto;
};

/// **Evaluación estática** de una escena (mismo gap de 1 línea que el runtime). No toca
/// hardware ni memoria dinámica; se puede usar en `static_assert`.
[[nodiscard]] constexpr SpriteLoadReport evaluate_static(
	eng::Span<const SpriteStaticDesc> descs, const SpriteBudget& budget = {}) noexcept {
	SpriteLoadReport r {};
	eng::u8 used_mask = 0u;
	eng::u16 last[eng::graphics::kSpriteChannels] {};
	bool used[eng::graphics::kSpriteChannels] {};
	eng::u16 scan[512] {};
	for (eng::usize i = 0u; i < descs.size(); ++i) {
		const SpriteStaticDesc& d = descs[i];
		++r.entities;
		const eng::u16 span = d.top < d.bottom ? static_cast<eng::u16>(d.bottom - d.top) : 0u;
		if (span == 0u) {
			continue;
		}
		eng::u8 need = d.channels != 0u ? d.channels : 1u;
		if (d.strip_span > need) need = d.strip_span;
		if (d.policy == SpritePolicy::Bob || d.policy == SpritePolicy::Cpu ||
		    d.policy == SpritePolicy::Layer || d.reuse == SpriteReuse::None) {
			++r.bob;
			++r.bob_jobs;
			r.blit_words += static_cast<eng::u32>(span) * need;
			continue;
		}
		eng::u8 base = 0xffu;
		for (eng::u8 c = 0u; c + need <= eng::graphics::kSpriteChannels; ++c) {
			bool ok = true;
			for (eng::u8 k = 0u; k < need; ++k) {
				const eng::u8 ch = static_cast<eng::u8>(c + k);
				if (used[ch] &&
				    d.top < static_cast<eng::u16>(
						     last[ch] + eng::graphics::kSpriteVerticalGapLines)) {
					ok = false;
					break;
				}
			}
			if (ok) {
				base = c;
				break;
			}
		}
		if (base == 0xffu) {
			++r.bob;
			++r.degraded;
			r.warn(SpriteWarning::Channels);
			continue;
		}
		for (eng::u8 k = 0u; k < need; ++k) {
			const eng::u8 ch = static_cast<eng::u8>(base + k);
			used[ch] = true;
			last[ch] = d.bottom;
			used_mask = static_cast<eng::u8>(used_mask | static_cast<eng::u8>(1u << ch));
			++r.hw;
		}
		if (need == 2u) ++r.attached_pairs;
		if (d.strip_span > 1u) ++r.strips;
		const eng::u16 from = d.top;
		const eng::u16 to = d.bottom < 512u ? d.bottom : 512u;
		for (eng::u16 line = from; line < to; ++line) {
			scan[line] = static_cast<eng::u16>(scan[line] + need);
			if (scan[line] > r.scanline_peak) r.scanline_peak = scan[line];
		}
	}
	r.channels_used = used_mask;
	apply_budget(r, budget);
	return r;
}

/// Paleta de una banda vertical con scroll. `top` y `scroll_y` se suman cada frame; el
/// director emite el `PaletteLine` en `top + scroll_y` (nunca toca una copperlist estática).
struct BandPalette {
	eng::s16 top = 0;
	eng::u16 height = 0u;
	eng::PaletteWords sprites {};
	eng::PaletteWords playfield {};
	eng::s16 scroll_y = 0;
};

/// Cue genérico de Copper: `colors[k]` → `COLOR[first+k]` en `line`.
struct CopperCue {
	eng::u16 line = 0u;
	eng::u8 first = 0u;
	eng::PaletteWords colors {};
};

/// Informe de una pasada de paletas por banda.
struct BandPaletteReport {
	eng::u16 bands = 0u;
	eng::u16 cues = 0u;
	eng::u16 sprite_events = 0u;
	eng::u16 playfield_events = 0u;
	eng::u16 lines = 0u;
	eng::u16 overlaps = 0u;
	eng::u16 out_of_range = 0u;
	bool ok = true;
};

/// Línea efectiva sin recortar (`top + scroll_y`).
[[nodiscard]] constexpr eng::s32 band_palette_line_signed(const BandPalette& b) noexcept {
	return static_cast<eng::s32>(b.top) + static_cast<eng::s32>(b.scroll_y);
}

/// Línea efectiva recortada a 0..511 para el Copper.
[[nodiscard]] constexpr eng::u16 band_palette_line(const BandPalette& b) noexcept {
	const eng::s32 line = band_palette_line_signed(b);
	if (line < 0) return 0u;
	if (line > 511) return 511u;
	return static_cast<eng::u16>(line);
}

/// Rellena hasta dos cues (playfield en `first=0`, sprites en `first=16`). Devuelve cuántos.
[[nodiscard]] inline eng::u8 band_palette_cues(const BandPalette& b,
					      eng::Span<CopperCue> out) noexcept {
	eng::u8 n = 0u;
	const eng::u16 line = band_palette_line(b);
	if (!b.playfield.empty() && n < out.size()) {
		const eng::usize count = b.playfield.size() > 32u ? 32u : b.playfield.size();
		out[n++] = CopperCue {line, 0u, b.playfield.first(count)};
	}
	if (!b.sprites.empty() && n < out.size()) {
		const eng::usize count = b.sprites.size() > 16u ? 16u : b.sprites.size();
		out[n++] = CopperCue {line, 16u, b.sprites.first(count)};
	}
	return n;
}

/// **Ventana de deadline** para un blit de CPU single-buffer. Extiende el contrato de
/// `safe_blitter_window` a la fachada: el trabajo debe empezar después de `first_line` y
/// terminar antes de `last_line`; si no, el reporte avisa con `BlitDeadline`.
struct BlitDeadline {
	eng::u16 first_line = 0u;
	eng::u16 last_line = 255u;
	eng::u32 max_words = 0xffffffffu;
	bool hard = false;
};

/// ¿Cabe un blit de `words` palabras dentro del deadline?
/// \param d ventana y tope de palabras.
/// \param words palabras de Blitter que consumirá el trabajo.
/// \return `true` si `words <= d.max_words` y la ventana está ordenada.
[[nodiscard]] constexpr bool blit_deadline_ok(const BlitDeadline& d, eng::u32 words) noexcept {
	return words <= d.max_words && d.first_line <= d.last_line;
}

/// Ventana segura para un blit de CPU: reutiliza `safe_blitter_window` (misma fórmula
/// `cpu_start + blitter_lines(words)`), acotada por el deadline de la fachada.
[[nodiscard]] constexpr eng::graphics::BlitterWindow blit_deadline_window(
	const BlitDeadline& d, eng::u16 cpu_words, eng::u16 cpu_start_line) noexcept {
	return eng::graphics::safe_blitter_window(cpu_words, cpu_start_line, d.first_line,
						  d.last_line);
}

// `SpritePresent`/`choose_present_policy` se retiraron de la API: la política de
// presentación todavía no tiene materialización propia; el caller decide cuándo publica
// la copperlist con su driver.
// 
// `blit_deadline_window` sigue siendo el único helper de ventana de CPU.
} // namespace eng::scene
