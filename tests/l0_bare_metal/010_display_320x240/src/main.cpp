// ============================================================================
// Test L0-010: display 320x240, 5 bitplanes, paleta de 32 colores y lineas.
// ============================================================================
//
// Este test es un TUTORIAL de programacion bare metal del Amiga 500 organizado
// por capas. La idea es que sirva de material de estudio y de prueba al mismo
// tiempo:
//
//   CAPA 0 (bare metal)  - registros custom ($dffxxx), bitplanes planares,
//                          construccion de una copperlist word a word, dibujo
//                          de lineas por CPU y restauracion del sistema.
//   CAPA 1 (backend)     - MinimalBackend se encarga de reservar Chip RAM
//                          (AllocMem) y de instalar la copperlist. La logica
//                          del test sigue conociendo el hardware porque esta
//                          capa es justamente la que estamos aprendiendo.
//
// Flujo verificado por el script host (verify-framebuffer.mjs):
//   1. La demo dibuja un patron determinista de lineas en los bitplanes.
//   2. El host lee el framebuffer por el canal lateral y comprueba los puntos
//      declarados en g_test_contract.
//   3. El host escribe figuras extra por poke (demostracion de escritura de
//      memoria) y captura una pantalla.
//   4. La demo restaura la copperlist del sistema y termina, volviendo a
//      Workbench.
//
// Modo grafico:
//   - Lowres 320x240 (ventana de display dentro del frame PAL).
//   - 5 bitplanes = 32 colores "plenos" (indices 0..31) en paleta RGB444.
//   - Sin EHB: el sexto bitplane no se usa, de modo que la paleta se lee
//     directamente de COLOR00..COLOR31.
//   - DDFSTRT=$38 / DDFSTOP=$D0 -> 320 px lowres (40 bytes por fila).
//     DIWSTRT=$2c81 / DIWSTOP=$1cc1 -> ventana de 240 lineas (44..284).

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

// ============================================================================
// CONTRATO DE VERIFICACION (lo lee el host por el canal lateral)
// ============================================================================
//
// Estructura POD de layout fijo en big-endian (68000) que el script
// verify-framebuffer.mjs lee con `mem` y usa para comprobar el framebuffer sin
// adivinar direcciones ni geometria. Vive a nivel global con linkage C para que
// el simbolo `g_test_contract` sea localizable en el .map.
struct TestContract {
	eng::u32 framebuffer_addr; // direccion fisica del bloque de bitplanes.
	eng::u32 copper_addr;      // direccion fisica de la copperlist.
	eng::u16 width;
	eng::u16 height;
	eng::u8 planes;
	eng::u8 check_count;
	eng::u16 reserved;
	eng::u32 check_xy[16];     // (x | y<<16) por punto.
	eng::u8 check_color[16];   // indice de color esperado por punto.
};

extern "C" {
__attribute__((used)) volatile TestContract g_test_contract {};
}

namespace {

// ============================================================================
// CAPA 0: BARE METAL DEL AMIGA 500
// ============================================================================

// Base de los registros custom. En el Amiga, el chipset se ve como un bloque de
// words de 16 bits en $dff000. Escribir aqui "a pelo" es programacion bare metal.
volatile eng::u16* const custom = reinterpret_cast<volatile eng::u16*>(0xdff000);

// Offsets de los registros que usa este test (relativos a $dff000). Coinciden
// con los campos de `struct Custom` del NDK y con el Hardware Reference Manual.
constexpr eng::u16 REG_DMACON = 0x096;
constexpr eng::u16 REG_COPJMP1 = 0x088;
constexpr eng::u16 REG_DIWSTRT = 0x08e;
constexpr eng::u16 REG_DIWSTOP = 0x090;
constexpr eng::u16 REG_DDFSTRT = 0x092;
constexpr eng::u16 REG_DDFSTOP = 0x094;
constexpr eng::u16 REG_BPLCON0 = 0x100;
constexpr eng::u16 REG_BPLCON1 = 0x102;
constexpr eng::u16 REG_BPLCON2 = 0x104;
constexpr eng::u16 REG_BPL1MOD = 0x108;
constexpr eng::u16 REG_BPL2MOD = 0x10a;
constexpr eng::u16 REG_BPL1PTH = 0x0e0;
constexpr eng::u16 REG_COLOR00 = 0x180;

// Flags de DMACON. El bit 15 es el selector set/clear: a 1 activa los bits
// indicados, a 0 los limpia.
constexpr eng::u16 DMACON_SET = 0x8000;
constexpr eng::u16 DMA_MASTER = 0x0200;
constexpr eng::u16 DMA_BITPLANE = 0x0100;
constexpr eng::u16 DMA_COPPER = 0x0080;

// Geometria del framebuffer.
constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 240;
constexpr eng::u8 kPlanes = 5;
constexpr eng::u16 kBytesPerRow = kWidth / 8; // 40 bytes lowres por fila.
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Frames que se mantiene vivo el framebuffer antes de restaurar el sistema.
// 240 frames PAL ~= 4,8 s: tiempo de sobra para que el host lea/verifique y
// capture. Ajustalo con calma si el host necesita mas margen.
constexpr eng::u16 kExitFrame = 240;

// --- Acceso a registros -----------------------------------------------------

void write_reg(eng::u16 offset, eng::u16 value) {
	custom[offset / 2] = value;
}

eng::u16 read_reg(eng::u16 offset) {
	return custom[offset / 2];
}

// Los registros de puntero BPLxPT son pares PTH (word alto) / PTL (word bajo).
void write_pointer(eng::u16 pth_offset, const void* pointer) {
	const eng::u32 raw = reinterpret_cast<eng::u32>(pointer);
	write_reg(pth_offset, static_cast<eng::u16>(raw >> 16));
	write_reg(static_cast<eng::u16>(pth_offset + 2), static_cast<eng::u16>(raw & 0xffffu));
}

// --- Copperlist a mano ------------------------------------------------------

// El Copper ejecuta una lista de pares de words:
//   MOVE: (offset de registro, valor)
//   WAIT: (posicion vertical|horizontal, mascara)
//   fin : (0xffff, 0xfffe)
// Este test construye la lista directamente para que se vea cada palabra. Mas
// arriba, la capa L2 usa `CopperScheduler` para lo mismo sin palabras magicas.
struct CopperList {
	eng::u16* words = nullptr;
	eng::u16 capacity = 0;
	eng::u16 used = 0;
	bool ok = true;
};

void copp_move(CopperList& list, eng::u16 reg_offset, eng::u16 value) {
	if (!list.ok || list.used + 2 > list.capacity) {
		list.ok = false;
		return;
	}
	list.words[list.used++] = reg_offset;
	list.words[list.used++] = value;
}

void copp_pointer(CopperList& list, eng::u16 reg_offset, const void* pointer) {
	const eng::u32 raw = reinterpret_cast<eng::u32>(pointer);
	copp_move(list, reg_offset, static_cast<eng::u16>(raw >> 16));
	copp_move(list, static_cast<eng::u16>(reg_offset + 2), static_cast<eng::u16>(raw & 0xffffu));
}

void copp_wait(CopperList& list, eng::u8 line) {
	const eng::u16 wait = static_cast<eng::u16>((static_cast<eng::u16>(line) << 8) | 0x0001u);
	copp_move(list, wait, 0xff00); // WAIT con mascara completa.
}

void copp_end(CopperList& list) {
	copp_move(list, 0xffff, 0xfffe);
}

// Construye la copperlist completa de la escena. Deja DMA de bitplane activado
// desde el primer MOVE (la copperlist tambien puede encenderlo, como hace aqui).
// `bitplanes` es la base de los 5 planos contiguos; `palette` tiene 32 RGB444.
void build_copperlist(CopperList& list, const eng::u8* bitplanes, const eng::u16* palette) {
	// Encender DMA master + copper + bitplane en cuanto arranca el Copper.
	copp_move(list, REG_DMACON, static_cast<eng::u16>(DMACON_SET | DMA_MASTER | DMA_BITPLANE | DMA_COPPER));

	// Modo lowres, 5 bitplanes, sin EHB, salida de color habilitada:
	//   BPU (bits 14-12) = 5 -> 0x5000
	//   COLOR (bit 9)      = 1 -> 0x0200
	copp_move(list, REG_BPLCON0, 0x5200);
	copp_move(list, REG_BPLCON1, 0x0000); // sin scroll fino.
	copp_move(list, REG_BPLCON2, 0x0000); // prioridad estandar.
	copp_move(list, REG_BPL1MOD, 0x0000); // planos contiguos.
	copp_move(list, REG_BPL2MOD, 0x0000);

	// Ventana de display 320x240 dentro del frame PAL. El ancho lowres se
	// consigue con DDFSTRT/DDFSTOP (fetch de 40 bytes por fila) y la ventana
	// visible con DIWSTRT/DIWSTOP.
	copp_move(list, REG_DIWSTRT, 0x2c81);
	copp_move(list, REG_DIWSTOP, 0x1cc1);
	copp_move(list, REG_DDFSTRT, 0x0038);
	copp_move(list, REG_DDFSTOP, 0x00d0);

	// Apuntar los 5 bitplanes. Cada plano ocupa kPlaneBytes.
	for (eng::u8 plane = 0; plane < kPlanes; ++plane) {
		const eng::u16 reg = static_cast<eng::u16>(REG_BPL1PTH + static_cast<eng::u16>(plane) * 4u);
		copp_pointer(list, reg, bitplanes + static_cast<eng::u32>(plane) * kPlaneBytes);
	}

	// Paleta fisica de 32 colores. En 5 bitplanes los indices 0..31 se leen
	// directamente de COLOR00..COLOR31.
	for (eng::u16 i = 0; i < 32; ++i) {
		copp_move(list, static_cast<eng::u16>(REG_COLOR00 + i * 2u), palette[i]);
	}

	// Fin de la zona visible: al llegar a la linea 0xf8 se apaga el fondo para
	// no dejar residuos bajo la pantalla.
	copp_wait(list, 0xf8);
	copp_move(list, REG_COLOR00, 0x0000);

	copp_end(list);
}

// --- Dibujo planar por CPU --------------------------------------------------

// El chipset no ve "pixeles", ve planos de bits. Para el color `color` de un
// pixel (x,y) hay que encender/apagar el bit correspondiente en cada uno de los
// kPlanes planos. Esta funcion es el corazon de cualquier escritor planar.
void set_pixel(eng::u8* planes, eng::u16 x, eng::u16 y, eng::u8 color) {
	for (eng::u8 plane = 0; plane < kPlanes; ++plane) {
		eng::u8* row = planes + static_cast<eng::u32>(plane) * kPlaneBytes + static_cast<eng::u32>(y) * kBytesPerRow;
		const eng::u8 mask = static_cast<eng::u8>(0x80u >> (x & 7u));
		if ((color & (1u << plane)) != 0u) {
			row[x >> 3] |= mask;
		} else {
			row[x >> 3] &= static_cast<eng::u8>(~mask);
		}
	}
}

// Algoritmo de Bresenham adaptado al formato planar. Dibuja una linea de
// (x0,y0) a (x1,y1) con el indice de color indicado.
void draw_line(eng::u8* planes, eng::u16 x0, eng::u16 y0, eng::u16 x1, eng::u16 y1, eng::u8 color) {
	eng::s16 dx = static_cast<eng::s16>(x1) - static_cast<eng::s16>(x0);
	eng::s16 dy = static_cast<eng::s16>(y1) - static_cast<eng::s16>(y0);
	eng::s16 step_x = (dx > 0) ? 1 : -1;
	eng::s16 step_y = (dy > 0) ? 1 : -1;
	if (dx < 0) {
		dx = static_cast<eng::s16>(-dx);
	}
	if (dy < 0) {
		dy = static_cast<eng::s16>(-dy);
	}

	eng::s16 err = static_cast<eng::s16>(dx - dy);
	eng::s16 x = static_cast<eng::s16>(x0);
	eng::s16 y = static_cast<eng::s16>(y0);

	for (;;) {
		set_pixel(planes, static_cast<eng::u16>(x), static_cast<eng::u16>(y), color);
		if (x == static_cast<eng::s16>(x1) && y == static_cast<eng::s16>(y1)) {
			break;
		}
		const eng::s16 e2 = static_cast<eng::s16>(2 * err);
		if (e2 > -dy) {
			err = static_cast<eng::s16>(err - dy);
			x = static_cast<eng::s16>(x + step_x);
		}
		if (e2 < dx) {
			err = static_cast<eng::s16>(err + dx);
			y = static_cast<eng::s16>(y + step_y);
		}
	}
}

// ============================================================================
// CAPA 1: BACKEND (memoria + instalacion de copperlist)
// ============================================================================

constexpr eng::u16 kTestPalette[32] {
	0x000, // 0  negro
	0xf00, // 1  rojo    (borde)
	0x0f0, // 2  verde   (linea horizontal)
	0x00f, // 3  azul    (linea vertical)
	0xff0, // 4  amarillo(diagonal)
	0xfff, // 5  blanco  (figuras del host)
	0xf0f, // 6  magenta (figuras del host)
	0x0ff, // 7  cian
	0x888, // 8  gris
	0x840, // 9
	0x480, // 10
	0x048, // 11
	0x808, // 12
	0x088, // 13
	0x880, // 14
	0x444, // 15
	0xf80, // 16
	0x8f0, // 17
	0x08f, // 18
	0xf08, // 19
	0x80f, // 20
	0x0f8, // 21
	0xc44, // 22
	0x4c4, // 23
	0x44c, // 24
	0xcc4, // 25
	0xc4c, // 26
	0x4cc, // 27
	0xe86, // 28
	0x6e8, // 29
	0x86e, // 30
	0x222, // 31
};

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		// CAPA 1: reservar Chip RAM para bitplanes + copperlist. La arena chip
		// del backend pide el bloque a Exec (AllocMem MEMF_CHIP|MEMF_CLEAR).
		m_memory_ok = backend.configure_memory({
			64u * 1024u, // Chip: 48 KB bitplanes + copperlist.
			8u * 1024u,  // Slow: reserva didactica.
			4u * 1024u,  // Frame scratch.
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000010u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(1024, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000011u);
			return;
		}

		eng::u8* bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		// Limpiar el framebuffer (por si la politica de memoria cambiara).
		for (eng::u32 i = 0; i < kBitplaneBytes; ++i) {
			bitplanes[i] = 0;
		}

		// CAPA 0: dibujar el patron de lineas en formato planar.
		draw_line(bitplanes, 0, 0, 319, 0, 1);      // borde superior.
		draw_line(bitplanes, 0, 239, 319, 239, 1);  // borde inferior.
		draw_line(bitplanes, 0, 0, 0, 239, 1);      // borde izquierdo.
		draw_line(bitplanes, 319, 0, 319, 239, 1);  // borde derecho.
		draw_line(bitplanes, 0, 32, 319, 32, 2);    // linea horizontal verde.
		draw_line(bitplanes, 64, 0, 64, 239, 3);    // linea vertical azul.
		draw_line(bitplanes, 0, 0, 200, 200, 4);    // diagonal amarilla.

		// CAPA 0: construir la copperlist word a word.
		CopperList list;
		list.words = static_cast<eng::u16*>(m_copper_block.data);
		list.capacity = static_cast<eng::u16>(m_copper_block.size / sizeof(eng::u16));
		list.used = 0;
		list.ok = true;
		build_copperlist(list, bitplanes, kTestPalette);

		// CAPA 1: instalar la copperlist (COP1LC + COPJMP1 + DMA master/copper).
		// Guardamos la copperlist del sistema para restaurarla al salir.
		m_old_copper = *reinterpret_cast<volatile eng::u32*>(0xdff080);
		if (list.ok) {
			backend.install_copper_list(list.words);
		}

		// Publicar el contrato para el verificador host.
		g_test_contract.framebuffer_addr = reinterpret_cast<eng::u32>(bitplanes);
		g_test_contract.copper_addr = reinterpret_cast<eng::u32>(list.words);
		g_test_contract.width = kWidth;
		g_test_contract.height = kHeight;
		g_test_contract.planes = kPlanes;
		g_test_contract.reserved = 0;
		g_test_contract.check_count = 8;
		g_test_contract.check_xy[0] = 0u | (0u << 16);       // (0,0)    borde sup-izq.
		g_test_contract.check_xy[1] = 319u | (0u << 16);     // (319,0)  borde sup-der.
		g_test_contract.check_xy[2] = 0u | (239u << 16);     // (0,239)  borde inf-izq.
		g_test_contract.check_xy[3] = 319u | (239u << 16);   // (319,239)borde inf-der.
		g_test_contract.check_xy[4] = 160u | (32u << 16);    // (160,32) linea verde.
		g_test_contract.check_xy[5] = 64u | (120u << 16);    // (64,120) linea azul.
		g_test_contract.check_xy[6] = 100u | (100u << 16);   // (100,100)diagonal.
		g_test_contract.check_xy[7] = 160u | (200u << 16);   // (160,200)fondo limpio.
		g_test_contract.check_color[0] = 1;
		g_test_contract.check_color[1] = 1;
		g_test_contract.check_color[2] = 1;
		g_test_contract.check_color[3] = 1;
		g_test_contract.check_color[4] = 2;
		g_test_contract.check_color[5] = 3;
		g_test_contract.check_color[6] = 4;
		g_test_contract.check_color[7] = 0;

		if (list.ok) {
			eng::debug::mark_ready(g_eng_run_status, 0x0000a5a5u);
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000012u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);

		// Al llegar al frame de salida, restaurar el sistema: devolvemos la
		// copperlist original y reactivamos el DMA de display. El proceso DOS
		// termina al salir de main y Workbench vuelve a aparecer.
		if (!m_system_restored && context.frame.frame_index >= kExitFrame) {
			m_system_restored = true;
			*reinterpret_cast<volatile eng::u32*>(0xdff080) = m_old_copper; // COP1LC.
			write_reg(REG_COPJMP1, 0x7fff);                                  // recargar lista.
			write_reg(REG_DMACON, static_cast<eng::u16>(DMACON_SET | DMA_MASTER | DMA_BITPLANE | DMA_COPPER));
			backend.wait_vblank();
			backend.wait_vblank();
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// Sin overlay: el analizador debe leer solo los bitplanes reales.
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_system_restored = false;
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::u32 m_old_copper = 0;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames(static_cast<eng::u16>(kExitFrame + 2));

	return 0;
}
