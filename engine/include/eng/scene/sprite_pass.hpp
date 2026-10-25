#pragma once

/// \file sprite_pass.hpp
/// **Pasada de sprites conducida por `App`**: interfaz `SpritePass<Backend>` y adaptador
/// `SpriteDirectorPass<Director, Host, Backend>`. **No es una capa visual**: no define
/// z-order ni a qué playfield va un sprite. Es la pieza que decide **cuándo se ejecuta
/// el `SpriteDirector`** y **dónde van sus salidas** antes de que la escena componga.
///
/// ## Qué decide y qué no
///
/// - **Decide** el orden del frame: ejecutar el director (emit o refresh), entregar sus
///   placements de sprite HW al *host* y su plan de BOB/CPU al plan de blits del frame.
/// - **No decide** canales, *attached*, paletas, prioridad frente a playfields ni la
///   representación de un actor (HW/BOB/CPU/playfield). Todo eso lo resuelve
///   `SpriteDirector` con los `SpriteHint` del actor; la pasada solo conduce el
///   resultado.
/// - **No es una capa de render**: un sprite HW puede ir delante o detrás del playfield
///   **por actor** con `SpriteHint::sprite_priority` (mapea a `BPLCON2`). Un BOB se
///   rasteriza en bitplanes y no tiene z-order de capa: es una materialización del
///   actor dentro del `FramePlan`.
///
/// ## Orden del frame
///
/// ```text
/// App::update
///   game.update              (mueve cámara/actores; no publica sprites)
///   pump_sprite_passes()     (esta pasada: emit/refresh -> placements + plan de BOB)
///   pump_scroll_passes()     (scroll -> ejecuta el plan de BOB -> compose -> install)
/// ```
///
/// La pasada se ejecuta **antes** que el scroll para que el host tenga ya los
/// placements y el plan de BOB cuando componga. El host ejecuta el plan de BOB entre
/// el avance del scroll y `compose`; así el BOB cae sobre el campo ya desplazado.
///
/// ## HW, BOB y CPU
///
/// `SpriteDirector::emit` materializa cada actor según su política, los canales
/// disponibles y el presupuesto. La pasada **no distingue** esos casos: recibe
/// `placements()` (sprite HW) y el `FramePlan` (BOB/CPU) y los entrega al host. Si el
/// director degrada un actor a BOB, el host lo dibuja con el Blitter; si lo mantiene
/// como sprite HW, el compositor lo publica en la copperlist.
///
/// ## Nombre
///
/// Se llama **pass** y no **layer** para no confundirla con una capa de playfield ni
/// con `eng::effects::SpriteLayer` (que sí es una capa visual de fondo basada en
/// sprites). La clase la **posee el juego**; el `App` solo la conduce.
///
/// El **host** debe ofrecer:
///   - `sprite_context()` → `eng::scene::ActorEmitContext` con display/anillo/destino;
///   - `set_sprite_placements(eng::Span<const HwSpritePlacement>)`;
///   - `attach_sprite_plan(eng::graphics::FramePlan&)`.
/// `XlimitedScene` implementa ese contrato; otro motor puede implementarlo sin tocar
/// el director ni la lógica del juego.

#include <eng/core/types/types.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/scene/actor_types.hpp>
#include <eng/scene/sprite_director.hpp>

namespace eng::scene {

/// Contrato de una **pasada de sprites** que el `App` arranca y conduce por frame.
/// No representa una capa visual: es el punto de orquestación entre el director de
/// actores y el host que materializa sus placements/BOB.
/// `Backend` es el backend concreto del `App` (no borrado).
template <class Backend>
class SpritePass : public eng::util::Noncopyable {
public:
	constexpr SpritePass() noexcept = default;

	/// **Arranque** (una vez). La pasada no suele reservar: el director y el host los
	/// inicializa el juego. `false` si la pasada no puede arrancar.
	/// \param memory   gestor de memoria del backend (por si la pasada reserva).
	/// \param backend  backend de la plataforma.
	/// \return `false` si la pasada no puede arrancar.
	[[nodiscard]] virtual bool begin(MemoryManager& memory, Backend& backend) = 0;

	/// **Un frame**: compone/refresca los actores y publica placements + plan de BOB en
	/// el host. El `App` la llama antes de conducir las capas de scroll.
	/// \param backend  backend de la plataforma.
	virtual void frame(Backend& backend) = 0;

protected:
	/// Destructor no virtual y protegido: la pasada la **posee el juego** y el `App`
	/// solo la conduce (sin `delete` sobre la base; el engine no usa heap).
	~SpritePass() = default;
};

/// Adaptador `SpriteDirector` → `SpritePass<Backend>` sobre un host con el contrato
/// de `sprite_pass.hpp`. El director es la fachada pública; el host decide cómo se
/// materializan los placements/BOB (p. ej. `XlimitedScene`).
///
/// \tparam Director  fachada con `emit(FramePlan&, const ActorEmitContext&)`,
///                   `refresh_positions(const ActorEmitContext&)` y `placements()`.
///                   Determina la representación (HW/BOB/CPU) y el reparto de canales.
/// \tparam Host      receptor de placements y del plan de BOB; debe sobrevivir a la
///                   pasada. Implementa `sprite_context`, `set_sprite_placements` y
///                   `attach_sprite_plan`.
/// \tparam Backend   backend concreto del `App`; solo ata la interfaz `SpritePass`.
template <class Director, class Host, class Backend>
class SpriteDirectorPass final : public SpritePass<Backend> {
	static_assert(requires(Host& h, eng::Span<const eng::graphics::HwSpritePlacement> p,
			       eng::graphics::FramePlan& plan) {
		h.sprite_context();
		h.set_sprite_placements(p);
		h.attach_sprite_plan(plan);
	}, "SpriteDirectorPass: Host debe exponer sprite_context/set_sprite_placements/attach_sprite_plan");

public:
	/// Liga el director y su host. Ambos los posee el juego y deben sobrevivir a la pasada.
	/// \param director  fachada de actores ya configurada.
	/// \param host      escena/destino que recibe placements y BOB.
	SpriteDirectorPass(Director& director, Host& host) noexcept
		: m_director(director), m_host(host) {}

	/// El director y el host ya están arrancados por el juego; no reserva nada.
	[[nodiscard]] bool begin(MemoryManager&, Backend&) noexcept override {
		m_initialized = false;
		m_force_emit = true;
		return true;
	}

	/// Conduce la pasada de un frame:
	/// 1. pide el contexto al host;
	/// 2. primera vez o tras `request_emit`: `emit` completo;
	/// 3. si no, intenta `refresh_positions` (posiciones + DATA animada) y cae a
	///    `emit` si la escena dejó de ser simple estable (BOB, CPU, plantilla o
	///    cambio de ancho de frame);
	/// 4. entrega placements y plan de BOB al host.
	/// \param backend  backend del `App` (la pasada no lo usa: el host materializa).
	void frame(Backend&) noexcept override {
		const eng::scene::ActorEmitContext ctx = m_host.sprite_context();
		m_plan.clear();
		if (!m_initialized || m_force_emit) {
			m_report = m_director.emit(m_plan, ctx);
			m_initialized = true;
			m_force_emit = false;
			++m_emit_count;
		} else if (!m_director.refresh_positions(ctx)) {
			m_report = m_director.emit(m_plan, ctx);
			++m_emit_count;
		} else {
			++m_refresh_count;
		}
		m_host.set_sprite_placements(m_director.placements());
		m_host.attach_sprite_plan(m_plan);
	}

	/// Pide una **emisión completa** en el siguiente frame (alta/baja de actores,
	/// cambio de contenido o de canal). El movimiento normal no lo necesita.
	void request_emit() noexcept { m_force_emit = true; }

	/// Último reporte del director (válido tras el primer `frame`).
	[[nodiscard]] const eng::scene::SpriteLoadReport& last_report() const noexcept {
		return m_report;
	}
	/// Nº de frames con **emisión completa** (emit) desde el arranque.
	[[nodiscard]] eng::u16 emit_count() const noexcept { return m_emit_count; }
	/// Nº de frames servidos por el camino rápido `refresh_positions`.
	[[nodiscard]] eng::u16 refresh_count() const noexcept { return m_refresh_count; }

private:
	Director& m_director; ///< fachada de actores (no propietaria)
	Host& m_host;         ///< receptor de placements/BOB (no propietario)
	eng::graphics::FramePlan m_plan {}; ///< plan de BOB del frame (se limpia tras ejecutarlo)
	eng::scene::SpriteLoadReport m_report {}; ///< copia del último reporte
	eng::u16 m_emit_count = 0u;   ///< frames con emit completo
	eng::u16 m_refresh_count = 0u; ///< frames por refresh_positions
	bool m_initialized = false; ///< `true` tras el primer `emit`
	bool m_force_emit = false;  ///< pide `emit` aunque `refresh` pudiera servir
};

} // namespace eng::scene
