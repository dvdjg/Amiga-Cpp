// Demo 081 - Tareas de fondo cooperativas (eng::task::BackgroundQueue).
//
// Demuestra el trabajo de fondo del engine: un proceso "pesado" rellena una barra
// progresiva mientras el bucle principal sigue animando la pantalla a 50 fps. La
// tarea NO bloquea el frame: el engine la drena en el hueco de VBlank (prioridad al
// bucle principal) y solo cuando el CPU estaria esperando.
//
//   - Display: 4 planos 320x256 (driver `HamScene`, sin cuadruplicado).
//   - Fondo (COLOR00): lo pulsa el bucle principal por CPU cada frame -> prueba viva
//     de que el juego sigue corriendo (la copperlist NO toca COLOR00).
//   - Barra (COLOR01, blanco): la rellena la tarea de fondo fila a fila. Su longitud
//     es el progreso (`progress().permille`), expuesto tambien en runStatus.detail.
//   - La tarea se adapta al barrido del CRT: si el raster ya va tarde (vpos > 220),
//     procesa la mitad por rebanada.
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/drivers/ham_scene.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/task/background.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

namespace amiga = eng::amiga;
namespace drivers = eng::graphics::drivers;
namespace task = eng::task;

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u16 kBytesPerRow = kWidth / 8; // 40
constexpr eng::u8 kPlanes = 4;

/// La barra de progreso ocupa una banda central, para que el fondo pulsado (bucle
/// principal) siga siendo visible a su alrededor.
constexpr eng::u16 kBarTop = 96;
constexpr eng::u16 kBarRows = 64;

/// Banda donde el bucle principal dibuja por **hardware** una linea que barre (prueba
/// de que sigue vivo y, ademas, provoca esperas de Blitter donde el engine drena las
/// tareas de fondo via `set_blitter_service`).
constexpr eng::u16 kLineBandTop = 176;
constexpr eng::u16 kLineBandRows = 24;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;

/// Paleta: COLOR00 lo controla el bucle principal (no la lista); COLOR01 = blanco
/// (la barra); COLOR02 = amarillo (la linea). El resto a negro.
constexpr eng::u16 kPalette[32] = {
	0x0000u, 0x0fffu, 0x0ff0u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
	0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
	0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
	0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
};

/// Ramps para pulsar el fondo (COLOR00) desde el bucle principal.
constexpr eng::u16 kRainbow[16] = {
	0x000u, 0x002u, 0x013u, 0x024u, 0x035u, 0x046u, 0x057u, 0x068u,
	0x069u, 0x06au, 0x06bu, 0x05cu, 0x04du, 0x03eu, 0x02fu, 0x01du,
};

/// Estado de la tarea de fondo: rellena la barra fila a fila.
struct FillTask {
	eng::u8* plane = nullptr;
	eng::u16 row = 0;
};

/// Rutina de fondo: rellena hasta `budget` filas (o la mitad si el raster va tarde)
/// y devuelve las filas hechas (1 fila = 1 unidad).
eng::u16 fill_bar_step(void* data, const task::TaskSlice& slice) {
	auto* t = static_cast<FillTask*>(data);
	const eng::u16 cap = (slice.vpos > 220u) ? static_cast<eng::u16>(slice.budget_units / 2u) : slice.budget_units;
	eng::u16 done = 0;
	while (done < cap && t->row < kBarRows) {
		eng::u8* p = t->plane + static_cast<eng::u32>(kBarTop + t->row) * kBytesPerRow;
		for (eng::u16 i = 0; i < kBytesPerRow; ++i) {
			p[i] = 0xffu;
		}
		++t->row;
		++done;
	}
	return done;
}

struct BackgroundDemo {
	void init(amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008101u);
			return;
		}

		drivers::HamSceneConfig cfg {};
		cfg.planes = kPlanes;
		cfg.rows = kHeight;
		cfg.bytes_per_row = kBytesPerRow;
		cfg.bplcon0 = 0x4200u;     // 4 planos, sin modos especiales
		cfg.row_repeat = 1u;       // sin cuadruplicado
		cfg.first_line = 0x2cu;
		cfg.bplcon1_shift = 0u;
		cfg.palette = kPalette;
		cfg.palette_first = 1u;    // NO tocar COLOR00: lo pulsa el bucle principal
		cfg.palette_count = 15u;
		if (!m_scene.init(backend.memory(), cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008102u);
			return;
		}

		m_plane0 = m_scene.plane(0);
		m_plane1 = m_scene.plane(1);
		for (eng::u32 i = 0; i < static_cast<eng::u32>(kBytesPerRow) * kHeight; ++i) {
			m_plane0[i] = 0u;
		}
		m_scene.takeover(backend);

		// Proceso de fondo: la barra progresa fila a fila. El engine lo drena en el
		// hueco de VBlank; el bucle principal sigue teniendo prioridad.
		m_fill.plane = m_plane0;
		m_fill.row = 0;
		if (context.background != nullptr) {
			m_task = context.background->add(&fill_bar_step, &m_fill, kBarRows, /*slice*/ 1u);
			context.background->set_max_slices_per_frame(1u);
		}

		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x0081u);
	}

	void update(amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) return;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);

		// Bucle principal vivo: pulsa el fondo (COLOR00) por CPU cada frame. El Copper
		// no toca COLOR00, asi que el valor persiste hasta el frame siguiente.
		m_hue = static_cast<eng::u8>((m_hue + 1u) & 0x0fu);
		backend.set_color(0, kRainbow[m_hue]);

		// Elemento animado del bucle principal, dibujado por HW: limpia una banda y
		// traza una linea que baja. Ambos blits esperan al Blitter, y ahi el engine
		// drena las tareas de fondo (`set_blitter_service`) sin parar el juego.
		backend.blitter_clear(m_plane1 + static_cast<eng::u32>(kLineBandTop) * kBytesPerRow,
				      1u, kBytesPerRow, kPlaneBytes, kWidth, kLineBandRows);
		const eng::s16 y = static_cast<eng::s16>(kLineBandTop + (m_line_y % kLineBandRows));
		backend.blitter_line(m_plane1, kBytesPerRow, 0, y, static_cast<eng::s16>(kWidth - 1), y);
		m_line_y = static_cast<eng::u16>((m_line_y + 2u) % kLineBandRows);

		if (context.background == nullptr) return;
		const task::TaskProgress p = context.background->progress(m_task);
		// Evidencia por canal lateral: progreso (permille) + frame.
		g_eng_run_status.detail =
			(static_cast<eng::u32>(p.permille) << 16) | (context.frame.frame_index & 0xffffu);

		if (p.finished()) {
			// Terminado: libera el slot (estaba en `Done`) y reinicia la barra para
			// mostrar el ciclo de nuevo.
			context.background->cancel(m_task);
			auto* band = m_plane0 + static_cast<eng::u32>(kBarTop) * kBytesPerRow;
			for (eng::u32 i = 0; i < static_cast<eng::u32>(kBytesPerRow) * kBarRows; ++i) {
				band[i] = 0u;
			}
			m_fill.row = 0;
			m_task = context.background->add(&fill_bar_step, &m_fill, kBarRows, 1u);
		}
	}

	void render(amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	drivers::HamScene m_scene {};
	eng::u8* m_plane0 = nullptr;
	eng::u8* m_plane1 = nullptr;
	FillTask m_fill {};
	task::TaskHandle m_task {};
	eng::u8 m_hue = 0;
	eng::u16 m_line_y = 0;
	bool m_init_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	amiga::MinimalBackend backend {};
	BackgroundDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff);

	return 0;
}
