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
/// BOBs sin escribir registros custom desde el juego.

#include <amg/core/types.hpp>

namespace amg::graphics {

/// Lugar logico donde se aplicara un parche de paleta.
enum class PalettePatchTarget : u8 {
	Base,
	Zone,
};

/// Cambio de uno o varios colores fisicos RGB444.
///
/// `colors` apunta a una paleta completa de 32 entradas; `first/count` selecciona
/// el tramo que se quiere aplicar. La razon de apuntar a la paleta completa es
/// mantener el contrato igual que `CopperScheduler::emit_palette()`: un efecto
/// puede pedir "actualiza colores 1..7" sin crear arrays temporales.
struct PalettePatch {
	PalettePatchTarget target = PalettePatchTarget::Base;
	u8 line = 0;
	u8 first = 0;
	u8 count = 0;
	const u16* colors = nullptr;
};

/// Tipos de trabajo de Blitter soportados por el plan actual.
enum class BlitJobKind : u8 {
	CopyRect,
	RestoreRect,
	MaskedBobCookieCut,
	MaskedBlobNoSave,
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
};

/// Trabajo planar de Blitter.
///
/// `CopyRect` y `RestoreRect` son copias rectangulares:
///
/// `dest = source`
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
/// - no hay shifts de Blitter, asi que X debe ser multiplo de 16 pixels;
/// - no hay clipping automatico;
/// - si `kind` es enmascarado, `mask` es un unico plano de 1 bit compartido por
///   todos los bitplanes;
/// - `source` contiene los planos del BOB/rect en formato planar contiguo.
///
/// Son restricciones intencionadas: nos dan una base verificable antes de meter
/// scroll fino, clipping, restauracion de fondo y dirty rects.
struct BlitJob {
	BlitJobKind kind = BlitJobKind::MaskedBobCookieCut;
	const u16* mask = nullptr;
	const u16* source = nullptr;
	u16* destination = nullptr;
	u16 words_per_row = 0;
	u16 height = 0;
	s16 source_modulo_bytes = 0;
	s16 destination_modulo_bytes = 0;
	u8 bitplane_count = 0;
	u32 source_plane_stride_bytes = 0;
	u32 destination_plane_stride_bytes = 0;
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
	static constexpr u8 max_blit_jobs = 8;

	void clear() {
		m_palette_patch_count = 0;
		m_blit_job_count = 0;
		m_blit_budget = {};
		m_ok = true;
	}

	bool add_base_palette_patch(const u16* colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Base, 0, first, count, colors});
	}

	bool add_zone_palette_patch(u8 line, const u16* colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Zone, line, first, count, colors});
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u8 palette_patch_count() const { return m_palette_patch_count; }
	constexpr u8 blit_job_count() const { return m_blit_job_count; }
	constexpr const BlitBudget& blit_budget() const { return m_blit_budget; }

	constexpr const PalettePatch& palette_patch(u8 index) const {
		return m_palette_patches[index];
	}

	constexpr const BlitJob& blit_job(u8 index) const {
		return m_blit_jobs[index];
	}

	bool add_masked_bob(const BlitJob& job) {
		BlitJob copy = job;
		copy.kind = BlitJobKind::MaskedBobCookieCut;
		return add_blit_job(copy);
	}

	bool add_masked_blob_no_save(const BlitJob& job) {
		BlitJob copy = job;
		copy.kind = BlitJobKind::MaskedBlobNoSave;
		return add_blit_job(copy);
	}

	bool add_copy_rect(const BlitJob& job) {
		BlitJob copy = job;
		copy.kind = BlitJobKind::CopyRect;
		return add_blit_job(copy);
	}

	bool add_restore_rect(const BlitJob& job) {
		BlitJob copy = job;
		copy.kind = BlitJobKind::RestoreRect;
		return add_blit_job(copy);
	}

private:
	bool add_blit_job(BlitJob job) {
		const bool masked =
			job.kind == BlitJobKind::MaskedBobCookieCut ||
			job.kind == BlitJobKind::MaskedBlobNoSave;
		if (
			job.source == nullptr ||
			job.destination == nullptr ||
			job.words_per_row == 0 ||
			job.height == 0 ||
			job.bitplane_count == 0 ||
			job.source_plane_stride_bytes == 0 ||
			job.destination_plane_stride_bytes == 0
		) {
			m_ok = false;
			return false;
		}
		if (masked && job.mask == nullptr) {
			m_ok = false;
			return false;
		}
		if (m_blit_job_count >= max_blit_jobs) {
			m_ok = false;
			return false;
		}

		m_blit_jobs[m_blit_job_count++] = job;
		m_blit_budget.jobs = m_blit_job_count;
		m_blit_budget.words +=
			static_cast<u32>(job.words_per_row) *
			static_cast<u32>(job.height) *
			static_cast<u32>(job.bitplane_count);
		if (masked) {
			++m_blit_budget.masked_jobs;
		} else {
			++m_blit_budget.copy_jobs;
		}
		if (job.kind == BlitJobKind::MaskedBlobNoSave) {
			++m_blit_budget.no_save_jobs;
		}
		return true;
	}

	bool add_palette_patch(PalettePatch patch) {
		if (patch.colors == nullptr || patch.first >= 32u || patch.count == 0u) {
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

	PalettePatch m_palette_patches[max_palette_patches] {};
	BlitJob m_blit_jobs[max_blit_jobs] {};
	BlitBudget m_blit_budget {};
	u8 m_palette_patch_count = 0;
	u8 m_blit_job_count = 0;
	bool m_ok = true;
};

} // namespace amg::graphics
