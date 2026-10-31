#pragma once

// Ventana del recolector de enlaces: los enlaces agrupados en paquetes, con su nombre, tamaño y estado. La persona
// marca los que quiere y pulsa «Descargar los marcados»: recién ahí empieza algo.

#include <wx/frame.h>
#include <wx/timer.h>
#include <wx/treelist.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ajustes.hpp"
#include "recolector.hpp"

class wxCheckBox;
class wxDirPickerCtrl;
class wxStaticText;
class wxButton;

class VentanaRecolector : public wxFrame
{
public:
    // «alConfirmar» recibe los enlaces marcados, la carpeta y si empiezan ya; devuelve los ids que ya se atendieron
    using AlConfirmar = std::function<std::vector<uint64_t>(const std::vector<hnd::EnlaceRecolectado>&, const std::wstring& carpeta,
                                                            bool porPaquete, bool empezar)>;

    VentanaRecolector(wxWindow* padre, hnd::Recolector& recolector, hnd::Ajustes& ajustes, AlConfirmar alConfirmar);

    void Refrescar();                               // la llama la ventana principal cada medio segundo
    void MostrarSinFoco();                          // aparece sin quitarle el teclado a otro programa
    void EstadoClickNLoad(const wxString& texto);   // «Click'n'Load: activo» o por qué no
    std::function<void()> alCambiarAjustes;         // la persona tocó «Vigilar el portapapeles»

private:
    void PegarEnlaces();
    void Confirmar(bool empezar);
    std::vector<uint64_t> Elegidos() const;         // los seleccionados (no los marcados)
    void AlMarcar(wxTreeListEvent& e);
    void MenuContextual(wxTreeListEvent& e);
    void Resumen(const std::vector<hnd::EnlaceRecolectado>& lista);

    hnd::Recolector& m_recolector;
    hnd::Ajustes& m_ajustes;
    AlConfirmar m_alConfirmar;
    wxTreeListCtrl* m_arbol = nullptr;
    wxStaticText* m_resumen = nullptr;
    wxStaticText* m_cnl = nullptr;
    wxCheckBox* m_portapapeles = nullptr;
    wxCheckBox* m_porPaquete = nullptr;
    wxDirPickerCtrl* m_carpeta = nullptr;
    wxButton* m_descargar = nullptr;
    wxButton* m_luego = nullptr;

    std::map<uint64_t, wxTreeListItem> m_items;          // id → fila
    std::map<uint64_t, std::wstring> m_paqueteDe;        // id → paquete donde está su fila
    std::map<std::wstring, wxTreeListItem> m_paquetes;   // paquete → fila
    uint64_t m_version = 0;
};
