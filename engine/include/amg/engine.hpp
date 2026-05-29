#pragma once

/// \file engine.hpp
/// Bucle principal generico del engine.
///
/// Esta cabecera demuestra la arquitectura que buscamos: la logica del juego no
/// conoce el Amiga directamente. El juego habla con un `Backend`, y el backend puede
/// ser Amiga, Mega Drive, Neo Geo o una version PC de herramientas.
///
/// Por ahora el bucle es intencionadamente pequeno. A medida que crezca incorporara
/// fases explicitas: input, update fijo, preparacion de render, blitter jobs,
/// copper commit, sprites, audio y profiler.

#include <amg/graphics/driver.hpp>

namespace amg {

/// Contexto mutable de juego por frame.
///
/// No debe contener ownership pesado. Es el paquete de estado que el engine pasa a
/// `init`, `update` y `render`.
struct GameContext {
	FrameStats frame {};
};

/// Contrato minimo que debe cumplir una demo/juego.
///
/// Usamos un concept de C++23 para obtener abstraccion sin coste runtime: si un tipo
/// no tiene `init`, `update` y `render`, el error aparece en compilacion.
template <typename Game, typename Backend>
concept GameModule = requires(Game game, Backend& backend, GameContext& context) {
	game.init(backend, context);
	game.update(backend, context);
	game.render(backend, context);
};

/// Engine generico parametrizado por backend y juego.
///
/// Esta clase es el primer paso para evitar que el juego sea "codigo Amiga". El
/// backend decide como esperar VBlank, reservar memoria o dibujar; el juego decide
/// que quiere que ocurra.
template <typename Backend, typename Game>
requires GameModule<Game, Backend>
class Engine {
public:
	constexpr Engine(Backend& backend, Game& game)
		: m_backend(backend), m_game(game) {}

	/// Ejecuta un numero fijo de frames.
	///
	/// Las demos actuales acaban tras `frame_count` para que el runner pueda capturar
	/// y cerrar WinUAE de forma determinista. Los juegos reales tendran un bucle
	/// controlado por estado/salida.
	void run_frames(u16 frame_count) {
		GameContext context {};

		m_backend.boot();
		m_game.init(m_backend, context);

		for (u16 i = 0; i < frame_count; ++i) {
			context.frame.frame_index = i;
			m_game.update(m_backend, context);
			m_game.render(m_backend, context);
			m_backend.wait_vblank();
		}
	}

private:
	Backend& m_backend;
	Game& m_game;
};

} // namespace amg
