#pragma once

#include <amg/graphics/driver.hpp>

namespace amg {

struct GameContext {
	FrameStats frame {};
};

template <typename Game, typename Backend>
concept GameModule = requires(Game game, Backend& backend, GameContext& context) {
	game.init(backend, context);
	game.update(backend, context);
	game.render(backend, context);
};

template <typename Backend, typename Game>
requires GameModule<Game, Backend>
class Engine {
public:
	constexpr Engine(Backend& backend, Game& game)
		: m_backend(backend), m_game(game) {}

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

