#include "ventanarecolector.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filepicker.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <windows.h>

#include <set>

#include "ayuda.hpp"
#include "dialogos.hpp"
#include "enlaces.hpp"
#include "sistema.hpp"
#include "util.hpp"

using namespace hnd;

namespace
{
    // Qué es cada fila: un enlace (id) o un paquete (id 0)
    struct DatoFila : wxClientData
    {
        DatoFila(uint64_t i, std::wstring p) : id(i), paquete(std::move(p)) {}
        uint64_t id;
        std::wstring paquete;
    };

    wxStaticText* Nota(wxWindow* padre, const wxString& texto, int ancho)
    {
        auto* t = new wxStaticText(padre, wxID_ANY, texto);
        t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        t->Wrap(ancho);
        return t;
    }
}

VentanaRecolector::VentanaRecolector(wxWindow* padre, Recolector& recolector, Ajustes& ajustes, AlConfirmar alConfirmar)
    : wxFrame(padre, wxID_ANY, L"Recolector de enlaces", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE),
      m_recolector(recolector), m_ajustes(ajustes), m_alConfirmar(std::move(alConfirmar))
{
    if (auto* marco = dynamic_cast<wxTopLevelWindow*>(padre)) SetIcons(marco->GetIcons());
    auto* fondo = new wxPanel(this);
    const int borde = FromDIP(10);
    auto* caja = new wxBoxSizer(wxVERTICAL);

    auto* arriba = new wxBoxSizer(wxHORIZONTAL);
    auto* pegar = new wxButton(fondo, wxID_ANY, L"Pegar enlaces...");
    auto* quitar = new wxButton(fondo, wxID_ANY, L"Quitar lo elegido");
    auto* vaciar = new wxButton(fondo, wxID_ANY, L"Vaciar");
    auto* todo = new wxButton(fondo, wxID_ANY, L"Marcar todo");
    auto* nada = new wxButton(fondo, wxID_ANY, L"Quitar marcas");
    for (auto* b : { pegar, quitar, vaciar, todo, nada }) arriba->Add(b, 0, wxRIGHT, FromDIP(6));
    arriba->AddStretchSpacer();
    m_portapapeles = new wxCheckBox(fondo, wxID_ANY, L"Vigilar el portapapeles");
    m_portapapeles->SetValue(m_ajustes.vigilarPortapapeles);
    m_portapapeles->SetToolTip(L"Los enlaces de archivos que copies (y los magnet) pasan solos a este recolector. No se baja nada hasta que confirmes.");
    arriba->Add(m_portapapeles, 0, wxALIGN_CENTER_VERTICAL);
    caja->Add(arriba, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    caja->Add(Nota(fondo, L"Aquí esperan los enlaces antes de bajarlos. Atarraya consulta el nombre y el tamaño de cada uno sin "
                          L"bajarlo, y los junta en paquetes. Marca los que quieras y pulsa «Descargar los marcados». Las páginas de "
                          L"sitios de descargas se abren en tu navegador, y los captchas siempre los resuelves tú ahí.",
                   FromDIP(820)),
              0, wxLEFT | wxRIGHT | wxTOP, borde);

    m_arbol = new wxTreeListCtrl(fondo, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(860, 340)), wxTL_MULTIPLE | wxTL_CHECKBOX | wxTL_3STATE);
    m_arbol->AppendColumn(L"Nombre", FromDIP(380), wxALIGN_LEFT, wxCOL_RESIZABLE);
    m_arbol->AppendColumn(L"Tamaño", FromDIP(90), wxALIGN_RIGHT, wxCOL_RESIZABLE);
    m_arbol->AppendColumn(L"Estado", FromDIP(250), wxALIGN_LEFT, wxCOL_RESIZABLE);
    m_arbol->AppendColumn(L"Sitio", FromDIP(140), wxALIGN_LEFT, wxCOL_RESIZABLE);
    caja->Add(m_arbol, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    m_cnl = new wxStaticText(fondo, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_cnl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    caja->Add(m_cnl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    auto* fila = new wxBoxSizer(wxHORIZONTAL);
    fila->Add(new wxStaticText(fondo, wxID_ANY, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_carpeta = new wxDirPickerCtrl(fondo, wxID_ANY, m_ajustes.CarpetaEfectiva(), L"Elige dónde guardar", wxDefaultPosition,
                                    wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
    m_carpeta->GetPickerCtrl()->SetLabel(L"Elegir...");
    fila->Add(m_carpeta, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    m_porPaquete = new wxCheckBox(fondo, wxID_ANY, L"Una carpeta por paquete");
    m_porPaquete->SetValue(m_ajustes.carpetaPorPaquete);
    fila->Add(m_porPaquete, 0, wxALIGN_CENTER_VERTICAL);
    caja->Add(fila, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    auto* abajo = new wxBoxSizer(wxHORIZONTAL);
    m_resumen = new wxStaticText(fondo, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    abajo->Add(m_resumen, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    m_descargar = new wxButton(fondo, wxID_ANY, L"Descargar los marcados");
    m_luego = new wxButton(fondo, wxID_ANY, L"Más tarde");
    m_luego->SetToolTip(L"Los pasa a la lista de descargas en pausa");
    auto* cerrar = new wxButton(fondo, wxID_ANY, L"Cerrar");
    cerrar->SetToolTip(L"Los enlaces se quedan aquí hasta que decidas");
    abajo->Add(m_descargar, 0, wxRIGHT, FromDIP(6));
    abajo->Add(m_luego, 0, wxRIGHT, FromDIP(6));
    abajo->Add(cerrar);
    caja->Add(abajo, 0, wxEXPAND | wxALL, borde);
    fondo->SetSizer(caja);

    pegar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PegarEnlaces(); });
    quitar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        auto ids = Elegidos();
        if (ids.empty())
        {
            ayuda::Aviso(L"Elige (con un clic) los enlaces o paquetes que quieres quitar del recolector.", GetTitle(), wxOK | wxICON_INFORMATION, this);
            return;
        }
        m_recolector.Quitar(ids);
        Refrescar();
    });
    vaciar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_recolector.Cantidad() == 0) return;
        wxMessageDialog d(this, L"¿Quitar todos los enlaces del recolector? No se borra ningún archivo.", GetTitle(),
                          wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        d.SetYesNoLabels(L"Quitar todos", L"Cancelar");
        if (d.ShowModal() != wxID_YES) return;
        std::vector<uint64_t> ids;
        for (auto& e : m_recolector.Lista()) ids.push_back(e.id);
        m_recolector.Quitar(ids);
        Refrescar();
    });
    auto marcarTodo = [this](bool si) {
        std::vector<uint64_t> ids;
        for (auto& e : m_recolector.Lista()) ids.push_back(e.id);
        m_recolector.Marcar(ids, si);
        Refrescar();
    };
    todo->Bind(wxEVT_BUTTON, [marcarTodo](wxCommandEvent&) { marcarTodo(true); });
    nada->Bind(wxEVT_BUTTON, [marcarTodo](wxCommandEvent&) { marcarTodo(false); });
    m_portapapeles->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        m_ajustes.vigilarPortapapeles = m_portapapeles->GetValue();
        m_ajustes.Guardar();
        if (alCambiarAjustes) alCambiarAjustes();
    });
    m_descargar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Confirmar(true); });
    m_luego->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Confirmar(false); });
    cerrar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); });
    m_arbol->Bind(wxEVT_TREELIST_ITEM_CHECKED, &VentanaRecolector::AlMarcar, this);
    m_arbol->Bind(wxEVT_TREELIST_ITEM_CONTEXT_MENU, &VentanaRecolector::MenuContextual, this);
    // La X solo esconde la ventana: los enlaces siguen esperando
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        if (e.CanVeto())
        {
            e.Veto();
            Hide();
        }
        else e.Skip();
    });

    fondo->Fit();
    SetClientSize(fondo->GetBestSize());
    SetMinSize(FromDIP(wxSize(700, 460)));
    CentreOnParent();
}

void VentanaRecolector::MostrarSinFoco()
{
    if (!IsShown())
    {
        m_version = 0;
        ShowWithoutActivating();
    }
    Refrescar();
}

void VentanaRecolector::EstadoClickNLoad(const wxString& texto)
{
    if (m_cnl->GetLabel() != texto) m_cnl->SetLabel(texto);
}

void VentanaRecolector::Refrescar()
{
    if (!IsShown()) return;
    uint64_t v = m_recolector.Version();
    if (v == m_version) return;
    m_version = v;
    auto lista = m_recolector.Lista();

    std::set<uint64_t> presentes;
    for (auto& e : lista) presentes.insert(e.id);
    for (auto it = m_items.begin(); it != m_items.end();)
    {
        if (!presentes.count(it->first))
        {
            m_arbol->DeleteItem(it->second);
            m_paqueteDe.erase(it->first);
            it = m_items.erase(it);
        }
        else ++it;
    }

    std::map<std::wstring, std::pair<size_t, int64_t>> cuentas;   // paquete → (enlaces, tamaño conocido)
    for (auto& e : lista)
    {
        std::wstring paquete = PaqueteDe(e);
        auto p = m_paquetes.find(paquete);
        if (p == m_paquetes.end())
        {
            auto fila = m_arbol->AppendItem(m_arbol->GetRootItem(), paquete, -1, -1, new DatoFila(0, paquete));
            p = m_paquetes.emplace(paquete, fila).first;
        }
        auto it = m_items.find(e.id);
        if (it != m_items.end() && m_paqueteDe[e.id] != paquete)
        {
            m_arbol->DeleteItem(it->second);   // el nombre cambió al consultarlo: va a otro paquete
            m_items.erase(it);
            it = m_items.end();
        }
        if (it == m_items.end())
        {
            auto fila = m_arbol->AppendItem(p->second, e.nombre, -1, -1, new DatoFila(e.id, paquete));
            it = m_items.emplace(e.id, fila).first;
            m_paqueteDe[e.id] = paquete;
            m_arbol->Expand(p->second);
        }
        auto fila = it->second;
        m_arbol->SetItemText(fila, 0, e.nombre.empty() ? wxString::FromUTF8(e.url) : wxString(e.nombre));
        m_arbol->SetItemText(fila, 1, e.tamano >= 0 ? wxString(TextoTamano(e.tamano)) : wxString());
        m_arbol->SetItemText(fila, 2, TextoComprobacion(e.estado));
        m_arbol->SetItemText(fila, 3, wxString::FromUTF8(enlaces::Sitio(e.url)));
        m_arbol->CheckItem(fila, e.marcado ? wxCHK_CHECKED : wxCHK_UNCHECKED);
        auto& c = cuentas[paquete];
        c.first++;
        if (e.tamano > 0) c.second += e.tamano;
    }
    for (auto it = m_paquetes.begin(); it != m_paquetes.end();)
    {
        auto c = cuentas.find(it->first);
        if (c == cuentas.end())
        {
            m_arbol->DeleteItem(it->second);
            it = m_paquetes.erase(it);
            continue;
        }
        m_arbol->SetItemText(it->second, 0, wxString::Format(L"%s  (%zu)", it->first, c->second.first));
        m_arbol->SetItemText(it->second, 1, c->second.second > 0 ? wxString(TextoTamano(c->second.second)) : wxString());
        auto hijo = m_arbol->GetFirstChild(it->second);
        if (hijo.IsOk()) m_arbol->UpdateItemParentStateRecursively(hijo);
        ++it;
    }
    Resumen(lista);
}

void VentanaRecolector::Resumen(const std::vector<EnlaceRecolectado>& lista)
{
    size_t marcados = 0, consultando = 0, paginas = 0, faltan = 0;
    int64_t tamano = 0;
    for (auto& e : lista)
    {
        if (e.estado == Comprobacion::Pendiente || e.estado == Comprobacion::Comprobando) consultando++;
        if (!e.marcado) continue;
        marcados++;
        if (e.tamano > 0) tamano += e.tamano;
        if (e.estado == Comprobacion::Pagina || e.estado == Comprobacion::Sitio) paginas++;
        if (e.estado == Comprobacion::NoExiste) faltan++;
    }
    wxString t = wxString::Format(L"%zu enlaces  ·  marcados: %zu", lista.size(), marcados);
    if (tamano > 0) t += L" (" + wxString(TextoTamano(tamano)) + L")";
    if (consultando > 0) t += wxString::Format(L"  ·  consultando: %zu", consultando);
    if (paginas > 0) t += wxString::Format(L"  ·  para el navegador: %zu", paginas);
    if (faltan > 0) t += wxString::Format(L"  ·  ya no están: %zu", faltan);
    m_resumen->SetLabel(t);
    m_descargar->Enable(marcados > 0);
    m_luego->Enable(marcados > 0);
}

std::vector<uint64_t> VentanaRecolector::Elegidos() const
{
    std::vector<uint64_t> ids;
    wxTreeListItems sel;
    m_arbol->GetSelections(sel);
    for (auto& fila : sel)
    {
        auto* d = static_cast<DatoFila*>(m_arbol->GetItemData(fila));
        if (!d) continue;
        if (d->id) ids.push_back(d->id);
        else
        {
            for (auto h = m_arbol->GetFirstChild(fila); h.IsOk(); h = m_arbol->GetNextSibling(h))
            {
                if (auto* dh = static_cast<DatoFila*>(m_arbol->GetItemData(h))) ids.push_back(dh->id);
            }
        }
    }
    return ids;
}

void VentanaRecolector::AlMarcar(wxTreeListEvent& e)
{
    auto fila = e.GetItem();
    auto* d = static_cast<DatoFila*>(m_arbol->GetItemData(fila));
    if (!d) return;
    bool si = m_arbol->GetCheckedState(fila) == wxCHK_CHECKED;
    std::vector<uint64_t> ids;
    if (d->id == 0)
    {
        m_arbol->CheckItemRecursively(fila, si ? wxCHK_CHECKED : wxCHK_UNCHECKED);
        for (auto h = m_arbol->GetFirstChild(fila); h.IsOk(); h = m_arbol->GetNextSibling(h))
        {
            if (auto* dh = static_cast<DatoFila*>(m_arbol->GetItemData(h))) ids.push_back(dh->id);
        }
    }
    else
    {
        ids.push_back(d->id);
        m_arbol->UpdateItemParentStateRecursively(fila);
    }
    m_recolector.Marcar(ids, si);
    Refrescar();
}

void VentanaRecolector::MenuContextual(wxTreeListEvent& e)
{
    auto* d = static_cast<DatoFila*>(m_arbol->GetItemData(e.GetItem()));
    if (!d || d->id == 0) return;
    EnlaceRecolectado enlace;
    bool hay = false;
    for (auto& x : m_recolector.Lista())
    {
        if (x.id == d->id) { enlace = x; hay = true; break; }
    }
    if (!hay) return;
    wxMenu m;
    const int abrir = wxWindow::NewControlId(), copiar = wxWindow::NewControlId(), quitar = wxWindow::NewControlId();
    if (!enlaces::EsMagnet(enlace.url)) m.Append(abrir, L"Abrir en el navegador");
    m.Append(copiar, L"Copiar el enlace");
    m.AppendSeparator();
    m.Append(quitar, L"Quitar del recolector");
    if (!enlace.detalle.empty())
    {
        m.AppendSeparator();
        m.Append(wxID_ANY, wxString::FromUTF8(enlace.detalle.substr(0, 120)))->Enable(false);
    }
    int r = GetPopupMenuSelectionFromUser(m);
    if (r == abrir) ayuda::AbrirEnlace(wxString::FromUTF8(enlace.url));
    else if (r == copiar) sistema::CopiarTexto(Ancho(enlace.url));
    else if (r == quitar)
    {
        m_recolector.Quitar({ enlace.id });
        Refrescar();
    }
}

void VentanaRecolector::PegarEnlaces()
{
    DialogoVarios d(this, m_ajustes);
    if (d.ShowModal() != wxID_OK) return;
    size_t n = m_recolector.Agregar(d.Enlaces(), L"", "", "lista");
    if (n < d.Enlaces().size())
        ayuda::Aviso(wxString::Format(L"Entraron %zu enlaces nuevos. Los demás ya estaban en el recolector.", n), GetTitle(),
                     wxOK | wxICON_INFORMATION, this);
    Refrescar();
}

void VentanaRecolector::Confirmar(bool empezar)
{
    std::vector<EnlaceRecolectado> marcados;
    for (auto& e : m_recolector.Lista())
    {
        if (e.marcado) marcados.push_back(e);
    }
    if (marcados.empty()) return;
    std::wstring carpeta = m_carpeta->GetPath().ToStdWstring();
    if (!ExisteCarpeta(carpeta))
    {
        ayuda::Aviso(L"La carpeta elegida no existe.", GetTitle(), wxOK | wxICON_WARNING, this);
        return;
    }
    m_ajustes.carpetaPorPaquete = m_porPaquete->GetValue();
    m_ajustes.Guardar();
    auto hechos = m_alConfirmar(marcados, carpeta, m_porPaquete->GetValue(), empezar);
    m_recolector.Quitar(hechos);
    Refrescar();
    if (m_recolector.Cantidad() == 0) Hide();
}
