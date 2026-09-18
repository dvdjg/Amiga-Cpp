#pragma once

/// \file templates.hpp
/// **Plantillas de frases** del explicador de ajedrez (NLG por plantillas). Cada
/// plantilla existe en español e inglés y puede tener variante neutra y enfática.
///
/// Los huecos son `{side}`, `{side_adj}`, `{diff}`, `{piece}` y `{square}`; el
/// motor de `explain.hpp` los sustituye en tiempo de ejecución. No hay ningún modelo
/// de lenguaje: se eligen y combinan frases preescritas según los rasgos de la
/// posición, que es lo que hace viable el sistema en un A500 (8–50 kB).
///
/// En el futuro las plantillas se cargarán desde `assets/amiga/board/*/explain/` y
/// se empaquetarán como bloques; estas constantes son el pack mínimo incorporado.
///
/// Verificación: HOST-149.

#include <eng/core/util/string_view.hpp>

namespace eng::board::chess {

/// Par de frases (español, inglés) como vistas de texto de solo lectura.
struct Phrase {
	eng::util::StringView es;
	eng::util::StringView en;
};

// --- Material ---
inline constexpr Phrase kPhraseMaterial {
    eng::util::StringView {"Las {side} tienen ventaja material de {diff} peones."},
    eng::util::StringView {"{side} has a material advantage of {diff} pawns."}};
inline constexpr Phrase kPhraseMaterialEmphatic {
    eng::util::StringView {"¡Las {side} tienen una ventaja material decisiva de {diff} peones!"},
    eng::util::StringView {"{side} has a decisive material advantage of {diff} pawns!"}};

// --- Desarrollo ---
inline constexpr Phrase kPhraseQueenEarly {
    eng::util::StringView {"Las {side} han sacado la dama demasiado pronto y podrá ser hostigada."},
    eng::util::StringView {"{side} has developed the queen prematurely and it may be harassed."}};
inline constexpr Phrase kPhraseUndeveloped {
    eng::util::StringView {"Las {side} tienen un retraso en el desarrollo de las piezas menores."},
    eng::util::StringView {"{side} is behind in the development of the minor pieces."}};
inline constexpr Phrase kPhraseKingInCenter {
    eng::util::StringView {"El rey de las {side} sigue en el centro y puede volverse vulnerable."},
    eng::util::StringView {"The {side_adj} king remains in the centre and may become vulnerable."}};

// --- Táctica inmediata ---
inline constexpr Phrase kPhraseInCheck {
    eng::util::StringView {"El rey de las {side} está en jaque."},
    eng::util::StringView {"The {side_adj} king is in check."}};
inline constexpr Phrase kPhraseInCheckEmphatic {
    eng::util::StringView {"¡El rey de las {side} está en jaque!"},
    eng::util::StringView {"The {side_adj} king is in check!"}};

// --- Conectores entre frases ---
inline constexpr Phrase kConnectorSecond {
    eng::util::StringView {"Además, "},
    eng::util::StringView {"Moreover, "}};
inline constexpr Phrase kConnectorThird {
    eng::util::StringView {"Por otro lado, "},
    eng::util::StringView {"On the other hand, "}};

} // namespace eng::board::chess
