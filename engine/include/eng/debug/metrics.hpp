#pragma once

/// \file metrics.hpp
/// Métricas de recursos del motor y del juego, con traza de eventos.
///
/// El motor publica un bloque de tamaño fijo (`MetricsBlock`) con el valor del
/// frame actual y un anillo de eventos. Una demo puede registrar consumo de
/// Blitter, grupos/celdas Tiled, actores, restauraciones, etc. sin acoplarse a
/// los detalles internos de cada sistema; el host lee el bloque por el canal
/// lateral o por GDB.
///
/// **Coste**: con `ENG_METRICS` sin definir, `metrics_enabled` es `false`, el
/// bloque no existe y `MetricsWriter` compila sus cuerpos a vacío (`if constexpr`).
/// La build normal no paga ni una instrucción por registro. Con `ENG_METRICS`, la
/// instrumentación cuesta unos pocos stores por contador y frame; es una build de
/// diagnóstico, no la de producción.
///
/// Uso:
///
/// ```cpp
/// eng::debug::metrics.begin_frame(context.frame.frame_index);
/// eng::debug::metrics.set(eng::debug::Metric::BlitJobs, budget.jobs);
/// eng::debug::metrics.add(eng::debug::Metric::TiledCellsPatched, stats.cells_patched);
/// eng::debug::metrics.event(eng::debug::Metric::TiledGroupsDeferred, 3u);
/// ```
///
/// El flag de build es el único preprocesador de la API: la selección de código
/// se hace con `if constexpr` sobre `metrics_enabled`.

#include <eng/core/types/types.hpp>

namespace eng::debug {

inline constexpr u32 metrics_magic = 0x4d545243u; ///< "MTRC".
inline constexpr u16 metrics_version = 1u;

/// Contadores simultáneos del bloque. El valor es estable para el host.
inline constexpr u8 metrics_slot_count = 16u;

/// Eventos conservados en el anillo de traza (el más reciente sobreescribe al
/// más antiguo). Un evento ocupa 12 bytes; 16 eventos = 192 bytes de `.data`.
inline constexpr u8 metrics_event_capacity = 16u;

/// Contadores conocidos por el engine. Cada valor es un índice fijo dentro de
/// `MetricsBlock::values`; añadir uno al final no cambia los anteriores.
enum class Metric : u8 {
	BlitJobs = 0u,       ///< Jobs de Blitter emitidos en el frame.
	BlitWords,           ///< Palabras de Blitter del frame.
	BlitMaskedJobs,      ///< Jobs enmascarados del frame.
	BlitCopyJobs,        ///< Jobs de copia del frame.
	CopperWords,         ///< Palabras de Copper emitidas.
	FillupExtra,         ///< Blits extra del ajuste de fillup.
	TiledGroupsScanned,  ///< Grupos Tiled recorridos por el parche.
	TiledGroupsPatched,  ///< Grupos Tiled efectivamente repintados.
	TiledGroupsDeferred, ///< Grupos Tiled que quedaron para el siguiente turno.
	TiledCellsPatched,   ///< Celdas Tiled repintadas.
	TiledCellsCulled,    ///< Celdas Tiled descartadas por no ser visibles.
	TiledBaseRestores,   ///< Bloques base repintados bajo overlays transparentes.
	ObjectDraws,         ///< Props/objetos dibujados desde el mapa.
	ActorDraws,          ///< Actores dibujados.
	ActorRestores,       ///< Restauraciones de save-under de actores.
	GuardOverlays,       ///< Repintados estáticos usados como oclusión.
	Count
};

/// Un evento de traza. `metric` es el índice de `Metric`; `value` es el dato
/// asociado (por ejemplo, número de grupos diferidos). El campo `pad` mantiene
/// el tamaño de 12 bytes y la alineación de 32 bits en 68000.
struct MetricEvent {
	u32 frame = 0u; ///< Frame en que ocurrió el evento.
	u16 metric = 0u; ///< Índice de `Metric`.
	u16 pad = 0u;    ///< Reservado; siempre 0.
	u32 value = 0u;  ///< Valor del evento.
};

/// Bloque publicado en `.data` y resuelto por nombre (`g_eng_metrics`).
///
/// Layout estable, big-endian en 68000:
///
///   offset 0  u32 magic
///   offset 4  u16 version
///   offset 6  u8  slot_count
///   offset 7  u8  event_capacity
///   offset 8  u32 frame
///   offset 12 u32 values[slot_count]
///   offset 76 u32 event_total
///   offset 80 u8  event_head
///   offset 81 u8  event_count
///   offset 82 u16 reserved
///   offset 84 MetricEvent events[event_capacity]
///
/// `values` se reinicia en cada `begin_frame`; `event_total` y el anillo de
/// eventos son acumulativos para el postmortem.
struct MetricsBlock {
	u32 magic = metrics_magic;
	u16 version = metrics_version;
	u8 slot_count = metrics_slot_count;
	u8 event_capacity = metrics_event_capacity;
	u32 frame = 0u;
	u32 values[metrics_slot_count] {};
	u32 event_total = 0u;
	u8 event_head = 0u;
	u8 event_count = 0u;
	u16 reserved = 0u;
	MetricEvent events[metrics_event_capacity] {};
};

// Se declara siempre para que las plantillas puedan nombrarlo; la definición
// solo existe en builds con `ENG_METRICS` (ver `amiga.cpp`). Sin el flag,
// `MetricsWriter<false>` no instancia ninguna referencia al símbolo.
extern "C" volatile MetricsBlock g_eng_metrics;

#if defined(ENG_METRICS)
inline constexpr bool metrics_enabled = true;
#else
inline constexpr bool metrics_enabled = false;
#endif

/// Escritor de métricas con coste nulo cuando `Enabled` es `false`.
///
/// \tparam Enabled activa o elimina los cuerpos mediante `if constexpr`. El
///         valor normal es `metrics_enabled`; un test puede instanciar `false`
///         para verificar que no toca memoria.
template <bool Enabled>
class MetricsWriter {
public:
	/// Indica si esta instancia escribe en el bloque global.
	static constexpr bool enabled = Enabled;

	constexpr MetricsWriter() noexcept = default;

	/// Abre un frame y pone sus contadores a cero. No borra la traza.
	/// \param frame_index índice de frame del motor; se publica en `frame`.
	void begin_frame(u32 frame_index) const noexcept {
		if constexpr (Enabled) {
			volatile MetricsBlock& b = g_eng_metrics;
			b.frame = frame_index;
			for (u8 i = 0u; i < metrics_slot_count; ++i) b.values[i] = 0u;
		} else {
			(void)frame_index;
		}
	}

	/// Fija el valor absoluto de un contador del frame actual.
	/// \param metric contador de `Metric` (se ignora si está fuera de rango).
	/// \param value nuevo valor.
	void set(Metric metric, u32 value) const noexcept {
		if constexpr (Enabled) {
			const u8 i = static_cast<u8>(metric);
			if (i < metrics_slot_count) g_eng_metrics.values[i] = value;
		} else {
			(void)metric;
			(void)value;
		}
	}

	/// Suma un delta atómico al contador del frame actual (un store, no RMW).
	/// \param metric contador de `Metric`.
	/// \param delta cantidad a sumar; 0 no modifica el bloque.
	/// \warning El escritor no es thread-safe: el mismo contador no debe
	///          actualizarse desde dos contextos (CPU e IRQ) sin coordinación.
	void add(Metric metric, u32 delta) const noexcept {
		if constexpr (Enabled) {
			const u8 i = static_cast<u8>(metric);
			if (i < metrics_slot_count && delta != 0u) {
				g_eng_metrics.values[i] = g_eng_metrics.values[i] + delta;
			}
		} else {
			(void)metric;
			(void)delta;
		}
	}

	/// Añade un evento al anillo de traza. El anillo conserva los últimos
	/// `metrics_event_capacity` eventos; al llenarse sobreescribe el más viejo.
	/// \param metric contador de `Metric` asociado al evento.
	/// \param value dato del evento (queda a interpretación del consumidor).
	void event(Metric metric, u32 value) const noexcept {
		if constexpr (Enabled) {
			volatile MetricsBlock& b = g_eng_metrics;
			const u8 slot = b.event_head;
			volatile MetricEvent& e = b.events[slot];
			e.frame = b.frame;
			e.metric = static_cast<u16>(metric);
			e.pad = 0u;
			e.value = value;
			b.event_head = static_cast<u8>((slot + 1u) % metrics_event_capacity);
			if (b.event_count < metrics_event_capacity) {
				b.event_count = static_cast<u8>(b.event_count + 1u);
			}
			b.event_total = b.event_total + 1u;
		} else {
			(void)metric;
			(void)value;
		}
	}

	/// Lee el valor actual de un contador. En build sin métricas devuelve 0.
	/// \param metric contador a consultar.
	/// \return valor del frame actual, o 0 si no está instrumentado.
	[[nodiscard]] u32 value(Metric metric) const noexcept {
		if constexpr (Enabled) {
			const u8 i = static_cast<u8>(metric);
			return i < metrics_slot_count ? g_eng_metrics.values[i] : 0u;
		}
		(void)metric;
		return 0u;
	}
};

/// Instancia global sin estado: con `ENG_METRICS` escribe en `g_eng_metrics`;
/// sin el flag no genera código ni bloque de memoria.
inline constexpr MetricsWriter<metrics_enabled> metrics {};

/// Nombre estable de un contador para HUD, volcados y herramientas host.
/// \param metric contador de `Metric`.
/// \return cadena literal; `"?"` para valores fuera del catálogo.
[[nodiscard]] constexpr const char* metric_name(Metric metric) noexcept {
	switch (metric) {
	case Metric::BlitJobs: return "blit_jobs";
	case Metric::BlitWords: return "blit_words";
	case Metric::BlitMaskedJobs: return "blit_masked_jobs";
	case Metric::BlitCopyJobs: return "blit_copy_jobs";
	case Metric::CopperWords: return "copper_words";
	case Metric::FillupExtra: return "fillup_extra";
	case Metric::TiledGroupsScanned: return "tiled_groups_scanned";
	case Metric::TiledGroupsPatched: return "tiled_groups_patched";
	case Metric::TiledGroupsDeferred: return "tiled_groups_deferred";
	case Metric::TiledCellsPatched: return "tiled_cells_patched";
	case Metric::TiledCellsCulled: return "tiled_cells_culled";
	case Metric::TiledBaseRestores: return "tiled_base_restores";
	case Metric::ObjectDraws: return "object_draws";
	case Metric::ActorDraws: return "actor_draws";
	case Metric::ActorRestores: return "actor_restores";
	case Metric::GuardOverlays: return "guard_overlays";
	default: return "?";
	}
}

} // namespace eng::debug
