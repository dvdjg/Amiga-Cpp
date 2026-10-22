#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/vblank_mode.hpp>

#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include <inline/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <dos/dosextens.h> // struct Process (pr_CLI): deteccion de "bajo SO vivo" (212)
#include <eng/debug/prof.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/hw/info.hpp>


#include "amiga_internal.hpp"

// Base de `graphics.library` para los stubs inline de `proto/graphics.h` (la toma de control
// la abre en `takeover_system` y la cierra en `restore_system`). Enlace C, como espera el stub.
struct GfxBase* GfxBase = nullptr;

// Contadores de diagnóstico del camino IRQ (visibles en el `.map` y legibles por el
// canal lateral sin GDB). Inicializados a un valor no nulo para que vivan en `.data` y el
// mapa publique su dirección. Coste: un incremento por VBlank.
extern "C" {
[[gnu::used]] volatile eng::u32 g_irq_diag_vbl_calls = 0x1000u;
[[gnu::used]] volatile eng::u32 g_irq_diag_vbl_adds = 0x2000u;
[[gnu::used]] volatile eng::u32 g_irq_diag_seq_value = 0x3000u;
}

using namespace eng::amiga::detail;

// Entradas de los servidores de exec (support/level3_irq.s). El stub garantiza el retorno
// con Z activo, que es obligatorio para VERTB y evita que el lenguaje de alto nivel lo pierda.
extern "C" void eng_vbl_server_entry();
extern "C" void eng_blit_server_entry();

// Definiciones fuera de linea del bloque de perfilado (declarado en `eng/debug/prof.hpp`):
// asi el `.map` publica una direccion final que `tools/debug/profile.mjs` puede resolver.
namespace eng::debug {
// Inicializador NO nulo a propósito: así el bloque vive en `.data` y el `.map` publica su
// dirección (en `.bss` el mapa de este toolchain no lista direcciones de símbolo y
// `tools/debug/profile.mjs` no lo resuelve). Enlace C, como `g_eng_run_status`, para que el
// mapa publique el nombre sin manglar. `ENG_PROF_INIT` lo reinicializa igualmente.
extern "C" {
[[gnu::used]] volatile ProfBlock g_eng_prof {
	prof_magic, 0u, {0u, 0u, 0u}, 0u, {}, {}, {}, {},
};
[[gnu::used]] volatile eng::u32 g_prof_start[prof_max_sections] {1u};
}
} // namespace eng::debug

namespace eng::amiga {

// Declaracion anticipada: `wait_vblank_run` (arriba) re-arma VERTB con ella.
void level3_sync();

namespace {
/// Etiqueta un bloque por su **direccion** (`hw::classify_region`), no por el flag de `AllocMem`:
/// en un A1200, `AllocMem(MEMF_ANY)` puede caer en Fast RAM (que no es Slow). Frontera: se
/// convierte el puntero a direccion solo para clasificar.
eng::MemoryKind mem_kind_of(const void* p, bool asked_chip, bool asked_fast) {
	if (p == nullptr) {
		return eng::MemoryKind::Any;
	}
	const eng::hw::MemRegionKind k = eng::hw::classify_region(
		asked_chip, asked_fast, static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(p)));
	switch (k) {
	case eng::hw::MemRegionKind::Chip: return eng::MemoryKind::Chip;
	case eng::hw::MemRegionKind::Fast: return eng::MemoryKind::Fast;
	case eng::hw::MemRegionKind::Slow: return eng::MemoryKind::Slow;
	default: return eng::MemoryKind::Any;
	}
}

/// Estado de la toma de control del sistema (ver `takeover_system`).
/// Vive aquí, antes de los servicios de IRQ, para que `level3_sync` sepa si el SO sigue
/// siendo el dueño del vector y de `INTENA` y haya que usar `AddIntServer` en vez del vector
/// crudo (familia 212/DT-011).
struct SystemTakeoverState {
	bool active = false;
	bool forbidden = false;
	struct View* old_view = nullptr;
	eng::u16 old_intena = 0u;
	eng::u16 old_dmacon = 0u;
	eng::u16 old_adkcon = 0u;
};
SystemTakeoverState g_sys {};

/// Modo cooperativo: el SO conserva el display y el autovector de nivel 3; el engine solo
/// encadena sus servidores con `AddIntServer` y duerme en `Wait()`. No se escribe `INTENA`
/// (exec ya la mantiene) ni se hace takeover. Ver `vblank_mode.hpp`.
bool g_cooperative_servers = false;

/// Servidores de exec y sus nodos (estáticos: `AddIntServer` conserva el puntero).
/// Bajo AmigaDOS el SO es dueño del autovector de nivel 3 y de `INTENA`; los handlers se
/// encadenan con `AddIntServer` y reciben `A1=is_Data` (aquí no se usa: el contexto vive en
/// `g_vbl_task`/`g_blit_task`). Ver Autodocs de `AddIntServer`.
struct ExecIrqServers {
	struct Interrupt vbl {};
	struct Interrupt blit {};
	bool vbl_added = false;
	bool blit_added = false;
};
ExecIrqServers g_exec_servers {};
char g_vbl_server_name[] = "eng vblank";
char g_blit_server_name[] = "eng blit";

/// Inicializa un nodo de servidor con prioridad dentro de su cadena.
void init_server(struct Interrupt& node, char* name, void (*code)(), eng::s8 pri) {
	node.is_Node.ln_Type = NT_INTERRUPT;
	node.is_Node.ln_Pri = pri;
	node.is_Node.ln_Name = name;
	node.is_Data = nullptr;
	node.is_Code = code;
}
} // namespace

void DebugOverlay::clear() {
	debug_clear();
}

void DebugOverlay::text(s16 x, s16 y, const char* value, u32 rgb) {
	debug_text(x, y, value, rgb);
}

void DebugOverlay::rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb) {
	debug_rect(left, top, right, bottom, rgb);
}

void DebugOverlay::filled_rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb) {
	debug_filled_rect(left, top, right, bottom, rgb);
}

void DebugOverlay::register_bitmap(const void* addr, const char* name, u16 width, u16 height,
				   u16 num_planes, bool interleaved, bool masked) noexcept {
	unsigned short flags = 0u;
	if (interleaved) {
		flags = static_cast<unsigned short>(flags | debug_resource_bitmap_interleaved);
	}
	if (masked) {
		flags = static_cast<unsigned short>(flags | debug_resource_bitmap_masked);
	}
	debug_register_bitmap(addr, name, static_cast<short>(width), static_cast<short>(height),
			      static_cast<short>(num_planes), flags);
}

void DebugOverlay::register_palette(const void* addr, const char* name, u16 num_entries) noexcept {
	debug_register_palette(addr, name, static_cast<short>(num_entries), 0u);
}

void DebugOverlay::register_copperlist(const void* addr, const char* name, u32 size) noexcept {
	debug_register_copperlist(addr, name, static_cast<unsigned int>(size), 0u);
}

void DebugOverlay::unregister(const void* addr) noexcept {
	debug_unregister(addr);
}

AmigaBackend::~AmigaBackend() {
	release_memory();
}

void AmigaBackend::boot() {
	set_warpmode(false);
}

bool AmigaBackend::configure_memory(const MemoryConfig& in) {
	MemoryConfig config = in;
	if (in.all) {
		// Máquina desnuda: reservar toda la memoria disponible. Deja margen en Chip para la pila
		// de ejecución y las estructuras del sistema; la Fast es CPU-privada (Agnus no la ve).
		constexpr u32 kChipMargin = 64u * 1024u;
		const u32 chip_avail = static_cast<u32>(AvailMem(MEMF_CHIP));
		config.chip_bytes = (chip_avail > kChipMargin) ? chip_avail - kChipMargin : 0u;
		config.fast_bytes = static_cast<u32>(AvailMem(MEMF_FAST));
		config.slow_bytes = 0u;
		config.frame_bytes = 0u;
	}
	release_memory();
	// `release_memory` conserva las raíces si una carga async/lease DMA sigue activa; no
	// sobreescribir esos punteros ni crear pools nuevos sobre una vida de recursos aún abierta.
	if (m_chip_alloc || m_slow_alloc || m_fast_alloc || m_frame_alloc) return false;

	// NOTA: AllocMem de AmigaOS 1.3 solo garantiza alineacion a 8 bytes, no 16.
	// Si el llamador reserva memoria alineada a 16 dentro de una arena (como los
	// planos EHB o copperlists), el puntero base puede tener offset 8-mod-16,
	// generando padding interno en cada allocate() alineado a 16. El tamano total
	// pedido al backend debe incluir +16 bytes de headroom para absorber este
	// padding acumulado. Ver regla en arena.hpp allocate().
	if (config.chip_bytes != 0) {
		m_chip_alloc = AllocMem(config.chip_bytes, MEMF_CHIP | MEMF_CLEAR);
		m_chip_alloc_size = m_chip_alloc ? config.chip_bytes : 0;
	}

	if (config.slow_bytes != 0) {
		// "Slow" NO es verdadera Fast (CPU-privada): es RAM no-Chip que Agnus no ve. Por eso NO
		// se pide con MEMF_FAST (eso daría Fast RAM); se pide como MEMF_ANY. El slow ranger
		// ($C00000, trapdoor A500) no es pedible por flag: `hw::info` lo clasifica por **dirección**
		// (`MemRegionKind::Slow`). Ver §3.8 de INTERNAL_TYPE_SYSTEM.md.
		m_slow_alloc = AllocMem(config.slow_bytes, MEMF_ANY | MEMF_CLEAR);
		m_slow_alloc_size = m_slow_alloc ? config.slow_bytes : 0;
	}

	if (config.frame_bytes != 0) {
		m_frame_alloc = AllocMem(config.frame_bytes, MEMF_CHIP | MEMF_CLEAR);
		m_frame_alloc_size = m_frame_alloc ? config.frame_bytes : 0;
	}

	if (config.fast_bytes != 0) {
		// Fast RAM: CPU-privada (Agnus no la ve). Solo para trabajo de CPU (descompresión,
		// simulación, pilas). Si no hay, queda vacía y `MemoryManager::has_fast()` es false.
		m_fast_alloc = AllocMem(config.fast_bytes, MEMF_FAST | MEMF_CLEAR);
		m_fast_alloc_size = m_fast_alloc ? config.fast_bytes : 0;
	}

	// Arena por bloque, etiquetada por **direccion** (no por el flag pedido).
	m_memory.chip.reset(m_chip_alloc, m_chip_alloc_size, mem_kind_of(m_chip_alloc, true, false));
	m_memory.slow.reset(m_slow_alloc, m_slow_alloc_size, mem_kind_of(m_slow_alloc, false, false));
	m_memory.frame.reset(m_frame_alloc, m_frame_alloc_size, mem_kind_of(m_frame_alloc, true, false));

	// Bancos tipados por uso: **pool propio** sobre el mismo buffer (mismo rango que las arenas,
	// pero con `free` real por bloque). Ya no se usa `configure_backing`: los consumidores
	// reservan del **banco**, no de la arena (la arena `frame` sigue siendo scratch).
	(void)m_memmanager.configure(m_chip_alloc, m_chip_alloc_size, m_slow_alloc, m_slow_alloc_size,
				     m_fast_alloc, m_fast_alloc_size);
	// Palabra de descarte del trazador de líneas (2 B Chip, ver `m_line_trash`). No es fatal:
	// sin ella el relleno por plano cae al comportamiento anterior (D = base del plano).
	m_line_trash = m_memmanager.chip().reserve<eng::MaskTag>(2u, 2u);

	// Caché de assets con el presupuesto de las arenas. El backend es estable, así que la
	// `Ref` que guarda la caché es válida; el runtime posee su propio backend-copia.
	res::CacheConfig asset_cfg {};
	asset_cfg.chip_budget = m_chip_alloc_size;
	asset_cfg.fast_budget = m_fast_alloc_size;
	asset_cfg.max_assets = 8u;
	asset_cfg.slow_budget = m_slow_alloc_size;
	(void)m_assets.init(AssetCacheBackend {m_memmanager}, asset_cfg);

	m_memory_report.chip = m_memmanager.chip().snapshot();
	m_memory_report.slow = m_memmanager.slow().snapshot();
	m_memory_report.frame = m_memory.frame.snapshot();
	m_memory_report.chip_ok = config.chip_bytes == 0 || m_chip_alloc != nullptr;
	m_memory_report.slow_ok = config.slow_bytes == 0 || m_slow_alloc != nullptr;
	m_memory_report.frame_ok = config.frame_bytes == 0 || m_frame_alloc != nullptr;
	m_memory_report.fast_ok = config.fast_bytes == 0 || m_fast_alloc != nullptr;

	return m_memory_report.ok();
}

void AmigaBackend::release_memory() {
	// Primero se cortan IRQs y callbacks: sus contextos suelen vivir en la escena/juego, no en
	// el backend. Después se detiene Paula y se espera al blit actual sin volver a programar jobs.
	clear_audio_service();
	clear_blit_service();
	clear_vblank_service();
	clear_blitter_service();
	background_timer_stop();
	m_paula.silence();
	m_paula.wait_idle();
	m_audio.shutdown();
	wait_blitter();
	if (!m_assets.shutdown()) {
		// Un caller conserva leases: mantener pools, manager y raíces válidas para que pueda cerrar.
		return;
	}

	if (m_fast_alloc) {
		FreeMem(m_fast_alloc, m_fast_alloc_size);
		m_fast_alloc = nullptr;
		m_fast_alloc_size = 0;
	}

	if (m_frame_alloc) {
		FreeMem(m_frame_alloc, m_frame_alloc_size);
		m_frame_alloc = nullptr;
		m_frame_alloc_size = 0;
	}

	if (m_slow_alloc) {
		FreeMem(m_slow_alloc, m_slow_alloc_size);
		m_slow_alloc = nullptr;
		m_slow_alloc_size = 0;
	}

	if (m_chip_alloc) {
		FreeMem(m_chip_alloc, m_chip_alloc_size);
		m_chip_alloc = nullptr;
		m_chip_alloc_size = 0;
	}

	m_memory = {};
	m_memory_report = {};
	// La palabra de descarte vive en el banco Chip: el bloque queda libre con el resto.
	m_line_trash = {};
}

void AmigaBackend::wait_vblank_run(void (*thunk)(void*, u16), void* user) {
	debug_start_idle();
	// **Re-arme del latido**: el SO (proceso AmigaDOS vivo) puede reescribir INTENA y
	// apagar VERTB (ver `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`).
	// Con espera activa el bucle esta despierto y la re-arma aqui; con `Wait()` bloqueante
	// cubre tambien el hueco entre esperas.
	if (g_vbl_task != nullptr && (custom_base[custom_intenar_offset] & 0x0020u) == 0u) {
		if (g_cooperative_servers) {
			// El SO apaga VERTB cuando su pantalla/ventana queda estática (familia 212). En
			// cooperativo no se toca el resto de INTENA: solo se **setea** VERTB (SETCLR=1)
			// para que la cadena de exec vuelva a despachar nuestro servidor; el vector y el
			// display siguen siendo del SO.
			custom_base[custom_intena_offset] = 0x8020u; // SETCLR | VERTB
		} else {
			level3_sync();
		}
	}
	// **Espera bloqueada por señal** (modelo de mensajes): con el servicio de VBlank
	// instalado, la IRQ levanta una señal Exec y esta tarea **duerme en `Wait()`** en vez de
	// sondear `VPOSR`. Solo cuando no hay trabajo de fondo (`thunk == nullptr`): con thunk
	// (drenaje de la cola) se mantiene el sondeo para no perder rebanadas.
	static unsigned long seen_seq = 0u;
	if (thunk == nullptr && vblank_signal_wait_next(seen_seq)) {
		debug_stop_idle();
		return;
	}
	// Camino **AmigaDOS con servidor de exec**: el latido de la IRQ es la referencia de campo;
	// no se sondea VPOSR. Además de ser el modelo correcto (exec es dueño del vector/INTENA),
	// evita que un `VPOSR` latcheado por el lightpen de WinUAE deje el bucle esperando una
	// línea que nunca verá. `g_vblank_seq` lo incrementa el servidor tras invocar la tarea.
	if (thunk == nullptr && g_sys.active && g_vbl_task != nullptr) {
		const unsigned long start = g_vblank_seq;
		while (g_vblank_seq == start) {
		}
		debug_stop_idle();
		return;
	}
	// Una sola lectura de VPOSR por iteracion: la condicion y el `vpos` que recibe
	// la tarea comparten el mismo valor (no anade accesos al bus en el bucle caliente).
	u32 vposr = *vpos_long;
	while ((vposr & 0x1ff00u) == (311u << 8)) {
		if (thunk != nullptr) thunk(user, static_cast<u16>((vposr & 0x1ff00u) >> 8));
		vposr = *vpos_long;
	}
	while ((vposr & 0x1ff00u) != (311u << 8)) {
		if (thunk != nullptr) thunk(user, static_cast<u16>((vposr & 0x1ff00u) >> 8));
		vposr = *vpos_long;
	}
	debug_stop_idle();
}

void AmigaBackend::install_blitter_service(ServiceSlot& slot) {
	g_blitter_service = slot.thunk;
	g_blitter_service_user = &slot;
}

void AmigaBackend::clear_blitter_service() {
	g_blitter_service = nullptr;
	g_blitter_service_user = nullptr;
	g_blit_service_referee_only = false; // el feeder async ya no esta registrado
}

AmigaBackend::BlitWaitService AmigaBackend::blitter_wait_service() const noexcept {
	return { g_blitter_service, g_blitter_service_user };
}

u16 AmigaBackend::current_raster_line() const {
	return static_cast<u16>((*vpos_long & 0x1ff00u) >> 8);
}

u32 AmigaBackend::cia_tod_ticks() const {
	// Orden de latch de la CIA: TODHI congela TODMID/TODLO (ver cia_chips.md).
	const u32 hi = *ciaa_reg(0x0au);   // TODHI
	const u32 mid = *ciaa_reg(0x09u);  // TODMID
	const u32 lo = *ciaa_reg(0x08u);   // TODLO
	return (hi << 16) | (mid << 8) | lo;
}

// Despacha el nivel 3: lee INTREQR y atiende VERTB (tick del juego) y BLIT (servicio de
// blit). Cada fuente limpia su propio bit antes de llamar a su tarea.
extern "C" void level3_dispatch() {
	const unsigned short req = custom_base[custom_intreqr_offset];
	const unsigned short vpos = static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8);
	if ((req & 0x0020u) != 0u) {                    // VERTB
		custom_base[custom_intreq_offset] = 0x0020u;
		if (g_vbl_task != nullptr) {
			g_vbl_task(g_vbl_task_user, vpos);
		}
		// Señal de VBlank: despierta al bucle que duerme en `Wait()` (modelo de mensajes).
		++g_vblank_seq;
		vblank_signal_raise();
	}
	if ((req & 0x0040u) != 0u) {                    // BLIT
		custom_base[custom_intreq_offset] = 0x0040u;
		if (g_blit_task != nullptr) {
			g_blit_task(g_blit_task_user, vpos);
		}
	}
}

namespace {
/// Sincroniza los servidores de exec con los servicios activos. Camino **AmigaDOS**:
/// `AddIntServer`/`RemIntServer` dejan que exec sea el dueño del vector de nivel 3 y de
/// `INTENA`; no se escribe el vector ni `INTENA` a mano (familia 212/DT-011).
void sync_os_servers() {
	if (!g_sys.active && !g_cooperative_servers) {
		return;
	}
	// Si una build anterior instaló el vector crudo, devolverlo antes de ceder el control.
	if (g_level3_installed) {
		*reinterpret_cast<volatile eng::u32*>(0x6cu) = g_level3_old_vector;
		g_level3_installed = false;
	}
	if (g_vbl_task != nullptr && !g_exec_servers.vbl_added) {
		// Prioridad alta dentro de la cadena VERTB: nuestro servidor debe ejecutarse antes
		// que el de graphics/Intuition. Los servidores VERTB retornan con Z activo, pero si
		// otro servidor de la cadena lo incumple, los siguientes no se ejecutan; adelantarnos
		// mantiene el latido del juego aunque el SO tenga su propio servidor de display.
		init_server(g_exec_servers.vbl, g_vbl_server_name, eng_vbl_server_entry, 100);
		AddIntServer(INTB_VERTB, &g_exec_servers.vbl);
		g_exec_servers.vbl_added = true;
		++g_irq_diag_vbl_adds;
	} else if (g_vbl_task == nullptr && g_exec_servers.vbl_added) {
		RemIntServer(INTB_VERTB, &g_exec_servers.vbl);
		g_exec_servers.vbl_added = false;
	}
	if (g_blit_task != nullptr && !g_exec_servers.blit_added) {
		init_server(g_exec_servers.blit, g_blit_server_name, eng_blit_server_entry, 100);
		AddIntServer(INTB_BLIT, &g_exec_servers.blit);
		g_exec_servers.blit_added = true;
	} else if (g_blit_task == nullptr && g_exec_servers.blit_added) {
		RemIntServer(INTB_BLIT, &g_exec_servers.blit);
		g_exec_servers.blit_added = false;
	}
	// `AddIntServer` solo habilita la cadena si es el **primer** servidor. La cadena de
	// VERTB ya existe (graphics.library), así que tras `takeover_display` (que limpia
	// INTENA) hay que devolver el enable que exec da por supuesto; lo mismo para BLIT.
	// No se sustituye el vector: exec sigue siendo el dueño del despacho.
	// Solo en exclusivo: el takeover limpió INTENA y hay que devolver el enable que exec da
	// por supuesto. En cooperativo el SO es dueño de INTENA; escribirla desde aquí puede
	// desarmar su despacho (el fallo de la cadena a los ~21 frames).
	if (!g_cooperative_servers) {
		eng::u16 bits = 0x4000u; // INTEN
		if (g_vbl_task != nullptr) bits |= 0x0020u;
		if (g_blit_task != nullptr) bits |= 0x0040u;
		if (bits != 0x4000u) {
			custom_base[custom_intena_offset] = static_cast<unsigned short>(0x8000u | bits);
		}
	}
}

/// Retira los servidores de exec antes de devolver el control al SO.
void remove_os_servers() {
	if (g_exec_servers.vbl_added) {
		RemIntServer(INTB_VERTB, &g_exec_servers.vbl);
		g_exec_servers.vbl_added = false;
	}
	if (g_exec_servers.blit_added) {
		RemIntServer(INTB_BLIT, &g_exec_servers.blit);
		g_exec_servers.blit_added = false;
	}
}
} // namespace

// Cuerpos de los servidores (llamados desde el stub ASM con Z garantizado al retornar).
// Exec ya ha confirmado (ack) el bit de INTREQ antes de recorrer la cadena; aquí no se
// escribe INTREQ. Se conserva la señal de VBlank para el bucle de mensajes.
extern "C" void eng_vbl_server_body() {
	++g_irq_diag_vbl_calls;
	const unsigned short vpos = static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8);
	if (g_vbl_task != nullptr) {
		g_vbl_task(g_vbl_task_user, vpos);
	}
	++g_vblank_seq;
	g_irq_diag_seq_value = g_vblank_seq;
	vblank_signal_raise();
}

extern "C" void eng_blit_server_body() {
	const unsigned short vpos = static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8);
	if (g_blit_task != nullptr) {
		g_blit_task(g_blit_task_user, vpos);
	}
}

// Instala/restaura el handler unico segun los servicios activos. Bajo AmigaDOS usa la
// cadena de servidores de exec; en bare-metal mantiene el vector crudo y `INTENA`.
// Solo toca INTEN/VERTB/BLIT; no desarma otros bits de INTENA (p. ej. el audio del mixer).
void level3_sync() {
	if (g_sys.active || g_cooperative_servers) {
		sync_os_servers();
		return;
	}
	const bool need = (g_vbl_task != nullptr) || (g_blit_task != nullptr);
	if (need && !g_level3_installed) {
		volatile eng::u32* const vector3 = reinterpret_cast<volatile eng::u32*>(0x6cu);
		g_level3_old_vector = *vector3;
		*vector3 = reinterpret_cast<eng::u32>(&level3_irq);
		g_level3_installed = true;
	}
	if (need) {
		unsigned short bits = 0x4000u;              // INTEN (master)
		if (g_vbl_task != nullptr) bits |= 0x0020u; // VERTB
		if (g_blit_task != nullptr) bits |= 0x0040u;// BLIT
		custom_base[custom_intena_offset] = static_cast<unsigned short>(0x8000u | bits);
	} else {
		custom_base[custom_intena_offset] = 0x0060u; // desarmar VERTB|BLIT (INTEN se deja)
		if (g_level3_installed) {
			*reinterpret_cast<volatile eng::u32*>(0x6cu) = g_level3_old_vector;
			g_level3_installed = false;
		}
	}
}

// **Restriccion conocida (demos bajo un SO vivo)**: sustituir el vector de nivel 3 y
// habilitar VERTB a mano no convive con el libro de interrupciones/dispatch de exec
// (`Wait`/`Switch` reescriben INTENA y el despacho de supervisor usa la tabla en
// `SysBase-$36`): el camino de `Wait()` puede morir. Para demos sin `takeover_display`
// se usa la espera **activa** (`set_vblank_sleep(false)`): mismo bucle por mensajes y
// latido por IRQ, sin dormir en `Wait()`. Ver
// `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`.
bool AmigaBackend::install_vblank_service(ServiceSlot& slot) {
	if (g_vbl_task != nullptr) {
		return false;
	}
	// Arma la señal de VBlank ANTES de habilitar la IRQ: el primer VBlank ya despierta al
	// bucle que espera (si `AllocSignal` falla, la espera cae al sondeo de VPOSR). Con
	// `set_vblank_sleep(false)` NO se arma: las esperas usan sus ramas de sondeo (espera
	// activa) y se evita el camino de `Wait()`.
	if (m_vblank_sleep) {
		(void)vblank_signal_arm();
	}
	g_vbl_task = slot.thunk;
	g_vbl_task_user = &slot;
	level3_sync();
	return true;
}

void AmigaBackend::clear_vblank_service() {
	if (g_vbl_task == nullptr) {
		return;
	}
	custom_base[custom_intreq_offset] = 0x0020u;
	g_vbl_task = nullptr;
	g_vbl_task_user = nullptr;
	level3_sync(); // desarma VERTB antes de liberar la señal (nadie la levanta ya)
	vblank_signal_release();
}

bool AmigaBackend::install_blit_service(ServiceSlot& slot) {
	if (g_blit_task != nullptr) {
		// Slot exclusivo ya ocupado (p. ej. el bombeo de fondo del `Engine` vs. el feeder
		// async de la demo): se contabiliza para que la incoherencia sea diagnosticable
		// (el fallo del 066 «L0/S0» fue exactamente esto). Ver `COHERENCIA_DE_SETUP.md`.
		++g_blit_service_conflicts;
		return false;
	}
	g_blit_task = slot.thunk;
	g_blit_task_user = &slot;
	level3_sync();
	return true;
}

bool AmigaBackend::has_blit_service() const noexcept {
	return g_blit_task != nullptr;
}

eng::u32 AmigaBackend::blit_service_conflicts() noexcept {
	return g_blit_service_conflicts;
}

void AmigaBackend::clear_blit_service() {
	if (g_blit_task == nullptr) {
		return;
	}
	custom_base[custom_intreq_offset] = 0x0040u;
	g_blit_task = nullptr;
	g_blit_task_user = nullptr;
	level3_sync();
}

// --- Cola de blits por IRQ: API del backend -------------------------------------------
// El detalle de la cola está en `amiga_internal.hpp` (estructura del job y arranque).
// Aquí solo el estado del lote en curso y el encolado.
void AmigaBackend::enable_blitter_queue() {
	// Toma el slot de la IRQ de blit (exclusivo, `install_blit_service`): la tarea de
	// nivel 3 arranca el siguiente job. Si otro servicio lo ocupa, no se toca.
	if (g_blit_task != nullptr) {
		return;
	}
	// La cola la leen el CPU y la IRQ: en Chip el Blitter en marcha se come el bus y cada
	// encolado se vuelve lentísimo (~7,7k ciclos/job medidos). Fast (o Slow) si existe.
	{
		const eng::u32 bytes = static_cast<eng::u32>(kBlitQueueDepth) * sizeof(BlitQueueJob);
		m_blit_queue_mem = eng::fast_or_slow<eng::WorkTag>(m_memmanager, bytes, 4u);
		if (m_blit_queue_mem.valid()) {
			g_blit_queue = reinterpret_cast<BlitQueueJob*>(m_blit_queue_mem.view.data());
		}
	}
	g_blit_task = +[](void*, unsigned short) {
		// Camino rápido: la IRQ BLIT llega en **cada** finalización de blit (también de los
		// síncronos); si no hay cola pendiente no se toca ningún registro (una lectura de RAM
		// y el retorno). Solo cuando hay un job encolado se programa el siguiente.
		if (g_blit_q_head == g_blit_q_tail) {
			g_blit_q_inflight = 0u;
			return;
		}
		blitter_queue_start_next();
	};
	g_blit_task_user = nullptr;
	level3_sync();
}

void AmigaBackend::blitter_queue_begin(eng::graphics::BlobOp op, u16 words, u16 height, s16 amod,
				       s16 bmod, s16 cmod, s16 dmod) {
	u16 use = 0u;
	u16 minterm = 0u;
	switch (op) {
		case eng::graphics::BlobOp::Or:        use = eng::graphics::kBlitterUseA | eng::graphics::kBlitterUseB | eng::graphics::kBlitterUseD; minterm = eng::graphics::kBlitterMintermAOrB; break;
		case eng::graphics::BlobOp::CookieCut: use = eng::graphics::kBlitterUseA | eng::graphics::kBlitterUseB | eng::graphics::kBlitterUseC | eng::graphics::kBlitterUseD; minterm = eng::graphics::kBlitterMintermCookieCut; break;
		case eng::graphics::BlobOp::Opaque:    use = eng::graphics::kBlitterUseA | eng::graphics::kBlitterUseD; minterm = eng::graphics::kBlitterMintermCopyA; break;
		case eng::graphics::BlobOp::Copy:      use = eng::graphics::kBlitterUseC | eng::graphics::kBlitterUseD; minterm = eng::graphics::kBlitterMintermCopyC; break;
		case eng::graphics::BlobOp::Clear:     use = eng::graphics::kBlitterUseD; minterm = eng::graphics::kBlitterMintermZero; break;
	}
	m_q_cookie = (op == eng::graphics::BlobOp::CookieCut) || (op == eng::graphics::BlobOp::Or);
	m_q_con0 = static_cast<u16>(use | minterm);
	m_q_size = static_cast<u16>((static_cast<u16>(height) << 6u) | words);
	m_q_amod = amod;
	m_q_bmod = bmod;
	m_q_cmod = cmod;
	m_q_dmod = dmod;
	m_q_afwm = 0xffffu;
	m_q_alwm = 0xffffu;
}

void AmigaBackend::blitter_queue_masks(u16 afwm, u16 alwm) {
	m_q_afwm = afwm;
	m_q_alwm = alwm;
}

void AmigaBackend::blitter_queue_one(eng::graphics::BlitPtr a, eng::graphics::BlitPtr b,
				     eng::graphics::BlitPtr d, u8 shift) {
	// Ring SPSC lock-free: solo se avanza `tail` (stores de 16 bits). No hay que enmascarar
	// la IRQ (no hay contador compartido que actualizar con RMW).
	BlitQueueJob& j = g_blit_queue[g_blit_q_tail];
	const u16 s = static_cast<u16>(static_cast<u16>(shift & 0x0fu) << 12u);
	j.con0 = static_cast<u16>(m_q_con0 | s);
	j.con1 = m_q_cookie ? s : 0u;
	j.afwm = m_q_afwm;
	j.alwm = m_q_alwm;
	j.amod = static_cast<u16>(m_q_amod);
	j.bmod = static_cast<u16>(m_q_bmod);
	j.cmod = static_cast<u16>(m_q_cmod);
	j.dmod = static_cast<u16>(m_q_dmod);
	j.size = m_q_size;
	j.a = a.addr.value;
	j.b = static_cast<eng::u32>(m_q_cookie ? b.addr.value : d.addr.value);
	j.c = m_q_cookie ? d.addr.value : 0u;
	j.d = d.addr.value;
	g_blit_q_tail = static_cast<u16>((g_blit_q_tail + 1u) & (kBlitQueueDepth - 1u));
	// El arranque de la cadena lo hace `blitter_queue_kick()` (al final del encolado): así los
	// jobs se escriben con el Blitter **parado** (en un A500 sin Fast la cola vive en Slow y
	// escribirla con el Blitter en marcha cuesta ~6k ciclos/job por contención de bus).
}

void AmigaBackend::blitter_queue_kick() {
	if (g_blit_q_inflight == 0u && g_blit_q_head != g_blit_q_tail) {
		g_blit_q_inflight = 1u;
		if ((custom_base[custom_dmaconr_offset] & 0x4000u) == 0u) {
			blitter_queue_start_next();
		}
		// Si el Blitter está ocupado (job síncrono), su IRQ encadenará este.
	}
}

void AmigaBackend::blitter_queue_drain() {
	// Espera a que la cola se vacíe y el ultimo job termine, en modo nasty (como `wait_blitter`).
	custom_base[custom_dmacon_offset] = dma_bltpri_set;
	while (g_blit_q_head != g_blit_q_tail || g_blit_q_inflight != 0u) {
	}
	custom_base[custom_dmacon_offset] = dma_bltpri_clear;
}

// Despacha el nivel 4: AUD0..3 comparten vector. Lee INTREQR, limpia el bit de audio que
// disparo y llama al servicio (que cambia el buffer de la voz en curso, sin parar el DMA).
extern "C" void level4_dispatch() {
	const unsigned short req = custom_base[custom_intreqr_offset];
	const unsigned short audio = static_cast<unsigned short>(req & 0x0780u); // AUD0..3
	if (audio != 0u) {
		custom_base[custom_intreq_offset] = audio;
		if (g_audio_task != nullptr) {
			g_audio_task(g_audio_task_user,
				     static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8));
		}
	}
}

// Instala/restaura el handler de nivel 4 segun el servicio de audio activo. Solo toca
// INTEN/AUD0..3; no desarma otros bits de INTENA.
void level4_sync() {
	const bool need = (g_audio_task != nullptr);
	if (need && !g_level4_installed) {
		volatile eng::u32* const vector4 = reinterpret_cast<volatile eng::u32*>(0x70u);
		g_level4_old_vector = *vector4;
		*vector4 = reinterpret_cast<eng::u32>(&level4_irq);
		g_level4_installed = true;
	}
	if (need) {
		custom_base[custom_intena_offset] = static_cast<unsigned short>(0x8000u | 0x4000u | 0x0780u);
	} else {
		custom_base[custom_intena_offset] = 0x0780u; // desarmar AUD0..3 (INTEN se deja)
		if (g_level4_installed) {
			*reinterpret_cast<volatile eng::u32*>(0x70u) = g_level4_old_vector;
			g_level4_installed = false;
		}
	}
}

bool AmigaBackend::install_audio_service(ServiceSlot& slot) {
	if (g_audio_task != nullptr) {
		return false;
	}
	g_audio_task = slot.thunk;
	g_audio_task_user = &slot;
	level4_sync();
	return true;
}

void AmigaBackend::clear_audio_service() {
	if (g_audio_task == nullptr) {
		return;
	}
	custom_base[custom_intreq_offset] = 0x0780u;
	g_audio_task = nullptr;
	g_audio_task_user = nullptr;
	level4_sync();
}

void AmigaBackend::set_blitter_priority(bool enabled) {
	// DMACON bit 10 (BLTPRI) = "blitter nasty": el Blitter no deja slots libres a la CPU.
	// SETCLR (0x8000) activa; sin SETCLR, el bit se limpia. No toca MASTER/BLITTER.
	custom_base[custom_dmacon_offset] = enabled ? static_cast<unsigned short>(0x8400u)
						    : static_cast<unsigned short>(0x0400u);
}

// Despachador de la IRQ de la CIA-A (nivel 2): lo llama `support/cia_irq.s`.
extern "C" void cia_dispatch() {
	const unsigned char icr = *ciaa_reg(0x0du);    // leer ICR reconoce la IRQ (limpia flags)
	custom_base[custom_intreq_offset] = 0x0008u;   // limpiar PORTS (por si acaso)
	if ((icr & 0x08u) != 0u && g_os_kbd_isr != nullptr) {
		g_os_kbd_isr();                            // SP: scancode del teclado (mini-SO)
	}
	if (g_cia_task != nullptr) {
		g_cia_task(g_cia_task_user, static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8));
	}
}

bool AmigaBackend::install_timer_service(u16 latch, ServiceSlot& slot) {
	if (g_cia_task != nullptr) {
		return false;
	}
	g_cia_task = slot.thunk;
	g_cia_task_user = &slot;

	// Autovector de nivel 2 (VBR=0 en 68000 -> 0x68).
	volatile eng::u32* const vector2 = reinterpret_cast<volatile eng::u32*>(0x68u);
	g_cia_old_vector = *vector2;
	*vector2 = reinterpret_cast<eng::u32>(&cia_irq);
	g_cia_installed = true;

	// Timer A **continuo** (CRA RUNMODE=0), reloj E (INMODE=0). El orden importa:
	// parar, cargar el latch, LOAD (stroby se autolimpia) y START.
	volatile unsigned char* const lo = ciaa_reg(4u);   // TALO
	volatile unsigned char* const hi = ciaa_reg(5u);   // TAHI
	volatile unsigned char* const cra = ciaa_reg(0x0eu);
	*cra = 0x00u;
	*lo = static_cast<unsigned char>(latch & 0xffu);
	*hi = static_cast<unsigned char>((latch >> 8) & 0xffu);
	*cra = 0x10u;                                      // LOAD
	*cra = 0x11u;                                      // LOAD|START (continuo)

	// Enmascara la IRQ del timer A y arma INTEN|PORTS.
	*ciaa_reg(0x0du) = 0x81u;                          // ICR: SETCLR | TA
	custom_base[custom_intena_offset] = 0xc008u;       // SETCLR | INTEN | PORTS
	return true;
}

void AmigaBackend::background_timer_stop() {
	if (g_cia_task == nullptr) {
		return;
	}
	*ciaa_reg(0x0eu) = 0x00u;                          // CRA: parar
	*ciaa_reg(0x0du) = 0x01u;                          // ICR: CLR | TA
	custom_base[custom_intena_offset] = 0x0008u;       // desarmar PORTS
	if (g_cia_installed) {
		*reinterpret_cast<volatile eng::u32*>(0x68u) = g_cia_old_vector;
		g_cia_installed = false;
	}
	(void)*ciaa_reg(0x0du);
	g_cia_task = nullptr;
	g_cia_task_user = nullptr;
}

void AmigaBackend::set_color(u8 index, u16 rgb444) {
	if (index < 32) {
		custom_base[custom_color_offset + index] = rgb444;
	}
}

void AmigaBackend::set_sprite_collision(graphics::SpriteCollisionConfig cfg) {
	custom_base[custom_clxcon_offset] = graphics::encode_clxcon(cfg);
}

graphics::SpriteCollisionResult AmigaBackend::read_sprite_collision() {
	// CLXDAT se autolimpia al leer: una lectura = el resultado del frame.
	return graphics::decode_clxdat(custom_base[custom_clxdat_offset]);
}

void AmigaBackend::takeover_display(eng::ChipView<eng::CopperTag> copper_words) {
	// Al arrancar, Kickstart/AmigaDOS dejan viva toda la maquina de
	// interrupciones y DMA: exec/graphics/intuition tienen sus handlers
	// de VBL/ports/CIAA armados, y Agnus sigue fetchando el sprite del
	// puntero del Workbench (SPREN activo). Si solo instalamos nuestra
	// copperlist por encima, el sistema sigue "vivo" debajo: handlers de
	// VBL cada frame y un canal de sprite apuntando a datos stale que,
	// si se apaga mientras la sprite está activa, conserva su última línea
	// como barra vertical (AHRM cap. 4, p. 109; WinUAE-DBG custom.cpp:3150-3158
	// y drawing.cpp:4364-4390). Por eso se espera primero al VBlank, se congela
	// TODO en blanking y se arranca nuestra lista antes del siguiente display. A partir de este
	// punto el engine NO usa servicios del SO: el bucle principal espera al
	// VBlank por **senal Exec** (la IRQ la levanta; `Wait()` duerme la tarea
	// hasta ella) y la depuracion usa el canal lateral 0xf0ff60.

	// AHRM exige no apagar SPREN dentro de VSTART..VSTOP: Denise puede seguir
	// mostrando el último par DATA/DATB como una barra. Esperar antes del CLEAR
	// pone el cambio de propiedad fuera de toda banda activa.
	while ((*vpos_long & 0x1ff00u) == (311u << 8)) {
	}
	while ((*vpos_long & 0x1ff00u) != (311u << 8)) {
	}

	// 1) Apagar interrupciones del sistema y limpiar peticiones.
	custom_base[custom_intena_offset] = dma_clear_all;   // INTENA=0x7FFF
	custom_base[custom_intreq_offset] = dma_clear_all;   // INTREQ=0x7FFF

	// 2) Higiene: esperar un blit que el sistema pudiera tener en vuelo.
	wait_blitter();

	// 3) Apagar TODO el DMA: sprites, disco, audio, blitter, bitplane y
	//    copper. La pantalla queda a COLOR00 un instante, pero nadie lo
	//    ve porque esto esta en el blanking del arranque.
	custom_base[custom_dmacon_offset] = dma_clear_all;   // DMACON=0x7FFF

	// 3b) CDANG (COPCON): permite que el Copper escriba los registros del Blitter
	//     (<0x80). Sin esto, la primera escritura del Copper a un registro del
	//     Blitter lo detiene (`test_copper_dangerous`, custom.cpp:2835-2840). Es la
	//     base de la Tecnica A (Copper lanza blits).
	custom_base[custom_copcon_offset] = copcon_cdang;

	// 4) Programar nuestra copperlist (puntero COP1LC como LONG).
	*cop1lc = static_cast<eng::u32>(copper_words.address(0).value);

	// 5) Arrancar master + copper en el VBlank ya alcanzado y forzar el
	//    inicio de la lista. Los MOVEs de setup terminan antes de DIWSTRT,
	//    así que el primer frame sale limpio sin truncar una sprite activa.
	custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_copper;
	custom_base[custom_copjmp1_offset] = 0x7fff;         // COPJMP1

	m_display_taken = true;
}

void AmigaBackend::install_copper_list(eng::ChipView<eng::CopperTag> copper_words) {
	// SWAP de copperlist (doble buffer; la 201 reinstala cada frame).
	// Solo actualizamos el puntero: el Copper recarga COP1LC solo al comienzo
	// del proximo VBlank. NUNCA COPJMP1 aqui: reiniciaria el Copper a media
	// pantalla (ver bug "banda de 1 frame").
	//
	// Retrocompatibilidad: drivers antiguos que solo conocian este metodo
	// (nunca llaman a takeover_display) siguen funcionando: la primera llamada
	// delega en la toma de control completa. Asi los compositores que ya
	// llaman a install_copper_list por frame no necesitan cambios inmediatos.
	if (!m_display_taken) {
		takeover_display(copper_words);
		return;
	}
	*cop1lc = static_cast<eng::u32>(copper_words.address(0).value);
}

void AmigaBackend::stop_display() {
	if (!m_display_taken) return;
	wait_blitter();
	// El Copper puede volver a cargar COP1LC al VBlank siguiente; desactivar el DMA completo
	// antes de destruir la escena evita que Exec lea copperlists/bitplanes ya liberados.
	custom_base[custom_dmacon_offset] = dma_clear_all;
	m_display_taken = false;
}

// ── Toma de control del sistema y restauración (procedimiento canónico) ─────────────
// Referencia: docs/reference/amiga/hardware/takeover-y-restauracion-canonica.md
// Entrada: Forbid → graphics.library (LoadView(NULL) + 2×WaitTOF + OwnBlitter) → guardar
// INTENA/DMACON/ADKCON → apagar todo → (el llamador instala su copper con takeover_display).
// Salida: apagar todo → restaurar vector nivel 3 → copper del SO (copinit) → restaurar
// registros → DisownBlitter/LoadView(antiguo)/2×WaitTOF → Permit.
bool AmigaBackend::runs_under_os() const {
	// Un proceso AmigaDOS (lanzado por el Shell/runner) tiene `pr_CLI` no nulo; una tarea
	// pura no. Es la senal de "SO vivo" que hace letal la espera por `Wait()` (familia 212).
	const struct Process* self = reinterpret_cast<const struct Process*>(FindTask(nullptr));
	return self != nullptr && self->pr_CLI != 0;
}

bool AmigaBackend::vblank_wait_unsafe() const {
	return m_vblank_sleep && !g_sys.active && !g_cooperative_servers && runs_under_os();
}

void set_vblank_cooperative(bool on) noexcept {
	g_cooperative_servers = on;
}

bool vblank_cooperative() noexcept {
	return g_cooperative_servers;
}

bool AmigaBackend::takeover_system() {
	if (g_sys.active) {
		return true;
	}
	// 1) Exec: congelar el planificador (control absoluto de la máquina). `Permit()` al salir.
	Forbid();
	g_sys.forbidden = true;
	// 2) graphics.library: matar el display de Intuition y quedarse el Blitter. Sin esto el
	// "libro de display" del SO puede reescribir COP1LC/INTENA en el siguiente VBlank.
	// **Fuente única `GfxBase`**: las macros del SDK (`LoadView`/`WaitTOF`/`OwnBlitter`…)
	// llaman a través del **global `GfxBase`** (declarado en `proto/graphics.h`, definido
	// arriba en este fichero). Guardar la base en otra variable y dejar el global a cero
	// produce `JSR (a6,-0xde)` con `a6 = 0` → PC `0xFFFFFF22` (crash real de la 066: la
	// incoherencia era invisible hasta la primera macro).
	GfxBase = reinterpret_cast<struct GfxBase*>(OpenLibrary("graphics.library", 0));
	if (GfxBase != nullptr) {
		g_sys.old_view = GfxBase->ActiView;
		LoadView(nullptr);
		WaitTOF();
		WaitTOF();
		OwnBlitter();
		WaitBlit();
	}
	// 3) Guardar estado hardware (bit15=SET para restaurarlo tal cual).
	g_sys.old_dmacon = static_cast<eng::u16>(custom_base[custom_dmaconr_offset] | 0x8000u);
	g_sys.old_intena = static_cast<eng::u16>(custom_base[custom_intenar_offset] | 0x8000u);
	g_sys.old_adkcon = static_cast<eng::u16>(custom_base[custom_adkconr_offset] | 0x8000u);
	g_sys.active = true;
	return true;
}

void AmigaBackend::restore_system() {
	if (!g_sys.active) {
		return;
	}
	// 1) Apagar DMA/IRQ (higiene antes de tocar vectores/copper).
	wait_blitter();
	custom_base[custom_dmacon_offset] = dma_clear_all;
	custom_base[custom_intena_offset] = dma_clear_all;
	custom_base[custom_intreq_offset] = dma_clear_all;
	custom_base[custom_adkcon_offset] = dma_clear_all;
	// 2) Retirar los servidores de exec (camino AmigaDOS) y restaurar el vector crudo si
	//    lo habíamos sustituido (bare-metal).
	remove_os_servers();
	if (g_level3_installed) {
		*reinterpret_cast<volatile eng::u32*>(0x6cu) = g_level3_old_vector;
		g_level3_installed = false;
	}
	g_vbl_task = nullptr;
	g_blit_task = nullptr;
	m_display_taken = false;
	m_async_busy = false;
	m_async_service_installed = false;
	// 3) Copper del sistema (copinit) + registros guardados.
	if (GfxBase != nullptr) {
		*cop1lc = reinterpret_cast<eng::u32>(GfxBase->copinit);
		custom_base[custom_copjmp1_offset] = 0x7fffu;
	}
	custom_base[custom_dmacon_offset] = g_sys.old_dmacon;
	custom_base[custom_intena_offset] = g_sys.old_intena;
	custom_base[custom_adkcon_offset] = g_sys.old_adkcon;
	// 4) Devolver Blitter y display al SO y reactivar multitasking.
	if (GfxBase != nullptr) {
		DisownBlitter();
		LoadView(g_sys.old_view);
		WaitTOF();
		WaitTOF();
		CloseLibrary(reinterpret_cast<struct Library*>(GfxBase));
		GfxBase = nullptr; // el global vuelve a cero con la librería cerrada
	}
	if (g_sys.forbidden) {
		Permit();
	}
	g_sys = SystemTakeoverState {};
}

void AmigaBackend::set_bitplane_dat(u8 plane, u16 value) {
	if (plane < 8u) {
		custom_base[custom_bpldat_offset + plane] = value;
	}
}
void AmigaBackend::set_warpmode(bool enabled) {
	warpmode(enabled ? 1 : 0);
}

} // namespace eng::amiga
