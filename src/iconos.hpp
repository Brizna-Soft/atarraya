#pragma once

// Íconos de la barra de botones: dibujos de línea en SVG. «COLOR» se cambia por el color del tema.

#include <wx/bmpbndl.h>
#include <wx/string.h>

namespace iconos
{
    inline const char* Agregar =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="COLOR" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 8v8M8 12h8"/></svg>)";
    inline const char* Reanudar =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="COLOR" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8 5l11 7-11 7z"/></svg>)";
    inline const char* Pausar =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="COLOR" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8 5v14M16 5v14"/></svg>)";
    inline const char* Quitar =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="COLOR" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/></svg>)";
    inline const char* Carpeta =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="COLOR" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h6l2 2h10v11H3z"/></svg>)";
    inline const char* Navegador =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="COLOR" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18"/></svg>)";
    inline const char* Ajustes =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="COLOR" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1"/></svg>)";
    inline const char* Corazon =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="COLOR" d="M12 21s-7.5-4.6-9.5-9.2C1.2 8.6 3.3 5 6.8 5c2 0 3.3 1.1 5.2 3 1.9-1.9 3.2-3 5.2-3 3.5 0 5.6 3.6 4.3 6.8C19.5 16.4 12 21 12 21z"/></svg>)";

    inline wxBitmapBundle Crear(const char* svg, const wxString& color, int lado = 20)
    {
        wxString texto = wxString::FromUTF8(svg);
        texto.Replace(L"COLOR", color);
        wxScopedCharBuffer utf8 = texto.utf8_str();
        return wxBitmapBundle::FromSVG(utf8.data(), wxSize(lado, lado));
    }
}
