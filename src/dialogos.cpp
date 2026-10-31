#include "dialogos.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/filedlg.h>
#include <wx/filepicker.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
#include <wx/weakref.h>

#include <algorithm>
#include <cmath>
#include <thread>

#include "ayuda.hpp"
#include "enlaces.hpp"
#include "hilos.hpp"
#include "jicaro.hpp"
#include "navegadores.hpp"
#include "nombres.hpp"
#include "sistema.hpp"
#include "util.hpp"

using namespace hnd;

namespace
{
    wxStaticText* Etiqueta(wxWindow* padre, const wxString& texto)
    {
        return new wxStaticText(padre, wxID_ANY, texto);
    }

    wxStaticText* Nota(wxWindow* padre, const wxString& texto, int ancho)
    {
        auto* t = new wxStaticText(padre, wxID_ANY, texto);
        t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        t->Wrap(ancho);
        return t;
    }

    wxDirPickerCtrl* ElegirCarpeta(wxWindow* padre, const std::wstring& carpeta, int ancho = -1)
    {
        auto* c = new wxDirPickerCtrl(padre, wxID_ANY, carpeta, L"Elige dónde guardar", wxDefaultPosition,
                                      ancho > 0 ? wxSize(ancho, -1) : wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
        c->GetPickerCtrl()->SetLabel(L"Elegir...");   // wx lo pondría en inglés («Browse»)
        return c;
    }

    // Tres botones al pie: el principal, «Más tarde» (lo deja en la lista en pausa) y «Cancelar»
    wxBoxSizer* Botones(wxDialog* d, const wxString& principal, wxButton** primero, wxButton** luego, bool conLuego = true)
    {
        auto* fila = new wxBoxSizer(wxHORIZONTAL);
        fila->AddStretchSpacer();
        *primero = new wxButton(d, wxID_OK, principal);
        (*primero)->SetDefault();
        fila->Add(*primero, 0, wxRIGHT, d->FromDIP(8));
        if (conLuego)
        {
            *luego = new wxButton(d, wxID_ANY, L"Más tarde");
            (*luego)->SetToolTip(L"Lo agrega a la lista en pausa");
            fila->Add(*luego, 0, wxRIGHT, d->FromDIP(8));
        }
        fila->Add(new wxButton(d, wxID_CANCEL, L"Cancelar"));
        return fila;
    }

    // Sobre la ventana de Atarraya si se ve; si no (por ejemplo, desde el navegador), en el centro de la pantalla
    void Centrar(wxDialog* d, wxWindow* padre)
    {
        auto* marco = wxDynamicCast(padre, wxTopLevelWindow);
        if (padre && padre->IsShown() && !(marco && marco->IsIconized())) d->CentreOnParent();
        else d->CentreOnScreen();
    }

    bool CarpetaValida(wxWindow* padre, wxDirPickerCtrl* c, const wxString& titulo)
    {
        if (ExisteCarpeta(c->GetPath().ToStdWstring())) return true;
        ayuda::Aviso(L"La carpeta elegida no existe.", titulo, wxOK | wxICON_WARNING, padre);
        return false;
    }
}

// ---------------------------------------------------------------- Nueva descarga

DialogoAgregar::DialogoAgregar(wxWindow* padre, const Ajustes& ajustes, const DatosDescarga& previa, bool delNavegador,
                               int64_t tamanoAnunciado)
    : wxDialog(padre, wxID_ANY, delNavegador ? L"Nueva descarga desde el navegador" : L"Nueva descarga",
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | (delNavegador ? wxSTAY_ON_TOP : 0)),
      m_previa(previa)
{
    const int borde = FromDIP(12);
    auto* caja = new wxBoxSizer(wxVERTICAL);
    auto* rejilla = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
    rejilla->AddGrowableCol(1, 1);

    wxString url = wxString::FromUTF8(previa.url);
    if (url.empty())
    {
        std::wstring pegado = sistema::TextoDelPortapapeles();
        if (sistema::EsDireccion(pegado) || sistema::EsMagnet(pegado)) url = pegado;
    }
    m_url = new wxTextCtrl(this, wxID_ANY, url, wxDefaultPosition, FromDIP(wxSize(460, -1)));
    m_url->SetHint(L"https://...  o  magnet:?...");
    rejilla->Add(Etiqueta(this, L"Dirección:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_url, 1, wxEXPAND);

    m_nombre = new wxTextCtrl(this, wxID_ANY, previa.nombre);
    m_nombre->SetHint(L"(automático)");
    rejilla->Add(Etiqueta(this, L"Nombre:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_nombre, 1, wxEXPAND);

    m_carpeta = ElegirCarpeta(this, previa.carpeta.empty() ? ajustes.CarpetaEfectiva() : previa.carpeta);
    rejilla->Add(Etiqueta(this, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_carpeta, 1, wxEXPAND);

    m_conexiones = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 16,
                                  previa.conexiones > 0 ? previa.conexiones : ajustes.conexiones);
    rejilla->Add(Etiqueta(this, L"Conexiones:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_conexiones, 0);

    caja->Add(rejilla, 0, wxEXPAND | wxALL, borde);

    wxString info = L"Más conexiones suelen bajar más rápido. Si el servidor no deja partir el archivo, se usa una sola.";
    if (tamanoAnunciado > 0)
        info = wxString::Format(L"Tamaño: %s. ", TextoTamano(tamanoAnunciado)) + info;
    if (!delNavegador) info += L"\nTambién puedes pegar un enlace magnet o una página de Internet Archive.";
    caja->Add(Nota(this, info, FromDIP(520)), 0, wxLEFT | wxRIGHT, borde);

    wxButton *descargar = nullptr, *luego = nullptr;
    caja->Add(Botones(this, L"Descargar", &descargar, &luego), 0, wxEXPAND | wxALL, borde);
    descargar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(true); });
    luego->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(false); });

    SetSizerAndFit(caja);
    SetMinSize(GetSize());
    Centrar(this, padre);
    if (url.empty()) m_url->SetFocus();
    else descargar->SetFocus();
}

void DialogoAgregar::AlAceptar(bool empezar)
{
    std::wstring url = m_url->GetValue().Strip(wxString::both).ToStdWstring();
    if (!sistema::EsDireccion(url) && !sistema::EsMagnet(url))
    {
        ayuda::Aviso(L"Escribe o pega una dirección que empiece por http://, https://, ftp:// o magnet:?", L"Nueva descarga",
                     wxOK | wxICON_WARNING, this);
        m_url->SetFocus();
        return;
    }
    if (enlaces::NoPermitido(Utf8(url)))
    {
        ayuda::Aviso(enlaces::TextoNoPermitido, L"Nueva descarga", wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (!CarpetaValida(this, m_carpeta, L"Nueva descarga")) return;
    m_empezar = empezar;
    EndModal(wxID_OK);
}

DatosDescarga DialogoAgregar::Datos() const
{
    DatosDescarga d = m_previa;
    std::string url = Utf8(m_url->GetValue().Strip(wxString::both).ToStdWstring());
    // Otra dirección: las cookies no corresponden, y ya no es el video de la página (una descarga normal, con el proxy)
    if (url != d.url)
    {
        d.cookies.clear();
        d.referer.clear();
        d.sinProxy = false;
    }
    d.url = url;
    d.carpeta = m_carpeta->GetPath().ToStdWstring();
    std::wstring nombre = m_nombre->GetValue().Strip(wxString::both).ToStdWstring();
    d.nombre = nombre.empty() ? L"" : LimpiarNombre(nombre);
    d.nombreFijo = !nombre.empty() && nombre != m_previa.nombre;
    d.conexiones = m_conexiones->GetValue();
    return d;
}

// ---------------------------------------------------------------- Torrent

DialogoTorrent::DialogoTorrent(wxWindow* padre, const Ajustes& ajustes, const InfoTorrent& info, Modo modo, bool delNavegador,
                               const std::wstring& carpeta)
    : wxDialog(padre, wxID_ANY, modo == Modo::Nuevo ? L"Nuevo torrent" : L"Elegir los archivos del torrent", wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | (delNavegador ? wxSTAY_ON_TOP : 0)),
      m_info(info), m_modo(modo)
{
    const int borde = FromDIP(12);
    const int ancho = FromDIP(600);
    auto* caja = new wxBoxSizer(wxVERTICAL);

    auto* titulo = new wxStaticText(this, wxID_ANY, info.nombre, wxDefaultPosition, wxSize(ancho, -1), wxST_ELLIPSIZE_MIDDLE);
    titulo->SetFont(titulo->GetFont().Scaled(1.1f).Bold());
    caja->Add(titulo, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    wxString datos;
    if (info.tieneDatos)
        datos = wxString::Format(L"Tamaño: %s  ·  %zu %s", TextoTamano(info.tamano), info.archivos.size(),
                                 info.archivos.size() == 1 ? L"archivo" : L"archivos");
    else
        datos = L"Enlace magnet: el nombre y la lista de archivos se piden a otras personas cuando empieza.";
    caja->Add(Nota(this, datos, ancho), 0, wxLEFT | wxRIGHT | wxTOP, borde);

    if (info.tieneDatos && !info.archivos.empty())
    {
        m_lista = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(600, 240)), wxDV_ROW_LINES);
        m_lista->AppendToggleColumn(L"Bajar", wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(52));
        m_lista->AppendTextColumn(L"Archivo", wxDATAVIEW_CELL_INERT, FromDIP(modo == Modo::Elegir ? 360 : 420), wxALIGN_LEFT,
                                  wxDATAVIEW_COL_RESIZABLE);
        m_lista->AppendTextColumn(L"Tamaño", wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_RIGHT);
        if (modo == Modo::Elegir) m_lista->AppendTextColumn(L"Bajado", wxDATAVIEW_CELL_INERT, FromDIP(70), wxALIGN_RIGHT);
        for (auto& a : info.archivos)
        {
            wxVector<wxVariant> fila;
            fila.push_back(wxVariant(a.bajar));
            fila.push_back(wxVariant(wxString(a.ruta)));
            fila.push_back(wxVariant(wxString(TextoTamano(a.tamano))));
            if (modo == Modo::Elegir)
                fila.push_back(wxVariant(a.tamano > 0 ? wxString::Format(L"%.0f %%", a.hecho * 100.0 / a.tamano) : wxString(L"—")));
            m_lista->AppendItem(fila);
        }
        caja->Add(m_lista, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);
        m_lista->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [this](wxDataViewEvent&) { Resumen(); });

        auto* fila = new wxBoxSizer(wxHORIZONTAL);
        auto* todos = new wxButton(this, wxID_ANY, L"Marcar todos");
        auto* ninguno = new wxButton(this, wxID_ANY, L"Quitar marcas");
        fila->Add(todos, 0, wxRIGHT, FromDIP(8));
        fila->Add(ninguno, 0, wxRIGHT, FromDIP(12));
        m_resumen = new wxStaticText(this, wxID_ANY, wxEmptyString);
        fila->Add(m_resumen, 1, wxALIGN_CENTER_VERTICAL);
        caja->Add(fila, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);
        todos->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Marcar(true); });
        ninguno->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Marcar(false); });
    }

    if (modo == Modo::Nuevo)
    {
        auto* rejilla = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
        rejilla->AddGrowableCol(1, 1);
        m_carpeta = ElegirCarpeta(this, carpeta.empty() || !ExisteCarpeta(carpeta) ? ajustes.CarpetaEfectiva() : carpeta);
        rejilla->Add(Etiqueta(this, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL);
        rejilla->Add(m_carpeta, 1, wxEXPAND);
        caja->Add(rejilla, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);
        if (!info.tieneDatos)
        {
            m_despues = new wxCheckBox(this, wxID_ANY, L"Preguntarme qué archivos bajar cuando lleguen los datos");
            m_despues->SetValue(ajustes.elegirArchivosMagnet);
            caja->Add(m_despues, 0, wxLEFT | wxRIGHT | wxTOP, borde);
        }
    }
    if (modo == Modo::Nuevo)
    {
        m_compartir = new wxCheckBox(this, wxID_ANY, L"Seguir compartiendo cuando termine (ayuda a que otras personas lo bajen)");
        m_compartir->SetValue(ajustes.compartirTorrents);
        caja->Add(m_compartir, 0, wxLEFT | wxRIGHT | wxTOP, borde);
        caja->Add(Nota(this, L"Mientras baja, Atarraya también sube partes a otras personas: así funcionan los torrents. "
                             L"Baja solo lo que tengas derecho a compartir.", ancho),
                  0, wxLEFT | wxRIGHT | wxTOP, borde);
    }

    wxButton *principal = nullptr, *luego = nullptr;
    caja->Add(Botones(this, modo == Modo::Nuevo ? L"Descargar" : L"Guardar", &principal, &luego, modo == Modo::Nuevo), 0,
              wxEXPAND | wxALL, borde);
    principal->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(true); });
    if (luego) luego->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(false); });

    SetSizerAndFit(caja);
    SetMinSize(FromDIP(wxSize(520, m_lista ? 420 : 240)));
    Resumen();
    Centrar(this, padre);
    principal->SetFocus();
}

void DialogoTorrent::Marcar(bool si)
{
    if (!m_lista) return;
    for (unsigned int i = 0; i < (unsigned int)m_lista->GetItemCount(); i++) m_lista->SetToggleValue(si, i, 0);
    Resumen();
}

void DialogoTorrent::Resumen()
{
    if (!m_lista || !m_resumen) return;
    size_t n = 0;
    int64_t tamano = 0;
    auto elegidos = Elegidos();
    for (size_t i = 0; i < elegidos.size() && i < m_info.archivos.size(); i++)
    {
        if (elegidos[i])
        {
            n++;
            tamano += m_info.archivos[i].tamano;
        }
    }
    m_resumen->SetLabel(wxString::Format(L"Elegidos: %zu de %zu (%s)", n, elegidos.size(), TextoTamano(tamano)));
}

std::vector<bool> DialogoTorrent::Elegidos() const
{
    std::vector<bool> r;
    if (!m_lista) return r;
    for (unsigned int i = 0; i < (unsigned int)m_lista->GetItemCount(); i++) r.push_back(m_lista->GetToggleValue(i, 0));
    return r;
}

std::wstring DialogoTorrent::Carpeta() const
{
    return m_carpeta ? m_carpeta->GetPath().ToStdWstring() : std::wstring();
}

bool DialogoTorrent::Compartir() const { return m_compartir && m_compartir->GetValue(); }
bool DialogoTorrent::ElegirDespues() const { return m_despues && m_despues->GetValue(); }

void DialogoTorrent::AlAceptar(bool empezar)
{
    if (m_lista)
    {
        auto e = Elegidos();
        if (std::find(e.begin(), e.end(), true) == e.end())
        {
            ayuda::Aviso(L"Marca al menos un archivo para bajar.", GetTitle(), wxOK | wxICON_INFORMATION, this);
            return;
        }
    }
    if (m_carpeta && !CarpetaValida(this, m_carpeta, GetTitle())) return;
    m_empezar = empezar;
    EndModal(wxID_OK);
}

// ---------------------------------------------------------------- Varios enlaces (para el recolector)

DialogoVarios::DialogoVarios(wxWindow* padre, const Ajustes&)
    : wxDialog(padre, wxID_ANY, L"Pegar enlaces en el recolector", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    const int borde = FromDIP(12);
    auto* caja = new wxBoxSizer(wxVERTICAL);
    caja->Add(Etiqueta(this, L"Pega los enlaces, uno por línea. También sirven enlaces magnet y textos con enlaces en medio."),
              0, wxLEFT | wxRIGHT | wxTOP, borde);

    std::wstring pegado = sistema::TodoElPortapapeles();
    m_texto = new wxTextCtrl(this, wxID_ANY, enlaces::DeTexto(pegado).enlaces.empty() ? std::wstring() : pegado, wxDefaultPosition,
                             FromDIP(wxSize(600, 240)), wxTE_MULTILINE | wxTE_DONTWRAP | wxHSCROLL);
    m_texto->SetHint(L"https://ejemplo.org/archivo1.zip\nhttps://ejemplo.org/archivo2.zip");
    caja->Add(m_texto, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    auto* fila = new wxBoxSizer(wxHORIZONTAL);
    auto* abrir = new wxButton(this, wxID_ANY, L"Abrir un archivo .txt...");
    fila->Add(abrir, 0, wxRIGHT, FromDIP(12));
    m_cuenta = new wxStaticText(this, wxID_ANY, wxEmptyString);
    fila->Add(m_cuenta, 1, wxALIGN_CENTER_VERTICAL);
    caja->Add(fila, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);
    caja->Add(Nota(this, L"Pasan al recolector: ahí ves el nombre y el tamaño de cada uno, sin bajarlo, y eliges cuáles bajar. "
                         L"Nada empieza hasta que lo confirmes.", FromDIP(600)),
              0, wxLEFT | wxRIGHT | wxTOP, borde);

    wxButton *agregar = nullptr, *luego = nullptr;
    caja->Add(Botones(this, L"Agregar al recolector", &agregar, &luego, false), 0, wxEXPAND | wxALL, borde);
    agregar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(); });
    abrir->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AbrirTxt(); });
    m_texto->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Contar(); });

    SetSizerAndFit(caja);
    SetMinSize(FromDIP(wxSize(480, 340)));
    Contar();
    Centrar(this, padre);
    m_texto->SetFocus();
}

void DialogoVarios::Contar()
{
    auto r = enlaces::DeTexto(m_texto->GetValue().ToStdWstring());
    m_enlaces = r.enlaces;
    wxString t = r.enlaces.empty() ? wxString(L"Todavía no hay enlaces.")
                                   : wxString::Format(L"Enlaces encontrados: %zu", r.enlaces.size());
    if (r.repetidos > 0) t += wxString::Format(L"  ·  repetidos: %d (entran una vez)", r.repetidos);
    if (r.noPermitidos > 0) t += wxString::Format(L"  ·  de YouTube: %d (no entran)", r.noPermitidos);
    m_cuenta->SetLabel(t);
}

void DialogoVarios::AbrirTxt()
{
    wxFileDialog d(this, L"Abrir una lista de enlaces", wxEmptyString, wxEmptyString, L"Texto (*.txt)|*.txt|Todos los archivos (*.*)|*.*",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (d.ShowModal() != wxID_OK) return;
    std::wstring texto;
    if (!sistema::LeerTexto(d.GetPath().ToStdWstring(), texto))
    {
        ayuda::Aviso(L"No se pudo leer ese archivo (o pesa más de 16 MB).", GetTitle(), wxOK | wxICON_WARNING, this);
        return;
    }
    wxString actual = m_texto->GetValue();
    if (!actual.empty() && !actual.EndsWith(L"\n")) actual += L"\n";
    m_texto->ChangeValue(actual + texto);
    Contar();
}

void DialogoVarios::AlAceptar()
{
    Contar();
    if (m_enlaces.empty())
    {
        ayuda::Aviso(L"No encontré enlaces en el texto. Cada uno tiene que empezar por http://, https://, ftp:// o magnet:?",
                     GetTitle(), wxOK | wxICON_INFORMATION, this);
        return;
    }
    EndModal(wxID_OK);
}
// ---------------------------------------------------------------- Enlaces de una página

DialogoPagina::DialogoPagina(wxWindow* padre, const Ajustes& ajustes, const std::wstring& url, bool buscarYa)
    : wxDialog(padre, wxID_ANY, L"Enlaces de una página", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    const int borde = FromDIP(12);
    auto* caja = new wxBoxSizer(wxVERTICAL);

    auto* fila = new wxBoxSizer(wxHORIZONTAL);
    fila->Add(Etiqueta(this, L"Página:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    std::wstring inicial = url;
    if (inicial.empty())
    {
        std::wstring pegado = sistema::TextoDelPortapapeles();
        if (sistema::EsDireccion(pegado)) inicial = pegado;
    }
    m_url = new wxTextCtrl(this, wxID_ANY, inicial, wxDefaultPosition, FromDIP(wxSize(480, -1)), wxTE_PROCESS_ENTER);
    m_url->SetHint(L"https://...");
    fila->Add(m_url, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_buscar = new wxButton(this, wxID_ANY, L"Buscar enlaces");
    fila->Add(m_buscar, 0, wxALIGN_CENTER_VERTICAL);
    caja->Add(fila, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    caja->Add(Nota(this, L"Atarraya lee la página y muestra los archivos que enlaza, para que marques cuáles bajar. En Internet "
                         L"Archive usa la lista oficial de cada elemento. Los enlaces que una página arma con JavaScript "
                         L"pueden no aparecer. No baja videos de YouTube.", FromDIP(640)),
              0, wxLEFT | wxRIGHT | wxTOP, borde);

    m_estado = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_estado->SetFont(m_estado->GetFont().Bold());
    caja->Add(m_estado, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    auto* filtros = new wxBoxSizer(wxHORIZONTAL);
    filtros->Add(Etiqueta(this, L"Mostrar:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_filtro = new wxChoice(this, wxID_ANY);
    for (const wchar_t* t : { L"Archivos", L"Videos", L"Audio", L"Imágenes", L"Documentos", L"Comprimidos e imágenes de disco",
                              L"Programas", L"Torrents y magnet", L"Todo (también páginas y archivos internos)" })
        m_filtro->Append(t);
    m_filtro->SetSelection(0);
    filtros->Add(m_filtro, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));
    filtros->Add(Etiqueta(this, L"Que contenga:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_contiene = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(160, -1)));
    filtros->Add(m_contiene, 0, wxALIGN_CENTER_VERTICAL);
    caja->Add(filtros, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    m_lista = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(680, 280)), wxDV_ROW_LINES);
    m_lista->AppendToggleColumn(L"Bajar", wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(52));
    m_lista->AppendTextColumn(L"Nombre", wxDATAVIEW_CELL_INERT, FromDIP(250), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_lista->AppendTextColumn(L"Tipo", wxDATAVIEW_CELL_INERT, FromDIP(110), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_lista->AppendTextColumn(L"Tamaño", wxDATAVIEW_CELL_INERT, FromDIP(80), wxALIGN_RIGHT);
    m_lista->AppendTextColumn(L"Dirección", wxDATAVIEW_CELL_INERT, FromDIP(320), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    caja->Add(m_lista, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    auto* marcas = new wxBoxSizer(wxHORIZONTAL);
    auto* todos = new wxButton(this, wxID_ANY, L"Marcar los que se ven");
    auto* ninguno = new wxButton(this, wxID_ANY, L"Quitar marcas");
    marcas->Add(todos, 0, wxRIGHT, FromDIP(8));
    marcas->Add(ninguno, 0, wxRIGHT, FromDIP(12));
    m_resumen = new wxStaticText(this, wxID_ANY, wxEmptyString);
    marcas->Add(m_resumen, 1, wxALIGN_CENTER_VERTICAL);
    caja->Add(marcas, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    auto* rejilla = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
    rejilla->AddGrowableCol(1, 1);
    m_carpeta = ElegirCarpeta(this, ajustes.CarpetaEfectiva());
    rejilla->Add(Etiqueta(this, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_carpeta, 1, wxEXPAND);
    caja->Add(rejilla, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    caja->Add(Botones(this, L"Descargar los marcados", &m_descargar, &m_luego), 0, wxEXPAND | wxALL, borde);
    m_descargar->SetDefault();
    m_descargar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(true); });
    m_luego->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(false); });
    m_buscar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Buscar(); });
    m_url->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Buscar(); });
    m_filtro->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { Llenar(); });
    m_contiene->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Llenar(); });
    todos->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Marcar(true); });
    ninguno->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Marcar(false); });
    m_lista->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [this](wxDataViewEvent& e) {
        int fila = m_lista->ItemToRow(e.GetItem());
        if (fila >= 0 && (size_t)fila < m_visibles.size()) m_marcado[m_visibles[(size_t)fila]] = m_lista->GetToggleValue((unsigned int)fila, 0);
        Resumen();
    });

    SetSizerAndFit(caja);
    SetMinSize(FromDIP(wxSize(620, 520)));
    Centrar(this, padre);
    Resumen();
    m_url->SetFocus();
    if (buscarYa && !inicial.empty()) CallAfter([this]() { Buscar(); });
}

DialogoPagina::~DialogoPagina()
{
    if (m_parar) *m_parar = true;   // el hilo que busca deja de esperar a la página
}

void DialogoPagina::Buscar()
{
    std::wstring url = m_url->GetValue().Strip(wxString::both).ToStdWstring();
    if (!sistema::EsDireccion(url) || Minusculas(url.substr(0, 6)) == L"ftp://")
    {
        ayuda::Aviso(L"Escribe o pega la dirección de una página (empieza por http:// o https://).", GetTitle(),
                     wxOK | wxICON_WARNING, this);
        return;
    }
    if (enlaces::NoPermitido(Utf8(url)))
    {
        ayuda::Aviso(enlaces::TextoNoPermitido, GetTitle(), wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (m_parar) *m_parar = true;
    auto parar = std::make_shared<std::atomic<bool>>(false);
    m_parar = parar;
    m_estado->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    m_estado->SetLabel(L"Buscando enlaces...");
    m_buscar->Disable();
    // «parar» hace también de señal de vida: el destructor lo pone en verdadero. El hilo no toca el diálogo: solo le
    // manda el resultado al hilo de la ventana, y allí se mira «parar» antes de usarlo.
    DialogoPagina* yo = this;
    std::string u = Utf8(url);
    std::thread([yo, parar, u]() {
        enlaces::Pagina p;
        std::string error;
        bool ok = false;
        try
        {
            ok = enlaces::Buscar(u, p, error, parar.get());
        }
        catch (...)
        {
            ok = false;
            error = "La página respondió algo que Atarraya no pudo leer.";
        }
        hilos::Enviar([yo, parar, ok, p, error, u]() {
            if (*parar) return;
            yo->AlTerminarBusqueda(ok, p, error, u);
        });
    }).detach();
}

void DialogoPagina::AlTerminarBusqueda(bool ok, enlaces::Pagina p, const std::string& error, const std::string& url)
{
    m_buscar->Enable();
    if (!ok)
    {
        m_estado->SetForegroundColour(wxColour(0xD6, 0x45, 0x45));
        m_estado->SetLabel(wxString::FromUTF8(error));
        return;
    }
    m_urlPagina = url;
    m_internetArchive = p.internetArchive;
    m_todos = std::move(p.enlaces);
    m_marcado.assign(m_todos.size(), 0);
    size_t archivos = 0;
    for (auto& e : m_todos)
    {
        if (e.tipo != enlaces::Tipo::Pagina && e.tipo != enlaces::Tipo::Imagen && !e.interno) archivos++;
    }
    m_estado->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    wxString origen = p.internetArchive ? wxString(L"Internet Archive: ") : wxString();
    m_estado->SetLabel(origen + p.titulo + wxString::Format(L"  ·  %zu %s", archivos, archivos == 1 ? L"archivo" : L"archivos"));
    if (archivos == 0 && !m_todos.empty()) m_filtro->SetSelection(8);
    Llenar();
}

bool DialogoPagina::Visible(const enlaces::Enlace& e) const
{
    using T = enlaces::Tipo;
    bool tipoBien;
    switch (m_filtro->GetSelection())
    {
    case 0: tipoBien = e.tipo != T::Pagina && e.tipo != T::Imagen && !e.interno; break;   // las imágenes suelen ser adornos
    case 1: tipoBien = e.tipo == T::Video; break;
    case 2: tipoBien = e.tipo == T::Audio; break;
    case 3: tipoBien = e.tipo == T::Imagen; break;
    case 4: tipoBien = e.tipo == T::Documento; break;
    case 5: tipoBien = e.tipo == T::Comprimido || e.tipo == T::Disco; break;
    case 6: tipoBien = e.tipo == T::Programa; break;
    case 7: tipoBien = e.tipo == T::Torrent; break;
    default: tipoBien = true; break;
    }
    if (!tipoBien) return false;
    if (m_filtro->GetSelection() >= 1 && m_filtro->GetSelection() <= 7 && e.interno) return false;
    std::wstring busca = Minusculas(m_contiene->GetValue().Strip(wxString::both).ToStdWstring());
    return busca.empty() || Minusculas(e.nombre).find(busca) != std::wstring::npos;
}

void DialogoPagina::Llenar()
{
    m_lista->DeleteAllItems();
    m_visibles.clear();
    for (size_t i = 0; i < m_todos.size(); i++)
    {
        const auto& e = m_todos[i];
        if (!Visible(e)) continue;
        wxString tipo = enlaces::TextoTipo(e.tipo);
        if (!e.formato.empty()) tipo += L" (" + e.formato + L")";
        wxVector<wxVariant> fila;
        fila.push_back(wxVariant((bool)m_marcado[i]));
        fila.push_back(wxVariant(wxString(e.nombre)));
        fila.push_back(wxVariant(tipo));
        fila.push_back(wxVariant(e.tamano >= 0 ? wxString(TextoTamano(e.tamano)) : wxString(L"?")));
        fila.push_back(wxVariant(wxString::FromUTF8(e.url)));
        m_lista->AppendItem(fila);
        m_visibles.push_back(i);
    }
    Resumen();
}

void DialogoPagina::Marcar(bool si)
{
    if (si)
    {
        for (size_t f = 0; f < m_visibles.size(); f++)
        {
            m_marcado[m_visibles[f]] = 1;
            m_lista->SetToggleValue(true, (unsigned int)f, 0);
        }
    }
    else
    {
        std::fill(m_marcado.begin(), m_marcado.end(), 0);
        for (size_t f = 0; f < m_visibles.size(); f++) m_lista->SetToggleValue(false, (unsigned int)f, 0);
    }
    Resumen();
}

void DialogoPagina::Resumen()
{
    size_t n = 0;
    int64_t tamano = 0;
    bool sinTamano = false;
    for (size_t i = 0; i < m_todos.size(); i++)
    {
        if (!m_marcado[i]) continue;
        n++;
        if (m_todos[i].tamano >= 0) tamano += m_todos[i].tamano;
        else sinTamano = true;
    }
    wxString t = wxString::Format(L"Se ven %zu de %zu  ·  marcados: %zu", m_visibles.size(), m_todos.size(), n);
    if (n > 0) t += L" (" + wxString(TextoTamano(tamano)) + (sinTamano ? L" o más)" : L")");
    m_resumen->SetLabel(t);
    m_descargar->Enable(n > 0);
    m_luego->Enable(n > 0);
}

std::vector<enlaces::Enlace> DialogoPagina::Elegidos() const
{
    std::vector<enlaces::Enlace> r;
    for (size_t i = 0; i < m_todos.size(); i++)
    {
        if (m_marcado[i]) r.push_back(m_todos[i]);
    }
    return r;
}

std::wstring DialogoPagina::Carpeta() const { return m_carpeta->GetPath().ToStdWstring(); }

void DialogoPagina::AlAceptar(bool empezar)
{
    if (Elegidos().empty()) return;
    if (!CarpetaValida(this, m_carpeta, GetTitle())) return;
    m_empezar = empezar;
    EndModal(wxID_OK);
}

// ---------------------------------------------------------------- Bajar video (HLS o DASH)

namespace
{
    // De la mejor a la peor: más bits por segundo; si empatan, más resolución
    template <class T> std::vector<int> DeMejorAPeor(const std::vector<T>& v)
    {
        std::vector<int> r(v.size());
        for (int i = 0; i < (int)r.size(); i++) r[i] = i;
        std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
            if (v[a].banda != v[b].banda) return v[a].banda > v[b].banda;
            return (int64_t)v[a].ancho * v[a].alto > (int64_t)v[b].ancho * v[b].alto;
        });
        return r;
    }

    const wchar_t* const NotaSinUnir =
        L"El video y el audio vienen por separado: Atarraya los guarda en dos archivos, «(video)» y «(audio)», sin unirlos. "
        L"Para juntarlos hace falta un programa aparte, como FFmpeg (Atarraya no lo trae).";

    // La opción elegida en una lista ordenada de la mejor a la peor (la mejor si no hay ninguna elegida); -1 si está vacía
    int Elegida(const wxChoice* c, const std::vector<int>& orden)
    {
        int s = c->GetSelection();
        return s >= 0 && s < (int)orden.size() ? orden[s] : (orden.empty() ? -1 : orden.front());
    }
}

DialogoVideo::DialogoVideo(wxWindow* padre, const Ajustes& ajustes, Pedido pedido)
    : wxDialog(padre, wxID_ANY, L"Bajar video", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
      m_pedido(std::move(pedido)), m_conexiones(ajustes.conexiones)
{
    const int borde = FromDIP(12);
    auto* caja = new wxBoxSizer(wxVERTICAL);
    m_estado = new wxStaticText(this, wxID_ANY, L"Leyendo la lista del video...");
    m_estado->SetFont(m_estado->GetFont().Bold());
    caja->Add(m_estado, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);

    auto* rejilla = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
    rejilla->AddGrowableCol(1, 1);
    std::wstring nombre = wxString(m_pedido.titulo).Strip(wxString::both).ToStdWstring();
    if (nombre.empty())
    {
        nombre = NombreDeUrl(m_pedido.url);
        auto punto = nombre.find_last_of(L'.');
        if (punto != std::wstring::npos && punto > 0) nombre = nombre.substr(0, punto);
    }
    if (nombre.empty()) nombre = L"video";
    m_nombre = new wxTextCtrl(this, wxID_ANY, LimpiarNombre(nombre), wxDefaultPosition, FromDIP(wxSize(420, -1)));
    rejilla->Add(Etiqueta(this, L"Nombre:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_nombre, 1, wxEXPAND);
    m_etiquetaCalidad = Etiqueta(this, m_pedido.formato == "dash" ? L"Video:" : L"Calidad:");
    m_calidad = new wxChoice(this, wxID_ANY);
    rejilla->Add(m_etiquetaCalidad, 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_calidad, 1, wxEXPAND);
    m_etiquetaAudio = Etiqueta(this, L"Audio:");
    m_audio = new wxChoice(this, wxID_ANY);
    rejilla->Add(m_etiquetaAudio, 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_audio, 1, wxEXPAND);
    m_etiquetaAudio->Hide();
    m_audio->Hide();
    // Con un ancho fijo: si no, al volver a acomodar la ventana (cuando aparece «Audio:») crecería hasta el largo de la carpeta
    m_carpeta = ElegirCarpeta(this, ajustes.CarpetaEfectiva(), FromDIP(420));
    rejilla->Add(Etiqueta(this, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_carpeta, 1, wxEXPAND);
    caja->Add(rejilla, 0, wxEXPAND | wxALL, borde);

    m_nota = Nota(this, L"Atarraya baja el video por partes, con tus conexiones y tu límite de velocidad, y las junta en un solo archivo.",
                  FromDIP(560));
    caja->Add(m_nota, 0, wxLEFT | wxRIGHT, borde);

    caja->Add(Botones(this, L"Descargar", &m_descargar, &m_luego), 0, wxEXPAND | wxALL, borde);
    m_descargar->Disable();
    m_luego->Disable();
    m_descargar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(true); });
    m_luego->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(false); });

    SetSizerAndFit(caja);
    SetMinSize(GetSize());
    Centrar(this, padre);
    CallAfter([this]() { Leer(); });
}

DialogoVideo::~DialogoVideo()
{
    if (m_parar) *m_parar = true;   // el hilo que lee la lista ya no toca esta ventana
}

void DialogoVideo::Leer()
{
    auto parar = std::make_shared<std::atomic<bool>>(false);
    m_parar = parar;
    DialogoVideo* yo = this;
    Pedido p = m_pedido;
    std::thread([yo, parar, p]() {
        std::string texto, urlFinal, error;
        bool local = false, ok = false;
        try
        {
            ok = BajarLista(p.url, p.referer, p.cookies, p.agente, texto, urlFinal, local, error, parar.get());
        }
        catch (...)
        {
            ok = false;
            error = "La lista del video llegó en un formato que Atarraya no pudo leer.";
        }
        hilos::Enviar([yo, parar, ok, texto, urlFinal, local, error]() {
            if (*parar) return;
            yo->AlLeer(ok, texto, urlFinal, local, error);
        });
    }).detach();
}

void DialogoVideo::NoSePuede(const wxString& estado, const wxString& nota)
{
    m_estado->SetForegroundColour(wxColour(0xD6, 0x45, 0x45));
    m_estado->SetLabel(estado);
    m_nota->SetLabel(nota);
    m_nota->Wrap(-1);   // wx no vuelve a partir el texto si se le pide el mismo ancho que la vez anterior
    m_nota->Wrap(FromDIP(560));
    m_calidad->Disable();
    m_audio->Disable();
    m_descargar->Disable();
    m_luego->Disable();
    m_listo = false;
    GetSizer()->Fit(this);
    Registrar("bajar video: no se puede (" + Utf8(estado.ToStdWstring()) + ")");
}

void DialogoVideo::AlLeer(bool ok, const std::string& texto, const std::string& urlFinal, bool local, const std::string& error)
{
    if (!ok)
    {
        NoSePuede(L"No se pudo leer la lista del video", wxString::FromUTF8(error));
        return;
    }
    m_urlLista = urlFinal.empty() ? m_pedido.url : urlFinal;
    m_local = local;
    // La lista es de esta PC o de la red de la casa (por dónde se conectó o por su nombre): antes de bajar nada de ahí
    // se le pregunta a la persona
    m_listaLocal = local || enlaces::DireccionLocal(m_urlLista) || enlaces::DireccionLocal(m_pedido.url);
    const wxString protegido = L"Este video está protegido";
    const wxString porQue = L"Usa DRM o está cifrado. Atarraya no baja videos protegidos ni cifrados.";
    const wxString enVivo = L"Es una transmisión en vivo";
    const wxString grande = L"Este video es demasiado grande";
    double duracion = 0;
    wxString nota;
    if (m_pedido.formato == "hls")
    {
        m_hls = flujos::LeerHls(texto, m_urlLista);
        if (m_hls.demasiadas) { NoSePuede(grande, wxString::FromUTF8(flujos::TextoDemasiadas)); return; }
        if (!m_hls.valida) { NoSePuede(L"No es una lista de video", L"La dirección no trae una lista HLS (.m3u8)."); return; }
        if (m_hls.cifrada) { NoSePuede(protegido, porQue); return; }
        if (!m_hls.maestra && m_hls.enVivo) { NoSePuede(enVivo, wxString::FromUTF8(flujos::TextoEnVivo)); return; }
        if (m_hls.maestra)
        {
            m_ordenCalidad = DeMejorAPeor(m_hls.variantes);
            for (int i : m_ordenCalidad)
            {
                const auto& v = m_hls.variantes[i];
                m_calidad->Append(flujos::TextoCalidad(v.ancho, v.alto, v.banda, false));
            }
            if (m_ordenCalidad.empty()) { NoSePuede(L"La lista no trae calidades", L"No hay nada para bajar en esta lista."); return; }
            m_calidad->SetSelection(0);
            nota = L"Se baja por partes, con tus conexiones y tu límite de velocidad, y se junta en un solo archivo (.ts, o .mp4 si "
                   L"la lista es CMAF).";
            if (flujos::AudioDeVariante(m_hls, m_ordenCalidad.front()) >= 0) nota += wxString(L" ") + NotaSinUnir;
        }
        else
        {
            m_calidad->Append(L"La única que trae la lista");
            m_calidad->SetSelection(0);
            duracion = m_hls.duracion;
            nota = wxString::Format(L"Son %zu partes: se bajan con tus conexiones y tu límite de velocidad, y se juntan en un solo "
                                    L"archivo (%s).", m_hls.trozos.size() + (m_hls.inicio.url.empty() ? 0 : 1),
                                    flujos::ExtensionHls(m_hls).c_str());
        }
    }
    else
    {
        m_dash = flujos::LeerDash(texto, m_urlLista);
        if (m_dash.demasiadas) { NoSePuede(grande, wxString::FromUTF8(flujos::TextoDemasiadas)); return; }
        if (!m_dash.valido) { NoSePuede(L"No es un manifiesto de video", L"La dirección no trae un manifiesto DASH (.mpd)."); return; }
        if (m_dash.protegido) { NoSePuede(protegido, porQue); return; }
        if (m_dash.enVivo) { NoSePuede(enVivo, wxString::FromUTF8(flujos::TextoEnVivo)); return; }
        if (m_dash.variasPartes) { NoSePuede(L"Este video no se puede bajar todavía", wxString::FromUTF8(flujos::TextoVariasPartes)); return; }
        if (m_dash.videos.empty() && m_dash.audios.empty()) { NoSePuede(L"El manifiesto no trae video ni audio", L"No hay nada para bajar."); return; }
        m_ordenCalidad = DeMejorAPeor(m_dash.videos);
        for (int i : m_ordenCalidad)
        {
            const auto& r = m_dash.videos[i];
            m_calidad->Append(flujos::TextoCalidad(r.ancho, r.alto, r.banda, false));
        }
        m_ordenAudio = DeMejorAPeor(m_dash.audios);
        for (int i : m_ordenAudio)
        {
            const auto& r = m_dash.audios[i];
            m_audio->Append(flujos::TextoCalidad(0, 0, r.banda, true));
        }
        if (m_ordenCalidad.empty())
        {
            m_etiquetaCalidad->Hide();
            m_calidad->Hide();
        }
        else m_calidad->SetSelection(0);
        if (!m_ordenAudio.empty())
        {
            m_etiquetaAudio->Show();
            m_audio->Show();
            m_audio->SetSelection(0);
        }
        duracion = m_dash.duracion;
        nota = !m_ordenCalidad.empty() && !m_ordenAudio.empty() ? wxString(NotaSinUnir)
                                                                  : wxString(L"Se baja por partes, con tus conexiones y tu límite de velocidad.");
    }
    m_estado->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    m_estado->SetLabel(duracion > 0 ? wxString(L"Listo para bajar: dura ") + TextoDuracion((int64_t)std::llround(duracion))
                                    : wxString(L"Listo para bajar"));
    m_nota->SetLabel(nota);
    m_nota->Wrap(-1);   // igual que arriba: si no, se queda en una sola línea muy larga
    m_nota->Wrap(FromDIP(560));
    m_listo = true;
    m_descargar->Enable();
    m_luego->Enable();
    m_descargar->SetFocus();
    GetSizer()->SetSizeHints(this);
    Fit();
    Layout();
}

void DialogoVideo::AlAceptar(bool empezar)
{
    if (!m_listo || m_revisando) return;
    if (!CarpetaValida(this, m_carpeta, GetTitle())) return;
    m_empezar = empezar;
    // Un video de esta PC o de la red de la casa: la persona decide si Atarraya se conecta ahí
    if (m_listaLocal && !m_localConfirmado)
    {
        wxString sitio = wxString::FromUTF8(enlaces::Sitio(m_urlLista));
        wxMessageDialog d(this, L"Este video está en tu red: " + sitio + L" (esta PC u otro aparato de tu casa).\n\n"
                                L"¿Lo bajas de ahí? Hazlo solo si confías en ese aparato.",
                          L"Video de tu red", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        d.SetYesNoLabels(L"Sí, bajarlo", L"No");
        if (d.ShowModal() != wxID_YES) return;
        m_localConfirmado = true;
    }
    Revisar();
}

void DialogoVideo::Revisar()
{
    // Lo que se usa con lo elegido. En HLS con calidades: la lista de esa calidad, la de su audio y las de sus
    // subtítulos, que todavía no se leyeron. Si no: las partes que ya se conocen (de la lista o del manifiesto).
    std::vector<std::string> listas;
    std::vector<std::pair<flujos::Trozo, std::vector<flujos::Trozo>>> conocidas;
    size_t bytesConocidos = 0;
    std::vector<flujos::Representacion> representaciones;   // DASH: sus direcciones se arman en el hilo, con el tope
    if (m_pedido.formato == "hls")
    {
        if (m_hls.maestra)
        {
            int v = Elegida(m_calidad, m_ordenCalidad);
            if (v < 0) return;
            listas.push_back(m_hls.variantes[v].url);
            for (const auto& u : flujos::ListasJuntas(m_hls, v)) listas.push_back(u);
        }
        else
        {
            conocidas.push_back({ m_hls.inicio, m_hls.trozos });
            bytesConocidos = m_hls.bytes;
        }
    }
    else
    {
        int v = m_ordenCalidad.empty() ? -1 : Elegida(m_calidad, m_ordenCalidad);
        int a = m_ordenAudio.empty() ? -1 : Elegida(m_audio, m_ordenAudio);
        if (v >= 0) representaciones.push_back(m_dash.videos[v]);
        if (a >= 0) representaciones.push_back(m_dash.audios[a]);
    }
    m_revisando = true;
    m_descargar->Disable();
    m_luego->Disable();
    m_calidad->Disable();
    m_audio->Disable();
    m_estado->SetLabel(L"Revisando las listas del video...");
    // Si la lista es de internet, nada de lo que pide puede ser de esta PC ni de su red
    bool soloAfuera = !m_listaLocal;
    Pedido p = m_pedido;
    auto parar = m_parar;
    DialogoVideo* yo = this;
    std::thread([yo, parar, p, listas, conocidas, bytesConocidos, representaciones, soloAfuera]() mutable {
        std::string error;
        size_t total = 0, bytes = bytesConocidos;
        try
        {
            if (listas.size() > flujos::MaximoListasJuntas + 1) error = flujos::TextoDemasiadas;
            for (size_t k = 0; error.empty() && k < listas.size(); k++)
            {
                std::string texto, urlFinal;
                bool local = false;
                if (!BajarLista(listas[k], p.referer, p.cookies, p.agente, texto, urlFinal, local, error, parar.get(), soloAfuera)) break;
                auto l = flujos::LeerHls(texto, urlFinal.empty() ? listas[k] : urlFinal);
                if (l.demasiadas || (bytes += l.bytes) > flujos::MaximoBytesDirecciones) error = flujos::TextoDemasiadas;
                else if (!l.valida || l.maestra) error = "Una de las listas del video no es una lista HLS.";
                else if (l.cifrada) error = flujos::TextoProtegido;   // también si es la del audio o la de los subtítulos
                else if (l.enVivo) error = flujos::TextoEnVivo;
                else
                {
                    error = flujos::RevisarTrozos(l.inicio, l.trozos, soloAfuera || !local);
                    total += l.trozos.size() + (l.inicio.url.empty() ? 0 : 1);
                }
            }
            for (const auto& [inicio, trozos] : conocidas)
            {
                if (!error.empty()) break;
                error = flujos::RevisarTrozos(inicio, trozos, soloAfuera);
                total += trozos.size() + (inicio.url.empty() ? 0 : 1);
            }
            // DASH: se arman las direcciones de lo elegido (el video y el audio), de una en una y con el tope de bytes
            for (auto& r : representaciones)
            {
                if (!error.empty()) break;
                if (!flujos::ArmarTrozos(r, bytes))
                {
                    error = flujos::TextoDemasiadas;
                    break;
                }
                error = flujos::RevisarTrozos(r.inicio, r.trozos, soloAfuera);
                total += r.trozos.size() + (r.inicio.url.empty() ? 0 : 1);
                r.trozos.clear();
                r.trozos.shrink_to_fit();
            }
            if (error.empty() && total > flujos::MaximoPartes) error = flujos::TextoDemasiadas;
        }
        catch (...)
        {
            error = "La lista del video llegó en un formato que Atarraya no pudo leer.";
        }
        hilos::Enviar([yo, parar, error]() {
            if (*parar) return;
            yo->AlRevisar(error);
        });
    }).detach();
}

void DialogoVideo::AlRevisar(const std::string& error)
{
    m_revisando = false;
    if (error.empty())
    {
        EndModal(wxID_OK);
        return;
    }
    wxString texto = wxString::FromUTF8(error);
    if (error == flujos::TextoProtegido)
        NoSePuede(L"Este video está protegido", L"Usa DRM o está cifrado (también su audio o sus subtítulos). Atarraya no baja videos protegidos ni cifrados.");
    else if (error == flujos::TextoEnVivo) NoSePuede(L"Es una transmisión en vivo", texto);
    else if (error == flujos::TextoRedLocal) NoSePuede(L"Este video pide archivos de tu red", texto);
    else if (error == flujos::TextoDemasiadas) NoSePuede(L"Este video es demasiado grande", texto);
    else if (error == Utf8(enlaces::TextoNoPermitido)) NoSePuede(L"Atarraya no baja este video", texto);
    else NoSePuede(L"No se pudo revisar el video", texto);
}

std::vector<DatosDescarga> DialogoVideo::Descargas() const
{
    std::vector<DatosDescarga> r;
    if (!m_listo) return r;
    DatosDescarga base;
    base.referer = m_pedido.referer;
    base.cookies = m_pedido.cookies;
    base.agente = m_pedido.agente;
    base.carpeta = m_carpeta->GetPath().ToStdWstring();
    base.conexiones = m_conexiones;
    // Lo de esta PC o de su red solo si la persona lo confirmó (si no, ni se llega aquí)
    base.flujoLocal = m_listaLocal && m_localConfirmado;
    std::wstring nombre = LimpiarNombre(m_nombre->GetValue().Strip(wxString::both).ToStdWstring());
    if (nombre.empty()) nombre = L"video";
    if (m_pedido.formato == "hls")
    {
        DatosDescarga d = base;
        d.formato = "hls";
        d.nombre = nombre;
        d.url = m_urlLista;
        if (m_hls.maestra)
        {
            int v = Elegida(m_calidad, m_ordenCalidad);
            if (v < 0) return r;
            d.url = m_hls.variantes[v].url;
            // Las otras listas de esta calidad (audio aparte, subtítulos): cada descarga las vuelve a revisar al empezar
            auto juntas = flujos::ListasJuntas(m_hls, v);
            d.flujoListas = juntas;
            int a = flujos::AudioDeVariante(m_hls, v);
            if (a >= 0)
            {
                // El audio va aparte: dos archivos sin unir
                d.nombre = nombre + L" (video)";
                DatosDescarga au = base;
                au.formato = "hls";
                au.url = m_hls.audios[a].url;
                au.nombre = nombre + L" (audio)";
                au.flujoListas.push_back(d.url);
                for (const auto& u : juntas)
                    if (u != au.url) au.flujoListas.push_back(u);
                r.push_back(d);
                r.push_back(au);
                return r;
            }
        }
        r.push_back(d);
        return r;
    }
    // DASH: el mejor video y el mejor audio (o los elegidos), cada uno en su archivo
    int v = m_ordenCalidad.empty() ? -1 : Elegida(m_calidad, m_ordenCalidad);
    int a = m_ordenAudio.empty() ? -1 : Elegida(m_audio, m_ordenAudio);
    bool dos = v >= 0 && a >= 0;
    auto agregar = [&](const flujos::Representacion& rep, bool audio) {
        DatosDescarga d = base;
        std::wstring n = dos ? nombre + (audio ? L" (audio)" : L" (video)") : nombre;
        if (rep.unArchivo)
        {
            // Todo el medio es un archivo: una descarga normal, con varias conexiones. Su dirección salió del
            // manifiesto: si el manifiesto es de internet, no puede llevar a esta PC ni a su red.
            d.url = rep.trozos.front().url;
            d.nombre = n + flujos::ExtensionDash(rep);
            d.nombreFijo = true;
            d.soloAfuera = !base.flujoLocal;
            d.sinProxy = true;   // como todo lo de un video de una página: directo, nunca por proxy
        }
        else
        {
            d.formato = "dash";
            d.url = m_urlLista;
            d.pista = std::string(audio ? "audio:" : "video:") + rep.id;
            d.nombre = n;
        }
        r.push_back(std::move(d));
    };
    if (v >= 0) agregar(m_dash.videos[v], false);
    if (a >= 0) agregar(m_dash.audios[a], true);
    return r;
}

// ---------------------------------------------------------------- Ajustes

DialogoAjustes::DialogoAjustes(wxWindow* padre, Ajustes& ajustes)
    : wxDialog(padre, wxID_ANY, L"Ajustes"), m_ajustes(ajustes)
{
    const int borde = FromDIP(12);
    auto* caja = new wxBoxSizer(wxVERTICAL);
    auto* rejilla = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
    rejilla->AddGrowableCol(1, 1);

    auto* carpeta = ElegirCarpeta(this, ajustes.CarpetaEfectiva(), FromDIP(380));
    rejilla->Add(Etiqueta(this, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(carpeta, 1, wxEXPAND);

    auto* conexiones = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 16, ajustes.conexiones);
    rejilla->Add(Etiqueta(this, L"Conexiones por archivo:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(conexiones);

    auto* simultaneas = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, ajustes.simultaneas);
    rejilla->Add(Etiqueta(this, L"Descargas a la vez:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* filaSimultaneas = new wxBoxSizer(wxHORIZONTAL);
    filaSimultaneas->Add(simultaneas, 0, wxALIGN_CENTER_VERTICAL);
    filaSimultaneas->Add(Nota(this, L"también cuentan los torrents que bajan", FromDIP(260)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    rejilla->Add(filaSimultaneas);

    auto* limite = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 1000000, ajustes.limiteKB);
    rejilla->Add(Etiqueta(this, L"Bajada máxima, en total (KB/s):"), 0, wxALIGN_CENTER_VERTICAL);
    auto* filaLimite = new wxBoxSizer(wxHORIZONTAL);
    filaLimite->Add(limite, 0, wxALIGN_CENTER_VERTICAL);
    filaLimite->Add(Nota(this, L"0 = sin límite", FromDIP(200)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    rejilla->Add(filaLimite);

    auto* subida = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 1000000, ajustes.limiteSubidaKB);
    rejilla->Add(Etiqueta(this, L"Subida máxima de los torrents (KB/s):"), 0, wxALIGN_CENTER_VERTICAL);
    auto* filaSubida = new wxBoxSizer(wxHORIZONTAL);
    filaSubida->Add(subida, 0, wxALIGN_CENTER_VERTICAL);
    filaSubida->Add(Nota(this, L"0 = sin límite", FromDIP(200)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    rejilla->Add(filaSubida);

    // El proxy es el de Windows (no se cambia aquí). Solo se muestra «host:puerto», nunca un usuario o una clave.
    std::string proxy = ProxyEnUso();
    rejilla->Add(Etiqueta(this, L"Proxy:"), 0, wxALIGN_TOP);
    auto* columnaProxy = new wxBoxSizer(wxVERTICAL);
    columnaProxy->Add(new wxStaticText(this, wxID_ANY, proxy.empty() ? wxString(L"Ninguno (si pones uno en Windows, las descargas lo usan)")
                                                                     : L"El de Windows: " + wxString::FromUTF8(proxy)));
    columnaProxy->Add(Nota(this, L"Los videos de las páginas y las consultas del recolector no usan el proxy.", FromDIP(380)), 0, wxTOP,
                      FromDIP(2));
    rejilla->Add(columnaProxy, 1, wxEXPAND);
    caja->Add(rejilla, 0, wxEXPAND | wxALL, borde);

    auto* preguntar = new wxCheckBox(this, wxID_ANY, L"Preguntar antes de bajar lo que llega del navegador");
    preguntar->SetValue(ajustes.preguntarNavegador);
    auto* bandeja = new wxCheckBox(this, wxID_ANY, L"Al cerrar la ventana, seguir junto al reloj (las descargas continúan)");
    bandeja->SetValue(ajustes.bandejaAlCerrar);
    auto* avisar = new wxCheckBox(this, wxID_ANY, L"Avisar cuando termina una descarga");
    avisar->SetValue(ajustes.avisarAlTerminar);
    auto* portapapeles = new wxCheckBox(this, wxID_ANY, L"Vigilar el portapapeles: los enlaces de archivos que copies pasan al recolector");
    portapapeles->SetValue(ajustes.vigilarPortapapeles);
    auto* cnl = new wxCheckBox(this, wxID_ANY, L"Recibir enlaces de Click'n'Load (solo desde esta PC, 127.0.0.1:9666)");
    cnl->SetValue(ajustes.clickNLoad);
    for (auto* c : { preguntar, bandeja, avisar, portapapeles, cnl }) caja->Add(c, 0, wxLEFT | wxRIGHT | wxBOTTOM, borde);
    caja->Add(Nota(this, L"Lo que llega del portapapeles o de Click'n'Load espera en el recolector: no se baja nada hasta que lo "
                         L"confirmes. Los captchas siempre los resuelves tú en tu navegador.", FromDIP(520)),
              0, wxLEFT | wxRIGHT | wxBOTTOM, borde);

    // Jícaro, el descompresor de Brizna Soft. Sin Jícaro, la casilla queda apagada (lo guardado no se toca).
    caja->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, borde);
    auto elegido = std::make_shared<std::wstring>(ajustes.rutaJicaro);
    auto quiere = std::make_shared<bool>(ajustes.jicaroAlTerminar);
    auto* casillaJicaro = new wxCheckBox(this, wxID_ANY, L"Descomprimir con Jícaro al terminar");
    caja->Add(casillaJicaro, 0, wxLEFT | wxRIGHT | wxBOTTOM, borde);
    auto* filaJicaro = new wxBoxSizer(wxHORIZONTAL);
    auto* dondeJicaro = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(300), -1), wxST_ELLIPSIZE_MIDDLE);
    dondeJicaro->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    auto* elegirJicaro = new wxButton(this, wxID_ANY, L"Elegir...");
    elegirJicaro->SetToolTip(L"Si pusiste Jícaro en otra carpeta, elige su archivo Jicaro.exe");
    auto* paginaJicaro = new wxButton(this, wxID_ANY, L"Abrir la página de Jícaro");
    paginaJicaro->SetToolTip(L"Abre en tu navegador la página de Jícaro, el descompresor gratis de Brizna Soft");
    filaJicaro->Add(dondeJicaro, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    filaJicaro->Add(elegirJicaro, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    filaJicaro->Add(paginaJicaro, 0, wxALIGN_CENTER_VERTICAL);
    caja->Add(filaJicaro, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, borde);
    caja->Add(Nota(this, L"Cuando termina un paquete (o un .rar, .zip o .7z suelto), Jícaro lo descomprime en una carpeta con su "
                         L"nombre, al lado de las partes. Las partes no se borran: después puedes mandarlas a la Papelera.", FromDIP(520)),
              0, wxLEFT | wxRIGHT | wxBOTTOM, borde);
    auto mostrarJicaro = [=, this]() {
        std::wstring ruta = jicaro::Buscar(*elegido);
        bool hay = !ruta.empty();
        casillaJicaro->Enable(hay);
        casillaJicaro->SetValue(hay && *quiere);
        dondeJicaro->SetLabel(hay ? L"Jícaro está en " + ruta : std::wstring(L"No se encontró Jícaro en esta PC."));
        dondeJicaro->SetToolTip(hay ? wxString(ruta) : wxString());
        paginaJicaro->Show(!hay);
        Layout();
    };
    mostrarJicaro();
    casillaJicaro->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent&) { *quiere = casillaJicaro->GetValue(); });
    elegirJicaro->Bind(wxEVT_BUTTON, [=, this](wxCommandEvent&) {
        wxFileDialog d(this, L"¿Dónde está Jícaro?", wxEmptyString, L"Jicaro.exe", L"Jícaro (Jicaro.exe)|Jicaro.exe",
                       wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (d.ShowModal() != wxID_OK) return;
        std::wstring ruta = d.GetPath().ToStdWstring();
        if (!jicaro::EsRutaDeJicaro(ruta))
        {
            ayuda::Aviso(L"Elige el archivo Jicaro.exe, el programa de Jícaro.", L"Ajustes", wxOK | wxICON_WARNING, this);
            return;
        }
        *elegido = ruta;
        *quiere = true;   // si la persona lo buscó, es porque lo quiere usar
        mostrarJicaro();
        Fit();
    });
    paginaJicaro->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { sistema::AbrirPaginaWeb("https://brizna-soft.github.io/programas/jicaro/"); });

    caja->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, borde);
    if (auto* ok = FindWindow(wxID_OK)) ok->SetLabel(L"Guardar");
    if (auto* cancelar = FindWindow(wxID_CANCEL)) cancelar->SetLabel(L"Cancelar");

    Bind(wxEVT_BUTTON, [=, this](wxCommandEvent& e) {
        if (casillaJicaro->IsEnabled()) m_ajustes.jicaroAlTerminar = casillaJicaro->GetValue();
        m_ajustes.rutaJicaro = *elegido;
        m_ajustes.carpeta = carpeta->GetPath().ToStdWstring();
        m_ajustes.conexiones = conexiones->GetValue();
        m_ajustes.simultaneas = simultaneas->GetValue();
        m_ajustes.limiteKB = limite->GetValue();
        m_ajustes.limiteSubidaKB = subida->GetValue();
        m_ajustes.preguntarNavegador = preguntar->GetValue();
        m_ajustes.bandejaAlCerrar = bandeja->GetValue();
        m_ajustes.avisarAlTerminar = avisar->GetValue();
        m_ajustes.vigilarPortapapeles = portapapeles->GetValue();
        m_ajustes.clickNLoad = cnl->GetValue();
        m_ajustes.Guardar();
        e.Skip();
    }, wxID_OK);

    SetSizerAndFit(caja);
    CentreOnParent();
}

// ---------------------------------------------------------------- Navegadores

DialogoNavegadores::DialogoNavegadores(wxWindow* padre)
    : wxDialog(padre, wxID_ANY, L"Conectar con los navegadores")
{
    const int borde = FromDIP(12);
    const int ancho = FromDIP(520);
    auto* caja = new wxBoxSizer(wxVERTICAL);

    auto* titulo = new wxStaticText(this, wxID_ANY, L"Que el navegador te pase las descargas");
    titulo->SetFont(titulo->GetFont().Scaled(1.2f).Bold());
    caja->Add(titulo, 0, wxALL, borde);

    auto* pasos = new wxStaticText(this, wxID_ANY,
        L"1. Pulsa «Conectar». Esto le dice a Chrome, Edge, Brave y Firefox dónde está Atarraya "
        L"(solo en tu cuenta de Windows; se puede quitar cuando quieras).\n\n"
        L"2. Instala la extensión «Atarraya» en tu navegador. Mientras no esté en las tiendas: en Chrome, Edge o "
        L"Brave abre la página de extensiones, activa el «Modo de desarrollador», pulsa «Cargar descomprimida» y elige "
        L"la carpeta «extension\\chrome» que viene con el programa.\n\n"
        L"3. Listo: lo que bajes en el navegador se pasa a Atarraya, de cualquier sitio. También puedes hacer "
        L"clic derecho en un enlace (también en un enlace magnet) y elegir «Descargar con Atarraya».");
    pasos->Wrap(ancho);
    caja->Add(pasos, 0, wxLEFT | wxRIGHT, borde);

    m_estado = new wxStaticText(this, wxID_ANY, wxEmptyString);
    caja->Add(m_estado, 0, wxALL, borde);

    auto* fila = new wxBoxSizer(wxHORIZONTAL);
    auto* conectar = new wxButton(this, wxID_ANY, L"Conectar");
    auto* quitar = new wxButton(this, wxID_ANY, L"Quitar la conexión");
    auto* carpeta = new wxButton(this, wxID_ANY, L"Abrir la carpeta de la extensión");
    fila->Add(conectar, 0, wxRIGHT, FromDIP(8));
    fila->Add(quitar, 0, wxRIGHT, FromDIP(8));
    fila->Add(carpeta);
    caja->Add(fila, 0, wxLEFT | wxRIGHT, borde);

    caja->Add(new wxHyperlinkCtrl(this, wxID_ANY, L"Instrucciones paso a paso",
                                  L"https://github.com/Brizna-Soft/atarraya#conectar-con-el-navegador"),
              0, wxLEFT | wxRIGHT | wxTOP, borde);

    caja->Add(CreateSeparatedButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, borde);
    if (auto* cerrar = FindWindow(wxID_CLOSE)) cerrar->SetLabel(L"Cerrar");
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);

    conectar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        std::wstring error;
        if (!navegadores::Conectar(error)) ayuda::Aviso(error, L"Conectar con los navegadores", wxOK | wxICON_WARNING, this);
        Actualizar();
    });
    quitar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        navegadores::Desconectar();
        Actualizar();
    });
    carpeta->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        std::wstring ruta = navegadores::CarpetaExtension() + L"\\chrome";
        if (!ExisteCarpeta(ruta)) ruta = navegadores::CarpetaExtension();
        sistema::Abrir(ruta);
    });

    Actualizar();
    SetSizerAndFit(caja);
    CentreOnParent();
}

void DialogoNavegadores::Actualizar()
{
    wxString texto = L"Estado:";
    for (auto& n : navegadores::Estado())
    {
        texto += L"\n   " + n.nombre + L": ";
        if (!n.instalado) texto += L"no está instalado";
        else texto += n.conectado ? L"conectado ✓" : L"sin conectar";
    }
    m_estado->SetLabel(texto);
    Layout();
    Fit();
}
