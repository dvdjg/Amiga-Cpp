#pragma once

/// \file blit_job.hpp
/// **Trabajo de Blitter**: el tipo de operación (`BlitJobKind`), los roles tipados de
/// fuente/destino (`BlitSource`/`BlitDest`) y los datos del trabajo (`BlitJob`).
///
/// Se separa de `frame_plan.hpp` (que contiene el plan y su presupuesto) para que quien
/// solo describe un trabajo no arrastre el contenedor, y los campos específicos de cada
/// operación se agrupan en sub-structs (`job.line`, `job.c2p`) en vez de convivir planos
/// en un único bloque: así no se mezcla el campo de una operación con el de otra.
///
/// Los campos **comunes** (fuente/destino, tamaño, módulos, planos, minterm) valen para
/// todas las operaciones; los grupos `line` y `c2p` solo se leen para sus tipos.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

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

/// Rol de **origen** de un blit (solo lectura). Junto con `BlitDest` evita pasar
/// un `BlitDest` donde se espera un `BlitSource` (o viceversa) en las firmas
/// internas. La construcción desde crudo es implícita por ergonomía de los
/// agregados (`BlitJob{...}`); el rol tipado no se convierte entre sí.
struct BlitSource {
	const u16* words = nullptr;
	constexpr BlitSource() noexcept = default;
	constexpr BlitSource(const u16* w) noexcept : words(w) {}
};

/// Rol de **destino** de un blit (escritura).
struct BlitDest {
	u16* words = nullptr;
	constexpr BlitDest() noexcept = default;
	constexpr BlitDest(u16* w) noexcept : words(w) {}
};

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
	BlitSource mask {};
	BlitSource source {};
	BlitDest destination {};
	u16 words_per_row = 0;
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
		BlitDest base {};
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
};

/// Configura `job` como **BOB interleaved enmascarado en UNA pasada** (cookie-cut `$CA` con
/// **máscara expandida**: una copia de la máscara por plano). `src` apunta al par
/// `[máscara `w/16` palabras][imagen `w/16` palabras]` de la primera fila del BOB (layout que
/// produce `kingcon ... -Interleaved -Format=N -Mask`); `dest` al bitmap interleaved en
/// `x & ~15`; `w`/`h` = tamaño en píxeles (`w` múltiplo de 16); `planes` = planos del bitmap;
/// `dest_row_bytes` = bytes de **una fila de un plano**; `shift` = `x & 15`.
///
/// Un solo blit recorre `h*planes` filas: `A` = máscara, `B` = imagen, `DMOD` = fila de plano.
/// Ver `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`. El job queda listo
/// para `FramePlan::add_masked_bob` o `AmigaBackend::blitter_submit`.
inline void make_interleaved_masked_bob(BlitJob& job, const u16* src, u16* dest, u16 w, u16 h,
					u8 planes, u16 dest_row_bytes, u8 shift) noexcept {
	const u16 words = static_cast<u16>(w / 16u);
	job = BlitJob {};
	job.kind = BlitJobKind::MaskedBobCookieCut;
	job.mask = BlitSource {src};
	job.source = BlitSource {src + words}; // 2ª mitad de la fila = imagen
	job.destination = BlitDest {dest};
	job.words_per_row = words;
	job.height = static_cast<u16>(h * planes);
	// Avance por "fila" (una fila de UN plano): el par ocupa `2*words` palabras.
	job.source_modulo_bytes = static_cast<s16>(words * 2u);
	job.destination_modulo_bytes = static_cast<s16>(dest_row_bytes - words * 2u);
	job.bitplane_count = 1u;
	job.source_shift = shift;
	job.minterm = 0xCAu;
	job.interleaved = true;
}

} // namespace eng::graphics
