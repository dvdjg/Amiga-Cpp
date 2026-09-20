#pragma once

/// \file frame_plan.hpp
/// Descripcion minima de trabajos de render para un frame.
///
/// Este archivo empieza a separar tres niveles:
///
/// - la logica de juego/effectos dice que quiere cambiar;
/// - el `FramePlan` recoge esas intenciones de forma portable;
/// - el driver Amiga decide si las materializa como parches de copperlist, blits,
///   sprites hardware, CPU writes o cualquier otro mecanismo.
///
/// La primera version contenia solo parches de paleta. Ahora tambien describe
/// operaciones de Blitter para que las demos puedan pedir copias, save/restore y
/// BOBs sin escribir registros custom desde el juego. La tercera pieza es la lista
/// de dirty rects: las areas de pantalla que un frame ha tocado y que, por tanto,
/// pueden necesitar restauracion, redraw o analisis de presupuesto.
///
/// ```text
///   lógica de juego/efectos          FramePlan (portable)               driver Amiga
///   ──────────────────────           ────────────────────               ────────────
///   "cambia paleta 1..7"  ──►  PalettePatch[] ─┐
///   "copia/restaura BOB"  ──►  BlitJob[]      ─┼──► decide la materialización: parches de
///                                              │    copperlist · blits · sprites hardware ·
///   área tocada           ──►  dirty rects   ─┘    escrituras CPU (según el backend)
/// ```

#include <eng/core/arith.hpp>
#include <eng/core/domains.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>

namespace eng::graphics {

/// Lugar logico donde se aplicara un parche de paleta.
enum class PalettePatchTarget : u8 {
	Base,
	Zone,
};

/// Cambio de uno o varios colores fisicos RGB444.
///
/// `colors` es la paleta de dominio (`PaletteWords`, con su tamaño); `first/count`
/// selecciona el tramo que se quiere aplicar. Un efecto puede pedir "actualiza
/// colores 1..7" pasando su paleta completa sin crear arrays temporales.
struct PalettePatch {
	PalettePatchTarget target = PalettePatchTarget::Base;
	u8 line = 0;
	u8 first = 0;
	u8 count = 0;
	eng::PaletteWords colors {};
};

/// Tipos de trabajo de Blitter soportados por el plan actual.
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
	/// **Línea por Blitter** (`BLTCON1` LINE): usa `line_x0..line_y1` y
	/// `line_row_bytes`; el `destination` apunta al plano. Sin fuentes ni mascara.
	Line,
	/// **Línea EOR (ONEDOT)** (`blitter_line_eor`): como `Line` pero XOR con `line_base`
	/// (el canal D), base del contorno XOR de `flatshade-convex` (demo 116).
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
	/// **Chunky→planar por Blitter** (13 fases): convierte `c2p_chunky` a los 4 planos
	/// `c2p_planes` (ver `MinimalBackend::c2p_4bpp_step`). Es la vía Blitter del seam
	/// `Rasterizer::c2p`.
	C2P,
};

/// Presupuesto acumulado de Blitter.
///
/// Es una estimacion deliberadamente sencilla: words procesadas por plano. No
/// intenta predecir todavia ciclos exactos de bus, pero ya permite comparar un BOB
/// de 16x16 con uno de 64x64 y fallar tests si una escena crece sin control.
struct BlitBudget {
	u16 jobs = 0;
	u32 words = 0;
	u16 masked_jobs = 0;
	u16 copy_jobs = 0;
	u16 no_save_jobs = 0;
	u16 tile_jobs = 0;
};

/// Severidad del presupuesto de Blitter para un frame.
///
/// `Warning` significa que el frame todavia se puede materializar, pero la escena
/// esta entrando en una zona cara que conviene mostrar en telemetria. `Exceeded`
/// no descarta jobs automaticamente: el driver o la demo decide si degrada,
/// difiere trabajo o falla. Separar el diagnostico de la ejecucion permite usar el
/// mismo `FramePlan` en demos didacticas y en futuros drivers mas agresivos.
enum class BlitBudgetStatus : u8 {
	Ok,
	Warning,
	Exceeded,
};

/// Limites configurables de Blitter.
///
/// Los limites son intencionadamente abstractos: words procesadas por plano y
/// cantidad de jobs. Todavia no modelan ciclos exactos del bus Amiga, pero ya
/// fijan una frontera verificable para evitar que un frame crezca sin control.
struct BlitBudgetLimits {
	u32 warning_words = 0xffffffffu;
	u32 max_words = 0xffffffffu;
	u16 warning_jobs = 0xffffu;
	u16 max_jobs = 0xffffu;
};

/// Informe derivado de comparar `BlitBudget` contra `BlitBudgetLimits`.
struct BlitBudgetReport {
	BlitBudgetStatus status = BlitBudgetStatus::Ok;
	bool words_warning = false;
	bool words_exceeded = false;
	bool jobs_warning = false;
	bool jobs_exceeded = false;
};

/// Rectangulo de pantalla en pixels.
///
/// Usamos coordenadas enteras pequenas y bordes exclusivos (`right/bottom`). Es el
/// formato mas comodo para fusionar rectangulos y para convertir despues a words
/// de Blitter, tiles o regiones de captura.
struct DirtyRect {
	s16 left = 0;
	s16 top = 0;
	s16 right = 0;
	s16 bottom = 0;

	constexpr bool valid() const {
		return right > left && bottom > top;
	}

	constexpr u16 width() const {
		return valid() ? static_cast<u16>(right - left) : 0;
	}

	constexpr u16 height() const {
		return valid() ? static_cast<u16>(bottom - top) : 0;
	}
};

/// Metricas de dirty rects tras fusionar.
struct DirtyReport {
	u8 rects = 0;
	u16 area = 0;
	u16 merges = 0;
	bool overflow = false;
};

/// Trabajo planar de Blitter.
///
/// `CopyRect`, `RestoreRect` y `TileBlockCopy` son copias rectangulares:
///
/// `dest = source`
///
/// `TileBlockCopy` existe como categoria propia aunque use el mismo minterm de
/// copia. Su contrato representa cargas de tiles/metatiles hacia zonas no visibles
/// del playfield: columnas nuevas de scroll, buffers de staging, mapas retenidos o
/// cualquier region que se puede sobrescribir completa sin save/restore.
///
/// `MaskedBobCookieCut` y `MaskedBlobNoSave` usan el clasico cookie-cut:
///
/// `dest = (mask & source) | (~mask & dest)`
///
/// La diferencia entre ambos en esta fase es semantica y de presupuesto:
/// `MaskedBobCookieCut` representa un actor que normalmente necesitara save/restore
/// si se mueve; `MaskedBlobNoSave` representa la tecnica tipo Mega Typhoon para
/// blobs no solapados o regiones de playfield que se pueden sobrescribir sin guardar
/// el fondo previo.
///
/// Restricciones de esta primera version:
///
/// - `destination` debe apuntar a una posicion alineada a word dentro del primer
///   bitplane de destino;
/// - `source_shift` permite desplazar A/B de 0..15 pixels para X no alineada;
/// - no hay clipping automatico;
/// - si `kind` es enmascarado, `mask` es un unico plano de 1 bit compartido por
///   todos los bitplanes;
/// - `source` contiene los planos del BOB/rect en formato planar contiguo.
///
/// Son restricciones intencionadas: nos dan una base verificable antes de meter
/// scroll fino, clipping, restauracion de fondo y dirty rects.
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
	// --- Línea (BlitJobKind::Line): coordenadas + módulo de fila del plano -----------
	s16 line_x0 = 0;        ///< x del punto inicial
	s16 line_y0 = 0;        ///< y del punto inicial
	s16 line_x1 = 0;        ///< x del punto final
	s16 line_y1 = 0;        ///< y del punto final
	u16 line_row_bytes = 0; ///< bytes por fila del plano destino (módulo de la línea)
	/// Base del bitmap para el canal D en una **línea EOR** (`BlitJobKind::LineEor`);
	/// `nullptr` = usar el propio plano. Ver `blitter_line_eor`.
	BlitDest line_base {};
	// --- C2P (BlitJobKind::C2P): chunky -> planar por fases -------------------------
	/// Buffer chunky en Chip RAM; su segunda mitad (`+ c2p_bytes`) es el staging planar
	/// que el C2P usa como destino intermedio.
	eng::u8* c2p_chunky = nullptr;
	/// Base de los 4 planos destino (a `c2p_planes + p*c2p_plane_stride`).
	eng::u8* c2p_planes = nullptr;
	/// Bytes entre planos destino.
	u32 c2p_plane_stride = 0;
	/// `bytes` del C2P (p. ej. `width*height/2` en 4 bpp); fija el `BLTSIZE`.
	u16 c2p_bytes = 0;
};

/// Plan de render de un frame.
///
/// Es un contenedor fijo, sin heap y sin STL. Cuando se llene, `ok()` pasa a false
/// para que la demo o el driver pueda fallar de forma controlada. En un engine de
/// Amiga es mejor rechazar un plan demasiado grande que degradarse en silencio y
/// descubrir corrupcion visual varios sistemas mas tarde.
class FramePlan {
public:
	static constexpr u8 max_palette_patches = 8;
	// A dual-playfield ring crossing both axes needs 12 shift copies plus 80
	// tile uploads (two playfields), so the former limit of 64 rejected a valid
	// frame plan before the backend could run it.
	static constexpr u8 max_blit_jobs = 128;
	static constexpr u8 max_dirty_rects = 8;

	void clear() {
		m_palette_patch_count = 0;
		m_blit_job_count = 0;
		m_dirty_rect_count = 0;
		m_blit_budget = {};
		m_blit_budget_report = {};
		m_dirty_report = {};
		m_ok = true;
	}

	bool add_base_palette_patch(eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Base, 0, first, count, colors});
	}

	bool add_zone_palette_patch(u8 line, eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Zone, line, first, count, colors});
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u8 palette_patch_count() const { return m_palette_patch_count; }
	constexpr u8 blit_job_count() const { return m_blit_job_count; }
	constexpr u8 dirty_rect_count() const { return m_dirty_rect_count; }
	constexpr const BlitBudget& blit_budget() const { return m_blit_budget; }
	constexpr const BlitBudgetLimits& blit_budget_limits() const { return m_blit_budget_limits; }
	constexpr const BlitBudgetReport& blit_budget_report() const { return m_blit_budget_report; }
	constexpr const DirtyReport& dirty_report() const { return m_dirty_report; }

	void set_blit_budget_limits(BlitBudgetLimits limits) {
		m_blit_budget_limits = limits;
		rebuild_blit_budget_report();
	}

	constexpr const PalettePatch& palette_patch(u8 index) const {
		return m_palette_patches[index];
	}

	constexpr const BlitJob& blit_job(u8 index) const {
		return m_blit_jobs[index];
	}

	constexpr const DirtyRect& dirty_rect(u8 index) const {
		return m_dirty_rects[index];
	}

	/// Anade un rectangulo sucio fusionandolo con los existentes.
	///
	/// Dos rectangulos se fusionan si se solapan o se tocan. Esto evita trabajos
	/// pequenos redundantes cuando un BOB se mueve poco: el area anterior y la nueva
	/// suelen formar una unica banda que conviene tratar junta a nivel de scheduler,
	/// aunque internamente el Blitter siga haciendo save/restore/draw concretos.
	bool add_dirty_rect(DirtyRect rect) {
		if (!rect.valid()) {
			m_ok = false;
			return false;
		}

		for (u8 i = 0; i < m_dirty_rect_count; ++i) {
			if (rects_touch_or_overlap(m_dirty_rects[i], rect)) {
				m_dirty_rects[i] = union_rect(m_dirty_rects[i], rect);
				++m_dirty_report.merges;
				rebuild_dirty_report();
				return true;
			}
		}

		if (m_dirty_rect_count >= max_dirty_rects) {
			m_dirty_report.overflow = true;
			m_ok = false;
			return false;
		}

		m_dirty_rects[m_dirty_rect_count++] = rect;
		rebuild_dirty_report();
		return true;
	}

	bool add_masked_bob(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::MaskedBobCookieCut);
	}

	bool add_masked_blob_no_save(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::MaskedBlobNoSave);
	}

	bool add_copy_rect(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::CopyRect);
	}

	bool add_restore_rect(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::RestoreRect);
	}

	bool add_tile_block_copy(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::TileBlockCopy);
	}

	/// Borrado de un rectangulo (solo D, minterm `$00`). Con `interleaved` borra la
	/// caja de un objeto en UN blit.
	bool add_clear_rect(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::ClearRect);
	}

	/// BOB OR (aditivo) por desplazamiento: `A`=objeto, `B=D`=destino, minterm `$FC`.
	/// El llamador rellena `source`/`destination` y deja `mask` vacia.
	__attribute__((always_inline)) inline bool add_or_blob(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::OrBlob);
	}

	/// **Blit con operación lógica** (`B = D`, minterm en `job.minterm`): `A` = fuente.
	/// El llamador fija `source`/`destination` y el minterm (`Or`/`And`/`Xor`).
	__attribute__((always_inline)) inline bool add_logic_blit(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::LogicBlit);
	}

	/// **Relleno con patrón** (`A` = patrón, `B = D` = destino, minterm `$FC`): el patrón es
	/// una fila de `words_per_row` palabras repetida en vertical con `source_modulo_bytes`.
	/// El llamador fija `source` (fila del patrón), `destination`, `words_per_row`,
	/// `height`, `bitplane_count` y `source_modulo_bytes = -(words_per_row*2)`.
	__attribute__((always_inline)) inline bool add_pattern_fill(const BlitJob& job) {
		return add_blit_job(job, BlitJobKind::PatternFill);
	}

	/// **Chunky→planar por Blitter** (`BlitJobKind::C2P`): `c2p_chunky` (Chip RAM, 2×
	/// `c2p_bytes`) → `c2p_planes` (4 planos). La vía Blitter del seam `Rasterizer::c2p`.
	bool add_c2p(const BlitJob& job) {
		if (job.c2p_chunky == nullptr || job.c2p_planes == nullptr ||
		    job.c2p_bytes == 0u || job.c2p_plane_stride == 0u) {
			m_ok = false;
			return false;
		}
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}
		BlitJob j = job;
		j.kind = BlitJobKind::C2P;
		m_blit_jobs[m_blit_job_count++] = j;
		m_blit_budget.jobs = m_blit_job_count;
		m_blit_budget.words += eng::math::mulu16(job.c2p_bytes, 13u); // 13 fases
		++m_blit_budget.copy_jobs;
		rebuild_blit_budget_report();
		return true;
	}

	/// **Línea por Blitter** (`BLTCON1` LINE) o **EOR/ONEDOT** (`LineEor`): `destination`
	/// = plano, `line_x0..line_y1` las coordenadas y `line_row_bytes` el módulo de fila.
	/// Sin fuentes/máscara. Para `LineEor`, `line_base` es la base del canal D.
	bool add_line(const BlitJob& job, BlitJobKind kind = BlitJobKind::Line) {
		if (job.destination.words == nullptr || job.line_row_bytes == 0u ||
		    job.bitplane_count == 0u) {
			m_ok = false;
			return false;
		}
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}
		BlitJob j = job;
		j.kind = kind;
		m_blit_jobs[m_blit_job_count++] = j;
		m_blit_budget.jobs = m_blit_job_count;
		// Coste aproximado: una word por fila de la línea + 2 de arranque por plano
		// (`mulu16` = `mulu.w`, sin `__mulsi3`).
		const s32 dy = job.line_y1 > job.line_y0 ? job.line_y1 - job.line_y0
							 : job.line_y0 - job.line_y1;
		m_blit_budget.words += eng::math::mulu16(static_cast<u16>(dy + 3),
							 job.bitplane_count);
		++m_blit_budget.copy_jobs;
		rebuild_blit_budget_report();
		return true;
	}

private:
	static constexpr s16 min_s16(s16 a, s16 b) { return a < b ? a : b; }
	static constexpr s16 max_s16(s16 a, s16 b) { return a > b ? a : b; }

	static constexpr bool rects_touch_or_overlap(const DirtyRect& a, const DirtyRect& b) {
		return !(a.right < b.left || b.right < a.left || a.bottom < b.top || b.bottom < a.top);
	}

	static constexpr DirtyRect union_rect(const DirtyRect& a, const DirtyRect& b) {
		return {
			min_s16(a.left, b.left),
			min_s16(a.top, b.top),
			max_s16(a.right, b.right),
			max_s16(a.bottom, b.bottom),
		};
	}

	void rebuild_dirty_report() {
		const u16 previous_merges = m_dirty_report.merges;
		const bool previous_overflow = m_dirty_report.overflow;
		m_dirty_report = {};
		m_dirty_report.rects = m_dirty_rect_count;
		m_dirty_report.merges = previous_merges;
		m_dirty_report.overflow = previous_overflow;

		for (u8 i = 0; i < m_dirty_rect_count; ++i) {
			m_dirty_report.area = static_cast<u16>(
				m_dirty_report.area +
				static_cast<u16>(m_dirty_rects[i].width() * m_dirty_rects[i].height())
			);
		}
	}

	/// Camino caliente (1 vez por BOB): `always_inline` para no pagar un `jsr` por objeto
	/// ni recargar `m_blit_job_count`/`m_blit_budget` desde memoria en cada anadido.
	__attribute__((always_inline)) inline bool add_blit_job(const BlitJob& input, BlitJobKind kind) {
		BlitJob job = input;
		job.kind = kind;
		const bool masked =
			job.kind == BlitJobKind::MaskedBobCookieCut ||
			job.kind == BlitJobKind::MaskedBlobNoSave;
		const bool clear = job.kind == BlitJobKind::ClearRect;
		if (
			(!clear && job.source.words == nullptr) ||
			job.destination.words == nullptr ||
			job.words_per_row == 0 ||
			job.height == 0 ||
			job.bitplane_count == 0 ||
			job.source_shift >= 16u ||
			(!clear && job.source_plane_stride_bytes == 0 && !job.interleaved) ||
			(job.destination_plane_stride_bytes == 0 && !job.interleaved)
		) {
			m_ok = false;
			return false;
		}
		if (masked && job.mask.words == nullptr) {
			m_ok = false;
			return false;
		}
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}

		m_blit_jobs[m_blit_job_count++] = job;
		m_blit_budget.jobs = m_blit_job_count;
		m_blit_budget.words += eng::math::mulu32x16(
			eng::math::mulu16(job.words_per_row, job.height),
			static_cast<u16>(job.bitplane_count));
		if (masked) {
			++m_blit_budget.masked_jobs;
		} else {
			++m_blit_budget.copy_jobs;
		}
		if (job.kind == BlitJobKind::MaskedBlobNoSave) {
			++m_blit_budget.no_save_jobs;
		}
		if (job.kind == BlitJobKind::TileBlockCopy) {
			++m_blit_budget.tile_jobs;
		}
		rebuild_blit_budget_report();
		return true;
	}

	void rebuild_blit_budget_report() {
		m_blit_budget_report = {};
		m_blit_budget_report.words_warning = m_blit_budget.words > m_blit_budget_limits.warning_words;
		m_blit_budget_report.words_exceeded = m_blit_budget.words > m_blit_budget_limits.max_words;
		m_blit_budget_report.jobs_warning = m_blit_budget.jobs > m_blit_budget_limits.warning_jobs;
		m_blit_budget_report.jobs_exceeded = m_blit_budget.jobs > m_blit_budget_limits.max_jobs;

		if (m_blit_budget_report.words_exceeded || m_blit_budget_report.jobs_exceeded) {
			m_blit_budget_report.status = BlitBudgetStatus::Exceeded;
		} else if (m_blit_budget_report.words_warning || m_blit_budget_report.jobs_warning) {
			m_blit_budget_report.status = BlitBudgetStatus::Warning;
		}
	}

	bool add_palette_patch(PalettePatch patch) {
		if (patch.colors.empty() || patch.first >= 32u || patch.count == 0u) {
			m_ok = false;
			return false;
		}
		// La vista debe cubrir el tramo pedido.
		if (static_cast<eng::u32>(patch.first) + patch.count > patch.colors.size()) {
			m_ok = false;
			return false;
		}
		if (patch.first + patch.count > 32u) {
			patch.count = static_cast<u8>(32u - patch.first);
		}
		if (m_palette_patch_count >= max_palette_patches) {
			m_ok = false;
			return false;
		}

		m_palette_patches[m_palette_patch_count++] = patch;
		return true;
	}

	eng::util::Array<PalettePatch, max_palette_patches> m_palette_patches {};
	eng::util::Array<BlitJob, max_blit_jobs> m_blit_jobs {};
	eng::util::Array<DirtyRect, max_dirty_rects> m_dirty_rects {};
	BlitBudget m_blit_budget {};
	BlitBudgetLimits m_blit_budget_limits {};
	BlitBudgetReport m_blit_budget_report {};
	DirtyReport m_dirty_report {};
	u8 m_palette_patch_count = 0;
	u8 m_blit_job_count = 0;
	u8 m_dirty_rect_count = 0;
	bool m_ok = true;
};

} // namespace eng::graphics
