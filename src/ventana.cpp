#include "ventana.hpp"

#include <wx/accel.h>
#include <wx/app.h>
#include <wx/dataview.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/graphics.h>
#include <wx/hyperlink.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/taskbar.h>
#include <wx/textdlg.h>
#include <wx/toolbar.h>
#include <wx/weakref.h>

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <thread>

#include "ayuda.hpp"
#include "dialogos.hpp"
#include "iconos.hpp"
#include "nombres.hpp"
#include "sistema.hpp"
#include "util.hpp"
#include "version.h"

using namespace hnd;

namespace
{
    bool TemaOscuro() { return wxSystemSettings::GetAppearance().IsDark(); }

    wxColour ColorEstado(Estado e)
    {
        bool oscuro = TemaOscuro();
        switch (e)
        {
        case Estado::Completada: return wxColour(0x1D, 0x9E, 0x75);
        case Estado::Error: return wxColour(0xD6, 0x45, 0x45);
        case Estado::Pausada:
        case Estado::EnCola: return wxColour(0x88, 0x87, 0x80);
        default: return oscuro ? wxColour(0x3F, 0xA9, 0xF5) : wxColour(0x1A, 0x8F, 0xD6);
        }
    }

    wxColour ColorPista() { return TemaOscuro() ? wxColour(0x3A, 0x3F, 0x47) : wxColour(0xE3, 0xE8, 0xEF); }
    wxColour ColorSuave() { return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT); }

    wxString NombreVisible(const DatosDescarga& d)
    {
        if (!d.nombre.empty()) return d.nombre;
        std::wstring n = NombreDeUrl(d.url);
        return n.empty() ? wxString::FromUTF8(d.url) : wxString(n);
    }

    int Permil(const Foto& f)
    {
        if (f.datos.estado == Estado::Completada) return 1000;
        if (f.datos.tamano <= 0) return 1001;   // tamaño desconocido
        return (int)std::min<int64_t>(1000, f.hecho * 1000 / f.datos.tamano);
    }
}

// ---------------------------------------------------------------- Modelo de la lista

class ModeloLista : public wxDataViewVirtualListModel
{
public:
    enum { Nombre, Tamano, Progreso, Velocidad, Falta, EstadoCol };

    std::vector<Foto> filas;

    ModeloLista() : wxDataViewVirtualListModel(0) {}

    void GetValueByRow(wxVariant& v, unsigned int fila, unsigned int col) const override
    {
        if (fila >= filas.size()) { v = wxString(); return; }
        const Foto& f = filas[fila];
        const auto& d = f.datos;
        bool bajando = d.estado == Estado::Descargando;
        switch (col)
        {
        case Nombre: v = NombreVisible(d); break;
        case Tamano:
            v = d.tamano >= 0 ? wxString(TextoTamano(d.tamano)) : (f.hecho > 0 ? wxString(TextoTamano(f.hecho)) : wxString(L"?"));
            break;
        case Progreso: v = (long)((int)d.estado * 10000 + Permil(f)); break;
        case Velocidad: v = bajando ? wxString(TextoVelocidad(f.velocidad)) : wxString(); break;
        case Falta:
            if (bajando && f.velocidad > 1 && d.tamano > 0)
                v = wxString(TextoDuracion((int64_t)((d.tamano - f.hecho) / f.velocidad)));
            else v = wxString();
            break;
        case EstadoCol:
            if (d.estado == Estado::Descargando && f.activas > 0)
                v = wxString::Format(L"Bajando (%d)", f.activas);
            else v = wxString(TextoEstado(d.estado));
            break;
        default: v = wxString();
        }
    }

    bool GetAttrByRow(unsigned int fila, unsigned int col, wxDataViewItemAttr& attr) const override
    {
        if (fila >= filas.size()) return false;
        if (col == EstadoCol && filas[fila].datos.estado == Estado::Error)
        {
            attr.SetColour(ColorEstado(Estado::Error));
            return true;
        }
        if (col == Tamano || col == Velocidad || col == Falta)
        {
            attr.SetColour(ColorSuave());
            return true;
        }
        return false;
    }

    bool SetValueByRow(const wxVariant&, unsigned int, unsigned int) override { return false; }
};

// Barra de progreso fina con el porcentaje al lado
class RenderBarra : public wxDataViewCustomRenderer
{
public:
    RenderBarra() : wxDataViewCustomRenderer(L"long", wxDATAVIEW_CELL_INERT) {}

    bool SetValue(const wxVariant& v) override { m_valor = v.GetLong(); return true; }
    bool GetValue(wxVariant& v) const override { v = m_valor; return true; }
    wxSize GetSize() const override { return GetView()->FromDIP(wxSize(-1, 18)); }

    bool Render(wxRect r, wxDC* dc, int) override
    {
        auto estado = (Estado)(m_valor / 10000);
        int permil = (int)(m_valor % 10000);
        wxWindow* v = GetView();
        wxString texto = permil > 1000 ? wxString(L"—") : wxString::Format(L"%.1f %%", permil / 10.0);
        wxSize t = dc->GetTextExtent(L"100.0 %");
        int alto = v->FromDIP(6);
        int anchoBarra = std::max(10, r.width - t.x - v->FromDIP(14));
        wxRect barra(r.x + v->FromDIP(4), r.y + (r.height - alto) / 2, anchoBarra, alto);

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(*dc));
        if (gc)
        {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(ColorPista()));
            gc->DrawRoundedRectangle(barra.x, barra.y, barra.width, barra.height, alto / 2.0);
            if (permil > 0 && permil <= 1000)
            {
                gc->SetBrush(wxBrush(ColorEstado(estado)));
                double ancho = std::max<double>(alto, barra.width * permil / 1000.0);
                gc->DrawRoundedRectangle(barra.x, barra.y, ancho, barra.height, alto / 2.0);
            }
        }
        dc->SetTextForeground(ColorSuave());
        dc->DrawText(texto, barra.GetRight() + v->FromDIP(8), r.y + (r.height - t.y) / 2);
        return true;
    }

private:
    long m_valor = 0;
};

// ---------------------------------------------------------------- Detalles de la descarga elegida

// Dibuja el archivo entero y, encima, lo que ya bajó cada parte (como un mapa)
class BarraPartes : public wxWindow
{
public:
    explicit BarraPartes(wxWindow* padre)
        : wxWindow(padre, wxID_ANY, wxDefaultPosition, padre->FromDIP(wxSize(-1, 16)), wxFULL_REPAINT_ON_RESIZE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &BarraPartes::AlPintar, this);
    }

    void Fijar(const Foto* f)
    {
        m_hay = f != nullptr;
        if (f) m_foto = *f;
        Refresh();
    }

private:
    void AlPintar(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        if (!m_hay) return;
        wxSize s = GetClientSize();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) return;
        double radio = 3;
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(ColorPista()));
        gc->DrawRoundedRectangle(0, 0, s.x, s.y, radio);

        const auto& d = m_foto.datos;
        wxColour color = ColorEstado(d.estado == Estado::Descargando || d.estado == Estado::Iniciando ? Estado::Descargando : d.estado);
        gc->SetBrush(wxBrush(color));
        if (d.estado == Estado::Completada)
        {
            gc->DrawRoundedRectangle(0, 0, s.x, s.y, radio);
            return;
        }
        if (d.tamano <= 0)
        {
            return;
        }
        for (auto& p : d.segmentos)
        {
            double x0 = (double)p.inicio / d.tamano * s.x;
            double x1 = (double)(p.inicio + p.hecho) / d.tamano * s.x;
            if (x1 - x0 >= 0.5) gc->DrawRectangle(x0, 0, x1 - x0, s.y);
        }
        // Rayitas donde empieza cada parte
        gc->SetPen(wxPen(GetParent()->GetBackgroundColour(), 1));
        for (auto& p : d.segmentos)
        {
            if (p.inicio == 0) continue;
            double x = (double)p.inicio / d.tamano * s.x;
            gc->StrokeLine(x, 0, x, s.y);
        }
    }

    Foto m_foto;
    bool m_hay = false;
};

class PanelDetalles : public wxPanel
{
public:
    explicit PanelDetalles(wxWindow* padre) : wxPanel(padre)
    {
        const int borde = FromDIP(10);
        auto* caja = new wxBoxSizer(wxVERTICAL);
        m_titulo = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        m_titulo->SetFont(m_titulo->GetFont().Bold());
        caja->Add(m_titulo, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, borde);
        m_barra = new BarraPartes(this);
        caja->Add(m_barra, 0, wxEXPAND | wxALL, borde);
        for (auto** linea : { &m_linea1, &m_linea2, &m_linea3, &m_linea4 })
        {
            *linea = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
            caja->Add(*linea, 0, wxEXPAND | wxLEFT | wxRIGHT, borde);
            caja->AddSpacer(FromDIP(4));
        }
        m_linea3->SetForegroundColour(ColorSuave());
        m_linea4->SetForegroundColour(ColorSuave());
        m_error = new wxStaticText(this, wxID_ANY, wxEmptyString);
        m_error->SetForegroundColour(ColorEstado(Estado::Error));
        caja->Add(m_error, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, borde);
        SetSizer(caja);
        Mostrar(nullptr);
    }

    void Mostrar(const Foto* f)
    {
        m_barra->Fijar(f);
        if (!f)
        {
            m_titulo->SetLabel(L"Elige una descarga para ver sus detalles");
            for (auto* l : { m_linea1, m_linea2, m_linea3, m_linea4, m_error }) l->SetLabel(wxEmptyString);
            return;
        }
        const auto& d = f->datos;
        m_titulo->SetLabel(NombreVisible(d));

        wxString bajado = d.tamano > 0
            ? wxString::Format(L"Bajado: %s de %s (%.1f %%)", TextoTamano(f->hecho), TextoTamano(d.tamano), f->hecho * 100.0 / d.tamano)
            : wxString::Format(L"Bajado: %s", TextoTamano(f->hecho));
        if (d.estado == Estado::Descargando && f->velocidad > 1)
        {
            bajado += L"  ·  Velocidad: " + wxString(TextoVelocidad(f->velocidad));
            if (d.tamano > 0) bajado += L"  ·  Falta: " + wxString(TextoDuracion((int64_t)((d.tamano - f->hecho) / f->velocidad)));
        }
        m_linea1->SetLabel(bajado);

        wxString partes = wxString::Format(L"Estado: %s  ·  Conexiones abiertas: %d  ·  Partes: %zu  ·  ", TextoEstado(d.estado),
                                           f->activas, d.segmentos.size());
        if (d.estado == Estado::EnCola || (d.tamano < 0 && d.segmentos.empty())) partes += L"Reanudable: se sabrá al empezar";
        else partes += d.rangos ? L"Reanudable: sí" : L"Reanudable: no (el servidor no lo permite)";
        m_linea2->SetLabel(partes);
        m_linea3->SetLabel(L"Dirección: " + wxString::FromUTF8(d.url));
        m_linea4->SetLabel(L"Carpeta: " + wxString(d.carpeta.empty() ? std::wstring(L"(se decide al empezar)") : d.carpeta));
        m_error->SetLabel(d.estado == Estado::Error ? wxString::FromUTF8(d.error) : wxString());
        m_error->Wrap(std::max(200, GetClientSize().x - FromDIP(20)));
    }

private:
    wxStaticText* m_titulo = nullptr;
    BarraPartes* m_barra = nullptr;
    wxStaticText* m_linea1 = nullptr;
    wxStaticText* m_linea2 = nullptr;
    wxStaticText* m_linea3 = nullptr;
    wxStaticText* m_linea4 = nullptr;
    wxStaticText* m_error = nullptr;
};

// ---------------------------------------------------------------- Ícono junto al reloj

class Bandeja : public wxTaskBarIcon
{
public:
    explicit Bandeja(VentanaPrincipal* v) : m_v(v)
    {
        Bind(wxEVT_TASKBAR_LEFT_DCLICK, [this](wxTaskBarIconEvent&) { m_v->Mostrar(); });
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->Mostrar(); }, ID_MOSTRAR);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->NuevaDescarga({}, false); }, ID_AGREGAR);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->MotorDescargas().PausarTodas(); }, ID_PAUSAR_TODAS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->MotorDescargas().ReanudarTodas(); }, ID_REANUDAR_TODAS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->Salir(); }, wxID_EXIT);
    }

    wxMenu* CreatePopupMenu() override
    {
        auto* m = new wxMenu();
        m->Append(ID_MOSTRAR, L"Mostrar Atarraya");
        m->Append(ID_AGREGAR, L"Agregar enlace...");
        m->AppendSeparator();
        m->Append(ID_PAUSAR_TODAS, L"Pausar todas");
        m->Append(ID_REANUDAR_TODAS, L"Reanudar todas");
        m->AppendSeparator();
        m->Append(wxID_EXIT, L"Salir");
        return m;
    }

private:
    VentanaPrincipal* m_v;
};

// ---------------------------------------------------------------- Ventana

VentanaPrincipal::VentanaPrincipal(hnd::Motor& motor, hnd::Ajustes& ajustes)
    : wxFrame(nullptr, wxID_ANY, L"Atarraya"), m_motor(motor), m_ajustes(ajustes), m_reloj(this)
{
    wxIconBundle iconos(wxString(L"AAAA_APP"), (WXHINSTANCE)GetModuleHandleW(nullptr));
    SetIcons(iconos);

    CrearMenu();
    CrearBarra();

    auto* fondo = new wxPanel(this);
    auto* caja = new wxBoxSizer(wxVERTICAL);

    m_division = new wxSplitterWindow(fondo, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_NOBORDER | wxSP_3DSASH);
    m_division->SetSashGravity(1.0);
    m_division->SetMinimumPaneSize(FromDIP(60));

    m_lista = new wxDataViewCtrl(m_division, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_MULTIPLE | wxDV_ROW_LINES | wxBORDER_NONE);
    m_modelo = new ModeloLista();
    m_lista->AssociateModel(m_modelo);
    m_modelo->DecRef();
    m_lista->SetRowHeight(FromDIP(28));
    auto texto = [&](const wxString& titulo, int col, int ancho, wxAlignment al = wxALIGN_LEFT) {
        m_lista->AppendColumn(new wxDataViewColumn(titulo, new wxDataViewTextRenderer(L"string", wxDATAVIEW_CELL_INERT, al), col,
                                                   FromDIP(ancho), al, wxDATAVIEW_COL_RESIZABLE));
    };
    texto(L"Nombre", ModeloLista::Nombre, 320);
    texto(L"Tamaño", ModeloLista::Tamano, 90, wxALIGN_RIGHT);
    m_lista->AppendColumn(new wxDataViewColumn(L"Progreso", new RenderBarra(), ModeloLista::Progreso, FromDIP(190), wxALIGN_LEFT,
                                               wxDATAVIEW_COL_RESIZABLE));
    texto(L"Velocidad", ModeloLista::Velocidad, 100, wxALIGN_RIGHT);
    texto(L"Falta", ModeloLista::Falta, 100, wxALIGN_RIGHT);
    texto(L"Estado", ModeloLista::EstadoCol, 130);

    m_detalles = new PanelDetalles(m_division);
    int altoDetalles = m_ajustes.altoDetalles > 0 ? m_ajustes.altoDetalles : FromDIP(150);
    m_division->SplitHorizontally(m_lista, m_detalles, -altoDetalles);
    caja->Add(m_division, 1, wxEXPAND);

    // Línea de abajo: resumen a la izquierda; reportar y donar a la derecha
    caja->Add(new wxStaticLine(fondo), 0, wxEXPAND);
    auto* abajo = new wxBoxSizer(wxHORIZONTAL);
    m_resumen = new wxStaticText(fondo, wxID_ANY, wxEmptyString);
    abajo->Add(m_resumen, 1, wxALIGN_CENTER_VERTICAL);
    auto* reportar = new wxHyperlinkCtrl(fondo, wxID_ANY, L"Reportar un problema",
                                         L"https://github.com/" APP_REPO "/issues/new/choose");
    abajo->Add(reportar, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));
    abajo->Add(new wxStaticBitmap(fondo, wxID_ANY, iconos::Crear(iconos::Corazon, L"#D64545", 14)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    auto* donar = new wxHyperlinkCtrl(fondo, wxID_ANY, L"Donar", L"https://paypal.me/BriznaSoft");
    donar->SetToolTip(L"Atarraya es gratis y sin publicidad. Si te sirve, puedes apoyar a Brizna Soft.");
    abajo->Add(donar, 0, wxALIGN_CENTER_VERTICAL);
    caja->Add(abajo, 0, wxEXPAND | wxALL, FromDIP(6));
    fondo->SetSizer(caja);

    m_lista->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &VentanaPrincipal::MenuContextual, this);
    m_lista->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent&) { AbrirArchivo(); });
    m_lista->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent&) { Refrescar(); });

    wxAcceleratorEntry teclas[] = {
        wxAcceleratorEntry(wxACCEL_CTRL, 'N', ID_AGREGAR),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_DELETE, ID_QUITAR),
    };
    SetAcceleratorTable(wxAcceleratorTable(2, teclas));

    // Comandos
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { NuevaDescarga({}, false); }, ID_AGREGAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Reanudar(); }, ID_REANUDAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Pausar(); }, ID_PAUSAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Quitar(); }, ID_QUITAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirArchivo(); }, ID_ABRIR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirCarpeta(); }, ID_CARPETA);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopiarEnlace(); }, ID_COPIAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CambiarEnlace(); }, ID_CAMBIAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CalcularHuella(); }, ID_HUELLA);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_motor.ReanudarTodas(); Refrescar(); }, ID_REANUDAR_TODAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_motor.PausarTodas(); Refrescar(); }, ID_PAUSAR_TODAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_motor.LimpiarTerminadas(); Refrescar(); }, ID_LIMPIAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { DialogoNavegadores d(this); d.ShowModal(); }, ID_NAVEGADORES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { DialogoAjustes d(this, m_ajustes); d.ShowModal(); }, ID_AJUSTES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Salir(); }, wxID_EXIT);
    Bind(wxEVT_CLOSE_WINDOW, &VentanaPrincipal::AlCerrar, this);

    m_motor.alTerminar = [this](const Foto& f) { AlTerminar(f); };

    m_bandeja = std::make_unique<Bandeja>(this);
    m_bandeja->SetIcon(wxBitmapBundle::FromIconBundle(iconos), L"Atarraya");

    // Tamaño y lugar de la última vez
    SetMinSize(FromDIP(wxSize(640, 400)));
    if (m_ajustes.ancho > 200 && m_ajustes.alto > 200)
    {
        wxRect r(m_ajustes.x, m_ajustes.y, m_ajustes.ancho, m_ajustes.alto);
        SetSize(r);
        if (wxDisplay::GetFromRect(r) == wxNOT_FOUND) CentreOnScreen();
    }
    else
    {
        SetSize(FromDIP(wxSize(980, 600)));
        CentreOnScreen();
    }
    if (m_ajustes.maximizada) Maximize();

    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Refrescar(); });
    m_reloj.Start(500);
    Refrescar();
}

VentanaPrincipal::~VentanaPrincipal()
{
    m_motor.alTerminar = nullptr;
}

void VentanaPrincipal::CrearMenu()
{
    auto* barra = new wxMenuBar();

    auto* archivo = new wxMenu();
    archivo->Append(ID_AGREGAR, L"&Agregar enlace...\tCtrl+N");
    archivo->AppendSeparator();
    archivo->Append(wxID_EXIT, L"&Salir");
    barra->Append(archivo, L"&Archivo");

    auto* descargas = new wxMenu();
    descargas->Append(ID_REANUDAR, L"&Reanudar");
    descargas->Append(ID_PAUSAR, L"&Pausar");
    descargas->Append(ID_QUITAR, L"&Quitar...\tSupr");
    descargas->AppendSeparator();
    descargas->Append(ID_ABRIR, L"A&brir el archivo");
    descargas->Append(ID_CARPETA, L"Abrir la &carpeta");
    descargas->Append(ID_COPIAR, L"Copiar el &enlace");
    descargas->Append(ID_CAMBIAR, L"Cambiar el en&lace...");
    descargas->Append(ID_HUELLA, L"Calcular la &huella SHA-256");
    descargas->AppendSeparator();
    descargas->Append(ID_REANUDAR_TODAS, L"Reanudar &todas");
    descargas->Append(ID_PAUSAR_TODAS, L"Pausar t&odas");
    descargas->Append(ID_LIMPIAR, L"Quitar de la lista las &terminadas");
    barra->Append(descargas, L"&Descargas");

    auto* herramientas = new wxMenu();
    herramientas->Append(ID_NAVEGADORES, L"Conectar con los &navegadores...");
    herramientas->Append(ID_AJUSTES, L"&Ajustes...");
    barra->Append(herramientas, L"&Herramientas");

    barra->Append(ayuda::CrearMenu(this, L"Gestor de descargas: baja archivos en varias partes a la vez, se puede pausar y "
                                         L"reanudar (incluso después de apagar la PC) y recibe las descargas de tu navegador."),
                  L"A&yuda");
    SetMenuBar(barra);
}

void VentanaPrincipal::CrearBarra()
{
    auto* barra = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_HORZ_LAYOUT | wxTB_FLAT | wxTB_NODIVIDER);
    wxString color = TemaOscuro() ? L"#C9D1D9" : L"#5F6B7A";
    barra->AddTool(ID_AGREGAR, L"Agregar enlace", iconos::Crear(iconos::Agregar, color), L"Agregar una descarga (Ctrl+N)");
    barra->AddSeparator();
    barra->AddTool(ID_REANUDAR, L"Reanudar", iconos::Crear(iconos::Reanudar, color), L"Reanudar lo elegido");
    barra->AddTool(ID_PAUSAR, L"Pausar", iconos::Crear(iconos::Pausar, color), L"Pausar lo elegido");
    barra->AddTool(ID_QUITAR, L"Quitar", iconos::Crear(iconos::Quitar, color), L"Quitar de la lista (pregunta antes)");
    barra->AddSeparator();
    barra->AddTool(ID_CARPETA, L"Abrir carpeta", iconos::Crear(iconos::Carpeta, color), L"Mostrar el archivo en su carpeta");
    barra->AddStretchableSpace();
    barra->AddTool(ID_NAVEGADORES, L"Navegadores", iconos::Crear(iconos::Navegador, color), L"Conectar con Chrome, Edge, Brave y Firefox");
    barra->AddTool(ID_AJUSTES, L"Ajustes", iconos::Crear(iconos::Ajustes, color), L"Carpeta, conexiones y velocidad");
    barra->Realize();
}

std::vector<uint64_t> VentanaPrincipal::Seleccion() const
{
    std::vector<uint64_t> ids;
    wxDataViewItemArray items;
    m_lista->GetSelections(items);
    for (auto& it : items)
    {
        unsigned int fila = m_modelo->GetRow(it);
        if (fila < m_modelo->filas.size()) ids.push_back(m_modelo->filas[fila].datos.id);
    }
    return ids;
}

uint64_t VentanaPrincipal::PrimeraSeleccion() const
{
    auto ids = Seleccion();
    return ids.empty() ? 0 : ids.front();
}

void VentanaPrincipal::Refrescar()
{
    m_motor.maxSimultaneas = m_ajustes.simultaneas;
    m_motor.limiteBytesSeg = (int64_t)m_ajustes.limiteKB * 1024;
    m_motor.Tick();

    auto fotos = m_motor.Fotos();
    std::reverse(fotos.begin(), fotos.end());   // las nuevas arriba

    bool misma = fotos.size() == m_modelo->filas.size();
    for (size_t i = 0; misma && i < fotos.size(); i++) misma = fotos[i].datos.id == m_modelo->filas[i].datos.id;
    if (!misma)
    {
        auto elegidas = Seleccion();
        m_modelo->filas = std::move(fotos);
        m_modelo->Reset((unsigned int)m_modelo->filas.size());
        wxDataViewItemArray items;
        for (size_t i = 0; i < m_modelo->filas.size(); i++)
        {
            if (std::find(elegidas.begin(), elegidas.end(), m_modelo->filas[i].datos.id) != elegidas.end())
                items.Add(m_modelo->GetItem((unsigned int)i));
        }
        m_lista->SetSelections(items);
    }
    else
    {
        m_modelo->filas = std::move(fotos);
        if (IsShown()) m_lista->Refresh();
    }

    // Detalles, resumen y botones
    uint64_t id = PrimeraSeleccion();
    const Foto* elegida = nullptr;
    double total = 0;
    int bajando = 0, enCola = 0, terminadas = 0;
    for (auto& f : m_modelo->filas)
    {
        if (f.datos.id == id) elegida = &f;
        total += f.velocidad;
        if (f.datos.estado == Estado::Descargando || f.datos.estado == Estado::Iniciando) bajando++;
        if (f.datos.estado == Estado::EnCola) enCola++;
        if (f.datos.estado == Estado::Completada) terminadas++;
    }
    if (IsShown()) m_detalles->Mostrar(elegida);

    wxString resumen;
    if (m_modelo->filas.empty()) resumen = L"Sin descargas. Pulsa «Agregar enlace» o baja algo desde tu navegador.";
    else
    {
        resumen = wxString::Format(L"↓ %s   ·   %d bajando   ·   %d en cola   ·   %d terminadas",
                                   total > 1 ? wxString(TextoVelocidad(total)) : wxString(L"0 B/s"), bajando, enCola, terminadas);
        if (m_ajustes.limiteKB > 0) resumen += wxString::Format(L"   ·   límite %d KB/s", m_ajustes.limiteKB);
    }
    m_resumen->SetLabel(resumen);
    if (m_bandeja)
    {
        wxString punta = bajando > 0 ? wxString::Format(L"Atarraya: %d bajando a %s", bajando, TextoVelocidad(total)) : wxString(L"Atarraya");
        static wxString ultima;
        if (punta != ultima)
        {
            ultima = punta;
            m_bandeja->SetIcon(wxBitmapBundle::FromIconBundle(GetIcons()), punta);
        }
    }

    bool hay = elegida != nullptr;
    bool activa = hay && (elegida->datos.estado == Estado::Descargando || elegida->datos.estado == Estado::Iniciando
                          || elegida->datos.estado == Estado::EnCola);
    bool detenida = hay && (elegida->datos.estado == Estado::Pausada || elegida->datos.estado == Estado::Error);
    if (auto* barra = GetToolBar())
    {
        barra->EnableTool(ID_REANUDAR, detenida || Seleccion().size() > 1);
        barra->EnableTool(ID_PAUSAR, activa || Seleccion().size() > 1);
        barra->EnableTool(ID_QUITAR, hay);
        barra->EnableTool(ID_CARPETA, hay);
    }
}

void VentanaPrincipal::Mostrar()
{
    if (!IsShown()) Show();
    if (IsIconized()) Iconize(false);
    Raise();
    Refrescar();
}

void VentanaPrincipal::NuevaDescarga(const DatosDescarga& previa, bool delNavegador, int64_t tamanoAnunciado)
{
    DialogoAgregar d(IsShown() ? this : nullptr, m_ajustes, previa, delNavegador, tamanoAnunciado);
    d.Raise();
    int respuesta = d.ShowModal();
    Registrar("ventana «Nueva descarga»: " + std::string(respuesta == wxID_OK ? "aceptada" : "cancelada"));
    if (respuesta != wxID_OK) return;
    DatosDescarga datos = d.Datos();
    if (uint64_t otra = m_motor.BuscarPorUrl(datos.url))
    {
        wxMessageDialog pregunta(this, L"Esa dirección ya está en la lista y no ha terminado. ¿Agregarla otra vez?",
                                 L"Nueva descarga", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        pregunta.SetYesNoLabels(L"Agregar otra vez", L"No, usar la que está");
        if (pregunta.ShowModal() != wxID_YES)
        {
            m_motor.Reanudar(otra);
            Mostrar();
            return;
        }
    }
    m_motor.Agregar(datos, d.EmpezarAhora());
    if (delNavegador) Mostrar();
    Refrescar();
}

void VentanaPrincipal::Pedido(const std::string& texto)
{
    nlohmann::json j;
    try { j = nlohmann::json::parse(texto); }
    catch (...) { Registrar("pedido con JSON no válido"); return; }
    std::string tipo = j.value("tipo", "");
    Registrar("pedido: " + tipo + " " + SoloSitio(j.value("url", "")));
    if (tipo == "mostrar") { Mostrar(); return; }
    if (tipo != "descargar") return;

    DatosDescarga d;
    d.url = j.value("url", "");
    if (!sistema::EsDireccion(Ancho(d.url))) { Registrar("pedido rechazado: dirección no válida"); return; }
    d.referer = j.value("referer", "");
    d.agente = j.value("agente", "");
    d.cookies = j.value("cookies", "");
    d.conexiones = m_ajustes.conexiones;
    std::wstring nombre = Ancho(j.value("nombre", ""));
    if (!nombre.empty()) d.nombre = LimpiarNombre(nombre);
    int64_t tamano = j.value("tamano", (int64_t)-1);

    if (m_ajustes.preguntarNavegador) NuevaDescarga(d, true, tamano);
    else
    {
        d.carpeta = m_ajustes.CarpetaEfectiva();
        m_motor.Agregar(d, true);
        Refrescar();
    }
}

void VentanaPrincipal::Reanudar()
{
    for (auto id : Seleccion()) m_motor.Reanudar(id);
    Refrescar();
}

void VentanaPrincipal::Pausar()
{
    for (auto id : Seleccion()) m_motor.Pausar(id);
    Refrescar();
}

void VentanaPrincipal::Quitar()
{
    auto ids = Seleccion();
    if (ids.empty()) return;
    wxString pregunta = ids.size() == 1 ? wxString(L"¿Quitar esta descarga de la lista?")
                                        : wxString::Format(L"¿Quitar estas %zu descargas de la lista?", ids.size());
    pregunta += L"\n\nPuedes dejar el archivo donde está o mandarlo a la Papelera (también lo bajado a medias).";
    wxMessageDialog d(this, pregunta, L"Quitar", wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
    d.SetYesNoCancelLabels(L"Quitar de la lista", L"Quitar y mandar a la Papelera", L"Cancelar");
    int r = d.ShowModal();
    if (r == wxID_CANCEL) return;
    for (auto id : ids)
    {
        Foto f;
        if (!m_motor.Buscar(id, f)) continue;
        std::wstring final = m_motor.RutaFinal(id), parcial = m_motor.RutaParcial(id);
        m_motor.Quitar(id);
        if (r == wxID_NO)
        {
            if (f.datos.estado == Estado::Completada && !f.datos.nombre.empty()) sistema::MandarAPapelera(final);
            if (!parcial.empty()) sistema::MandarAPapelera(parcial);
        }
    }
    Refrescar();
}

void VentanaPrincipal::AbrirArchivo()
{
    Foto f;
    if (!m_motor.Buscar(PrimeraSeleccion(), f)) return;
    if (f.datos.estado != Estado::Completada)
    {
        AbrirCarpeta();
        return;
    }
    std::wstring ruta = m_motor.RutaFinal(f.datos.id);
    if (!ExisteArchivo(ruta))
    {
        wxMessageBox(L"El archivo ya no está en su carpeta (quizá se movió o se borró).", L"Abrir", wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (sistema::EsEjecutable(f.datos.nombre))
    {
        wxMessageDialog d(this, L"Este archivo es un programa. Ábrelo solo si viene de un sitio en el que confías.\n\n¿Abrirlo?",
                          L"Abrir un programa", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        d.SetYesNoLabels(L"Abrir", L"No");
        if (d.ShowModal() != wxID_YES) return;
    }
    sistema::Abrir(ruta);
}

void VentanaPrincipal::AbrirCarpeta()
{
    Foto f;
    if (!m_motor.Buscar(PrimeraSeleccion(), f)) return;
    std::wstring ruta = f.datos.estado == Estado::Completada ? m_motor.RutaFinal(f.datos.id) : m_motor.RutaParcial(f.datos.id);
    if (ruta.empty()) ruta = (f.datos.carpeta.empty() ? m_ajustes.CarpetaEfectiva() : f.datos.carpeta) + L"\\";
    sistema::MostrarEnCarpeta(ruta);
}

void VentanaPrincipal::CopiarEnlace()
{
    Foto f;
    if (m_motor.Buscar(PrimeraSeleccion(), f)) sistema::CopiarTexto(Ancho(f.datos.url));
}

void VentanaPrincipal::CambiarEnlace()
{
    Foto f;
    if (!m_motor.Buscar(PrimeraSeleccion(), f)) return;
    if (f.datos.estado == Estado::Completada) return;
    if (f.datos.estado == Estado::Descargando || f.datos.estado == Estado::Iniciando)
    {
        wxMessageBox(L"Primero pausa la descarga.", L"Cambiar el enlace", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxTextEntryDialog d(this, L"Si el enlace venció, pega uno nuevo del MISMO archivo. Se sigue desde donde quedó.",
                        L"Cambiar el enlace", wxString::FromUTF8(f.datos.url));
    if (auto* b = d.FindWindow(wxID_OK)) b->SetLabel(L"Cambiar");
    if (auto* b = d.FindWindow(wxID_CANCEL)) b->SetLabel(L"Cancelar");
    if (d.ShowModal() != wxID_OK) return;
    std::wstring url = d.GetValue().Strip(wxString::both).ToStdWstring();
    if (!sistema::EsDireccion(url))
    {
        wxMessageBox(L"Esa no es una dirección válida (http, https o ftp).", L"Cambiar el enlace", wxOK | wxICON_WARNING, this);
        return;
    }
    if (m_motor.CambiarEnlace(f.datos.id, Utf8(url))) m_motor.Reanudar(f.datos.id);
    Refrescar();
}

void VentanaPrincipal::CalcularHuella()
{
    Foto f;
    if (!m_motor.Buscar(PrimeraSeleccion(), f) || f.datos.estado != Estado::Completada) return;
    std::wstring ruta = m_motor.RutaFinal(f.datos.id);
    wxString nombre = f.datos.nombre;
    m_resumen->SetLabel(L"Calculando la huella SHA-256 de " + nombre + L"...");
    wxWeakRef<VentanaPrincipal> yo(this);
    std::thread([yo, ruta, nombre]() {
        std::string huella = Sha256Archivo(ruta);
        wxTheApp->CallAfter([yo, huella, nombre]() {
            if (!yo) return;
            if (huella.empty())
            {
                wxMessageBox(L"No se pudo leer el archivo.", L"Huella SHA-256", wxOK | wxICON_WARNING, yo.get());
                return;
            }
            wxMessageDialog d(yo.get(), nombre + L"\n\nSHA-256:\n" + wxString::FromUTF8(huella)
                                  + L"\n\nCompárala con la que publica el sitio de descarga: si es igual, el archivo llegó entero.",
                              L"Huella SHA-256", wxYES_NO | wxICON_INFORMATION);
            d.SetYesNoLabels(L"Copiar", L"Cerrar");
            if (d.ShowModal() == wxID_YES) sistema::CopiarTexto(Ancho(huella));
        });
    }).detach();
}

void VentanaPrincipal::MenuContextual(wxDataViewEvent&)
{
    if (Seleccion().empty()) return;
    wxMenu m;
    m.Append(ID_REANUDAR, L"Reanudar");
    m.Append(ID_PAUSAR, L"Pausar");
    m.AppendSeparator();
    m.Append(ID_ABRIR, L"Abrir el archivo");
    m.Append(ID_CARPETA, L"Abrir la carpeta");
    m.Append(ID_COPIAR, L"Copiar el enlace");
    m.Append(ID_CAMBIAR, L"Cambiar el enlace...");
    m.Append(ID_HUELLA, L"Calcular la huella SHA-256");
    m.AppendSeparator();
    m.Append(ID_QUITAR, L"Quitar...");
    PopupMenu(&m);
}

void VentanaPrincipal::Avisar(const wxString& titulo, const wxString& texto, bool error)
{
    if (m_bandeja && m_bandeja->IsIconInstalled())
        m_bandeja->ShowBalloon(titulo, texto, 6000, error ? wxICON_WARNING : wxICON_INFORMATION);
}

void VentanaPrincipal::AlTerminar(const Foto& f)
{
    wxString nombre = NombreVisible(f.datos);
    if (f.datos.estado == Estado::Completada)
    {
        std::wstring ruta = f.datos.carpeta + L"\\" + f.datos.nombre;
        wxWeakRef<VentanaPrincipal> yo(this);
        sistema::MarcarComoDescargado(ruta, f.datos.url, f.datos.referer, [yo, nombre](bool existe) {
            if (existe) return;
            wxTheApp->CallAfter([yo, nombre]() {
                if (!yo) return;
                wxMessageBox(L"Windows (o tu antivirus) bloqueó o quitó «" + nombre + L"» porque podría ser peligroso.",
                             L"Archivo bloqueado", wxOK | wxICON_WARNING, yo.get());
            });
        });
        if (m_ajustes.avisarAlTerminar) Avisar(L"Descarga terminada", nombre);
    }
    else if (f.datos.estado == Estado::Error && m_ajustes.avisarAlTerminar)
    {
        Avisar(L"La descarga se detuvo", nombre + L"\n" + wxString::FromUTF8(f.datos.error), true);
    }
}

void VentanaPrincipal::GuardarGeometria()
{
    m_ajustes.maximizada = IsMaximized();
    if (!IsMaximized() && !IsIconized())
    {
        wxRect r = GetRect();
        m_ajustes.x = r.x;
        m_ajustes.y = r.y;
        m_ajustes.ancho = r.width;
        m_ajustes.alto = r.height;
    }
    if (m_division && m_division->IsSplit()) m_ajustes.altoDetalles = m_division->GetClientSize().y - m_division->GetSashPosition();
    m_ajustes.Guardar();
}

void VentanaPrincipal::AlCerrar(wxCloseEvent& e)
{
    if (!m_saliendo && m_ajustes.bandejaAlCerrar && e.CanVeto())
    {
        e.Veto();
        GuardarGeometria();
        Hide();
        if (!m_ajustes.avisoBandejaMostrado)
        {
            Avisar(L"Atarraya sigue aquí", L"Las descargas continúan. Para cerrarlo del todo: clic derecho en este ícono → Salir.");
            m_ajustes.avisoBandejaMostrado = true;
            m_ajustes.Guardar();
        }
        return;
    }
    Salir();
}

void VentanaPrincipal::Salir()
{
    if (m_saliendo) return;
    m_saliendo = true;
    m_reloj.Stop();
    if (IsShown()) GuardarGeometria();
    m_motor.alTerminar = nullptr;
    m_motor.Detener();
    if (m_bandeja)
    {
        m_bandeja->RemoveIcon();
        m_bandeja.release()->Destroy();   // wx lo borra cuando termina de atender sus eventos
    }
    Destroy();
}
