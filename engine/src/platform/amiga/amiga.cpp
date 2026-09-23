#include <eng/platform/amiga/backend.hpp>

#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include <exec/memory.h>


#include "amiga_internal.hpp"

using namespace eng::amiga::detail;


namespace eng::amiga {

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

AmigaBackend::~AmigaBackend() {
	release_memory();
}

void AmigaBackend::boot() {
	set_warpmode(false);
}

bool AmigaBackend::configure_memory(const MemoryConfig& config) {
	release_memory();

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
		// On an A500 trapdoor expansion AmigaOS exposes this as non-chip memory.
		// It is still "Slow" from the engine perspective because it is not true
		// CPU-private Fast RAM.
		m_slow_alloc = AllocMem(config.slow_bytes, MEMF_FAST | MEMF_CLEAR);
		if (!m_slow_alloc) {
			m_slow_alloc = AllocMem(config.slow_bytes, MEMF_ANY | MEMF_CLEAR);
		}
		m_slow_alloc_size = m_slow_alloc ? config.slow_bytes : 0;
	}

	if (config.frame_bytes != 0) {
		m_frame_alloc = AllocMem(config.frame_bytes, MEMF_CHIP | MEMF_CLEAR);
		m_frame_alloc_size = m_frame_alloc ? config.frame_bytes : 0;
	}

	m_memory.chip.reset(m_chip_alloc, m_chip_alloc_size, MemoryKind::Chip);
	m_memory.slow.reset(m_slow_alloc, m_slow_alloc_size, MemoryKind::Slow);
	m_memory.frame.reset(m_frame_alloc, m_frame_alloc_size, MemoryKind::Chip);

	m_memory_report.chip = m_memory.chip.snapshot();
	m_memory_report.slow = m_memory.slow.snapshot();
	m_memory_report.frame = m_memory.frame.snapshot();
	m_memory_report.chip_ok = config.chip_bytes == 0 || m_chip_alloc != nullptr;
	m_memory_report.slow_ok = config.slow_bytes == 0 || m_slow_alloc != nullptr;
	m_memory_report.frame_ok = config.frame_bytes == 0 || m_frame_alloc != nullptr;

	return m_memory_report.ok();
}

void AmigaBackend::release_memory() {
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
}

void AmigaBackend::wait_vblank_run(void (*thunk)(void*, u16), void* user) {
	debug_start_idle();
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
	}
	if ((req & 0x0040u) != 0u) {                    // BLIT
		custom_base[custom_intreq_offset] = 0x0040u;
		if (g_blit_task != nullptr) {
			g_blit_task(g_blit_task_user, vpos);
		}
	}
}

// Instala/restaura el handler unico segun los servicios activos. Solo toca INTEN/VERTB/
// BLIT; no desarma otros bits de INTENA (p. ej. el audio del mixer).
void level3_sync() {
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

bool AmigaBackend::install_vblank_service(ServiceSlot& slot) {
	if (g_vbl_task != nullptr) {
		return false;
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
	level3_sync();
}

bool AmigaBackend::install_blit_service(ServiceSlot& slot) {
	if (g_blit_task != nullptr) {
		return false;
	}
	g_blit_task = slot.thunk;
	g_blit_task_user = &slot;
	level3_sync();
	return true;
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

void AmigaBackend::takeover_display(const u16* copper_words) {
	// Al arrancar, Kickstart/AmigaDOS dejan viva toda la maquina de
	// interrupciones y DMA: exec/graphics/intuition tienen sus handlers
	// de VBL/ports/CIAA armados, y Agnus sigue fetchando el sprite del
	// puntero del Workbench (SPREN activo). Si solo instalamos nuestra
	// copperlist por encima, el sistema sigue "vivo" debajo: handlers de
	// VBL cada frame y un canal de sprite apuntando a datos stale que,
	// cuando se apaga/recarga a media pantalla, deja una barra vertical
	// de un color de paleta (AHRM cap. 4: sprite DMA apagado a mitad de
	// listado -> ultima linea fetchada -> barra vertical). Por eso aqui
	// congelamos TODO antes de arrancar nuestra lista. A partir de este
	// punto el engine NO vuelve a usar exec: el bucle es espera activa
	// por VPOSR y la depuracion usa el canal lateral 0xf0ff60.

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
	*cop1lc = reinterpret_cast<u32>(copper_words);

	// 5) Esperar el arranque de VBlank (linea 311 -> 0) para que el Copper
	//    arranque ALINEADO al frame y no a media pantalla. Misma espera
	//    activa que wait_vblank() sobre VPOSR (sin interrupciones).
	while ((*vpos_long & 0x1ff00u) == (311u << 8)) {
	}
	while ((*vpos_long & 0x1ff00u) != (311u << 8)) {
	}

	// 6) Arrancar master + copper y forzar el inicio de la lista ya (aun
	//    linea 0-2). Los MOVEs de setup terminan mucho antes de DIWSTRT,
	//    asi que el primer frame sale limpio.
	custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_copper;
	custom_base[custom_copjmp1_offset] = 0x7fff;         // COPJMP1

	m_display_taken = true;
}

void AmigaBackend::install_copper_list(const u16* copper_words) {
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
	*cop1lc = reinterpret_cast<u32>(copper_words);
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
