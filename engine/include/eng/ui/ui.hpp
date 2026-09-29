#pragma once

/// \file ui.hpp
/// **Fachada de `eng::ui`**: un solo include con la superficie estable de la librería GUI
/// (tema, painter, árbol de widgets, contexto de entrada, ventanas, layout, keymap, adaptador
/// de mensajes y compositor). Reúne las cabeceras canónicas sin definir tipos nuevos.
///
/// La incluye `eng/api/api.hpp`; una demo/juego puede usar esta fachada directamente si solo
/// necesita la GUI. Ver `docs/engine/architecture/GUI_LIBRARY.md`.

#include <eng/ui/backing.hpp>
#include <eng/ui/compositor.hpp>
#include <eng/ui/context.hpp>
#include <eng/ui/dirty.hpp>
#include <eng/ui/double_buffer.hpp>
#include <eng/ui/editbox.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/hardware_cursor.hpp>
#include <eng/ui/keymap.hpp>
#include <eng/ui/keys.hpp>
#include <eng/ui/layout.hpp>
#include <eng/ui/list.hpp>
#include <eng/ui/msg_adapter.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/scroll.hpp>
#include <eng/ui/slider.hpp>
#include <eng/ui/text.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/ui_bridge.hpp>
#include <eng/ui/widget.hpp>
#include <eng/ui/widgets.hpp>
#include <eng/ui/window.hpp>
