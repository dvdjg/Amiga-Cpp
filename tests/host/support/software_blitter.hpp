#include <cstdio>
#pragma once

/// \file software_blitter.hpp
/// **Ejecutor SOFTWARE de un `FramePlan`** sobre un bitmap de prueba (planar o interleaved):
/// réplica de la semántica del Blitter (AHRM cap. 6) tal como la aplican el **encoder único**
/// (`graphics/blit_queue.hpp:blitter_job_from`) y el **backend** (`amiga_blitter.cpp`,
/// `submit_blit_job`). Convierte cada `BlitJob` a registros y los "ejecuta" escribiendo en la
/// memoria del bitmap: permite validar por **bytes** los caminos de bobs (HOST-439) sin
/// emulador y sin duplicar expectativas a mano.
///
/// Semántica cubierta (lo que usan los caminos de BOB):
///   - canales A/B/C/D por puntero presente (los no usados aportan 0 al minterm);
///   - minterm canónico de 4 canales (`m = (minterm >> (A·8+B·4+C·2+D)) & 1`);
///   - barrel shifter de A (`ASH`) y B (`BSH`): la palabra leída se desplaza a la derecha
///     `ash` bits, entrando bits de la palabra siguiente (por eso la guarda de la hoja);
///   - `AFWM`/`ALWM` sobre la primera/última palabra de A;
///   - módulos por canal (bytes, con signo) y strides de plano (planar contiguo);
///   - interleaved (`bitplane_count == 1`, `height = h·planos`, sin stride de plano).
/// Fuera de alcance (no lo usan los bobs): LINE/LINE-EOR, C2P, FillRect y `descending`
/// (BLITREVERSE) — se marcan como `unsupported` para que el test lo detecte.

#include <eng/core/types/types.hpp>
#include <eng/graphics/blit_queue.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::test {

/// Bitmap de prueba sobre memoria del llamador (mismo contrato que `BobTarget`).
struct SoftBitmap {
	eng::u8* base = nullptr;   ///< plano 0 (o bloque interleaved)
	eng::u16 row_bytes = 0;    ///< bytes por fila de un plano
	eng::u16 height = 0;       ///< filas
	eng::u8 planes = 0;        ///< planos
	bool interleaved = false;  ///< planos intercalados por fila (bitplane_count==1)
	eng::u32 plane_step = 0u;  ///< bytes entre planos (0 = row_bytes·height)

	[[nodiscard]] eng::u32 plane_stride() const {
		if (plane_step != 0u) {
			return plane_step;
		}
		const eng::u32 rows = static_cast<eng::u32>(row_bytes) * static_cast<eng::u32>(height);
		return interleaved ? rows * planes : rows;
	}
	/// Base de un plano lógico: en planar contiguo, bloque completo; en interleaved, las
	/// filas contienen los planos consecutivos (la base del plano p está `row_bytes` más allá).
	[[nodiscard]] eng::u8* plane_base(eng::u8 p) const {
		if (interleaved) {
			return base + static_cast<eng::u32>(p) * row_bytes;
		}
		return base + static_cast<eng::u32>(p) * plane_stride();
	}
	[[nodiscard]] eng::u8* row_ptr(eng::u8 p, eng::u16 y) const {
		if (interleaved) {
			return plane_base(p) + static_cast<eng::u32>(y) * row_bytes * planes;
		}
		return plane_base(p) + static_cast<eng::u32>(y) * row_bytes;
	}
};

/// Resultado de ejecutar un plan.
struct SoftBlitterResult {
	eng::u16 jobs_executed = 0u;
	bool unsupported = false;  ///< había un job fuera de alcance (línea/c2p/fill/desc)
	bool out_of_bounds = false; ///< un write salió de la fila/plano (bug de geometría)
	eng::u16 oob_job = 0u;      ///< índice del job que desbordó (si out_of_bounds)
};

namespace soft_detail {

[[nodiscard]] inline eng::u16 read16(const eng::u8* p) {
	return static_cast<eng::u16>((static_cast<eng::u16>(p[0]) << 8u) | p[1]);
}
inline void write16(eng::u8* p, eng::u16 v) {
	p[0] = static_cast<eng::u8>(v >> 8u);
	p[1] = static_cast<eng::u8>(v & 0xffu);
}

/// Barrel shifter (WinUAE-DBG `blitter.cpp:531`): `out = (hold << (16-ash)) | (raw >> ash)`,
/// con `hold` = palabra ANTERIOR del canal (persistente; 0 al inicio del blit) y la máscara
/// `AFWM`/`ALWM` aplicada a la palabra CRUDA antes del shifter (`blitter.cpp:530`).
[[nodiscard]] inline eng::u16 barrel(eng::u16 hold, eng::u16 raw, eng::u8 ash) {
	if (ash == 0u) {
		return raw;
	}
	return static_cast<eng::u16>((static_cast<eng::u16>(hold << (16u - ash))) |
				     static_cast<eng::u16>(raw >> ash));
}

/// Minterm canónico (AHRM cap. 6): la salida es 1 cuando el bit del minterm indexado por
/// `(A,B,C)` (A = MSB, 3 bits) vale 1. El canal D NO indexa: es el valor previo del destino
/// (para `D = A|D` etc. B = D = destino, como en `bob_draw`).
[[nodiscard]] inline eng::u16 minterm4(eng::u16 m, eng::u16 a, eng::u16 b, eng::u16 c,
				       eng::u16 /*d*/) {
	eng::u16 out = 0u;
	for (eng::u8 bit = 0u; bit < 16u; ++bit) {
		const eng::u8 abc = static_cast<eng::u8>(((a >> bit) & 1u) << 2u |
							 ((b >> bit) & 1u) << 1u |
							 ((c >> bit) & 1u));
		if (((m >> abc) & 1u) != 0u) {
			out = static_cast<eng::u16>(out | (1u << bit));
		}
	}
	return out;
}

/// Ejecuta UN trabajo (vía el encoder único) sobre el bitmap. `job_index` solo etiqueta
/// diagnósticos de OOB.
inline void execute_job(const eng::graphics::BlitJob& job, eng::u16 job_index,
			const SoftBitmap& bm, SoftBlitterResult& res) {
	using eng::graphics::BlitJobKind;
	if (job.kind == BlitJobKind::Line || job.kind == BlitJobKind::LineEor ||
	    job.kind == BlitJobKind::C2P || job.kind == BlitJobKind::FillRect ||
	    job.descending) {
		res.unsupported = true;
		return;
	}

	const eng::graphics::BlitterJob r = eng::graphics::blitter_job_from(job);
	const eng::u8 ash = static_cast<eng::u8>((r.bltcon0 >> 12u) & 0x0fu);
	const eng::u8 bsh = static_cast<eng::u8>((r.bltcon1 >> 12u) & 0x0fu);
	const eng::u16 minterm = static_cast<eng::u16>(r.bltcon0 & 0x00ffu);
	const eng::u16 words = static_cast<eng::u16>(r.bltsize & 0x3fu);
	const eng::u16 height =
		static_cast<eng::u16>(r.bltsize >> 6u) == 0u ? 1024u : static_cast<eng::u16>(r.bltsize >> 6u);
	if (words == 0u) {
		res.unsupported = true; // BLTSIZE sin palabras no es un blit válido
		return;
	}

	const bool use_a = r.bltapt != nullptr;
	const bool use_b = r.bltbpt != nullptr;
	const bool use_c = r.bltcpt != nullptr;
	const bool use_d = r.bltdpt != nullptr;

	const eng::u32 plane_step = bm.plane_stride();
	const eng::u8 planes = job.bitplane_count;
	const eng::u32 src_step = job.source_plane_stride_bytes;
	const eng::u32 dst_step =
		job.destination_plane_stride_bytes != 0u ? job.destination_plane_stride_bytes : plane_step;
	// Destino FUERA del bitmap (p. ej. el buffer de save-under): no se valida contra el
	// bitmap (es un buffer del llamador); la comprobación de fila/plano solo aplica al
	// bitmap bajo prueba.
	const eng::u8* const bm_lo = bm.base;
	const eng::u32 block_bytes = bm.interleaved
		? static_cast<eng::u32>(bm.row_bytes) * bm.height * bm.planes
		: bm.plane_stride() * bm.planes;
	const eng::u8* const bm_hi = bm.base + block_bytes;
	const bool d_external = r.bltdpt != nullptr &&
				(static_cast<const eng::u8*>(r.bltdpt) < bm_lo ||
				 static_cast<const eng::u8*>(r.bltdpt) >= bm_hi);
	// Paso FÍSICO de fila del canal D: lo que avanza el Blitter por fila (`words·2 + DMOD`).
	// No se asume `row_bytes`: en interleaved con blit POR PLANO el paso es `row_bytes·planos`.
	const eng::s32 d_row_step = static_cast<eng::s32>(words) * 2 + r.bltdmod;
	// El avance POR PLANO de cada canal sigue al backend (`amiga_blitter.cpp:392-429`):
	// en el cookie-cut planar la máscara (A) es un único plano (NO avanza), la imagen (B)
	// avanza con el stride de la hoja y C/D con el del destino; en or_blob B = D = destino.
	const bool masked_kind = job.kind == eng::graphics::BlitJobKind::MaskedBobCookieCut ||
				 job.kind == eng::graphics::BlitJobKind::MaskedBlobNoSave;
	const eng::u32 a_step = masked_kind ? 0u : src_step;
	const eng::u32 b_step = masked_kind ? src_step : dst_step;
	const eng::u32 c_step = masked_kind ? dst_step : src_step;
	const eng::u32 d_step = dst_step;

	for (eng::u8 plane = 0u; plane < planes; ++plane) {
		const eng::u8* a_ptr = use_a ? static_cast<const eng::u8*>(r.bltapt) + plane * a_step
					     : nullptr;
		const eng::u8* b_ptr = use_b ? static_cast<const eng::u8*>(r.bltbpt) + plane * b_step
					     : nullptr;
		const eng::u8* c_ptr = use_c ? static_cast<const eng::u8*>(r.bltcpt) + plane * c_step
					     : nullptr;
		eng::u8* d_ptr = use_d ? static_cast<eng::u8*>(r.bltdpt) + plane * d_step : nullptr;

		// Región del plano para la detección de OOB: en planar contiguo es su bloque; en
		// interleaved los planos **comparten** el bloque (las filas físicas intercalan los
		// planos), así que la banda válida es el bloque entero.
		const eng::u8* plane_lo = nullptr;
		const eng::u8* plane_hi = nullptr;
		if (bm.interleaved) {
			plane_lo = bm.base;
			plane_hi = bm.base + block_bytes;
		} else {
			plane_lo = bm.base + static_cast<eng::u32>(plane) * plane_step;
			plane_hi = plane_lo + plane_step;
		}
		const eng::u8* d0 = use_d ? static_cast<const eng::u8*>(r.bltdpt) + plane * d_step
					  : nullptr;
		const eng::u32 start_row =
			d0 != nullptr && d0 >= plane_lo && d_row_step > 0
				? static_cast<eng::u32>(d0 - plane_lo) /
					  static_cast<eng::u32>(d_row_step)
				: 0u;

		// Holds del barrel shifter (por canal, persisten entre filas como en el hardware).
		eng::u16 hold_a = 0u;
		eng::u16 hold_b = 0u;
		for (eng::u16 row = 0u; row < height; ++row) {
			for (eng::u16 w = 0u; w < words; ++w) {
				eng::u16 a = 0u;
				if (use_a) {
					// Máscara de primera/última palabra (masktable) sobre la palabra cruda.
					const eng::u16 mask = w == 0u ? r.bltafwm
							      : (w + 1u == words ? r.bltalwm : 0xffffu);
					const eng::u16 raw =
						static_cast<eng::u16>(soft_detail::read16(a_ptr + w * 2u) & mask);
					a = soft_detail::barrel(hold_a, raw, ash);
					hold_a = raw;
				}
				eng::u16 b = 0u;
				if (use_b) {
					const eng::u16 raw = soft_detail::read16(b_ptr + w * 2u);
					b = soft_detail::barrel(hold_b, raw, bsh);
					hold_b = raw;
				}
				const eng::u16 c = use_c ? soft_detail::read16(c_ptr + w * 2u) : 0u;
				const eng::u16 d = use_d ? soft_detail::read16(d_ptr + w * 2u) : 0u;
#ifdef SOFT_BLIT_DEBUG
				std::printf("[blit] plane=%u row=%u w=%u a=%04x b=%04x c=%04x d=%04x dst=%p\n",
					    plane, row, w, a, b, c, d, static_cast<void*>(d_ptr + w * 2u));
#endif
				if (use_d) {
					eng::u8* const dst = d_ptr + w * 2u;
					const eng::u8* const row_base =
						plane_lo +
						static_cast<eng::u32>(start_row + row) *
							static_cast<eng::u32>(d_row_step);
					if (!d_external &&
					    (dst < plane_lo || dst + 2u > plane_hi ||
					     dst < row_base ||
					     dst + 2u > row_base + static_cast<eng::u32>(d_row_step))) {
						res.out_of_bounds = true;
						res.oob_job = job_index;
					} else {
						soft_detail::write16(dst, soft_detail::minterm4(minterm, a, b, c, d));
					}
				}
			}
			// Avance por fila del Blitter: palabras procesadas + módulo (los punteros
			// avanzan `words·2 + modulo` por fila, no solo el módulo).
			if (use_a) a_ptr += static_cast<eng::s32>(words) * 2 + r.bltamod;
			if (use_b) b_ptr += static_cast<eng::s32>(words) * 2 + r.bltbmod;
			if (use_c) c_ptr += static_cast<eng::s32>(words) * 2 + r.bltcmod;
			if (use_d) d_ptr += static_cast<eng::s32>(words) * 2 + r.bltdmod;
		}
	}
}

} // namespace soft_detail

/// Ejecuta el plan completo sobre `bm` (en orden de emisión).
[[nodiscard]] inline SoftBlitterResult execute_plan(const eng::graphics::FramePlan& plan,
						    const SoftBitmap& bm) {
	SoftBlitterResult res {};
	for (eng::u16 i = 0u; i < plan.blit_job_count(); ++i) {
		soft_detail::execute_job(plan.blit_job(i), i, bm, res);
		++res.jobs_executed;
	}
	return res;
}

/// Píxel del plano `p` en (x, y) (bit 15 = píxel izquierdo del word).
[[nodiscard]] inline bool soft_pixel(const SoftBitmap& bm, eng::s16 x, eng::s16 y, eng::u8 p) {
	const eng::u8* row = bm.row_ptr(p, static_cast<eng::u16>(y));
	return ((row[x >> 3] >> (7 - (x & 7))) & 1u) != 0u;
}

/// Valor de color (0..2^planes-1) del píxel en (x, y).
[[nodiscard]] inline eng::u8 soft_color(const SoftBitmap& bm, eng::s16 x, eng::s16 y) {
	eng::u8 v = 0u;
	for (eng::u8 p = 0u; p < bm.planes; ++p) {
		v = static_cast<eng::u8>(v | (soft_pixel(bm, x, y, p) ? (1u << p) : 0u));
	}
	return v;
}

} // namespace eng::test
