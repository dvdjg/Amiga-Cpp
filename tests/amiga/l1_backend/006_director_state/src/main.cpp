// ============================================================================
// L1-006 - Persistencia de estado de SpriteDirector (sonda canónica)
// ============================================================================
//
// Sonda estática y determinista: da de alta un visual y ocho entidades, comprueba
// que `count()` sigue siendo 8 después de la inicialización y que los canarios
// alrededor del director siguen intactos. Publica el resultado en `detail`:
//
//   bits  0-7 : 1 = todas las comprobaciones OK
//   bits  8-15: entidades vivas (debe ser 8)
//   bits 16-31: 0xBEEF (magic de la sonda)
//
// No dibuja actores ni emite sprites: aísla el alta y la contabilidad. Para
// depurar direcciones en WinUAE, no calcular `.data` desde `baseText`: leer
// `state.sections` del canal lateral y resolver el símbolo contra la base de
// `.data`. Ver README.md.

#include <eng/api/api.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/scene/sprite_director.hpp>

namespace {

using Director = eng::scene::SpriteDirector<16u, 16u, 4u, 8u, 32u, 16u>;

constexpr eng::u16 kActors = 8u;
constexpr eng::u32 kBefore = 0x13572468u;
constexpr eng::u32 kAfter = 0x24681357u;
constexpr eng::u32 kMagic = 0xdeadbeefu;
constexpr eng::u32 kReadyDetail = 0xbeef0000u | (kActors << 8u) | 1u;

// 16x16 a un plano: 16 palabras (una por línea). El alta del actor solo valida
// que la vista tenga píxeles y dimensiones; no se compone ningún frame.
eng::u16 g_pixels[16] {};

/// Canarios a ambos lados del director: si un array interno se desborda, cambian
/// antes de que el contador pueda parecer "reseteado".
struct GuardedDirector {
	eng::u32 before = kBefore;
	Director director {};
	eng::u32 after = kAfter;
	eng::u32 magic = kMagic;
};

GuardedDirector g_guarded {};

struct DirectorStateProbe {
	void init(auto& app) {
		(void)app;
		bool ok = g_guarded.before == kBefore && g_guarded.after == kAfter &&
			  g_guarded.magic == kMagic;

		eng::graphics::Visual visual {};
		visual.pixels = eng::Span<const eng::u16> {g_pixels, 16u};
		visual.w = 16u;
		visual.h = 16u;
		visual.bitplanes = 1u;
		const eng::scene::VisualId visual_id = g_guarded.director.add_visual(visual);
		ok = ok && visual_id.valid();

		eng::scene::SpriteDesc desc {};
		desc.content = eng::scene::SpriteVisualRef::simple(visual_id);
		desc.hint.policy = eng::scene::SpritePolicy::Sprite;
		desc.x = 16;
		desc.y = 32;
		for (eng::u16 i = 0u; i < kActors; ++i) {
			desc.x = static_cast<eng::s16>(16u + i * 24u);
			const eng::scene::SpriteId id = g_guarded.director.add(desc);
			ok = ok && id.valid();
		}
		ok = ok && g_guarded.director.count() == kActors;
		ok = ok && g_guarded.before == kBefore && g_guarded.after == kAfter &&
		     g_guarded.magic == kMagic;

		if (!ok) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		eng::amiga::mark_ready(kReadyDetail);
	}

	void update(auto&) {}
	void render(auto&) {}
};

constexpr eng::Palette32 kPalette {{
	0x000u, 0xfffu,
}};

} // namespace

int main() {
	eng::GameDisplay display {};
	display.width = 320u;
	display.height = 256u;
	display.color_depth = 1u;
	display.buffers = 1u;
	display.palette = kPalette;
	constexpr eng::MemoryConfig memory {64u * 1024u, 4u * 1024u, 4u * 1024u};
	DirectorStateProbe game {};
	return eng::amiga::run_demo(game, display, memory, eng::amiga::VblankWait::Active);
}
