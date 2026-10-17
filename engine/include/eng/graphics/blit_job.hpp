#pragma once

/// \file blit_job.hpp
/// **Trabajo de Blitter**: el tipo de operación (`BlitJobKind`), los roles tipados de
/// fuente/destino (`BlitPtr`) y los datos del trabajo (`BlitJob`).
///
/// Se separa de `frame_plan.hpp` (que contiene el plan y su presupuesto) para que quien
/// solo describe un trabajo no arrastre el contenedor, y los campos específicos de cada
/// operación se agrupan en sub-structs (`job.line`, `job.c2p`) en vez de convivir planos
/// en un único bloque: así no se mezcla el campo de una operación con el de otra.
///
/// Los campos **comunes** (fuente/destino, tamaño, módulos, planos, minterm) valen para
/// todas las operaciones; los grupos `line` y `c2p` solo se leen para sus tipos.

#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/graphics/target_bounds.hpp>

namespace eng::graphics {

/// **Módulo de Blitter/Copper** (`BLTxMOD`, registro de 16 bits): convierte un valor calculado en
/// `s32`/`u32` a `s16` **comprobando el rango**. Es el único sitio de la regla: los módulos son
/// `row_bytes·planes − words·2`, que rara vez son potencia de 2 (`40·4 − 42 = 118`), así que no se
/// restringe a potencias de 2; lo que se evita es el **truncado silencioso** de un
/// `static_cast<s16>` a ciegas. En release no cuesta nada (la aserción desaparece).
[[nodiscard]] constexpr s16 mod16(s32 v) noexcept {
	ENG_ASSERT(v >= -32768 && v <= 32767);
	return static_cast<s16>(v);
}
/// Igual, para un valor sin signo conocido (p. ej. `words·2`): debe caber en `s16` no negativo.
[[nodiscard]] constexpr s16 mod16u(u32 v) noexcept {
	ENG_ASSERT(v <= 32767u);
	return static_cast<s16>(v);
}

/// Tipos de trabajo de Blitter soportados por el plan.
enum class BlitJobKind : u8 {
	CopyRect,
	RestoreRect,
	TileBlockCopy,
	MaskedBobCookieCut,
	MaskedBlobNoSave,
	/// Borrado del rectangulo: un blit sin fuentes (solo D). Con bitmaps
	/// intercalados borra la caja del objeto en UN blit (`height = alto*planos`).
	ClearRect,
	/// **Relleno de color** de un rectangulo (solo D, sin fetch de fuentes): un blit por plano con
	/// `D = A` (minterm `$F0`), **A deshabilitada** y `BLTADAT` preload con `$FFFF`/`$0000`; las
	/// mascaras de borde (`fill.afwm`/`fill.alwm`) recortan la primera/ultima palabra. Es el
	/// relleno de color **diferido** (`Screen::fill_box`). Ref.: AHRM 3.ª §"Extracting a Range of
	/// Columns" (`BLTADAT` constante con A deshabilitada) + WinUAE `custom.cpp` `BLTADAT`.
	FillRect,
	/// BOB **OR por desplazamiento** (estilo `bobs3d`): `A` = bitmap del objeto,
	/// `B = D` = destino, minterm `$FC` (`D = A | D`). Sin mascara: los ceros del
	/// objeto dejan el fondo (aditivo/glow). Con destino intercalado es UN blit.
	OrBlob,
	/// **Línea por Blitter** (`BLTCON1` LINE): usa `line.x0..line.y1` y
	/// `line.row_bytes`; el `destination` apunta al plano. Sin fuentes ni mascara.
	Line,
	/// **Línea EOR (ONEDOT)** (`blitter_line_eor`): como `Line` pero XOR con
	/// `line.base` (el canal D), base del contorno XOR de `flatshade-convex` (demo 116).
	LineEor,
	/// **Area fill XOR de un rectángulo** (barrido descendente por paridad, técnica
	/// contorno+fill de `flatshade-convex`): `A = D` = semilla (última palabra del rect),
	/// `BLTCON1` = `FILL_XOR|DESC`, módulos al ancho de fila. Rellena el interior del
	/// contorno dibujado antes (líneas EOR) en UN job por plano. Construir con
	/// `make_area_fill_rect`. Es encadenable por el referee async como cualquier job.
	AreaFill,
	/// **Blit con operación lógica** (`B = D`): `A` = fuente, `B` = destino (mismo puntero),
	/// minterm del job (`Or`/`And`/`Xor`). Base de sombras/glow/máscaras. Ver `RasterOp`.
	LogicBlit,
	/// **Relleno con patrón** (suelos/techos 3D, UI texturizada): `A` = patrón, `B = D` =
	/// destino, minterm `$FC` (`D = A | D`). El patrón es **una fila** de `words_per_row`
	/// palabras; el módulo de A (`source_modulo_bytes = -(words_per_row*2)`) la repite en
	/// todas las filas (patrón uniforme en vertical). Un patrón de varias filas necesita
	/// más blits o reprogramar A por fila. Reusa el camino `OrBlob` del backend.
	PatternFill,
	/// **Chunky→planar por Blitter** (13 fases): convierte `c2p.chunky` a los 4 planos
	/// `c2p.planes` (ver `AmigaBackend::c2p_4bpp_step`). Es la vía Blitter del seam
	/// `Rasterizer::c2p`.
	C2P,
};

/// Rol de **origen** de un blit (solo lectura). La dirección es un `Address<MemoryKind::Chip>`: el
/// rol lleva la **procedencia** (DMA), no un `u16*` suelto. `words()` da la vista de **registro**
/// **Puntero de Blitter a Chip RAM** (una dirección DMA a words): origen, destino o máscara de un
/// `BlitJob`. Un **solo** tipo para los tres roles — el hardware del Blitter es simétrico (los
/// canales A/B/C/D se intercambian) y **quién es fuente o destino lo decide el programador**, no el
/// tipo; el rol lo expresa el **campo** del `BlitJob` (`source`/`destination`/`mask`). El
/// invariante que **sí** vale (memoria DMA en Chip) lo lleva `Address<MemoryKind::Chip>` dentro.
struct BlitPtr {
	eng::Address<eng::MemoryKind::Chip> addr {};

	constexpr BlitPtr() noexcept = default;
	constexpr BlitPtr(eng::Address<eng::MemoryKind::Chip> a) noexcept : addr(a) {}
	/// **Frontera explícita** desde un puntero crudo a words: solo para código que ya garantiza
	/// Chip RAM (backend/test con procedencia certificada). Nombrada para que el acto se lea como
	/// tal — igual que `Address<Chip>::from_storage`. Ver `MEMORY_OWNERSHIP.md`. Preferir el ctor
	/// desde `ChipView<Tag>`.
	[[nodiscard]] static constexpr BlitPtr from_storage(const u16* w) noexcept {
		return BlitPtr {eng::Address<eng::MemoryKind::Chip>::from_storage(
			reinterpret_cast<const eng::u8*>(w))};
	}
	/// Desde una vista tipada en Chip (`ChipView<Tag>`) con `off` en bytes.
	template <class Tag>
	constexpr BlitPtr(eng::ChipView<Tag> v, eng::s32 off = 0) noexcept : addr(v.address(off)) {}
	/// Vista de **registro** (word) mutable: el Blitter lee o escribe según el canal.
	[[nodiscard]] constexpr u16* words() const noexcept {
		return reinterpret_cast<u16*>(addr.ptr());
	}
	/// Vista de registro **solo lectura** (para código que quiere dejar claro que no escribe).
	[[nodiscard]] constexpr const u16* cwords() const noexcept {
		return reinterpret_cast<const u16*>(addr.cptr());
	}
	/// Avanza la dirección `off` **bytes** (mismo banco y rol). El offset conserva su tipo dentro
	/// de la expresión (GCC puede usar direccionamiento indexado de 16 bits cuando cabe en `s16`/`s8`).
	/// Misma semántica que `Address<MemoryKind::Chip>::operator+`.
	template <class Off>
		requires eng::util::is_integral_v<Off>
	[[nodiscard]] constexpr BlitPtr operator+(Off off) const noexcept {
		return BlitPtr {addr + off};
	}
	/// Retrocede la dirección `off` **bytes** (mismo banco y rol).
	template <class Off>
		requires eng::util::is_integral_v<Off>
	[[nodiscard]] constexpr BlitPtr operator-(Off off) const noexcept {
		return BlitPtr {addr - off};
	}
	/// Avanza la dirección en sitio `off` **bytes** (mismo banco y rol).
	template <class Off>
		requires eng::util::is_integral_v<Off>
	constexpr BlitPtr& operator+=(Off off) noexcept {
		addr += off;
		return *this;
	}
	/// Retrocede la dirección en sitio `off` **bytes** (mismo banco y rol).
	template <class Off>
		requires eng::util::is_integral_v<Off>
	constexpr BlitPtr& operator-=(Off off) noexcept {
		addr -= off;
		return *this;
	}
	/// Distancia en **bytes** entre dos punteros de Blitter (mismo banco).
	[[nodiscard]] constexpr eng::uintptr operator-(BlitPtr o) const noexcept { return addr - o.addr; }
	/// Igualdad de dirección (mismo banco).
	[[nodiscard]] constexpr bool operator==(BlitPtr o) const noexcept { return addr == o.addr; }
	/// Desigualdad de dirección (mismo banco).
	[[nodiscard]] constexpr bool operator!=(BlitPtr o) const noexcept { return addr != o.addr; }
	/// Orden por dirección (mismo banco).
	[[nodiscard]] constexpr bool operator<(BlitPtr o) const noexcept { return addr < o.addr; }
};

/// **BlitPtr desde una vista tipada** (bytes o words) con offset en **bytes**: eleva la vista a
/// Chip con `as_chip` — el único punto donde se audita la procedencia del almacén (ver
/// `INTERNAL_TYPE_SYSTEM.md` §«Procedencia de un Address<Chip>») — y construye la dirección DMA.
/// Es el camino recomendado para quien tiene una vista de dominio (copperlist, plano, atlas) y
/// necesita entregar una dirección al Blitter: no hay puntero crudo en la llamada. Para vistas de
/// words el offset va en bytes (×2 del índice de palabra).
template <class T, class Tag>
[[nodiscard]] constexpr BlitPtr blit_ptr(eng::TaggedSpan<T, Tag> view, eng::s32 byte_off = 0) noexcept {
	if constexpr (sizeof(T) == 1u) {
		return BlitPtr {eng::as_chip(view, byte_off)};
	} else {
		return BlitPtr {eng::as_chip(view.as_bytes(), byte_off)};
	}
}

/// Trabajo planar de Blitter.
///
/// `CopyRect`, `RestoreRect` y `TileBlockCopy` son copias rectangulares (`dest = source`);
/// `TileBlockCopy` es una categoría propia por contrato (carga de tiles/metatiles a zonas
/// no visibles del playfield). `MaskedBobCookieCut` y `MaskedBlobNoSave` usan el clásico
/// cookie-cut `dest = (mask & source) | (~mask & dest)` y se diferencian por el presupuesto
/// (un BOB suele necesitar save/restore si se mueve).
///
/// Restricciones: `destination` alineado a word; `source_shift` de 0..15 px; sin clipping
/// automático; `mask` es un único plano de 1 bit compartido por todos los bitplanes;
/// `source` va en planar contiguo (o intercalado con `interleaved`).
struct BlitJob {
	BlitJobKind kind = BlitJobKind::MaskedBobCookieCut;
	BlitPtr mask {};
	BlitPtr source {};
	BlitPtr destination {};
	u16 words_per_row = 0;
	/// Filas físicas del job; en OCS/ECS el valor 1024 se codifica como BLTSIZE height=0.
	u16 height = 0;
	s16 source_modulo_bytes = 0;
	s16 destination_modulo_bytes = 0;
	u8 bitplane_count = 0;
	u8 source_shift = 0;
	u32 source_plane_stride_bytes = 0;
	u32 destination_plane_stride_bytes = 0;
	/// Procesa el blit en orden descendente (BLTCON1 DESC): necesario para copias
	/// de regiones solapadas en las que el destino queda por delante del origen
	/// (p. ej. desplazar el scroll ring hacia la derecha/abajo).
	bool descending = false;
	/// Minterm del Blitter (BLTCON0 bits 7..0). Permite el MISMO camino para
	/// **cookie-cut** `$CA` (`D=A·B+¬A·C`, con mascara), **OR aditivo por
	/// desplazamiento** `$FC` (`D=A|D`, bobs/glow sin mascara, ver
	/// `demoscene-repo-orig/effects/bobs3d/bobs3d.c`), **copia** `$F0`/`$AA` y
	/// **borrado** `$00`. Referencia: `amiga-bootcamp/08_graphics/blitter_programming.md`
	/// (tabla de minterms).
	u8 minterm = 0xCAu;
	/// El destino es un bitmap **INTERCALADO**: una sola "columna" de canales y
	/// `height` ya incluye los planos (filas = alto_objeto * planos), con los modulos
	/// del bitmap. Es el truco de **un blit por objeto** (AHRM 6; `blitter_programming.md`
	/// *Use Case 4: interleaved bitplane BOBs*). Exime de dar strides de plano.
	bool interleaved = false;

	/// Palabras de **fuente por scanline**. Sólo se usa si `source_words_per_row != 0`: la
	/// fuente es un bitmap con su propio ancho de fila (A) distinto del bloque a copiar
	/// (`words_per_row`); el módulo de A se deriva como
	/// `(source_words_per_row - words_per_row) * 2` y el avance por plano es
	/// `source_plane_stride_bytes` (fuente en un playfield aparte). Si es `0`, la fuente se
	/// lee compacta como `words_per_row` palabras por fila (fuente «apretada»). Campo al
	/// final para no romper los *aggregate initializers* posicionales existentes.
	u16 source_words_per_row = 0;

	/// Campos de **línea** (`BlitJobKind::Line`/`LineEor`): coordenadas y módulo de fila.
	struct Line {
		s16 x0 = 0;        ///< x del punto inicial
		s16 y0 = 0;        ///< y del punto inicial
		s16 x1 = 0;        ///< x del punto final
		s16 y1 = 0;        ///< y del punto final
		u16 row_bytes = 0; ///< bytes por fila del plano destino (módulo de la línea)
		/// Base del bitmap para el canal D en una **línea EOR**; `nullptr` = usar el
		/// propio plano. Ver `blitter_line_eor`.
		BlitPtr base {};
	};
	Line line {};

	/// Campos de **C2P** (`BlitJobKind::C2P`): chunky → planar por fases.
	struct C2p {
		/// Buffer chunky en Chip RAM; su segunda mitad (`+ bytes`) es el staging planar
		/// que el C2P usa como destino intermedio.
		eng::u8* chunky = nullptr;
		/// Base de los 4 planos destino (a `planes + p*plane_stride`).
		eng::u8* planes = nullptr;
		/// Bytes entre planos destino.
		u32 plane_stride = 0;
		/// `bytes` del C2P (p. ej. `width*height/2` en 4 bpp); fija el `BLTSIZE`.
		u16 bytes = 0;
	};
	C2p c2p {};

	/// Campos de **relleno de color** (`BlitJobKind::FillRect`, solo D): mascaras de primera y
	/// ultima palabra (rect no alineado a 16 px). El color va en `minterm` (`$FF` = plano a 1,
	/// `$00` = plano a 0); el backend traduce a `BLTADAT` `$FFFF`/`$0000` con A deshabilitada.
	struct Fill {
		u16 afwm = 0xffffu; ///< `BLTAFWM` (mascara de la primera palabra)
		u16 alwm = 0xffffu; ///< `BLTALWM` (mascara de la ultima palabra)
	};
	Fill fill {};
};

/// **¿El trabajo escribe solo dentro de `bounds`?** Cubre los jobs rectangulares
/// (`destination`, `words_per_row`, `height`, módulo y `destination_plane_stride_bytes`):
/// se comprueba el rango de la **primera y última** fila de cada plano (con módulo positivo
/// o negativo) contra la reserva. Es la validación que permite al `FramePlan` rechazar un
/// `BlitJob` construido a mano fuera del framebuffer. Los jobs de **línea** y **C2P** tienen
/// su propio chequeo en el plan (usan otros campos).
[[nodiscard]] inline bool blit_job_within(const BlitJob& job, const TargetBounds& bounds) {
	if (!bounds.active()) {
		return true;
	}
	if (job.destination.words() == nullptr || job.words_per_row == 0u || job.height == 0u) {
		return false;
	}
	const eng::s32 base = static_cast<eng::s32>(bounds.base.value);
	const eng::s32 first = static_cast<eng::s32>(job.destination.addr.value) - base;
	// Si el destino **no empieza** en esta reserva, el job es de otro buffer (save-under,
	// staging de C2P, patrón…): no es competencia de estos límites y no se rechaza.
	if (first < 0 || static_cast<eng::u32>(first) >= bounds.bytes) {
		return true;
	}
	const eng::s32 step = static_cast<eng::s32>(job.words_per_row) * 2 +
			      static_cast<eng::s32>(job.destination_modulo_bytes);
	const eng::u32 len = static_cast<eng::u32>(job.words_per_row) * 2u;
	const eng::s32 rows = static_cast<eng::s32>(job.height) - 1;
	// `rows*step` y `p*pstride` con `mulu.w` por mitades (`mulu32x16`), no `__mulsi3` (libcall
	// de ~50 ciclos en cada job validado). `step` puede ser negativo (módulo negativo): el
	// producto se calcula sobre la magnitud —exacto para todo `s32` por la descomposición
	// baja/alta— y el signo se restaura.
	const eng::u32 step_abs = static_cast<eng::u32>(step < 0 ? -step : step);
	const eng::s32 span = static_cast<eng::s32>(
		eng::math::mulu32x16(static_cast<eng::u32>(rows), static_cast<eng::u16>(step_abs)) +
		(eng::math::mulu32x16(static_cast<eng::u32>(rows), static_cast<eng::u16>(step_abs >> 16))
		 << 16));
	const eng::s32 row_delta = step < 0 ? -span : span;
	for (eng::u8 p = 0u; p < job.bitplane_count; ++p) {
		const eng::s32 row0 = first + static_cast<eng::s32>(
			eng::math::mulu32x16(job.destination_plane_stride_bytes, static_cast<eng::u16>(p)));
		const eng::s32 row1 = row0 + row_delta;
		const eng::s32 lo = row0 < row1 ? row0 : row1;
		const eng::s32 hi = row0 < row1 ? row1 : row0;
		if (!bounds.contains(lo, len) || !bounds.contains(hi, len)) {
			return false;
		}
	}
	return true;
}

/// Configura `job` como **area fill XOR de un rectángulo** (`BlitJobKind::AreaFill`): el
/// barrido descendente del Blitter (`A = D`, `FILL_XOR`) desde la ultima palabra del rect
/// conmuta el relleno del interior por **paridad de scanline** (port de `BlitterFillArea`;
/// demo 116). `plane` = base del plano (fila 0); `wx0` alineado a 16 px; `words*2` debe
/// caber en `row_bytes`. Un job por plano; encadenable por el referee async.
inline void make_area_fill_rect(BlitJob& job, BlitPtr plane, u16 row_bytes, u16 wx0, s16 y0,
				u16 words, u16 rows) {
	job.kind = BlitJobKind::AreaFill;
	const u32 roff = static_cast<u32>(y0 + static_cast<s16>(rows) - 1) * row_bytes +
			 (static_cast<u32>(wx0) >> 3u) + (static_cast<u32>(words) - 1u) * 2u;
	job.destination = plane + roff; // semilla del barrido (A = D)
	job.words_per_row = words;
	job.height = rows;
	job.destination_modulo_bytes = static_cast<s16>(row_bytes - words * 2u);
	job.bitplane_count = 1;
}

/// Configura `job` como **BOB interleaved enmascarado en UNA pasada** (cookie-cut `$CA` con
/// **máscara expandida**: una copia de la máscara por plano). `src` apunta al par
/// `[imagen `w/16` palabras][máscara `w/16` palabras]` de la primera fila del BOB (layout que
/// produce `kingcon ... -Interleaved -Format=N -Mask`); `dest` al bitmap interleaved en
/// `x & ~15`; `w`/`h` = tamaño en píxeles (`w` múltiplo de 16); `planes` = planos del bitmap;
/// `dest_row_bytes` = bytes de **una fila de un plano**; `shift` = `x & 15`.
///
/// Un solo blit recorre `h*planes` filas: `A` = máscara (segunda mitad), `B` = imagen (primera
/// mitad), `C = D` = destino; `ASH`/`BSH` desplazan ambos canales con el mismo `shift`. Ver
/// `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`. El job queda listo para
/// `FramePlan::add_masked_bob` o `AmigaBackend::blitter_submit`.
/// **Sin palabra de guarda** (contrato del par «denso»): con `shift > 0` procesa `w/16`
/// palabras y recorta el borde derecho del arte; para subpíxel exacto en ambas mitades usar
/// la hoja con guarda (`pack_interleaved_pair` + `BobMaskPack::InterleavedPair`), donde la
/// guarda aporta al barrel los bits del borde (AHRM 6; HOST-453).
/// **Frontera tipada**: `src`/`dest` llegan como `BlitPtr` (direcciones **Chip** con
/// procedencia en el tipo — no compila pasar memoria no-Chip ni un `u16*` suelto).
inline void make_interleaved_masked_bob(BlitJob& job, BlitPtr src, BlitPtr dest, u16 w, u16 h,
					u8 planes, u16 dest_row_bytes, u8 shift) noexcept {
	const u16 words = static_cast<u16>(w / 16u);
	job = BlitJob {};
	job.kind = BlitJobKind::MaskedBobCookieCut;
	job.source = src;                                 // primera mitad de la fila = imagen
	job.mask = src + static_cast<s32>(words) * 2;     // segunda mitad = máscara (+words, en bytes)
	job.destination = dest;
	job.words_per_row = words;
	job.height = static_cast<u16>(h * planes);
	// Avance por "fila" (una fila de UN plano): el par ocupa `2*words` palabras.
	job.source_modulo_bytes = mod16u(words * 2u);
	job.destination_modulo_bytes = mod16(static_cast<s32>(dest_row_bytes) - static_cast<s32>(words) * 2);
	job.bitplane_count = 1u;
	job.source_shift = shift;
	job.minterm = 0xCAu;
	job.interleaved = true;
}

/// **Cocina la hoja interleaved pair CON GUARDA** que consume `Bob` con
/// `BobMaskPack::InterleavedPair` sobre destinos interleaved (bitmaps del corkscrew y
/// campos de doble playfield): por **cada fila del objeto y cada plano del destino**,
/// `[imagen w][guarda][máscara w][guarda]` (la guarda = 1 palabra a cero tras cada
/// mitad). Con `shift > 0` el barrel arrastra bits del byte bajo de la palabra
/// siguiente; la guarda a cero los neutraliza — y como la máscara de la guarda también
/// es 0, esos píxeles quedan **transparentes** (no se escriben). Receta fijada por
/// HOST-453 (subpíxeles 0..15 exactos en ambas mitades del byte).
///
/// El `Bob` del consumidor declara `width = 2·W` (ventana: cada mitad — imagen y máscara —
/// es arte `W` más un bloque de guarda del tamaño del arte; el objeto ocupa los `W`
/// primeros píxeles) y `height = H`; `bob_draw` deriva `words = 2·W/16` y el par de
/// canales del propio layout.
///
/// \tparam W ancho del objeto en píxeles (múltiplo de 16). \tparam H alto en filas.
/// \tparam TargetPlanes planos del **destino** (las filas del blit).
/// \param pixels índices de color W×H (bit `p` del índice = plano `p`; 0 = transparente).
/// \param mask máscara W×H (0 = transparente) o `nullptr` (se deriva: `pixels != 0`).
/// \param out hoja destino: `H · TargetPlanes · (4·W/16)` palabras = por fila física
///        `[imagen k][guarda k][máscara k][guarda k]` con `k = W/16` (guardas a cero).
template <eng::u16 W, eng::u16 H, eng::u8 TargetPlanes>
constexpr void pack_interleaved_pair(const eng::u8* pixels, const eng::u8* mask, eng::u16* out) {
	constexpr eng::u16 kWords = W / 16u; // palabras por mitad (imagen / máscara)
	constexpr eng::u16 kRowWords = static_cast<eng::u16>(4u * kWords); // img+g+máscara+g
	for (eng::u16 r = 0u; r < H; ++r) {
		for (eng::u8 p = 0u; p < TargetPlanes; ++p) {
			eng::u16* row = out + (static_cast<eng::u32>(r) * TargetPlanes + p) * kRowWords;
			for (eng::u16 w = 0u; w < kWords; ++w) {
				eng::u16 img = 0u;
				eng::u16 msk = 0u;
				for (eng::u16 b = 0u; b < 16u; ++b) {
					const eng::u16 x = static_cast<eng::u16>(w * 16u + b);
					const eng::u8 v = pixels[static_cast<eng::u32>(r) * W + x];
					const bool opaque = (mask != nullptr)
								    ? (mask[static_cast<eng::u32>(r) * W + x] != 0u)
								    : (v != 0u);
					if (((v >> p) & 1u) != 0u) {
						img = static_cast<eng::u16>(img | (0x8000u >> b));
					}
					if (opaque) {
						msk = static_cast<eng::u16>(msk | (0x8000u >> b));
					}
				}
				row[w] = img;                   // imagen
				row[kWords + w] = 0u;           // guarda de la imagen
				row[2u * kWords + w] = msk;     // máscara
				row[3u * kWords + w] = 0u;      // guarda de la máscara
			}
		}
	}
}

} // namespace eng::graphics
