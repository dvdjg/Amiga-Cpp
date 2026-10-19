#pragma once

/// \file verb.hpp
/// **Verbos de la interfaz de aventura** (`eng::adv`): el verbo es **contenido**, no código.
/// El juego construye su `VerbSet` en la carga (etiqueta, semántica, aridad y cursor por
/// verbo) y el runtime solo lo consulta: cambiar la barra de verbos no recompila el motor.
/// Diseño: `docs/engine/architecture/ADVENTURE_LAYER.md` §Verbos; hito A1 de
/// `docs/guides/roadmap/ROADMAP_ADVENTURE.md`.
///
/// Glosario del flujo de interacción (el orden importa y `pick_pointer` lo hace explícito):
///
/// ```text
///   puntero --> GUI (barra de verbos / inventario)  -> HUD consume el click
///           \-> hotspot de sala (por PROFUNDIDAD)   -> se construye la frase
///           \-> suelo (walkbox)                     -> walk-to del actor
///
///   frase en construccion (AdvAction)  --resolve_action-->  ResolvedAction
///     "Usar LLAVE con PUERTA"              (aridad valida)      semantica + sujetos
/// ```
///
/// `resolve_action` valida la **aridad** (qué objetos pide el verbo) y normaliza la
/// semántica; el **enrutado al kernel** (`rpg::pick_up`/`drop_item`/`LockSet::try_open`…) es
/// una tabla pequeña en el pegamento del juego: el motor no conoce el mundo.

#include <eng/adv/hotspot.hpp>
#include <eng/adv/types.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::adv {

/// Identificador de verbo dentro de un `VerbSet` (índice de la tabla de contenido).
using VerbId = eng::u8;

/// Verbo inválido (no existe en la tabla).
inline constexpr VerbId kInvalidVerb = 0xffu;

/// Sin objeto asignado en un slot de la frase (sujeto u objetivo vacío).
inline constexpr eng::u8 kNoObject = 0xffu;

/// **Semántica** del verbo: lo que hace. La etiqueta visible («Mira», «Usa», «Coge») es
/// contenido y no forma parte de la semántica.
enum class VerbSemantic : eng::u8 {
	WalkTo, ///< ir al objetivo (o al suelo)
	Look,   ///< mirar
	Take,   ///< coger (candidato a `rpg::pick_up`)
	Talk,   ///< hablar
	Use,    ///< usar sujeto **con** objetivo (`rpg::item_tags`/cerraduras…)
	Give,   ///< dar sujeto a objetivo
	Open,   ///< abrir (cerraduras)
	Close,  ///< cerrar
	Push,   ///< empujar
	Pull,   ///< tirar
};

/// **Aridad**: objetos que la frase debe tener para que el verbo se pueda ejecutar.
enum class VerbArity : eng::u8 {
	Target,     ///< un objetivo (`LOOK AT door`): el sujeto va vacío
	TwoObjects, ///< sujeto + objetivo (`USE key WITH door`: «Usar», «Dar»)
};

/// Modo del cursor mientras el verbo está seleccionado (la máquina de puntero del juego
/// elige arte/color por modo). `Default` es el puntero sin verbo.
enum class CursorMode : eng::u8 {
	Default,
	Walk,
	Look,
	Use,
	Talk,
	Exit, ///< salida de sala (el contenido lo asigna al verbo de salida)
};

/// Fila de la tabla de verbos (contenido).
struct Verb {
	const char* label;         ///< etiqueta de la barra (ASCII/LATIN-1, como el HUD)
	VerbSemantic semantic;     ///< qué hace
	VerbArity arity;           ///< objetos que pide
	CursorMode cursor;         ///< modo de cursor con el verbo seleccionado
};

/// Tabla de verbos de capacidad fija (contenido; sin heap, patrón del engine).
template <eng::u8 MaxVerbs>
class VerbSet {
	static_assert(MaxVerbs >= 1u, "VerbSet: capacidad de al menos un verbo");

public:
	/// Registra un verbo; devuelve su id o `kInvalidVerb` si la tabla está llena.
	[[nodiscard]] VerbId add(const char* label, VerbSemantic semantic, VerbArity arity,
				 CursorMode cursor) noexcept {
		if (m_count >= MaxVerbs) {
			return kInvalidVerb;
		}
		m_verbs[m_count] = Verb {label, semantic, arity, cursor};
		const VerbId id = m_count;
		++m_count;
		return id;
	}

	/// ¿`id` apunta a un verbo registrado?
	[[nodiscard]] bool valid(VerbId id) const noexcept { return id < m_count; }

	/// Verbo `id` (precondición: `valid(id)`).
	[[nodiscard]] const Verb& at(VerbId id) const noexcept { return m_verbs[id]; }

	/// Primer verbo con la semántica `s`, o `kInvalidVerb` (contenido con dos verbos de la
	/// misma semántica: gana el primero).
	[[nodiscard]] VerbId find(VerbSemantic s) const noexcept {
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (m_verbs[i].semantic == s) {
				return i;
			}
		}
		return kInvalidVerb;
	}

	/// Modo de cursor del verbo `id` (`Default` si no es válido).
	[[nodiscard]] CursorMode cursor_for(VerbId id) const noexcept {
		// La doble cota (`MaxVerbs` + `m_count`) deja el índice probadamente dentro del array.
		if (id >= MaxVerbs || id >= m_count) {
			return CursorMode::Default;
		}
		return m_verbs[id].cursor;
	}

	/// Verbos registrados.
	[[nodiscard]] eng::u8 size() const noexcept { return m_count; }

private:
	Verb m_verbs[MaxVerbs] {}; ///< tabla (0..m_count-1 válidas)
	eng::u8 m_count = 0u;      ///< verbos registrados
};

/// **Frase en construcción** (la «sentence» clásica): verbo + sujeto + objetivo. Sujeto y
/// objetivo son ids de objeto del juego (`kNoObject` = slot vacío).
struct AdvAction {
	VerbId verb {kInvalidVerb};
	eng::u8 subject {kNoObject}; ///< objeto del inventario (para «Usar»/«Dar»)
	eng::u8 target {kNoObject};  ///< hotspot de sala objetivo

	/// ¿La frase está lista para `resolve_action`? (atajo de presentación; la validación
	/// canónica la hace el resolver contra la aridad del verbo).
	[[nodiscard]] constexpr bool complete() const noexcept {
		return verb != kInvalidVerb && target != kNoObject;
	}
};

/// Acción resuelta: semántica normalizada + sujetos a pasar al pegamento del juego.
struct ResolvedAction {
	VerbSemantic semantic {VerbSemantic::Look};
	eng::u8 subject {kNoObject};
	eng::u8 target {kNoObject};
};

/// Valida la frase contra la tabla y la normaliza. Reglas de aridad:
///   - `TwoObjects` («Usar», «Dar»): exige **sujeto y objetivo**.
///   - `Target`: exige objetivo y **sujeto vacío** (un verbo de un objetivo se ejecuta sin
///     objeto previo en la mano; si el jugador venía de «Usar», el sujeto se limpia antes).
/// \return `false` si el verbo no existe o la aridad no se cumple (`out` no se toca).
template <eng::u8 MaxVerbs>
[[nodiscard]] constexpr bool resolve_action(const VerbSet<MaxVerbs>& set, AdvAction action,
					    ResolvedAction& out) noexcept {
	if (!set.valid(action.verb)) {
		return false;
	}
	const Verb& v = set.at(action.verb);
	if (v.arity == VerbArity::TwoObjects) {
		if (action.subject == kNoObject || action.target == kNoObject) {
			return false;
		}
	} else {
		if (action.subject != kNoObject || action.target == kNoObject) {
			return false;
		}
	}
	out.semantic = v.semantic;
	out.subject = action.subject;
	out.target = action.target;
	return true;
}

/// Destino del puntero tras aplicar la **prioridad** de la sala: GUI (barra de verbos /
/// inventario) → hotspot de sala (el más al frente) → suelo.
enum class PointerKind : eng::u8 {
	Gui,     ///< el HUD/barra consume la posición
	Hotspot, ///< objeto de sala (`PointerHit::id` válido)
	Floor,   ///< suelo: walk-to por walkbox
};

/// Resultado del hit-test de puntero.
struct PointerHit {
	PointerKind kind {PointerKind::Floor};
	HotspotId id {kInvalidHotspot}; ///< solo válido con `kind == Hotspot`
};

/// Clasifica el punto `p` (coordenadas de sala) con la prioridad de la figura del archivo.
/// `over_gui` lo decide el HUD (su `hud_hit_test` con las regiones de la franja) y se
/// consulta **primero**: una barra de verbos tapa la sala aunque debajo haya un hotspot.
/// \param over_gui ¿la posición cae sobre una región interactiva del HUD?
/// \param hotspots hotspots de la sala (orden de datos: frente al final).
/// \param p posición en coordenadas de sala.
template <class S>
[[nodiscard]] constexpr PointerHit pick_pointer(bool over_gui,
						eng::Span<const Hotspot<S>> hotspots,
						eng::math::Vec<2, S> p) noexcept {
	if (over_gui) {
		return PointerHit {PointerKind::Gui, kInvalidHotspot};
	}
	const HotspotId h = hit_test(hotspots, p);
	if (h != kInvalidHotspot) {
		return PointerHit {PointerKind::Hotspot, h};
	}
	return PointerHit {PointerKind::Floor, kInvalidHotspot};
}

} // namespace eng::adv
