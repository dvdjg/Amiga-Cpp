#pragma once

/// \file quest.hpp
/// **Narrativa** (`eng::rpg`, hito M5): quests declarativas sobre hechos. Condiciones y
/// acciones son **datos** (tablas en ROM), no código por misión: nada de
/// `if (quest == 3 && step == 2)`. Diseño: `docs/engine/architecture/RPG_LAYER.md` §5.5.
///
/// Modelo: una quest es una lista de **stages**; cada stage tiene condiciones (AND) que se
/// comprueban contra el `FactStore` (y, si el juego aporta la política `Items`, contra su
/// inventario) y acciones que se ejecutan al cumplirse (escribir hechos, consumir objetos).
/// Completar el último stage (o el marcado `close`) cierra la quest.
///
/// El pegamento es el **FactStore**: matar al lobo escribe un hecho y la quest, el
/// comerciante y la puerta reaccionan sin conocerse. `quest_tick` sale en O(1) cuando nada
/// cambió desde la última pasada (epoch de los hechos).
///
/// Verificación: HOST-463. NO VERIFICADA por demo (el slice 400 la ejercita con quests
/// cruzadas; la marca se levanta al cerrar su campaña).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/rpg/facts.hpp>
#include <eng/rpg/items.hpp>

namespace eng::rpg {

/// Condiciones evaluables (declarativas). `FactGe`: hecho ausente vale 0.
enum class CondKind : eng::u8 {
	FactGe = 0u, ///< a = FactId, b = valor mínimo
	ItemGe = 1u, ///< a = ItemTypeId (0xff = cualquiera), b = cantidad mínima, c = etiquetas exigidas
};

/// Una condición (fila de la tabla del libro; 7 bytes).
struct QuestCond {
	eng::u8 kind = static_cast<eng::u8>(CondKind::FactGe); ///< `CondKind`
	eng::u16 a = 0u; ///< hecho o tipo según `kind`
	eng::u16 b = 0u; ///< mínimo (valor o unidades)
	eng::u16 c = 0u; ///< (ItemGe) etiquetas exigidas del tipo
};

/// Acciones ejecutables al cumplirse un stage.
enum class ActKind : eng::u8 {
	SetFact = 0u,  ///< a = FactId, b = valor
	AddFact = 1u,  ///< a = FactId, b = delta (saturado)
	ClearFact = 2u,///< a = FactId
	ConsumeItem = 3u, ///< a, b, c como en `CondKind::ItemGe`
};

/// Una acción (fila de la tabla del libro; 7 bytes).
struct QuestAct {
	eng::u8 kind = static_cast<eng::u8>(ActKind::SetFact); ///< `ActKind`
	eng::u16 a = 0u;
	eng::u16 b = 0u;
	eng::u16 c = 0u;
};

/// Fase de una quest: condiciones (todas) y acciones al cumplirse.
struct QuestStage {
	eng::u8 cond_first = 0u; ///< primer índice en la tabla de condiciones
	eng::u8 cond_count = 0u; ///< condiciones del stage (AND)
	eng::u8 act_first = 0u;  ///< primer índice en la tabla de acciones
	eng::u8 act_count = 0u;  ///< acciones del stage (en orden)
	bool close = false;      ///< cerrar la quest (Done) al completar este stage
};

/// Definición de quest: rango de stages en la tabla del libro.
struct QuestDef {
	eng::u8 stage_first = 0u; ///< primer índice en la tabla de stages
	eng::u8 stage_count = 0u; ///< nº de stages
};

/// Libro de quests: vistas sobre las tablas de contenido (ROM).
struct QuestBook {
	eng::Span<const QuestDef> quests {};   ///< definiciones (índice = QuestId)
	eng::Span<const QuestStage> stages {}; ///< tabla plana de stages
	eng::Span<const QuestCond> conds {};   ///< tabla plana de condiciones
	eng::Span<const QuestAct> acts {};     ///< tabla plana de acciones
};

/// Estado de una quest.
enum class QuestState : eng::u8 {
	Inactive = 0u, ///< aún no abierta (el juego la abre al aceptar)
	Active = 1u,   ///< en curso (se evalúa)
	Done = 2u,     ///< completada (journal)
};

/// Estado de ejecución de las quests (parte de la partida; bloques pequeños, serializable).
template <eng::u8 MaxQuests>
struct QuestRuntime {
	eng::u8 stage[MaxQuests] {};         ///< índice GLOBAL del stage actual por quest
	QuestState state[MaxQuests] {};      ///< estado por quest
	eng::u32 seen_epoch = 0xffffffffu;   ///< último epoch de hechos procesado (fast-out)
};

/// Abre la quest `q` (la marca activa y la sitúa en su primer stage). No evalúa: el primer
/// `quest_tick` la procesa.
template <eng::u8 MaxQuests>
inline void quest_open(const QuestBook& book, QuestRuntime<MaxQuests>& rt, eng::u8 q) noexcept {
	if (q >= MaxQuests || q >= book.quests.size()) {
		return;
	}
	rt.state[q] = QuestState::Active;
	rt.stage[q] = book.quests[q].stage_first;
	rt.seen_epoch = 0xffffffffu; // fuerza la evaluación en el próximo tick
}

/// Estado de la quest `q` (`Inactive` si no existe).
template <eng::u8 MaxQuests>
[[nodiscard]] inline QuestState quest_state(const QuestRuntime<MaxQuests>& rt,
					    eng::u8 q) noexcept {
	return q < MaxQuests ? rt.state[q] : QuestState::Inactive;
}

/// Fuerza la reevaluación en el próximo `quest_tick`: para cambios que **no** tocan hechos
/// (inventario, estado de la escena). El fast-out por epoch solo cubre hechos; el juego
/// llama a esto al recoger/soltar objetos relevantes.
template <eng::u8 MaxQuests>
inline void quest_invalidate(QuestRuntime<MaxQuests>& rt) noexcept {
	rt.seen_epoch = 0xffffffffu;
}

/// Stage actual de la quest dentro de su definición (0..stage_count-1); 0 si inactiva.
template <eng::u8 MaxQuests>
[[nodiscard]] inline eng::u8 quest_stage(const QuestBook& book, const QuestRuntime<MaxQuests>& rt,
					 eng::u8 q) noexcept {
	if (q >= MaxQuests || q >= book.quests.size()) {
		return 0u;
	}
	return static_cast<eng::u8>(rt.stage[q] - book.quests[q].stage_first);
}

/// Nº de stages de la quest (para el journal: "stage 2/3").
[[nodiscard]] inline eng::u8 quest_stage_count(const QuestBook& book, eng::u8 q) noexcept {
	return q < book.quests.size() ? book.quests[q].stage_count : 0u;
}

/// **Evalúa y avanza las quests activas.** `Items` es una política aportada por el juego:
/// `eng::u16 count(eng::u16 type, eng::u16 tags) const` y
/// `bool consume(eng::u16 type, eng::u16 tags, eng::u8 qty)`. Devuelve cuántos stages se
/// completaron (telemetría/tests). Fast-out por epoch salvo `force`. El avance **cascada**
/// dentro del tick (un hecho escrito por un stage puede cumplir el siguiente).
template <eng::u16 MaxFacts, eng::u8 MaxQuests, class Items>
eng::u8 quest_tick(FactStore<MaxFacts>& facts, const QuestBook& book, QuestRuntime<MaxQuests>& rt,
		   Items& items, bool force = false) noexcept {
	if (!force && facts.epoch() == rt.seen_epoch) {
		return 0u; // nada cambió: ninguna quest puede avanzar
	}
	eng::u8 advanced = 0u;
	for (eng::u8 q = 0u; q < book.quests.size() && q < MaxQuests; ++q) {
		if (rt.state[q] != QuestState::Active) {
			continue;
		}
		const QuestDef& def = book.quests[q];
		// Cascada: como máximo, los stages de la quest (los hechos escritos pueden encadenar).
		for (eng::u8 steps = 0u; steps < def.stage_count; ++steps) {
			const eng::u8 cur = rt.stage[q];
			if (cur < def.stage_first ||
			    cur >= static_cast<eng::u8>(def.stage_first + def.stage_count) ||
			    cur >= book.stages.size()) {
				break; // definición mal formada: la quest queda congelada, sin leer fuera
			}
			const QuestStage& stage = book.stages[cur];
			const eng::usize cond_end =
			    static_cast<eng::usize>(stage.cond_first) + stage.cond_count;
			const eng::usize act_end =
			    static_cast<eng::usize>(stage.act_first) + stage.act_count;
			if (cond_end > book.conds.size() || act_end > book.acts.size()) {
				break;
			}
			bool met = true;
			for (eng::u8 k = 0u; k < stage.cond_count && met; ++k) {
				const QuestCond& cond = book.conds[stage.cond_first + k];
				switch (static_cast<CondKind>(cond.kind)) {
					case CondKind::FactGe:
						met = facts.value_or(cond.a, 0u) >= cond.b;
						break;
					case CondKind::ItemGe:
						met = items.count(cond.a, cond.c) >= cond.b;
						break;
					default:
						met = false; // condición desconocida: no se cumple
						break;
				}
			}
			if (!met) {
				break;
			}
			// Acciones en orden; si consumir falla (inventario cambiado entre la validación
			// y la acción), el stage NO avanza y el estado queda consistente.
			bool applied = true;
			for (eng::u8 k = 0u; k < stage.act_count && applied; ++k) {
				const QuestAct& act = book.acts[stage.act_first + k];
				switch (static_cast<ActKind>(act.kind)) {
					case ActKind::SetFact:
						applied = facts.set(act.a, act.b);
						break;
					case ActKind::AddFact:
						applied = facts.add(act.a, act.b);
						break;
					case ActKind::ClearFact:
						applied = facts.clear(act.a);
						break;
					case ActKind::ConsumeItem:
						applied = items.consume(act.a, act.c,
									static_cast<eng::u8>(act.b));
						break;
					default:
						applied = false;
						break;
				}
			}
			if (!applied) {
				break;
			}
			++advanced;
			const bool last = (steps + 1u >= def.stage_count);
			if (stage.close || last) {
				rt.state[q] = QuestState::Done;
				break;
			}
			rt.stage[q] = static_cast<eng::u8>(cur + 1u);
		}
	}
	rt.seen_epoch = facts.epoch();
	return advanced;
}

} // namespace eng::rpg
