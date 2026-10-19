// ============================================================================
// HOST-482: recorte de caras al bitmap en el relleno por plano (camino Blitter)
// ============================================================================
//
// El trazador de lineas del Blitter escribe cada pixel UN PASO por detras: una
// vertical dada en `x` se dibuja en `x-1` (y sus filas, un paso mas abajo). Por
// eso un vertice con `x == width` (un pixel fuera, como deja el clip de camara)
// cae en la primera palabra de la fila siguiente y uno en `x == 0` se dibuja en
// `x == -1`, que por el complemento a dos cae en la cola de la fila anterior. El
// contorno queda fuera del bbox de la cara y el clear por rectangulo sucio no lo
// borra: residuo permanente en x≈width-1 (observado en la demo 088 con clip
// lateral y aislado con un watchpoint sobre la palabra del borde; el dump "solo
// contornos" de L1-003 muestra la vertical de x=0 pintada en x=319 de la fila
// anterior).
//
// `clip_face_to_rect` recorta cada cara al rectangulo SEGURO
// [min_x,max_x] x [min_y,max_y]; el camino del Blitter usa min_x=1 (margen
// izquierdo que absorbe el desfase del trazador) y max_x/max_y = width-1/height-1
// (ultimo pixel incluido; el trazador lo dibuja dentro). Asi las lineas nunca
// salen del bitmap y el contorno queda CERRADO en el borde (lo que el FILL_XOR
// necesita para la paridad par dentro de la caja).
// ============================================================================

#include <eng/graphics/polygon_planes.hpp>

#include <cstdio>

using eng::s16;
using eng::s32;
using eng::u8;
using eng::u32;

namespace g = eng::graphics;

namespace {

int g_fails = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("FALLO: %s\n", what);
		++g_fails;
	}
}

struct Poly {
	s16 x[20] {};
	s16 y[20] {};
	u8 n = 0;
};

Poly make_poly(const s16* xs, const s16* ys, u8 n) {
	Poly p {};
	p.n = n;
	for (u8 i = 0; i < n; ++i) {
		p.x[i] = xs[i];
		p.y[i] = ys[i];
	}
	return p;
}

Poly clip(const s16* xs, const s16* ys, u8 n, s16 mx, s16 my) {
	Poly p {};
	p.n = g::clip_face_to_rect(xs, ys, n, s16 {1}, mx, s16 {0}, my, p.x, p.y);
	return p;
}

bool inside(const Poly& p, s16 mx, s16 my) {
	// Margen izquierdo de 1 px: el trazador de lineas dibuja la vertical de `x` en
	// `x-1`, asi que el contorno no puede bajar de 1.
	for (u8 i = 0; i < p.n; ++i) {
		if (p.x[i] < 1 || p.x[i] > mx || p.y[i] < 0 || p.y[i] > my) {
			return false;
		}
	}
	return true;
}

bool has_vertex(const Poly& p, s16 x, s16 y) {
	for (u8 i = 0; i < p.n; ++i) {
		if (p.x[i] == x && p.y[i] == y) {
			return true;
		}
	}
	return false;
}

/// Vertice sobre el segmento a-b con distancia <= 1 px: |cross| <= |edge| y
/// proyeccion dentro del segmento (sin division).
bool near_segment(s32 px, s32 py, s32 ax, s32 ay, s32 bx, s32 by) {
	const s32 ex = bx - ax;
	const s32 ey = by - ay;
	const s32 vx = px - ax;
	const s32 vy = py - ay;
	const s32 cross = vx * ey - vy * ex;
	const s32 len2 = ex * ex + ey * ey;
	const s32 dot = vx * ex + vy * ey;
	return cross * cross <= len2 && dot >= 0 && dot <= len2;
}

/// Hull: cada vertice de salida es un vertice de entrada o un punto de una arista
/// de entrada (+-1 px por el truncado de la interpolacion).
bool hull_ok(const Poly& in, const Poly& out) {
	for (u8 i = 0; i < out.n; ++i) {
		bool ok = has_vertex(in, out.x[i], out.y[i]);
		for (u8 k = 0; !ok && k < in.n; ++k) {
			const u8 j = static_cast<u8>((k + 1u == in.n) ? 0u : (k + 1u));
			if (near_segment(out.x[i], out.y[i], in.x[k], in.y[k], in.x[j], in.y[j])) {
				ok = true;
			}
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}

/// Plan acotado: todas las aristas dentro de [1,mx] x [0,my] (el margen izquierdo
/// de 1 px absorbe el desfase del trazador de lineas).
bool plan_in_bounds(const g::PlanePolygon* faces, u32 n, u8 bit, s16 mx, s16 my, u32& count,
		    g::PlaneEdge* edges, u32 cap) {
	count = g::plan_plane_edges_in_bounds(faces, n, bit, s16 {1}, mx, s16 {0}, my, edges, cap);
	if (count > cap) {
		return false;
	}
	for (u32 i = 0; i < count; ++i) {
		if (edges[i].x0 < 1 || edges[i].x0 > mx || edges[i].x1 < 1 || edges[i].x1 > mx ||
		    edges[i].y0 < 0 || edges[i].y0 > my || edges[i].y1 < 0 || edges[i].y1 > my) {
			return false;
		}
	}
	return true;
}

} // namespace

int main() {
	const s16 mx = 319;
	const s16 my = 231;

	// 1) Cara interior: el recorte no toca nada (regresion: sin divisiones ni cambios).
	{
		const s16 x[4] = {10, 50, 50, 10};
		const s16 y[4] = {10, 10, 60, 60};
		const Poly p = clip(x, y, 4u, mx, my);
		check(p.n == 4u, "interior: 4 vertices");
		bool same = p.n == 4u;
		for (u8 i = 0; same && i < 4u; ++i) {
			same = (p.x[i] == x[i] && p.y[i] == y[i]);
		}
		check(same, "interior: coordenadas intactas");
	}

	// 2) Caso del bug: vertice en x == width (320, un pixel FUERA del bitmap). El
	//    recorte debe dejarlo en 319 y no emitir ninguna arista en x >= width.
	{
		const s16 x[4] = {300, 320, 320, 300};
		const s16 y[4] = {10, 10, 100, 100};
		const Poly p = clip(x, y, 4u, mx, my);
		check(p.n >= 3u && inside(p, mx, my), "x=width: recortado dentro del bitmap");
		check(has_vertex(p, mx, 10) || has_vertex(p, mx, 100), "x=width: borde en x=max");
		const g::PlanePolygon face {p.x, p.y, p.n, 1u};
		g::PlaneEdge edges[16] {};
		u32 ne = 0u;
		check(plan_in_bounds(&face, 1u, 0u, mx, my, ne, edges, 16u),
		      "x=width: aristas dentro del bitmap");
		bool touching = false;
		for (u32 i = 0; i < ne; ++i) {
			if (edges[i].x0 == mx || edges[i].x1 == mx) {
				touching = true;
			}
		}
		check(touching, "x=width: el contorno se cierra en x=max (paridad del fill)");
	}

	// 3) Los cuatro bordes por separado: salida dentro y con los vertices de corte
	//    sobre las aristas de la entrada (hull).
	{
		const s16 lx[3] = {-40, 40, -40};
		const s16 ly[3] = {50, 100, 160};
		const Poly l = clip(lx, ly, 3u, mx, my);
		check(l.n >= 3u && inside(l, mx, my), "izquierda: dentro");
		check(hull_ok(make_poly(lx, ly, 3u), l), "izquierda: vertices sobre aristas");

		const s16 rx[3] = {280, 400, 280};
		const s16 ry[3] = {50, 100, 160};
		const Poly r = clip(rx, ry, 3u, mx, my);
		check(r.n >= 3u && inside(r, mx, my), "derecha: dentro");
		check(hull_ok(make_poly(rx, ry, 3u), r), "derecha: vertices sobre aristas");

		const s16 tx[3] = {100, 200, 150};
		const s16 ty[3] = {-60, -60, 80};
		const Poly t = clip(tx, ty, 3u, mx, my);
		check(t.n >= 3u && inside(t, mx, my), "arriba: dentro");

		const s16 bx[3] = {100, 200, 150};
		const s16 by[3] = {180, 180, 320};
		const Poly b = clip(bx, by, 3u, mx, my);
		check(b.n >= 3u && inside(b, mx, my), "abajo: dentro");
	}

	// 4) Fuera del todo: vacio.
	{
		const s16 x[3] = {400, 500, 400};
		const s16 y[3] = {10, 10, 100};
		check(clip(x, y, 3u, mx, my).n == 0u, "fuera: 0 vertices");
	}

	// 5) Arista compartida por dos caras del MISMO plano: tras recortar sigue siendo
	//    identica en ambas (se cancela por paridad) y el plan no la incluye.
	{
		const s16 ax[4] = {10, 100, 100, 10};
		const s16 ay[4] = {10, 10, 90, 90};
		const s16 bx[4] = {100, 150, 150, 100};
		const s16 by[4] = {10, 10, 90, 90};
		const g::PlanePolygon faces[2] = {{ax, ay, 4u, 1u}, {bx, by, 4u, 1u}};
		g::PlaneEdge edges[16] {};
		u32 ne = 0u;
		check(plan_in_bounds(faces, 2u, 0u, mx, my, ne, edges, 16u), "compartida: dentro");
		bool shared = false;
		for (u32 i = 0; i < ne; ++i) {
			if (edges[i].x0 == 100 && edges[i].x1 == 100) {
				shared = true;
			}
		}
		check(!shared, "compartida: la arista de multiplicidad par no se dibuja");
	}

	// 6) La plantilla vale para otro tipo de coordenada (s32), no solo s16.
	{
		const s32 xs[3] = {300, 400, 300};
		const s32 ys[3] = {10, 10, 200};
		s32 ox[20] {};
		s32 oy[20] {};
		const u8 n = g::clip_face_to_rect(xs, ys, 3u, s32 {1}, s32 {319}, s32 {0}, s32 {231}, ox, oy);
		bool ok = n >= 3u;
		for (u8 i = 0; ok && i < n; ++i) {
			ok = ox[i] >= 0 && ox[i] <= 319 && oy[i] >= 0 && oy[i] <= 231;
		}
		check(ok, "s32: recorte generico");
	}

	if (g_fails == 0) {
		std::printf("OK: recorte de caras al bitmap en el relleno por plano\n");
		return 0;
	}
	std::printf("%d fallo(s)\n", g_fails);
	return 1;
}
