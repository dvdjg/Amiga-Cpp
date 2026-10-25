#pragma once

/// \file sprite_layer.hpp
/// **Capa de sprites conducida por `App`**: interfaz `SpriteLayer<Backend>` y adaptador
/// `SpriteDirectorLayer<Director, Host, Backend>`. El juego declara su `SpriteDirector`
/// y un *host* que recibe los placements (`XlimitedScene`), registra la capa con
/// `App::add_sprite_layer` y deja de llamar al motor de scroll para publicar sprites.
///
/// El `App` conduce las capas de sprites **antes** que las de scroll: el host recibe
/// los placements y el plan de BOB del frame, y la capa de scroll los materializa al
/// componer (los BOB se ejecutan entre el scroll y `compose`). Una llamada virtual por
/// capa y frame, fuera de los bucles por píxel.
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

/// Contrato de una capa de sprites que el `App` arranca y conduce por frame.
/// `Backend` es el backend concreto del `App` (no borrado).
template <class Backend>
class SpriteLayer : public eng::util::Noncopyable {
public:
	constexpr SpriteLayer() noexcept = default;

	/// **Arranque** (una vez). El director y el host los inicializa el juego; una capa
	/// sin recursos propios devuelve `true`. `false` si el arranque falla.
	/// \param memory   gestor de memoria del backend (por si la capa reserva).
	/// \param backend  backend de la plataforma.
	/// \return `false` si la capa no puede arrancar.
	[[nodiscard]] virtual bool begin(MemoryManager& memory, Backend& backend) = 0;

	/// **Un frame**: compone/refresca los actores y publica placements + plan de BOB en
	/// el host. Se llama antes de las capas de scroll.
	/// \param backend  backend de la plataforma.
	virtual void frame(Backend& backend) = 0;

protected:
	/// Destructor no virtual y protegido: la capa la **posee el juego** y el `App` solo
	/// la conduce (sin `delete` sobre la base; el engine no usa heap).
	~SpriteLayer() = default;
};

/// Adaptador `SpriteDirector` → `SpriteLayer<Backend>` sobre un host con el contrato
/// de `sprite_layer.hpp`. El director es la fachada pública; el host decide cómo se
/// materializan los placements/BOB (p. ej. `XlimitedScene`).
///
/// \tparam Director  fachada con `emit(FramePlan&, const ActorEmitContext&)`,
///                   `refresh_positions(const ActorEmitContext&)` y `placements()`.
/// \tparam Host      receptor de placements y del plan de BOB; debe sobrevivir a la capa.
/// \tparam Backend   backend concreto del `App`.
template <class Director, class Host, class Backend>
class SpriteDirectorLayer final : public SpriteLayer<Backend> {
	static_assert(requires(Host& h, eng::Span<const eng::graphics::HwSpritePlacement> p,
			       eng::graphics::FramePlan& plan) {
		h.sprite_context();
		h.set_sprite_placements(p);
		h.attach_sprite_plan(plan);
	}, "SpriteDirectorLayer: Host debe exponer sprite_context/set_sprite_placements/attach_sprite_plan");

public:
	/// Liga el director y su host. Ambos los posee el juego y deben sobrevivir a la capa.
	/// \param director  fachada de actores ya configurada.
	/// \param host      escena/destino que recibe placements y BOB.
	SpriteDirectorLayer(Director& director, Host& host) noexcept
		: m_director(director), m_host(host) {}

	/// El directo y el host ya están arrancados por el juego; no reserva nada.
	[[nodiscard]] bool begin(MemoryManager&, Backend&) noexcept override {
		m_initialized = false;
		m_force_emit = true;
		return true;
	}

	/// Compone el frame: primera vez (o tras `request_emit`) hace `emit`; después
	/// intenta `refresh_positions` (posiciones + DATA animada) y cae a `emit` si la
	/// escena dejó de ser simple estable (BOB, CPU, plantilla o cambio de ancho).
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
