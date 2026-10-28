#include "dialogos.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filepicker.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

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
        if (sistema::EsDireccion(pegado)) url = pegado;
    }
    m_url = new wxTextCtrl(this, wxID_ANY, url, wxDefaultPosition, FromDIP(wxSize(460, -1)));
    m_url->SetHint(L"https://...");
    rejilla->Add(Etiqueta(this, L"Dirección:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_url, 1, wxEXPAND);

    m_nombre = new wxTextCtrl(this, wxID_ANY, previa.nombre);
    m_nombre->SetHint(L"(automático)");
    rejilla->Add(Etiqueta(this, L"Nombre:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(m_nombre, 1, wxEXPAND);

    std::wstring carpeta = previa.carpeta.empty() ? ajustes.CarpetaEfectiva() : previa.carpeta;
    m_carpeta = new wxDirPickerCtrl(this, wxID_ANY, carpeta, L"Elige dónde guardar", wxDefaultPosition, wxDefaultSize,
                                    wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
    m_carpeta->GetPickerCtrl()->SetLabel(L"Elegir...");   // wx lo pondría en inglés («Browse»)
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
    caja->Add(Nota(this, info, FromDIP(520)), 0, wxLEFT | wxRIGHT, borde);

    auto* botones = new wxBoxSizer(wxHORIZONTAL);
    botones->AddStretchSpacer();
    auto* descargar = new wxButton(this, wxID_OK, L"Descargar");
    descargar->SetDefault();
    botones->Add(descargar, 0, wxRIGHT, FromDIP(8));
    auto* luego = new wxButton(this, wxID_ANY, L"Más tarde");
    luego->SetToolTip(L"La agrega a la lista en pausa");
    botones->Add(luego, 0, wxRIGHT, FromDIP(8));
    botones->Add(new wxButton(this, wxID_CANCEL, L"Cancelar"));
    caja->Add(botones, 0, wxEXPAND | wxALL, borde);

    descargar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(true); });
    luego->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AlAceptar(false); });

    SetSizerAndFit(caja);
    SetMinSize(GetSize());
    CentreOnScreen();
    if (url.empty()) m_url->SetFocus();
    else descargar->SetFocus();
}

void DialogoAgregar::AlAceptar(bool empezar)
{
    std::wstring url = m_url->GetValue().Strip(wxString::both).ToStdWstring();
    if (!sistema::EsDireccion(url))
    {
        ayuda::Aviso(L"Escribe o pega una dirección que empiece por http://, https:// o ftp://", L"Nueva descarga",
                     wxOK | wxICON_WARNING, this);
        m_url->SetFocus();
        return;
    }
    if (!ExisteCarpeta(m_carpeta->GetPath().ToStdWstring()))
    {
        ayuda::Aviso(L"La carpeta elegida no existe.", L"Nueva descarga", wxOK | wxICON_WARNING, this);
        return;
    }
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

// ---------------------------------------------------------------- Ajustes

DialogoAjustes::DialogoAjustes(wxWindow* padre, Ajustes& ajustes)
    : wxDialog(padre, wxID_ANY, L"Ajustes"), m_ajustes(ajustes)
{
    const int borde = FromDIP(12);
    auto* caja = new wxBoxSizer(wxVERTICAL);
    auto* rejilla = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
    rejilla->AddGrowableCol(1, 1);

    auto* carpeta = new wxDirPickerCtrl(this, wxID_ANY, ajustes.CarpetaEfectiva(), L"Elige dónde guardar",
                                        wxDefaultPosition, FromDIP(wxSize(380, -1)), wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
    carpeta->GetPickerCtrl()->SetLabel(L"Elegir...");
    rejilla->Add(Etiqueta(this, L"Guardar en:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(carpeta, 1, wxEXPAND);

    auto* conexiones = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 16, ajustes.conexiones);
    rejilla->Add(Etiqueta(this, L"Conexiones por archivo:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(conexiones);

    auto* simultaneas = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, ajustes.simultaneas);
    rejilla->Add(Etiqueta(this, L"Descargas a la vez:"), 0, wxALIGN_CENTER_VERTICAL);
    rejilla->Add(simultaneas);

    auto* limite = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 1000000, ajustes.limiteKB);
    rejilla->Add(Etiqueta(this, L"Velocidad máxima (KB/s):"), 0, wxALIGN_CENTER_VERTICAL);
    auto* filaLimite = new wxBoxSizer(wxHORIZONTAL);
    filaLimite->Add(limite, 0, wxALIGN_CENTER_VERTICAL);
    filaLimite->Add(Nota(this, L"0 = sin límite", FromDIP(200)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    rejilla->Add(filaLimite);
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
        L"clic derecho en un enlace y elegir «Descargar con Atarraya».");
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
