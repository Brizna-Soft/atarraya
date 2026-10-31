#include "ventana.hpp"

#include <wx/accel.h>
#include <wx/app.h>
#include <wx/dataview.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/filedlg.h>
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
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <thread>

#include "ayuda.hpp"
#include "dialogos.hpp"
#include "enlaces.hpp"
#include "hilos.hpp"
#include "iconos.hpp"
#include "nombres.hpp"
#include "sistema.hpp"
#include "util.hpp"
#include "ventanarecolector.hpp"
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
        case Estado::Compartiendo: return wxColour(0x7F, 0x77, 0xDD);
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

    bool Activa(const Foto& f)
    {
        return f.datos.estado == Estado::Iniciando || f.datos.estado == Estado::Descargando;
    }

    int Permil(const Foto& f)
    {
        if (f.datos.estado == Estado::Completada || f.datos.estado == Estado::Compartiendo) return 1000;
        if (f.datos.tamano <= 0) return 1001;   // tamaño desconocido
        return (int)std::min<int64_t>(1000, f.hecho * 1000 / f.datos.tamano);
    }

    wxString TextoEstadoFila(const Foto& f)
    {
        const auto& d = f.datos;
        if (f.torrent)
        {
            if (f.eligeArchivos) return L"Elige los archivos";
            switch (d.estado)
            {
            case Estado::Iniciando:
                if (f.buscandoDatos) return wxString::Format(L"Buscando datos (%d pares)", f.activas);
                if (f.revisando) return L"Revisando archivos";
                return L"Conectando...";
            case Estado::Descargando:
                return f.activas > 0 ? wxString::Format(L"Bajando (%d pares)", f.activas) : wxString(L"Buscando pares...");
            case Estado::Compartiendo:
                return wxString::Format(L"Compartiendo (%d pares)", f.activas);
            default:
                return TextoEstado(d.estado);
            }
        }
        if (d.estado == Estado::Descargando && f.activas > 0) return wxString::Format(L"Bajando (%d)", f.activas);
        return TextoEstado(d.estado);
    }

    // Un torrent tendría archivos con el mismo nombre que otros que ya están en la carpeta. Devuelve 0 = cancelar,
    // 1 = usar los que ya están (si son de este mismo torrent), 2 = guardar en una carpeta aparte, 3 = no bajar esos.
    // Sin «aparte» (un torrent que ya bajó algo no se puede mover) la primera opción es no bajar esos archivos.
    int PreguntarChoques(wxWindow* padre, const std::vector<std::wstring>& choques, bool aparte = true)
    {
        std::wstring ejemplo = choques.front().substr(choques.front().find_last_of(L'\\') + 1);
        wxString texto = choques.size() == 1
            ? wxString(L"En la carpeta elegida ya hay un archivo con el mismo nombre que uno del torrent: «") + ejemplo + L"»."
            : wxString::Format(L"En la carpeta elegida ya hay %zu archivos con el mismo nombre que los del torrent, por ejemplo «%s».",
                               choques.size(), ejemplo);
        texto += L"\n\nSi son de este mismo torrent (de una descarga anterior), Atarraya los revisa y sigue desde ahí. "
                 L"Si son otros archivos, se dañarían. ¿Qué hacemos?";
        wxMessageDialog d(padre, texto, L"Archivos con el mismo nombre", wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxICON_WARNING);
        d.SetYesNoCancelLabels(aparte ? L"Guardar en una carpeta aparte" : L"No bajar esos archivos", L"Usar los que ya están", L"Cancelar");
        int r = d.ShowModal();
        return r == wxID_YES ? (aparte ? 2 : 3) : r == wxID_NO ? 1 : 0;
    }

    // Una carpeta nueva dentro de «base», con el nombre del torrent (y un número si ya existe)
    std::wstring CarpetaAparte(const std::wstring& base, const std::wstring& nombre)
    {
        std::wstring n = LimpiarNombre(nombre);
        std::wstring c = base + L"\\" + n;
        for (int i = 1; ExisteArchivo(c) || ExisteCarpeta(c); i++) c = base + L"\\" + n + L" (" + std::to_wstring(i) + L")";
        return SHCreateDirectoryExW(nullptr, c.c_str(), nullptr) == ERROR_SUCCESS ? c : std::wstring();
    }

    wxBitmapBundle IconoFila(bool torrent)
    {
        static wxBitmapBundle archivo, iman;
        static bool oscuro = !TemaOscuro();
        if (oscuro != TemaOscuro() || !archivo.IsOk())
        {
            oscuro = TemaOscuro();
            wxString color = oscuro ? L"#9AA4B2" : L"#6B7686";
            archivo = iconos::Crear(iconos::Descarga, color, 16);
            iman = iconos::Crear(iconos::Torrent, oscuro ? L"#A59FF0" : L"#7F77DD", 16);
        }
        return torrent ? iman : archivo;
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
        case Nombre: v << wxDataViewIconText(NombreVisible(d), IconoFila(f.torrent)); break;
        case Tamano:
            v = d.tamano >= 0 ? wxString(TextoTamano(d.tamano)) : (f.hecho > 0 ? wxString(TextoTamano(f.hecho)) : wxString(L"?"));
            break;
        case Progreso: v = (long)((int)d.estado * 10000 + Permil(f)); break;
        case Velocidad:
            if (bajando) v = wxString(TextoVelocidad(f.velocidad));
            else if (d.estado == Estado::Compartiendo && f.subida >= 1) v = L"↑ " + wxString(TextoVelocidad(f.subida));
            else v = wxString();
            break;
        case Falta:
            if (bajando && f.velocidad > 1 && d.tamano > 0)
                v = wxString(TextoDuracion((int64_t)((d.tamano - f.hecho) / f.velocidad)));
            else v = wxString();
            break;
        case EstadoCol: v = TextoEstadoFila(f); break;
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

// Dibuja el archivo entero y, encima, lo que ya bajó cada parte (como un mapa). En un torrent, cada tramo de piezas.
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
        if (d.estado == Estado::Completada || d.estado == Estado::Compartiendo)
        {
            gc->DrawRoundedRectangle(0, 0, s.x, s.y, radio);
            return;
        }
        if (m_foto.torrent)
        {
            // Cada tramo se pinta más o menos lleno según lo que ya se tiene
            const auto& mapa = m_foto.mapa;
            if (mapa.empty()) return;
            double ancho = (double)s.x / mapa.size();
            for (size_t i = 0; i < mapa.size(); i++)
            {
                if (mapa[i] == 0) continue;
                wxColour c(color.Red(), color.Green(), color.Blue(), (unsigned char)std::max(60, (int)mapa[i]));
                gc->SetBrush(wxBrush(c));
                gc->DrawRectangle(i * ancho, 0, ancho + 0.5, s.y);
            }
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
        if (f->torrent && f->subida >= 1) bajado += L"  ·  Subida: " + wxString(TextoVelocidad(f->subida));
        m_linea1->SetLabel(bajado);

        wxString partes;
        if (f->torrent)
        {
            partes = wxString::Format(L"Estado: %s  ·  Pares: %d (con todo: %d)  ·  Subido a otras personas: %s  ·  ",
                                      TextoEstadoFila(*f), f->activas, f->semillas, TextoTamano(f->subido));
            if (f->tieneDatos) partes += wxString::Format(L"Archivos: %d  ·  ", f->archivos);
            partes += f->compartir ? L"Al terminar: sigue compartiendo" : L"Al terminar: se detiene";
        }
        else
        {
            partes = wxString::Format(L"Estado: %s  ·  Conexiones abiertas: %d  ·  Partes: %zu  ·  ", TextoEstado(d.estado), f->activas,
                                      d.segmentos.size());
            if (d.estado == Estado::EnCola || (d.tamano < 0 && d.segmentos.empty())) partes += L"Reanudable: se sabrá al empezar";
            else partes += d.rangos ? L"Reanudable: sí" : L"Reanudable: no (el servidor no lo permite)";
        }
        m_linea2->SetLabel(partes);
        m_linea3->SetLabel((f->torrent ? L"Enlace magnet: " : L"Dirección: ") + wxString::FromUTF8(d.url));
        m_linea4->SetLabel(L"Carpeta: " + wxString(d.carpeta.empty() ? std::wstring(L"(se decide al empezar)") : d.carpeta));
        m_error->SetLabel(d.estado == Estado::Error ? wxString::FromUTF8(d.error) : wxString());
        m_error->Wrap(-1);   // wx no vuelve a partir el texto si se le pide el mismo ancho que la vez anterior
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
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->AbrirRecolector(false); }, ID_VARIOS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->MotorDescargas().PausarTodas(); m_v->MotorTorrents().PausarTodas(); }, ID_PAUSAR_TODAS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->MotorDescargas().ReanudarTodas(); m_v->MotorTorrents().ReanudarTodas(); }, ID_REANUDAR_TODAS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_v->Salir(); }, wxID_EXIT);
    }

    wxMenu* CreatePopupMenu() override
    {
        auto* m = new wxMenu();
        m->Append(ID_MOSTRAR, L"Mostrar Atarraya");
        m->Append(ID_AGREGAR, L"Agregar enlace...");
        m->Append(ID_VARIOS, L"Recolector de enlaces...");
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

VentanaPrincipal::VentanaPrincipal(hnd::Motor& motor, hnd::MotorTorrent& torrents, hnd::Recolector& recolector, hnd::Ajustes& ajustes)
    : wxFrame(nullptr, wxID_ANY, L"Atarraya"), m_motor(motor), m_torrents(torrents), m_recolector(recolector), m_ajustes(ajustes),
      m_cnl(std::make_unique<hnd::ServidorCnL>()), m_reloj(this)
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
        // Centrado en el alto de la fila, igual que el nombre con su ícono
        m_lista->AppendColumn(new wxDataViewColumn(titulo, new wxDataViewTextRenderer(L"string", wxDATAVIEW_CELL_INERT, al | wxALIGN_CENTER_VERTICAL),
                                                   col, FromDIP(ancho), al, wxDATAVIEW_COL_RESIZABLE));
    };
    m_lista->AppendColumn(new wxDataViewColumn(L"Nombre", new wxDataViewIconTextRenderer(), ModeloLista::Nombre, FromDIP(320),
                                               wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE));
    texto(L"Tamaño", ModeloLista::Tamano, 90, wxALIGN_RIGHT);
    m_lista->AppendColumn(new wxDataViewColumn(L"Progreso", new RenderBarra(), ModeloLista::Progreso, FromDIP(190), wxALIGN_LEFT,
                                               wxDATAVIEW_COL_RESIZABLE));
    texto(L"Velocidad", ModeloLista::Velocidad, 100, wxALIGN_RIGHT);
    texto(L"Falta", ModeloLista::Falta, 100, wxALIGN_RIGHT);
    texto(L"Estado", ModeloLista::EstadoCol, 160);

    m_detalles = new PanelDetalles(m_division);
    int altoDetalles = m_ajustes.altoDetalles > 0 ? m_ajustes.altoDetalles : FromDIP(150);
    m_division->SplitHorizontally(m_lista, m_detalles, -altoDetalles);
    caja->Add(m_division, 1, wxEXPAND);

    // Línea de abajo: resumen a la izquierda; reportar y donar a la derecha
    caja->Add(new wxStaticLine(fondo), 0, wxEXPAND);
    auto* abajo = new wxBoxSizer(wxHORIZONTAL);
    m_resumen = new wxStaticText(fondo, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
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
        wxAcceleratorEntry(wxACCEL_CTRL, 'L', ID_VARIOS),
        wxAcceleratorEntry(wxACCEL_CTRL, 'P', ID_PAGINA),
        wxAcceleratorEntry(wxACCEL_CTRL, 'O', ID_TORRENT),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_DELETE, ID_QUITAR),
    };
    SetAcceleratorTable(wxAcceleratorTable(5, teclas));

    // Comandos
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { NuevaDescarga({}, false); }, ID_AGREGAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirRecolector(false); }, ID_VARIOS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirPagina(L"", false); }, ID_PAGINA);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirTorrent(); }, ID_TORRENT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Reanudar(); }, ID_REANUDAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Pausar(); }, ID_PAUSAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Quitar(); }, ID_QUITAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirArchivo(); }, ID_ABRIR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirCarpeta(); }, ID_CARPETA);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopiarEnlace(); }, ID_COPIAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CambiarEnlace(); }, ID_CAMBIAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CalcularHuella(); }, ID_HUELLA);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ElegirArchivos(PrimeraSeleccion()); }, ID_ELEGIR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CambiarCompartir(); }, ID_COMPARTIR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_motor.ReanudarTodas(); m_torrents.ReanudarTodas(); Refrescar(); }, ID_REANUDAR_TODAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_motor.PausarTodas(); m_torrents.PausarTodas(); Refrescar(); }, ID_PAUSAR_TODAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_motor.LimpiarTerminadas(); m_torrents.LimpiarTerminadas(); Refrescar(); }, ID_LIMPIAR);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { DialogoNavegadores d(this); d.ShowModal(); }, ID_NAVEGADORES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        DialogoAjustes d(this, m_ajustes);
        if (d.ShowModal() == wxID_OK) AplicarAjustes();
    }, ID_AJUSTES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Salir(); }, wxID_EXIT);
    Bind(wxEVT_CLOSE_WINDOW, &VentanaPrincipal::AlCerrar, this);

    // Las opciones de torrents solo se pueden usar con un torrent elegido
    auto soloTorrent = [this](wxUpdateUIEvent& e) {
        hnd::Foto f;
        bool hay = BuscarFoto(PrimeraSeleccion(), f) && f.torrent;
        e.Enable(hay && (e.GetId() != ID_ELEGIR || f.tieneDatos));
        if (e.GetId() == ID_COMPARTIR) e.Check(hay && f.compartir);
    };
    Bind(wxEVT_UPDATE_UI, soloTorrent, ID_ELEGIR);
    Bind(wxEVT_UPDATE_UI, soloTorrent, ID_COMPARTIR);

    m_motor.alTerminar = [this](const hnd::Foto& f) { AlTerminar(f); };
    m_torrents.alTerminar = [this](const hnd::Foto& f, const std::vector<std::wstring>& archivos) { AlTerminarTorrent(f, archivos); };

    m_bandeja = std::make_unique<Bandeja>(this);
    m_bandeja->SetIcon(wxBitmapBundle::FromIconBundle(iconos), L"Atarraya");

    // Tamaño y lugar de la última vez
    SetMinSize(FromDIP(wxSize(760, 420)));
    if (m_ajustes.ancho > 200 && m_ajustes.alto > 200)
    {
        wxRect r(m_ajustes.x, m_ajustes.y, m_ajustes.ancho, m_ajustes.alto);
        SetSize(r);
        if (wxDisplay::GetFromRect(r) == wxNOT_FOUND) CentreOnScreen();
    }
    else
    {
        SetSize(FromDIP(wxSize(1060, 620)));
        CentreOnScreen();
    }
    if (m_ajustes.maximizada) Maximize();

    // Portapapeles y Click'n'Load. Lo que ya está copiado al abrir el programa no se recolecta: solo lo que se copie después.
    m_ultimoPortapapeles = std::hash<std::wstring>{}(sistema::TodoElPortapapeles(256 * 1024));
    AplicarAjustes();

    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Refrescar(); });
    m_reloj.Start(500);
    Refrescar();
}

VentanaPrincipal::~VentanaPrincipal()
{
    *m_vivo = false;   // lo que llegue tarde de otros hilos ya no toca esta ventana
    m_motor.alTerminar = nullptr;
    m_torrents.alTerminar = nullptr;
}

void VentanaPrincipal::CrearMenu()
{
    auto* barra = new wxMenuBar();

    auto* archivo = new wxMenu();
    archivo->Append(ID_AGREGAR, L"&Agregar enlace...\tCtrl+N");
    archivo->Append(ID_VARIOS, L"&Recolector de enlaces (pegar muchos, revisar y elegir)...\tCtrl+L");
    archivo->Append(ID_PAGINA, L"Enlaces de una &página...\tCtrl+P");
    archivo->Append(ID_TORRENT, L"Abrir un archivo .&torrent...\tCtrl+O");
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
    descargas->Append(ID_ELEGIR, L"Elegir los archivos del torrent...");
    descargas->AppendCheckItem(ID_COMPARTIR, L"Seguir compartiendo el torrent al terminar");
    descargas->AppendSeparator();
    descargas->Append(ID_REANUDAR_TODAS, L"Reanudar &todas");
    descargas->Append(ID_PAUSAR_TODAS, L"Pausar t&odas");
    descargas->Append(ID_LIMPIAR, L"Quitar de la lista las &terminadas");
    barra->Append(descargas, L"&Descargas");

    auto* herramientas = new wxMenu();
    herramientas->Append(ID_NAVEGADORES, L"Conectar con los &navegadores...");
    herramientas->Append(ID_AJUSTES, L"&Ajustes...");
    barra->Append(herramientas, L"&Herramientas");

    barra->Append(ayuda::CrearMenu(this, L"Descargas, torrents y enlaces magnet en un solo programa: baja archivos en varias partes "
                                         L"a la vez, listas de enlaces, los archivos de una página y videos de sitios que lo permiten. "
                                         L"Se puede pausar y reanudar, incluso después de apagar la PC, y recibe las descargas de tu "
                                         L"navegador."),
                  L"A&yuda");
    SetMenuBar(barra);
}

void VentanaPrincipal::CrearBarra()
{
    auto* barra = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_HORZ_LAYOUT | wxTB_FLAT | wxTB_NODIVIDER);
    wxString color = TemaOscuro() ? L"#C9D1D9" : L"#5F6B7A";
    barra->AddTool(ID_AGREGAR, L"Agregar enlace", iconos::Crear(iconos::Agregar, color), L"Agregar una descarga o un enlace magnet (Ctrl+N)");
    barra->AddTool(ID_VARIOS, L"Recolector", iconos::Crear(iconos::Varios, color),
                   L"Recolector de enlaces: pega muchos de una vez, mira su nombre y tamaño y elige cuáles bajar (Ctrl+L)");
    barra->AddTool(ID_PAGINA, L"De una página", iconos::Crear(iconos::Pagina, color),
                   L"Ver los archivos que enlaza una página o un elemento de Internet Archive (Ctrl+P)");
    barra->AddTool(ID_TORRENT, L"Torrent", iconos::Crear(iconos::Torrent, color), L"Abrir un archivo .torrent (Ctrl+O)");
    barra->AddSeparator();
    barra->AddTool(ID_REANUDAR, L"Reanudar", iconos::Crear(iconos::Reanudar, color), L"Reanudar lo elegido");
    barra->AddTool(ID_PAUSAR, L"Pausar", iconos::Crear(iconos::Pausar, color), L"Pausar lo elegido");
    barra->AddTool(ID_QUITAR, L"Quitar", iconos::Crear(iconos::Quitar, color), L"Quitar de la lista (pregunta antes)");
    barra->AddSeparator();
    barra->AddTool(ID_CARPETA, L"Carpeta", iconos::Crear(iconos::Carpeta, color), L"Mostrar el archivo en su carpeta");
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

bool VentanaPrincipal::BuscarFoto(uint64_t id, hnd::Foto& f)
{
    if (id == 0) return false;
    return EsIdTorrent(id) ? m_torrents.Buscar(id, f) : m_motor.Buscar(id, f);
}

void VentanaPrincipal::Refrescar()
{
    // 1. Estados al día: lo que terminó desde la última vez ya no ocupa lugar
    m_motor.Recoger();
    m_torrents.Recoger();
    auto actuales = m_motor.Fotos();
    for (auto& f : m_torrents.Fotos()) actuales.push_back(std::move(f));

    // 2. Cola compartida: «Descargas a la vez» cuenta las descargas normales y los torrents que bajan (los que solo
    //    comparten no ocupan lugar). Las que esperan arrancan en el orden en que se agregaron, sean del tipo que sean.
    int activasHttp = 0, activosTorrent = 0;
    double velHttp = 0, velTorrent = 0;
    std::vector<std::pair<int64_t, uint64_t>> cola;
    for (auto& f : actuales)
    {
        if (Activa(f)) (f.torrent ? activosTorrent : activasHttp)++;
        (f.torrent ? velTorrent : velHttp) += f.velocidad;
        if (f.datos.estado == Estado::EnCola && !f.eligeArchivos) cola.push_back({ f.datos.agregada, f.datos.id });
    }
    std::sort(cola.begin(), cola.end());
    int libres = std::max(0, m_ajustes.simultaneas - activasHttp - activosTorrent);
    int nuevasHttp = 0, nuevosTorrent = 0;
    // Se autorizan exactamente las que siguen en la cola (no una cantidad por tipo): así se respeta el orden
    m_motor.conPermisos = true;
    m_torrents.conPermisos = true;
    m_motor.permitidas.clear();
    m_torrents.permitidos.clear();
    for (auto& [cuando, id] : cola)
    {
        if (libres <= 0) break;
        if (EsIdTorrent(id))
        {
            nuevosTorrent++;
            m_torrents.permitidos.push_back(id);
        }
        else
        {
            nuevasHttp++;
            m_motor.permitidas.push_back(id);
        }
        libres--;
    }
    m_motor.maxSimultaneas = activasHttp + nuevasHttp;
    m_torrents.maxSimultaneas = activosTorrent + nuevosTorrent;

    // 3. Límite de bajada en total. Si bajan las dos clases a la vez, se reparte un solo presupuesto: la mitad para
    //    cada una, y lo que una no usa pasa a la otra. La suma nunca pasa del límite.
    int64_t limite = (int64_t)m_ajustes.limiteKB * 1024;
    int64_t limHttp = limite, limTorrent = limite;
    if (limite > 0 && (activasHttp + nuevasHttp) > 0 && (activosTorrent + nuevosTorrent) > 0)
    {
        int64_t mitad = limite / 2, piso = std::max<int64_t>(1, limite / 10);
        limHttp = mitad;
        if (velHttp < mitad * 0.8) limHttp = std::max<int64_t>(piso, (int64_t)(velHttp * 1.25));
        else if (velTorrent < mitad * 0.8) limHttp = limite - std::max<int64_t>(piso, (int64_t)(velTorrent * 1.25));
        limHttp = std::clamp<int64_t>(limHttp, piso, limite - piso);
        limTorrent = limite - limHttp;
    }
    m_motor.limiteBytesSeg = limHttp;
    m_torrents.limiteBajada = limTorrent;
    m_torrents.limiteSubida = (int64_t)m_ajustes.limiteSubidaKB * 1024;
    m_motor.Tick();
    m_torrents.Tick();
    m_recolector.Tick();

    // El botón del recolector dice cuántos enlaces esperan
    size_t enRecolector = m_recolector.Cantidad();
    if (enRecolector != m_cuentaRecolector)
    {
        m_cuentaRecolector = enRecolector;
        if (auto* barra = GetToolBar())
        {
            if (auto* boton = barra->FindById(ID_VARIOS))
            {
                boton->SetLabel(enRecolector ? wxString::Format(L"Recolector (%zu)", enRecolector) : wxString(L"Recolector"));
                barra->Realize();
            }
        }
    }
    if (m_ventanaRecolector) m_ventanaRecolector->Refrescar();

    auto fotos = m_motor.Fotos();
    auto deTorrents = m_torrents.Fotos();
    fotos.insert(fotos.end(), std::make_move_iterator(deTorrents.begin()), std::make_move_iterator(deTorrents.end()));
    // Las nuevas arriba
    std::stable_sort(fotos.begin(), fotos.end(), [](const hnd::Foto& a, const hnd::Foto& b) {
        if (a.datos.agregada != b.datos.agregada) return a.datos.agregada > b.datos.agregada;
        return a.datos.id > b.datos.id;
    });

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
    const hnd::Foto* elegida = nullptr;
    double total = 0, subida = 0;
    int bajando = 0, enCola = 0, terminadas = 0, compartiendo = 0;
    for (auto& f : m_modelo->filas)
    {
        if (f.datos.id == id) elegida = &f;
        total += f.velocidad;
        subida += f.subida;
        if (Activa(f)) bajando++;
        if (f.datos.estado == Estado::EnCola) enCola++;
        if (f.datos.estado == Estado::Completada) terminadas++;
        if (f.datos.estado == Estado::Compartiendo) compartiendo++;
    }
    if (IsShown()) m_detalles->Mostrar(elegida);

    wxString resumen;
    if (m_modelo->filas.empty()) resumen = L"Sin descargas. Pulsa «Agregar enlace», abre un torrent o baja algo desde tu navegador.";
    else
    {
        resumen = wxString::Format(L"↓ %s   ·   %d bajando   ·   %d en cola   ·   %d terminadas",
                                   total > 1 ? wxString(TextoVelocidad(total)) : wxString(L"0 B/s"), bajando, enCola, terminadas);
        if (compartiendo > 0 || subida >= 1)
            resumen += wxString::Format(L"   ·   ↑ %s, %d compartiendo", subida >= 1 ? wxString(TextoVelocidad(subida)) : wxString(L"0 B/s"),
                                        compartiendo);
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
    bool activa = hay && (Activa(*elegida) || elegida->datos.estado == Estado::EnCola || elegida->datos.estado == Estado::Compartiendo);
    bool detenida = hay && (elegida->datos.estado == Estado::Pausada || elegida->datos.estado == Estado::Error
                            || (elegida->torrent && elegida->datos.estado == Estado::Completada));
    if (auto* barra = GetToolBar())
    {
        barra->EnableTool(ID_REANUDAR, detenida || Seleccion().size() > 1);
        barra->EnableTool(ID_PAUSAR, activa || Seleccion().size() > 1);
        barra->EnableTool(ID_QUITAR, hay);
        barra->EnableTool(ID_CARPETA, hay);
    }

    // Avisos de los torrents (por ejemplo, archivos que no se pudieron mandar a la Papelera)
    for (auto& a : m_torrents.Avisos()) Avisar(L"Torrent", a, true);

    // Torrents cuyos datos llegaron y esperan que la persona elija qué bajar
    for (auto t : m_torrents.PorElegir()) m_porElegir.push_back(t);
    if (!m_porElegir.empty() && !m_ocupada && !m_saliendo)
    {
        if (IsShown() && !IsIconized())
        {
            uint64_t t = m_porElegir.front();
            m_porElegir.erase(m_porElegir.begin());
            m_ocupada = true;
            CallAfter([this, t]() {
                ElegirArchivos(t);
                m_ocupada = false;
            });
        }
        else
        {
            for (auto t : m_porElegir)
            {
                if (std::find(m_avisadosElegir.begin(), m_avisadosElegir.end(), t) != m_avisadosElegir.end()) continue;
                m_avisadosElegir.push_back(t);
                hnd::Foto f;
                if (m_torrents.Buscar(t, f))
                    Avisar(L"Elige los archivos", L"Ya llegaron los datos de «" + NombreVisible(f.datos) + L"». Abre Atarraya para elegir qué bajar.");
            }
        }
    }
}

void VentanaPrincipal::Mostrar()
{
    if (!IsShown()) Show();
    if (IsIconized()) Iconize(false);
    Raise();
    Refrescar();
}

uint64_t VentanaPrincipal::AgregarEnlace(const std::string& url, const std::wstring& carpeta, const std::string& referer, bool empezar,
                                         bool& yaEstaba)
{
    yaEstaba = false;
    if (m_motor.BuscarPorUrl(url))
    {
        yaEstaba = true;
        return 0;
    }
    DatosDescarga d;
    d.url = url;
    d.referer = referer;
    d.carpeta = carpeta;
    d.conexiones = m_ajustes.conexiones;
    return m_motor.Agregar(d, empezar);
}

void VentanaPrincipal::NuevaDescarga(const DatosDescarga& previa, bool delNavegador, int64_t tamanoAnunciado)
{
    DialogoAgregar d(IsShown() ? this : nullptr, m_ajustes, previa, delNavegador, tamanoAnunciado);
    d.Raise();
    int respuesta = d.ShowModal();
    Registrar("ventana «Nueva descarga»: " + std::string(respuesta == wxID_OK ? "aceptada" : "cancelada"));
    if (respuesta != wxID_OK) return;
    DatosDescarga datos = d.Datos();
    // Un magnet o una página de Internet Archive tienen su propia ventana
    if (sistema::EsMagnet(Ancho(datos.url)))
    {
        AgregarMagnet(datos.url, delNavegador, datos.carpeta);
        return;
    }
    std::string ia;
    if (enlaces::EsInternetArchive(datos.url, ia))
    {
        AbrirPagina(Ancho(datos.url), true);
        return;
    }
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

void VentanaPrincipal::AbrirRecolector(bool sinFoco)
{
    if (!m_ventanaRecolector)
    {
        m_ventanaRecolector = new VentanaRecolector(this, m_recolector, m_ajustes,
            [this](const std::vector<EnlaceRecolectado>& lista, const std::wstring& carpeta, bool porPaquete, bool empezar) {
                return ConfirmarRecolector(lista, carpeta, porPaquete, empezar);
            });
        m_ventanaRecolector->alCambiarAjustes = [this]() { AplicarAjustes(); };
        m_ventanaRecolector->EstadoClickNLoad(m_estadoCnl);
    }
    if (sinFoco) m_ventanaRecolector->MostrarSinFoco();
    else
    {
        if (m_ventanaRecolector->IsIconized()) m_ventanaRecolector->Iconize(false);
        m_ventanaRecolector->Show();
        m_ventanaRecolector->Raise();
        m_ventanaRecolector->Refrescar();
    }
}

void VentanaPrincipal::AplicarAjustes()
{
    // Portapapeles: Windows avisa con WM_CLIPBOARDUPDATE cada vez que alguien copia algo
    HWND h = (HWND)GetHWND();
    if (m_ajustes.vigilarPortapapeles && !m_oyendoPortapapeles) m_oyendoPortapapeles = AddClipboardFormatListener(h) != FALSE;
    else if (!m_ajustes.vigilarPortapapeles && m_oyendoPortapapeles)
    {
        RemoveClipboardFormatListener(h);
        m_oyendoPortapapeles = false;
    }

    // Click'n'Load: solo en 127.0.0.1
    if (m_ajustes.clickNLoad && !m_cnl->Activo())
    {
        VentanaPrincipal* yo = this;
        hilos::Vivo vivo = m_vivo;
        std::string error;
        bool ok = m_cnl->Iniciar([yo, vivo](std::vector<std::string> lista, std::wstring paquete, std::string origen) {
            hilos::Enviar([yo, vivo, lista, paquete, origen]() {
                if (*vivo) yo->AlRecibirCnL(lista, paquete, origen);
            });
        }, error);
        m_estadoCnl = ok ? wxString(L"Click'n'Load: listo en 127.0.0.1:9666 (solo desde esta PC). Lo que llegue espera aquí.")
                         : L"Click'n'Load no se pudo encender: " + wxString::FromUTF8(error);
    }
    else if (!m_ajustes.clickNLoad && m_cnl->Activo())
    {
        m_cnl->Detener();
    }
    if (!m_ajustes.clickNLoad) m_estadoCnl = L"Click'n'Load: apagado (se enciende en Ajustes).";
    if (m_ventanaRecolector) m_ventanaRecolector->EstadoClickNLoad(m_estadoCnl);
}

WXLRESULT VentanaPrincipal::MSWWindowProc(WXUINT mensaje, WXWPARAM w, WXLPARAM l)
{
    if (mensaje == WM_CLIPBOARDUPDATE)
    {
        if (!m_saliendo) CallAfter([this]() { PortapapelesCambio(); });
        return 0;
    }
    return wxFrame::MSWWindowProc(mensaje, w, l);
}

void VentanaPrincipal::PortapapelesCambio()
{
    if (!m_ajustes.vigilarPortapapeles || m_saliendo) return;
    if (sistema::PortapapelesPrivado()) return;   // una contraseña o algo que pidió no ser leído
    std::wstring texto = sistema::TodoElPortapapeles(256 * 1024);
    size_t huella = std::hash<std::wstring>{}(texto);
    if (texto.empty() || huella == m_ultimoPortapapeles) return;
    m_ultimoPortapapeles = huella;
    if (texto == m_copiadoPorAtarraya) return;
    std::vector<std::string> utiles;
    for (const auto& u : enlaces::DeTexto(texto).enlaces)
    {
        if (!EnlaceParaRecolectar(u) || m_motor.BuscarPorUrl(u)) continue;
        if (enlaces::EsMagnet(u))
        {
            InfoTorrent info;
            std::string error;
            if (MotorTorrent::LeerMagnet(u, info, error) && m_torrents.BuscarPorHuella(info.huella)) continue;
        }
        utiles.push_back(u);
    }
    if (utiles.empty()) return;
    size_t n = m_recolector.Agregar(utiles, L"", "", "portapapeles");
    if (n == 0) return;
    Registrar("portapapeles: " + std::to_string(n) + " enlaces al recolector");
    Avisar(L"Recolector de enlaces", n == 1 ? wxString(L"Copiaste un enlace: está en el recolector. Ábrelo para revisarlo y bajarlo.")
                                            : wxString::Format(L"Copiaste %zu enlaces: están en el recolector. Ábrelo para revisarlos y elegir.", n));
}

void VentanaPrincipal::AlRecibirCnL(std::vector<std::string> lista, std::wstring paquete, std::string origen)
{
    if (m_saliendo) return;
    size_t n = m_recolector.Agregar(lista, paquete, origen, "clicknload");
    if (n == 0) return;
    AbrirRecolector(true);
    std::string sitio = enlaces::Sitio(origen);
    Avisar(L"Click'n'Load", wxString::Format(L"Llegaron %zu enlaces%s. Están en el recolector: revisa y elige cuáles bajar.", n,
                                             sitio.empty() ? wxString() : L" de " + wxString::FromUTF8(sitio)));
}

std::vector<uint64_t> VentanaPrincipal::ConfirmarRecolector(const std::vector<EnlaceRecolectado>& lista, const std::wstring& carpeta,
                                                            bool porPaquete, bool empezar)
{
    std::vector<uint64_t> hechos;
    std::vector<const EnlaceRecolectado*> paginas;
    int descargas = 0, torrents = 0, yaEstaban = 0, noEstan = 0, fallaron = 0;
    for (const auto& e : lista)
    {
        if (e.estado == Comprobacion::NoExiste || e.estado == Comprobacion::NoPermitido) { noEstan++; continue; }   // se quedan en el recolector
        if (e.estado == Comprobacion::Pagina || e.estado == Comprobacion::Sitio)
        {
            paginas.push_back(&e);
            continue;
        }
        std::wstring destino = carpeta;
        if (porPaquete)
        {
            destino = carpeta + L"\\" + LimpiarNombre(PaqueteDe(e));
            SHCreateDirectoryExW(nullptr, destino.c_str(), nullptr);
        }
        if (enlaces::EsMagnet(e.url))
        {
            InfoTorrent info;
            std::string error;
            if (!MotorTorrent::LeerMagnet(e.url, info, error)) { fallaron++; continue; }
            if (m_torrents.BuscarPorHuella(info.huella)) { yaEstaban++; hechos.push_back(e.id); continue; }
            NuevoTorrent n;
            n.magnet = e.url;
            n.carpeta = destino;
            n.compartir = m_ajustes.compartirTorrents;
            n.elegirDespues = m_ajustes.elegirArchivosMagnet;
            n.empezar = empezar;
            if (m_torrents.Agregar(n, error)) { torrents++; hechos.push_back(e.id); }
            else fallaron++;
            continue;
        }
        if (m_motor.BuscarPorUrl(e.url)) { yaEstaban++; hechos.push_back(e.id); continue; }
        DatosDescarga d;
        d.url = e.url;
        d.referer = e.referer;
        d.carpeta = destino;
        d.conexiones = m_ajustes.conexiones;
        if (e.estado == Comprobacion::Archivo && !e.nombre.empty()) d.nombre = e.nombre;
        m_motor.Agregar(d, empezar);
        descargas++;
        hechos.push_back(e.id);
    }
    wxWindow* padre = m_ventanaRecolector && m_ventanaRecolector->IsShown() ? (wxWindow*)m_ventanaRecolector : (wxWindow*)this;
    if (!paginas.empty())
    {
        const size_t tope = 10;
        wxString texto = wxString::Format(L"%zu de los enlaces marcados son páginas web, por ejemplo de sitios de descargas.\n\n"
                                          L"Atarraya no se salta las esperas ni los límites de esos sitios, y los captchas los resuelves tú: "
                                          L"se abren en tu navegador para que bajes desde ahí. Si tienes la extensión de Atarraya, la "
                                          L"descarga llega aquí sola.\n\n¿Abrirlos en el navegador?", paginas.size());
        if (paginas.size() > tope) texto += wxString::Format(L"\n\nSe abren los primeros %zu; los demás se quedan en el recolector.", tope);
        wxMessageDialog p(padre, texto, L"Páginas para el navegador", wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION);
        p.SetYesNoLabels(L"Abrir en el navegador", L"Ahora no");
        if (p.ShowModal() == wxID_YES)
        {
            for (size_t i = 0; i < paginas.size() && i < tope; i++)
            {
                ayuda::AbrirEnlace(wxString::FromUTF8(paginas[i]->url));
                hechos.push_back(paginas[i]->id);
            }
        }
    }
    Registrar("recolector: " + std::to_string(descargas) + " descargas y " + std::to_string(torrents) + " torrents a la lista");
    if (noEstan > 0 || fallaron > 0)
    {
        int pasaron = descargas + torrents;
        wxString t = pasaron == 1 ? wxString(L"Pasó a la lista 1 descarga") : wxString::Format(L"Pasaron a la lista %d descargas", pasaron);
        if (yaEstaban > 0) t += yaEstaban == 1 ? wxString(L" (1 ya estaba)") : wxString::Format(L" (%d ya estaban)", yaEstaban);
        t += L".";
        if (noEstan == 1) t += L" 1 ya no está en el servidor: se queda en el recolector.";
        else if (noEstan > 1) t += wxString::Format(L" %d ya no están en el servidor: se quedan en el recolector.", noEstan);
        if (fallaron == 1) t += L" 1 no se pudo agregar.";
        else if (fallaron > 1) t += wxString::Format(L" %d no se pudieron agregar.", fallaron);
        ayuda::Aviso(t, L"Recolector de enlaces", wxOK | wxICON_INFORMATION, padre);
    }
    Refrescar();
    return hechos;
}
void VentanaPrincipal::AbrirPagina(const std::wstring& url, bool buscarYa)
{
    DialogoPagina d(IsShown() ? this : nullptr, m_ajustes, url, buscarYa);
    if (d.ShowModal() != wxID_OK) return;
    int agregadas = 0, yaEstaban = 0;
    for (const auto& e : d.Elegidos())
    {
        if (enlaces::EsMagnet(e.url))
        {
            InfoTorrent info;
            std::string error;
            if (!MotorTorrent::LeerMagnet(e.url, info, error)) continue;
            if (m_torrents.BuscarPorHuella(info.huella)) { yaEstaban++; continue; }
            NuevoTorrent n;
            n.magnet = e.url;
            n.carpeta = d.Carpeta();
            n.compartir = m_ajustes.compartirTorrents;
            n.elegirDespues = m_ajustes.elegirArchivosMagnet;
            n.empezar = d.EmpezarAhora();
            if (m_torrents.Agregar(n, error)) agregadas++;
            continue;
        }
        bool yaEstaba = false;
        if (AgregarEnlace(e.url, d.Carpeta(), d.Pagina(), d.EmpezarAhora(), yaEstaba)) agregadas++;
        else if (yaEstaba) yaEstaban++;
    }
    Registrar("enlaces de una página: " + std::to_string(agregadas) + " agregados de " + SoloSitio(d.Pagina()));
    wxString texto = wxString::Format(L"Se agregaron %d %s.", agregadas, agregadas == 1 ? L"archivo" : L"archivos");
    if (yaEstaban > 0) texto += wxString::Format(L" %d ya estaban en la lista.", yaEstaban);
    Avisar(L"Enlaces de una página", texto);
    Refrescar();
}

void VentanaPrincipal::AbrirTorrent()
{
    if (!MotorTorrent::Disponible())
    {
        ayuda::Aviso(L"Esta copia de Atarraya se compiló sin torrents.", L"Torrent", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxFileDialog d(this, L"Abrir un archivo .torrent", wxEmptyString, wxEmptyString, L"Torrents (*.torrent)|*.torrent",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (d.ShowModal() != wxID_OK) return;
    AbrirTorrentDe(d.GetPath().ToStdWstring());
}

void VentanaPrincipal::AbrirTorrentDe(const std::wstring& ruta)
{
    std::string datos;
    int64_t tamano = TamanoArchivo(ruta);
    if (tamano <= 0 || tamano > 20LL * 1024 * 1024 || !LeerArchivo(ruta, datos))
    {
        ayuda::Aviso(L"No se pudo leer ese archivo .torrent (o pesa más de 20 MB).", L"Nuevo torrent", wxOK | wxICON_WARNING, this);
        return;
    }
    InfoTorrent info;
    std::string error;
    if (!MotorTorrent::LeerTorrent(datos, info, error))
    {
        ayuda::Aviso(wxString::FromUTF8(error), L"Nuevo torrent", wxOK | wxICON_WARNING, this);
        return;
    }
    Mostrar();
    AgregarTorrent(info, datos, false, L"");
}

void VentanaPrincipal::AgregarMagnet(const std::string& magnet, bool delNavegador, const std::wstring& carpeta)
{
    InfoTorrent info;
    std::string error;
    if (!MotorTorrent::LeerMagnet(magnet, info, error))
    {
        if (delNavegador && !MotorTorrent::Disponible()) return;
        ayuda::Aviso(wxString::FromUTF8(error), L"Nuevo torrent", wxOK | wxICON_WARNING, IsShown() ? this : nullptr);
        return;
    }
    if (delNavegador && !m_ajustes.preguntarNavegador)
    {
        // Sin preguntar: baja todos los archivos en la carpeta de siempre
        if (m_torrents.BuscarPorHuella(info.huella)) return;
        NuevoTorrent n;
        n.magnet = magnet;
        n.carpeta = m_ajustes.CarpetaEfectiva();
        n.compartir = m_ajustes.compartirTorrents;
        n.elegirDespues = m_ajustes.elegirArchivosMagnet;
        if (!m_torrents.Agregar(n, error)) Avisar(L"Nuevo torrent", wxString::FromUTF8(error), true);
        Refrescar();
        return;
    }
    AgregarTorrent(info, "", delNavegador, carpeta);
}

void VentanaPrincipal::AgregarTorrent(const InfoTorrent& info, const std::string& datos, bool delNavegador, const std::wstring& carpeta)
{
    if (uint64_t ya = m_torrents.BuscarPorHuella(info.huella))
    {
        ayuda::Aviso(L"Ese torrent ya está en la lista.", L"Nuevo torrent", wxOK | wxICON_INFORMATION, IsShown() ? this : nullptr);
        (void)ya;
        return;
    }
    DialogoTorrent d(IsShown() ? this : nullptr, m_ajustes, info, DialogoTorrent::Modo::Nuevo, delNavegador, carpeta);
    d.Raise();
    int respuesta = d.ShowModal();
    Registrar("ventana «Nuevo torrent»: " + std::string(respuesta == wxID_OK ? "aceptada" : "cancelada"));
    if (respuesta != wxID_OK) return;
    NuevoTorrent n;
    n.datos = datos;
    n.magnet = datos.empty() ? info.magnet : std::string();
    n.carpeta = d.Carpeta();
    n.bajar = d.Elegidos();
    n.elegirDespues = d.ElegirDespues();
    n.compartir = d.Compartir();
    n.empezar = d.EmpezarAhora();
    m_ajustes.compartirTorrents = n.compartir;
    if (datos.empty()) m_ajustes.elegirArchivosMagnet = n.elegirDespues;
    m_ajustes.Guardar();
    // Que no pise archivos que ya están con el mismo nombre (en un magnet se revisa cuando llegan los datos)
    if (!datos.empty())
    {
        auto choques = MotorTorrent::Choques(info, n.carpeta, n.bajar);
        if (!choques.empty())
        {
            int r = PreguntarChoques(IsShown() ? this : nullptr, choques);
            if (r == 0) return;
            if (r == 2)
            {
                n.carpeta = CarpetaAparte(n.carpeta, info.nombre);
                if (n.carpeta.empty())
                {
                    ayuda::Aviso(L"No se pudo crear la carpeta aparte.", L"Nuevo torrent", wxOK | wxICON_WARNING, IsShown() ? this : nullptr);
                    return;
                }
            }
        }
    }
    std::string error;
    if (!m_torrents.Agregar(n, error))
    {
        ayuda::Aviso(wxString::FromUTF8(error), L"Nuevo torrent", wxOK | wxICON_WARNING, IsShown() ? this : nullptr);
        return;
    }
    if (delNavegador) Mostrar();
    Refrescar();
}

void VentanaPrincipal::Pedido(const std::string& texto)
{
    nlohmann::json j;
    try { j = nlohmann::json::parse(texto); }
    catch (...) { Registrar("pedido con JSON no válido"); return; }
    if (!j.is_object()) return;
    // Cualquier programa de esta PC puede mandar un pedido: los campos que no son texto se ignoran (no lanzan errores)
    auto campo = [&](const char* nombre) {
        auto it = j.find(nombre);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    std::string tipo = campo("tipo");
    std::string urlPedido = campo("url");
    Registrar("pedido: " + tipo + (urlPedido.empty() ? std::string() : " " + (enlaces::EsMagnet(urlPedido) ? std::string("(magnet)") : SoloSitio(urlPedido))));
    if (tipo == "mostrar") { Mostrar(); return; }
    if (tipo == "torrent")
    {
        // Otra copia del programa abierta con un .torrent
        std::wstring ruta = Ancho(campo("ruta"));
        if (ruta.size() > 8 && Minusculas(ruta.substr(ruta.size() - 8)) == L".torrent" && ExisteArchivo(ruta)) AbrirTorrentDe(ruta);
        return;
    }
    if (tipo != "descargar") return;

    std::string url = urlPedido;
    if (sistema::EsMagnet(Ancho(url)))
    {
        AgregarMagnet(url, true);
        return;
    }
    DatosDescarga d;
    d.url = url;
    if (!sistema::EsDireccion(Ancho(d.url))) { Registrar("pedido rechazado: dirección no válida"); return; }
    if (enlaces::NoPermitido(d.url))
    {
        Registrar("pedido rechazado: sitio que no permite bajar sus videos");
        Avisar(L"Atarraya", enlaces::TextoNoPermitido);
        return;
    }
    d.referer = campo("referer");
    d.agente = campo("agente");
    d.cookies = campo("cookies");
    d.conexiones = m_ajustes.conexiones;
    std::wstring nombre = Ancho(campo("nombre"));
    if (!nombre.empty()) d.nombre = LimpiarNombre(nombre);
    auto t = j.find("tamano");
    int64_t tamano = t != j.end() && t->is_number_integer() ? t->get<int64_t>() : -1;

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
    for (auto id : Seleccion())
    {
        if (EsIdTorrent(id))
        {
            hnd::Foto f;
            if (m_torrents.Buscar(id, f) && f.eligeArchivos && f.tieneDatos)
            {
                ElegirArchivos(id);
                continue;
            }
            m_torrents.Reanudar(id);
        }
        else m_motor.Reanudar(id);
    }
    Refrescar();
}

void VentanaPrincipal::Pausar()
{
    for (auto id : Seleccion())
    {
        if (EsIdTorrent(id)) m_torrents.Pausar(id);
        else m_motor.Pausar(id);
    }
    Refrescar();
}

void VentanaPrincipal::Quitar()
{
    auto ids = Seleccion();
    if (ids.empty()) return;
    wxString pregunta = ids.size() == 1 ? wxString(L"¿Quitar esta descarga de la lista?")
                                        : wxString::Format(L"¿Quitar estas %zu descargas de la lista?", ids.size());
    pregunta += L"\n\nPuedes dejar los archivos donde están o mandarlos a la Papelera (también lo bajado a medias).";
    wxMessageDialog d(this, pregunta, L"Quitar", wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
    d.SetYesNoCancelLabels(L"Quitar de la lista", L"Quitar y mandar a la Papelera", L"Cancelar");
    int r = d.ShowModal();
    if (r == wxID_CANCEL) return;
    for (auto id : ids)
    {
        if (EsIdTorrent(id))
        {
            m_torrents.Quitar(id, r == wxID_NO);
            continue;
        }
        hnd::Foto f;
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
    hnd::Foto f;
    if (!BuscarFoto(PrimeraSeleccion(), f)) return;
    if (f.torrent && f.eligeArchivos && f.tieneDatos)
    {
        ElegirArchivos(f.datos.id);
        return;
    }
    bool terminada = f.datos.estado == Estado::Completada || (f.torrent && f.datos.estado == Estado::Compartiendo);
    if (!terminada)
    {
        AbrirCarpeta();
        return;
    }
    std::wstring ruta = f.torrent ? m_torrents.RutaFinal(f.datos.id) : m_motor.RutaFinal(f.datos.id);
    if (f.torrent && !m_torrents.UnSoloArchivo(f.datos.id))
    {
        // Un torrent de varios archivos: se abre su carpeta
        if (ExisteCarpeta(ruta)) sistema::Abrir(ruta);
        else AbrirCarpeta();
        return;
    }
    if (!ExisteArchivo(ruta))
    {
        ayuda::Aviso(L"El archivo ya no está en su carpeta (quizá se movió o se borró).", L"Abrir", wxOK | wxICON_INFORMATION, this);
        return;
    }
    std::wstring nombre = ruta.substr(ruta.find_last_of(L'\\') + 1);
    // Un .torrent bajado se abre aquí mismo, como torrent
    if (!f.torrent && Minusculas(nombre).size() > 8 && Minusculas(nombre).substr(nombre.size() - 8) == L".torrent" && MotorTorrent::Disponible())
    {
        AbrirTorrentDe(ruta);
        return;
    }
    if (sistema::EsEjecutable(nombre))
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
    hnd::Foto f;
    if (!BuscarFoto(PrimeraSeleccion(), f)) return;
    std::wstring ruta;
    if (f.torrent)
    {
        ruta = m_torrents.RutaFinal(f.datos.id);
        if (!ExisteArchivo(ruta) && !ExisteCarpeta(ruta)) ruta = f.datos.carpeta + L"\\";
    }
    else
    {
        ruta = f.datos.estado == Estado::Completada ? m_motor.RutaFinal(f.datos.id) : m_motor.RutaParcial(f.datos.id);
        if (ruta.empty()) ruta = (f.datos.carpeta.empty() ? m_ajustes.CarpetaEfectiva() : f.datos.carpeta) + L"\\";
    }
    sistema::MostrarEnCarpeta(ruta);
}

void VentanaPrincipal::CopiarEnlace()
{
    hnd::Foto f;
    if (!BuscarFoto(PrimeraSeleccion(), f)) return;
    m_copiadoPorAtarraya = Ancho(f.datos.url);   // que el recolector no lo tome como un enlace nuevo
    sistema::CopiarTexto(m_copiadoPorAtarraya);
}

void VentanaPrincipal::CambiarEnlace()
{
    hnd::Foto f;
    if (!BuscarFoto(PrimeraSeleccion(), f)) return;
    if (f.torrent)
    {
        ayuda::Aviso(L"Un torrent no tiene un enlace que se pueda cambiar: sus partes llegan de otras personas.", L"Cambiar el enlace",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    if (f.datos.estado == Estado::Completada) return;
    if (f.datos.estado == Estado::Descargando || f.datos.estado == Estado::Iniciando)
    {
        ayuda::Aviso(L"Primero pausa la descarga.", L"Cambiar el enlace", wxOK | wxICON_INFORMATION, this);
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
        ayuda::Aviso(L"Esa no es una dirección válida (http, https o ftp).", L"Cambiar el enlace", wxOK | wxICON_WARNING, this);
        return;
    }
    if (m_motor.CambiarEnlace(f.datos.id, Utf8(url))) m_motor.Reanudar(f.datos.id);
    Refrescar();
}

void VentanaPrincipal::CalcularHuella()
{
    hnd::Foto f;
    if (!BuscarFoto(PrimeraSeleccion(), f)) return;
    bool terminada = f.datos.estado == Estado::Completada || (f.torrent && f.datos.estado == Estado::Compartiendo);
    if (!terminada) return;
    if (f.torrent && !m_torrents.UnSoloArchivo(f.datos.id))
    {
        ayuda::Aviso(L"La huella se calcula para un solo archivo, y este torrent tiene varios. (Los torrents ya comprueban "
                     L"cada parte al bajarla.)", L"Huella SHA-256", wxOK | wxICON_INFORMATION, this);
        return;
    }
    std::wstring ruta = f.torrent ? m_torrents.RutaFinal(f.datos.id) : m_motor.RutaFinal(f.datos.id);
    wxString nombre = ruta.substr(ruta.find_last_of(L'\\') + 1);
    m_resumen->SetLabel(L"Calculando la huella SHA-256 de " + nombre + L"...");
    VentanaPrincipal* yo = this;
    hilos::Vivo vivo = m_vivo;
    std::thread([yo, vivo, ruta, nombre]() {
        std::string huella = Sha256Archivo(ruta);
        hilos::Enviar([yo, vivo, huella, nombre]() {
            if (!*vivo) return;
            if (huella.empty())
            {
                ayuda::Aviso(L"No se pudo leer el archivo.", L"Huella SHA-256", wxOK | wxICON_WARNING, yo);
                return;
            }
            wxMessageDialog d(yo, nombre + L"\n\nSHA-256:\n" + wxString::FromUTF8(huella)
                                  + L"\n\nCompárala con la que publica el sitio de descarga: si es igual, el archivo llegó entero.",
                              L"Huella SHA-256", wxYES_NO | wxICON_INFORMATION);
            d.SetYesNoLabels(L"Copiar", L"Cerrar");
            if (d.ShowModal() == wxID_YES) sistema::CopiarTexto(Ancho(huella));
        });
    }).detach();
}

void VentanaPrincipal::ElegirArchivos(uint64_t id)
{
    if (!EsIdTorrent(id)) return;
    InfoTorrent info;
    if (!m_torrents.Archivos(id, info)) return;
    if (!info.tieneDatos)
    {
        ayuda::Aviso(L"Todavía no llegan los datos de este torrent (su lista de archivos). Déjalo bajando un momento.",
                     L"Elegir los archivos", wxOK | wxICON_INFORMATION, this);
        return;
    }
    Mostrar();
    DialogoTorrent d(this, m_ajustes, info, DialogoTorrent::Modo::Elegir);
    if (d.ShowModal() != wxID_OK) return;
    auto elegidos = d.Elegidos();
    hnd::Foto f;
    if (m_torrents.Buscar(id, f) && f.eligeArchivos)
    {
        // Todavía no bajó nada: si choca con archivos que ya están, se pregunta antes de empezar
        auto choques = m_torrents.Choques(id, elegidos);
        if (!choques.empty())
        {
            int r = PreguntarChoques(this, choques);
            if (r == 0) return;   // sigue esperando la elección
            if (r == 2)
            {
                std::wstring nueva = CarpetaAparte(f.datos.carpeta, info.nombre);
                if (nueva.empty() || !m_torrents.CambiarCarpeta(id, nueva))
                {
                    ayuda::Aviso(L"No se pudo usar una carpeta aparte para este torrent.", L"Elegir los archivos", wxOK | wxICON_WARNING, this);
                    return;
                }
            }
        }
    }
    else if (m_torrents.Buscar(id, f))
    {
        // Archivos que se suman a la selección y que el torrent nunca usó: si ya hay otros con ese nombre, se pregunta
        std::vector<bool> nuevos(elegidos.size(), false);
        for (size_t i = 0; i < elegidos.size() && i < info.archivos.size(); i++)
            nuevos[i] = elegidos[i] && !info.archivos[i].bajar && info.archivos[i].hecho == 0;
        auto choques = m_torrents.Choques(id, nuevos);
        if (!choques.empty())
        {
            int r = PreguntarChoques(this, choques, false);
            if (r == 0) return;
            if (r == 3)
            {
                for (size_t i = 0; i < nuevos.size(); i++)
                {
                    if (nuevos[i] && ExisteArchivo(f.datos.carpeta + L"\\" + info.archivos[i].ruta)) elegidos[i] = false;
                }
            }
        }
    }
    if (!m_torrents.ElegirArchivos(id, elegidos))
        ayuda::Aviso(L"No se pudo cambiar la elección de archivos.", L"Elegir los archivos", wxOK | wxICON_WARNING, this);
    Refrescar();
}

void VentanaPrincipal::CambiarCompartir()
{
    hnd::Foto f;
    if (!BuscarFoto(PrimeraSeleccion(), f) || !f.torrent) return;
    m_torrents.Compartir(f.datos.id, !f.compartir);
    Refrescar();
}

void VentanaPrincipal::MenuContextual(wxDataViewEvent&)
{
    auto ids = Seleccion();
    if (ids.empty()) return;
    hnd::Foto f;
    bool torrent = BuscarFoto(ids.front(), f) && f.torrent;
    wxMenu m;
    m.Append(ID_REANUDAR, torrent && f.datos.estado == Estado::Completada ? L"Volver a compartir" : L"Reanudar");
    m.Append(ID_PAUSAR, L"Pausar");
    m.AppendSeparator();
    m.Append(ID_ABRIR, torrent && !m_torrents.UnSoloArchivo(f.datos.id) ? L"Abrir" : L"Abrir el archivo");
    m.Append(ID_CARPETA, L"Abrir la carpeta");
    m.Append(ID_COPIAR, torrent ? L"Copiar el enlace magnet" : L"Copiar el enlace");
    if (torrent)
    {
        m.Append(ID_ELEGIR, L"Elegir los archivos...");
        m.AppendCheckItem(ID_COMPARTIR, L"Seguir compartiendo al terminar");
        m.Check(ID_COMPARTIR, f.compartir);
        m.Enable(ID_ELEGIR, f.tieneDatos);
    }
    else
    {
        m.Append(ID_CAMBIAR, L"Cambiar el enlace...");
    }
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

void VentanaPrincipal::AlTerminar(const hnd::Foto& f)
{
    wxString nombre = NombreVisible(f.datos);
    if (f.datos.estado == Estado::Completada)
    {
        std::wstring ruta = f.datos.carpeta + L"\\" + f.datos.nombre;
        VentanaPrincipal* yo = this;
        hilos::Vivo vivo = m_vivo;
        sistema::MarcarComoDescargado(ruta, f.datos.url, f.datos.referer, [yo, vivo, nombre](bool existe) {
            if (existe) return;
            hilos::Enviar([yo, vivo, nombre]() {
                if (!*vivo) return;
                ayuda::Aviso(L"Windows (o tu antivirus) bloqueó o quitó «" + nombre + L"» porque podría ser peligroso.",
                             L"Archivo bloqueado", wxOK | wxICON_WARNING, yo);
            });
        });
        bool esTorrent = Minusculas(f.datos.nombre).size() > 8 && Minusculas(f.datos.nombre).substr(f.datos.nombre.size() - 8) == L".torrent";
        if (m_ajustes.avisarAlTerminar)
            Avisar(L"Descarga terminada", esTorrent ? nombre + L"\nHaz doble clic en él para abrirlo en Atarraya." : nombre);
    }
    else if (f.datos.estado == Estado::Error && m_ajustes.avisarAlTerminar)
    {
        Avisar(L"La descarga se detuvo", nombre + L"\n" + wxString::FromUTF8(f.datos.error), true);
    }
}

void VentanaPrincipal::AlTerminarTorrent(const hnd::Foto& f, const std::vector<std::wstring>& archivos)
{
    wxString nombre = NombreVisible(f.datos);
    VentanaPrincipal* yo = this;
    hilos::Vivo vivo = m_vivo;
    sistema::MarcarVariosComoDescargados(archivos, [yo, vivo, nombre](size_t faltan) {
        if (faltan == 0) return;
        hilos::Enviar([yo, vivo, nombre, faltan]() {
            if (!*vivo) return;
            ayuda::Aviso(wxString::Format(L"Windows (o tu antivirus) bloqueó o quitó %zu %s de «", faltan, faltan == 1 ? L"archivo" : L"archivos")
                             + nombre + L"» porque podría ser peligroso.",
                         L"Archivo bloqueado", wxOK | wxICON_WARNING, yo);
        });
    });
    if (m_ajustes.avisarAlTerminar)
        Avisar(L"Torrent terminado", f.compartir ? nombre + L"\nSigue compartiendo con otras personas." : nombre);
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
    Hide();   // se va enseguida; los motores terminan de guardar por detrás
    if (m_ventanaRecolector) m_ventanaRecolector->Hide();
    if (m_bandeja)
    {
        m_bandeja->RemoveIcon();
        m_bandeja.release()->Destroy();   // wx lo borra cuando termina de atender sus eventos
    }
    if (m_oyendoPortapapeles) RemoveClipboardFormatListener((HWND)GetHWND());
    m_oyendoPortapapeles = false;
    m_cnl->Detener();
    m_motor.alTerminar = nullptr;
    m_torrents.alTerminar = nullptr;
    m_motor.Detener();
    m_torrents.Detener();
    m_recolector.Detener();
    Destroy();
}
