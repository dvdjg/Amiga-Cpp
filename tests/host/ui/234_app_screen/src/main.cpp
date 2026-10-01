// ============================================================================
// Test HOST-234: fachada de juego `eng::App` + `eng::Screen` (eng/api/api.hpp).
// ============================================================================
//
// Valida el borrador del API publico de juego sobre lo que ya existe: `App` junta el bucle,
// la pantalla y las tareas, y el juego se escribe con `init/update/render(App&)` sin ver el
// backend ni `GameContext`/`FramePlan`, importando por la puerta unica `api.hpp`.
// `Screen` es el contexto de dibujo de alto nivel.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/234_app_screen

#include <cstdio>

#include <eng/api/api.hpp>
#include <eng/hw/info.hpp>

using namespace eng;

namespace {

alignas(16) u8 g_chip[512 * 1024];
alignas(16) u8 g_app_chip[128 * 1024];
alignas(16) u8 g_small_chip[16 * 1024];
u8 g_bitmap_background[96u * 64u];

bool pixel_bit(const u8* base, u32 plane_bytes, u16 row_bytes, u8 plane, u16 x, u16 y);

MemoryManager make_memory() {
	MemoryManager mem;
	mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	return mem;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Backend minimo: solo el ciclo que el bucle necesita (`boot` + `wait_vblank`). Sin
/// `execute_frame_plan`: `App::present` lo omite.
struct MockBackend {
	MemoryManager* memory = nullptr;
	bool stopped_while_allocated = false;
	const u16* installed_copper = nullptr;
	const u8* scene_buffer = nullptr;
	u32 scene_plane_bytes = 0u;
	bool camera_pixel = false;
	bool clipped_pixel = false;
	bool bitmap_pixel = false;
	void boot() {}
	void wait_vblank() {}
	void takeover_display(const u16* words) { installed_copper = words; }
	void install_raster(graphics::composition::Scene& scene) {
		scene_buffer = scene.buffer(0u).data();
		scene_plane_bytes = scene.plane_bytes();
	}
	bool execute_frame_plan(const graphics::FramePlan&) {
		if (scene_buffer != nullptr) {
			camera_pixel = !pixel_bit(scene_buffer, scene_plane_bytes, 8u, 0u, 8u, 8u) &&
					pixel_bit(scene_buffer, scene_plane_bytes, 8u, 1u, 8u, 8u);
			clipped_pixel = pixel_bit(scene_buffer, scene_plane_bytes, 8u, 0u, 32u, 8u) &&
					!pixel_bit(scene_buffer, scene_plane_bytes, 8u, 1u, 32u, 8u);
			bitmap_pixel = pixel_bit(scene_buffer, scene_plane_bytes, 8u, 1u, 8u, 8u) &&
				       !pixel_bit(scene_buffer, scene_plane_bytes, 8u, 0u, 8u, 8u);
		}
		return true;
	}
	void stop_display() {
		stopped_while_allocated = memory != nullptr && memory->chip().free_bytes() < memory->chip().capacity();
	}
	template <class F, class P>
	void wait_vblank(F, P) {} // variante con bombeo de fondo (no usada aqui)
};

struct StartGame {
	void init(auto&) {}
	void update(auto&) {}
	void render(auto&) {}
};

struct BackgroundGame {
	bool background_added = false;
	bool invalid_background_rejected = false;
	bool render_called = false;
	bool materialization_ok = false;
	void init(auto& app) {
		invalid_background_rejected =
			!app.add_background("bad-color", 0u, Box {0, 0, 64u, 64u}, 4u).valid() &&
			!app.add_background(nullptr, 0u, Box {0, 0, 64u, 64u}, 1u).valid() &&
			!app.add_background("too-large", 0u, Box {0, 0, 0x8000u, 64u}, 1u).valid();
		const auto front = app.add_background("front", 1u, Box {40, 0, 32u, 64u}, 2u);
		if (front) {
			front->camera().reset(scene::WorldRect {0u, 0u, 128u, 64u}, Size2u {64u, 64u});
			front->camera().set_scroll_x(48u);
		}
		const auto sky = app.add_background("sky", 0u, Box {0, 0, 96u, 64u}, 1u);
		background_added = sky.valid();
		if (sky) {
			sky->camera().reset(scene::WorldRect {0u, 0u, 128u, 64u}, Size2u {64u, 64u});
			sky->camera().set_scroll_x(16u);
		}
	}
	void update(auto&) {}
	void render(auto& app) {
		render_called = true;
		materialization_ok = app.world_materialization_ok();
		(void)app.screen().fill(Box {0, 0, 4u, 4u}, 0u);
		(void)app.screen().fill(Box {0, 0, 4u, 4u}, 3u);
		app.present();
	}
};

struct BitmapBackgroundGame {
	bool bitmap_added = false;
	bool materialization_ok = false;
	void init(auto& app) {
		const auto layer = app.add_bitmap_background(
			"bitmap", 0u, Span<const u8> {g_bitmap_background, sizeof(g_bitmap_background)},
			96u, 64u);
		bitmap_added = layer.valid();
		if (layer) {
			layer->camera().reset(scene::WorldRect {0u, 0u, 96u, 64u}, Size2u {64u, 64u});
			layer->camera().set_scroll_x(16u);
		}
	}
	void update(auto&) {}
	void render(auto& app) {
		materialization_ok = app.world_materialization_ok();
		app.present();
	}
};

/// Juego de prueba con el contrato del API publico (`auto&` = no nombra el tipo del App).
struct TestGame {
	graphics::composition::Scene* scene = nullptr;
	bool inited = false;
	bool updated = false;
	bool rendered = false;
	u32 seen_frame = 0xffffffffu;

	void init(auto& app) {
		inited = true;
		app.bind_scene(*scene);
	}
	void update(auto& app) {
		updated = true;
		seen_frame = app.frame();
		Screen s = app.screen();
		s.clear(0);
		(void)s.fill(Box {0, 0, 4u, 4u}, 1u);
		app.present();
	}
	void render(auto& app) {
		rendered = true;
		(void)app;
	}
};

u32 bits_in_plane(const u8* base, u32 bytes) {
	u32 n = 0;
	for (u32 i = 0; i < bytes; ++i) {
		u8 b = base[i];
		while (b != 0u) {
			n += static_cast<u32>(b & 1u);
			b = static_cast<u8>(b >> 1u);
		}
	}
	return n;
}

bool pixel_bit(const u8* base, u32 plane_bytes, u16 row_bytes, u8 plane, u16 x, u16 y) {
	const u32 offset = static_cast<u32>(plane) * plane_bytes + static_cast<u32>(y) * row_bytes + x / 8u;
	return (base[offset] & static_cast<u8>(0x80u >> (x & 7u))) != 0u;
}

} // namespace

int main() {
	for (usize i = 0u; i < sizeof(g_bitmap_background); ++i) g_bitmap_background[i] = 2u;
	{
		MemoryManager bitmap_mem;
		(void)bitmap_mem.configure(g_app_chip, sizeof(g_app_chip), nullptr, 0u, nullptr, 0u, 16u);
		MockBackend bitmap_backend {};
		bitmap_backend.memory = &bitmap_mem;
		BitmapBackgroundGame bitmap_game {};
		const u32 free_before = bitmap_mem.chip().free_bytes();
		{
			App bitmap_app {bitmap_backend, bitmap_game, bitmap_mem};
			GameDisplay display {};
			display.width = 64u;
			display.height = 64u;
			display.color_depth = 2u;
			display.buffers = 1u;
			(void)bitmap_app.set_display(display);
			check(bitmap_app.start().has_value(), "App compone display para la capa bitmap");
			bitmap_app.run(1u);
		check(bitmap_game.bitmap_added, "World registra y App copia el asset bitmap");
		check(bitmap_game.materialization_ok, "World materializa el bitmap después del scroll");
			check(bitmap_mem.chip().free_bytes() < free_before,
			      "App conserva ownership Chip del asset bitmap durante su vida");
			check(bitmap_backend.scene_buffer != nullptr &&
			      bitmap_backend.bitmap_pixel,
			      "pixel de bitmap del mundo aparece en el viewport tras aplicar cámara");
		}
		check(bitmap_mem.chip().free_bytes() == free_before,
		      "destruir App libera la reserva propietaria del bitmap");
	}

	{
		MemoryManager layer_mem;
		(void)layer_mem.configure(g_app_chip, sizeof(g_app_chip), nullptr, 0u, nullptr, 0u, 16u);
		MockBackend layer_backend {};
		layer_backend.memory = &layer_mem;
		BackgroundGame layer_game {};
		App layer_app {layer_backend, layer_game, layer_mem};
		GameDisplay display {};
		display.width = 64u;
		display.height = 64u;
		display.color_depth = 2u;
		display.buffers = 1u;
		(void)layer_app.set_display(display);
		check(layer_app.start().has_value(), "App compone el display antes de materializar capas Fill");
		layer_app.run(1u);
		check(layer_game.background_added && layer_game.invalid_background_rejected && layer_game.render_called,
		      "World conserva el background y App renderiza la capa antes del juego");
		check(layer_game.materialization_ok,
		      "App materializa background antes del render personalizado del juego");
		check(layer_backend.scene_buffer != nullptr && layer_backend.scene_plane_bytes == 512u,
		      "backend de prueba observa el bitmap compuesto por App");
		check(layer_backend.camera_pixel,
		      "cámara y profundidad ordenan Fill aunque la capa frontal se añada primero");
		check(layer_backend.clipped_pixel, "la capa frontal recortada deja ver el fondo inferior");
	}

	{
		MockBackend missing_memory_backend {};
		StartGame start_game {};
		App missing_memory_app {missing_memory_backend, start_game};
		const auto missing = missing_memory_app.start();
		check(!missing && missing.error() == StartError::MemoryUnavailable,
		      "start sin MemoryManager preconfigurado devuelve error tipado");
		check(!missing_memory_app.screen().valid(), "sin memoria no se expone una pantalla válida");
	}
	{
		MemoryManager invalid_mem;
		(void)invalid_mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
		MockBackend invalid_backend {};
		StartGame start_game {};
		App invalid_app {invalid_backend, start_game, invalid_mem};
		GameDisplay invalid_display {};
		invalid_display.color_depth = 7u;
		check(invalid_app.set_display(invalid_display), "display no válido aún puede editarse");
		const auto invalid = invalid_app.start();
		check(!invalid && invalid.error() == StartError::InvalidDisplay,
		      "start distingue una configuración de display no válida");
		invalid_display.color_depth = 2u;
		invalid_display.width = 64u;
		invalid_display.height = 64u;
		check(invalid_app.set_display(invalid_display), "display puede corregirse tras fallo de validación");
		check(invalid_app.start().has_value(), "App::start puede reintentarse después de corregir el display");
	}

	// App puede poseer y componer su display desde un pool preconfigurado. Al salir del scope
	// libera los bloques de escena; el MemoryManager sigue perteneciendo al composition root.
	{
		MemoryManager app_mem;
		check(app_mem.configure(g_app_chip, sizeof(g_app_chip), nullptr, 0u, nullptr, 0u, 16u),
		      "composition root configura el pool de la App");
		MockBackend app_backend {};
		app_backend.memory = &app_mem;
		StartGame start_game {};
		const u32 free_before = app_mem.chip().free_bytes();
		{
			App start_app {app_backend, start_game, app_mem};
			GameDisplay display {};
			display.width = 64u;
			display.height = 64u;
			display.color_depth = 2u;
			Palette32 palette {};
			palette.color[1] = 0x0f00u;
			display.palette = palette;
			check(start_app.set_display(display), "display se declara antes de start");
			const auto started = start_app.start();
			check(started.has_value(), "App::start compone la escena propia");
			check(app_backend.installed_copper != nullptr,
			      "App::start instala la lista de display cuando el backend admite takeover");
			check(start_app.screen().valid(), "App::start deja Screen ligada a la escena");
			check(start_app.screen().fill(Box {0, 0, 8u, 8u}, 1u),
			      "Screen de la escena propia acepta primitivas de dibujo");
			check(start_app.remaining_chip() < free_before, "la escena propia ocupa el pool Chip");
			check(!start_app.set_display(display), "display queda fijo tras start");
			const auto again = start_app.start();
			check(!again && again.error() == StartError::AlreadyStarted,
			      "start repetido se rechaza de forma explícita");
			start_app.run(1u);
			check(start_app.shutdown_complete() && app_backend.stopped_while_allocated,
			      "run finito apaga display mientras la escena DMA todavía conserva sus buffers");
		}
		check(app_mem.chip().free_bytes() == free_before,
		      "la destrucción de App libera el display propio sin destruir el pool externo");
		check(app_backend.stopped_while_allocated,
		      "App detiene el display antes de liberar los buffers DMA de su escena");
	}
	{
		MemoryManager small_mem;
		(void)small_mem.configure(g_small_chip, sizeof(g_small_chip), nullptr, 0u, nullptr, 0u, 16u);
		MockBackend small_backend {};
		StartGame start_game {};
		App small_app {small_backend, start_game, small_mem};
		GameDisplay oversized {};
		oversized.width = 320u;
		oversized.height = 256u;
		oversized.color_depth = 4u;
		check(small_app.set_display(oversized), "display grande puede describirse durante setup");
		const auto failed = small_app.start();
		check(!failed && failed.error() == StartError::OutOfMemory,
		      "display que supera el presupuesto Chip falla antes de reservar parcialmente");
		check(!small_app.screen().valid(), "fallo de start deja Screen sin escena ligada");
	}

	MemoryManager mem = make_memory();
	graphics::composition::Scene scene {};
	const bool composed = graphics::composition::compose(
		scene, mem, graphics::composition::planar(320, 256, 4),
		graphics::composition::ocs_a500,
		graphics::composition::display(graphics::composition::kPal320x256,
					       graphics::composition::kBplcon0_4Planes));
	check(composed && scene.ok(), "compose() construye la escena");

	// La composicion declara el display vigente en el inventario de hardware (paso 4):
	// `bind_hw_info` publica el modo de la escena con `hw::set_display`.
	hw::HwInfo hw {};
	scene.bind_hw_info(hw);
	check(hw.display.width == 320u && hw.display.height == 256u,
	      "Scene publica el tamano del display");
	check(hw.display.depth == 4u && hw.display.max_colors == 16u,
	      "Scene publica profundidad y colores");
	check(!hw.display.ham && !hw.display.extrahalfbrite, "Scene publica el modo estandar");

	MockBackend backend {};
	TestGame game {};
	game.scene = &scene;
	App app {backend, game};

	app.run(1);

	check(game.inited, "el juego recibe init(App&)");
	check(game.updated, "el juego recibe update(App&)");
	check(game.rendered, "el juego recibe render(App&)");
	check(game.seen_frame == 0u, "app.frame() = 0 en el primer frame");

	// El `fill` de 4x4 color 1 deja 16 bits en el plano 0 y nada en los demas.
	const u8* p0 = scene.buffer(0).data();
	check(p0 != nullptr, "la escena tiene buffer");
	if (p0 != nullptr) {
		check(bits_in_plane(p0, scene.plane_bytes()) == 16u, "Screen::fill: 16 bits en el plano 0");
	}
	const u8* p1 = scene.buffer(0).data() + scene.plane_bytes();
	check(bits_in_plane(p1, scene.plane_bytes()) == 0u, "Screen::fill no toca el plano 1");

	if (failures == 0) {
		std::printf("OK: App/Screen (fachada de juego) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
