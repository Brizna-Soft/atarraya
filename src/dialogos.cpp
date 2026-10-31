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

#include <thread>

#include "ayuda.hpp"
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
    if (url != d.url) { d.cookies.clear(); d.referer.clear(); }   // otra dirección: las cookies no corresponden
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

// ---------------------------------------------------------------- Varios enlaces

DialogoVarios::DialogoVarios(wxWindow* padre, const Ajustes& ajustes)
    : wxDialog(padre, wxID_ANY, L"Agregar varios enlaces", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
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

    auto* rejilla = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
    rejilla->AddGrowableCol(1, 1);
    m_carpeta = ElegirCarpeta(this, ajustes.CarpetaEfectiva());
    rejilla->Add(Etiqueta(this, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_carpeta, 1, wxEXPAND);
    caja->Add(rejilla, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);
    caja->Add(Nota(this, L"Entran a la cola: bajan de a pocos a la vez, según «Descargas a la vez» en Ajustes. "
                         L"Los enlaces que ya están en la lista sin terminar no se repiten.", FromDIP(600)),
              0, wxLEFT | wxRIGHT | wxTOP, borde);

    wxButton *descargar = nullptr, *luego = nullptr;
    caja->Add(Botones(this, L"Descargar", &descargar, &luego), 0, wxEXPAND | wxALL, borde);
    descargar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(true); });
    luego->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(false); });
    abrir->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AbrirTxt(); });
    m_texto->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Contar(); });

    SetSizerAndFit(caja);
    SetMinSize(FromDIP(wxSize(480, 360)));
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
    if (r.repetidos > 0) t += wxString::Format(L"  ·  repetidos: %d (se agregan una vez)", r.repetidos);
    if (r.noPermitidos > 0) t += wxString::Format(L"  ·  de YouTube: %d (no se agregan)", r.noPermitidos);
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

std::wstring DialogoVarios::Carpeta() const { return m_carpeta->GetPath().ToStdWstring(); }

void DialogoVarios::AlAceptar(bool empezar)
{
    Contar();
    if (m_enlaces.empty())
    {
        ayuda::Aviso(L"No encontré enlaces en el texto. Cada uno tiene que empezar por http://, https://, ftp:// o magnet:?",
                     GetTitle(), wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (!CarpetaValida(this, m_carpeta, GetTitle())) return;
    m_empezar = empezar;
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
    wxWeakRef<DialogoPagina> yo(this);
    std::string u = Utf8(url);
    std::thread([yo, parar, u]() {
        enlaces::Pagina p;
        std::string error;
        bool ok = enlaces::Buscar(u, p, error, parar.get());
        wxTheApp->CallAfter([yo, parar, ok, p, error, u]() {
            if (!yo || *parar) return;
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
    caja->Add(rejilla, 0, wxEXPAND | wxALL, borde);

    auto* preguntar = new wxCheckBox(this, wxID_ANY, L"Preguntar antes de bajar lo que llega del navegador");
    preguntar->SetValue(ajustes.preguntarNavegador);
    auto* bandeja = new wxCheckBox(this, wxID_ANY, L"Al cerrar la ventana, seguir junto al reloj (las descargas continúan)");
    bandeja->SetValue(ajustes.bandejaAlCerrar);
    auto* avisar = new wxCheckBox(this, wxID_ANY, L"Avisar cuando termina una descarga");
    avisar->SetValue(ajustes.avisarAlTerminar);
    for (auto* c : { preguntar, bandeja, avisar }) caja->Add(c, 0, wxLEFT | wxRIGHT | wxBOTTOM, borde);

    caja->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, borde);
    if (auto* ok = FindWindow(wxID_OK)) ok->SetLabel(L"Guardar");
    if (auto* cancelar = FindWindow(wxID_CANCEL)) cancelar->SetLabel(L"Cancelar");

    Bind(wxEVT_BUTTON, [=, this](wxCommandEvent& e) {
        m_ajustes.carpeta = carpeta->GetPath().ToStdWstring();
        m_ajustes.conexiones = conexiones->GetValue();
        m_ajustes.simultaneas = simultaneas->GetValue();
        m_ajustes.limiteKB = limite->GetValue();
        m_ajustes.limiteSubidaKB = subida->GetValue();
        m_ajustes.preguntarNavegador = preguntar->GetValue();
        m_ajustes.bandejaAlCerrar = bandeja->GetValue();
        m_ajustes.avisarAlTerminar = avisar->GetValue();
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
