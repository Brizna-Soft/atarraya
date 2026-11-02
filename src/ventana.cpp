#include "ventana.hpp"

#include <wx/accel.h>
#include <wx/app.h>
#include <wx/button.h>
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
#include "navegadores.hpp"
#include "nombres.hpp"
#include "sistema.hpp"
#include "util.hpp"
#include "ventanarecolector.hpp"
#include "version.h"

using namespace hnd;

namespace
{
    bool TemaOscuro() { return wxSystemSettings::GetAppearance().IsDark(); }

    // El reloj de las esperas de la cola de «de una en una»
    int64_t RelojEsperas()
    {
#ifdef ATARRAYA_PRUEBAS
        // Solo en la copia de prueba: se puede acelerar (ATARRAYA_PRUEBA_RELOJ=60: un minuto pasa en un segundo) para
        // probar una espera de 10 minutos en un escritorio aparte sin esperar 10 minutos
        static const int64_t inicio = AhoraUnix();
        static const int64_t factor = [] {
            wchar_t b[16] = {};
            DWORD n = GetEnvironmentVariableW(L"ATARRAYA_PRUEBA_RELOJ", b, 16);
            int64_t f = n > 0 && n < 16 ? _wtoi64(b) : 1;
            return f >= 1 && f <= 3600 ? f : 1;
        }();
        return inicio + (AhoraUnix() - inicio) * factor;
#else
        return AhoraUnix();
#endif
    }

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
        // Un video por partes: por las partes ya juntadas (su tamaño no se sabe hasta el final)
        if (f.datos.tamano <= 0 && f.datos.flujoTotal > 0) return (int)std::min<int64_t>(1000, (int64_t)f.datos.flujoHechos * 1000 / f.datos.flujoTotal);
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
            else if (bajando && f.velocidad > 1 && d.flujoTotal > 0 && d.flujoHechos > 0)
            {
                // Video por partes: lo que falta, calculado con lo que pesan en promedio las ya juntadas
                double total = (double)f.hecho * d.flujoTotal / d.flujoHechos;
                v = wxString(TextoDuracion((int64_t)(std::max(0.0, total - (double)f.hecho) / f.velocidad)));
            }
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
      m_cnl(std::make_unique<hnd::ServidorCnL>()), m_reloj(this), m_sitios(hnd::CarpetaDatos() + L"\\de-una-en-una.json"),
      m_sitiosNavegador(hnd::CarpetaDatos() + L"\\sitios-navegador.json")
{
    wxIconBundle iconos(wxString(L"AAAA_APP"), (WXHINSTANCE)GetModuleHandleW(nullptr));
    SetIcons(iconos);

    CrearMenu();
    CrearBarra();

    auto* fondo = new wxPanel(this);
    auto* caja = new wxBoxSizer(wxVERTICAL);

    // Barra de «de una en una» (se ve solo mientras hay páginas de sitios de descargas en la cola)
    CrearBarraSitios(fondo);
    caja->Add(m_barraSitios, 0, wxEXPAND);
    // Barra de Jícaro (se ve mientras descomprime y con el resultado, hasta que la persona la cierra)
    CrearBarraJicaro(fondo);
    caja->Add(m_barraJicaro, 0, wxEXPAND);

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
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AbrirElegidasEnNavegador(); }, ID_ABRIR_EN_NAVEGADOR);
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

    // La cola de «de una en una» sigue donde iba (al abrir no se abre ninguna página sola). Si había terminado pero
    // las que no bajaron no llegaron al recolector, se intenta otra vez.
    m_sitios.Cargar();
    m_sitiosNavegador.Cargar();
    // Lo que se estaba probando cuando Atarraya se cerró queda para el navegador, que siguió bajando la suya: se quita
    // la de Atarraya antes de que arranque, así nunca bajan las dos
    auto aMedias = m_sitios.ResolverTomasAlAbrir();
    for (auto id : aMedias) m_motor.Quitar(id);
    if (!aMedias.empty())
    {
        GuardarCola();
        Registrar("de una en una: " + std::to_string(aMedias.size()) + " descargas que se estaban probando quedan para el navegador");
    }
    // Lo que el navegador terminó y no se alcanzó a pasar a la carpeta del paquete (Atarraya se cerró en el medio)
    for (int i : m_sitios.PorMover()) CallAfter([this, i]() { MoverParteDelNavegador(i); });
    if (m_sitios.Terminada()) Avanzar();
    ActualizarBarraSitios();

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
    // Las partes «sin confirmar» siguen en pausa y las que había que reanudar se reanudan (antes de ver qué arranca)
    RevisarPausas();
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
    // Las que prueban seguir una descarga del navegador arrancan ya: reemplazan una que ya está bajando ahí (si
    // esperaran turno, el navegador la terminaría sola)
    for (const auto& p : m_sitios.Partes())
    {
        if (p.estadoToma != EstadoToma::Probando || !p.descarga) continue;
        auto it = std::find_if(cola.begin(), cola.end(), [&](const auto& c) { return c.second == p.descarga; });
        if (it == cola.end()) continue;
        cola.erase(it);
        m_motor.permitidas.push_back(p.descarga);
        nuevasHttp++;
    }
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
    RevisarPartes();
    RevisarTomas();
    RevisarEsperas();
    if (!m_porAbrir.empty()) RevisarPorAbrir();
    // Las cuentas regresivas de la cola cambian cada segundo
    if (m_sitios.HayEnEspera() || m_sitios.Cuantas(EstadoParte::Navegador) > 0
        || std::any_of(m_sitios.Partes().begin(), m_sitios.Partes().end(), [](const Parte& p) { return p.hasta > 0; }))
        ActualizarBarraSitios();

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
                                         bool& yaEstaba, const std::wstring& paquete, const std::string& grupo)
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
    d.paquete = paquete;
    d.grupo = grupo;
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
    // La página de un sitio de descargas (ddownload, Rapidgator...): bajarla daría la página, no el archivo. Como en el
    // recolector, se abre en el navegador de una en una.
    if (VaAlNavegador(datos.url, delNavegador))
    {
        EnlaceRecolectado e;
        e.url = datos.url;
        e.referer = datos.referer;
        e.nombre = NombreDeUrl(datos.url);
        e.estado = Comprobacion::Sitio;
        Registrar("nueva descarga: es la página de un sitio de descargas (" + SoloSitio(datos.url) + "); va de una en una");
        MandarDeUnaEnUna({ e }, datos.carpeta, d.EmpezarAhora());
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
    if (n < lista.size())
        Registrar("Click'n'Load: " + std::to_string(n) + " nuevos en el recolector; " + std::to_string(lista.size() - n)
                  + " ya estaban ahí o no cabían");
    AbrirRecolector(true);
    if (n == 0)
    {
        // El sitio recibió «success»: que la persona vea dónde están, aunque no haya nada nuevo
        Avisar(L"Click'n'Load", m_recolector.Cantidad() >= Recolector::Maximo
                                    ? wxString(L"El recolector está lleno: quita algunos enlaces y vuelve a intentar.")
                                    : wxString(L"No llegó nada nuevo: esos enlaces ya estaban en el recolector."));
        return;
    }
    std::string sitio = enlaces::Sitio(origen);
    Avisar(L"Click'n'Load", wxString::Format(L"Llegaron %zu enlaces%s. Están en el recolector: revisa y elige cuáles bajar.", n,
                                             sitio.empty() ? wxString() : L" de " + wxString::FromUTF8(sitio)));
}

std::vector<uint64_t> VentanaPrincipal::ConfirmarRecolector(const std::vector<EnlaceRecolectado>& lista, const std::wstring& carpeta,
                                                            bool porPaquete, bool empezar)
{
    std::vector<uint64_t> hechos;
    std::vector<const EnlaceRecolectado*> paginas, sitios;
    // Un grupo por paquete, que comparten las directas y las de «de una en una». Las que ya estaban en la lista sin
    // terminar entran a su paquete antes de agregar las nuevas: así Jícaro espera también a esas y corre una sola vez.
    jicaro::Tanda tanda;
    for (const auto& e : lista)
    {
        if (e.estado == Comprobacion::NoExiste || e.estado == Comprobacion::NoPermitido || e.estado == Comprobacion::Sitio
            || e.estado == Comprobacion::Pagina || enlaces::EsMagnet(e.url))
            continue;
        hnd::Foto f;
        if (uint64_t ya = m_motor.BuscarPorUrl(e.url); ya && m_motor.Buscar(ya, f)) tanda.Sumar(PaqueteDe(e), ya, f.datos.grupo);
    }
    AplicarTanda(tanda);
    int descargas = 0, torrents = 0, yaEstaban = 0, noEstan = 0, fallaron = 0;
    for (const auto& e : lista)
    {
        if (e.estado == Comprobacion::NoExiste || e.estado == Comprobacion::NoPermitido) { noEstan++; continue; }   // se quedan en el recolector
        if (e.estado == Comprobacion::Sitio)
        {
            sitios.push_back(&e);   // de una en una
            continue;
        }
        if (e.estado == Comprobacion::Pagina)
        {
            // Una página que llegó por Click'n'Load, o cuya dirección termina con el nombre de un archivo, es la de un
            // sitio de descargas (aunque Atarraya no lo conozca): también va de una en una
            if (PaginaDeSitioDeDescargas(e)) sitios.push_back(&e);
            else paginas.push_back(&e);
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
        d.paquete = PaqueteDe(e);
        d.grupo = tanda.Grupo(d.paquete);
        m_motor.Agregar(d, empezar);
        descargas++;
        hechos.push_back(e.id);
    }
    wxWindow* padre = m_ventanaRecolector && m_ventanaRecolector->IsShown() ? (wxWindow*)m_ventanaRecolector : (wxWindow*)this;
    // Las páginas de sitios de descargas se abren de una en una: la siguiente cuando termina la anterior
    if (!sitios.empty() && EmpezarDeUnaEnUna(sitios, carpeta, porPaquete, empezar, padre, &tanda))
    {
        for (auto* e : sitios) hechos.push_back(e->id);
    }
    if (!paginas.empty())
    {
        const size_t tope = 10;
        wxString texto = wxString::Format(L"%zu de los enlaces marcados son páginas web, no archivos.\n\n"
                                          L"Se abren en tu navegador para que bajes desde ahí. Si tienes la extensión de Atarraya, la "
                                          L"descarga llega aquí sola.\n\n¿Abrirlas en el navegador?", paginas.size());
        if (paginas.size() > tope) texto += wxString::Format(L"\n\nSe abren los primeros %zu; los demás se quedan en el recolector.", tope);
        wxMessageDialog p(padre, texto, L"Páginas para el navegador", wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION);
        p.SetYesNoLabels(L"Abrir en el navegador", L"Ahora no");
        if (p.ShowModal() == wxID_YES)
        {
            for (size_t i = 0; i < paginas.size() && i < tope; i++)
            {
                sistema::AbrirPaginaWeb(paginas[i]->url);
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
    std::vector<EnlaceRecolectado> sitios;   // páginas de sitios de descargas: de una en una
    // Las partes de un mismo archivo van juntas, como en el recolector (con las que ya estaban en la lista sin terminar)
    auto paqueteDe = [](const std::string& enlace) {
        EnlaceRecolectado como;
        como.url = enlace;
        como.nombre = NombreDeUrl(enlace);
        como.estado = Comprobacion::Archivo;
        return PaqueteDe(como);
    };
    jicaro::Tanda tanda;
    for (const auto& e : d.Elegidos())
    {
        if (VaAlNavegador(e.url, false) || enlaces::EsMagnet(e.url)) continue;
        hnd::Foto f;
        if (uint64_t ya = m_motor.BuscarPorUrl(e.url); ya && m_motor.Buscar(ya, f)) tanda.Sumar(paqueteDe(e.url), ya, f.datos.grupo);
    }
    AplicarTanda(tanda);
    for (const auto& e : d.Elegidos())
    {
        if (VaAlNavegador(e.url, false))
        {
            EnlaceRecolectado s;
            s.url = e.url;
            s.referer = d.Pagina();
            s.nombre = NombreDeUrl(e.url);
            s.estado = Comprobacion::Sitio;
            sitios.push_back(std::move(s));
            continue;
        }
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
        std::wstring paquete = paqueteDe(e.url);
        if (AgregarEnlace(e.url, d.Carpeta(), d.Pagina(), d.EmpezarAhora(), yaEstaba, paquete, tanda.Grupo(paquete))) agregadas++;
        else if (yaEstaba) yaEstaban++;
    }
    Registrar("enlaces de una página: " + std::to_string(agregadas) + " agregados de " + SoloSitio(d.Pagina()));
    if (agregadas > 0 || yaEstaban > 0)
    {
        wxString texto = wxString::Format(L"Se agregaron %d %s.", agregadas, agregadas == 1 ? L"archivo" : L"archivos");
        if (yaEstaban > 0) texto += wxString::Format(L" %d ya estaban en la lista.", yaEstaban);
        Avisar(L"Enlaces de una página", texto);
    }
    if (!sitios.empty()) MandarDeUnaEnUna(sitios, d.Carpeta(), d.EmpezarAhora());
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
    if (tipo == "video")
    {
        // Un video de la página (el botón «Descargar este video» o la lista de la extensión)
        auto t = j.find("tamano");
        PedidoVideo(campo("formato"), urlPedido, Ancho(campo("titulo")), campo("referer"), campo("pagina"), campo("cookies"),
                    campo("agente"), campo("tipoMime"), t != j.end() && t->is_number_integer() ? t->get<int64_t>() : -1);
        return;
    }
    auto numero = [&](const char* nombre, int64_t siNo) {
        auto it = j.find(nombre);
        return it != j.end() && it->is_number_integer() ? it->get<int64_t>() : siNo;
    };
    if (tipo == "navegador")
    {
        // Una descarga que baja el navegador y sigue la extensión (de una parte de «de una en una»)
        std::wstring nombre = Ancho(campo("nombre"));
        if (!nombre.empty()) nombre = LimpiarNombre(nombre);
        PedidoNavegador(campo("toma"), campo("estado"), urlPedido, campo("referer"),
                        campo("paginaCola").empty() ? campo("pagina") : campo("paginaCola"), nombre, numero("bajado", 0),
                        numero("total", -1), campo("ruta"));
        return;
    }
    if (tipo == "espera")
    {
        // La página de una parte pide esperar (lo leyó la extensión en la página que abrió Atarraya)
        PedidoEspera(campo("paginaCola"), campo("clase"), numero("segundos", -1));
        return;
    }
    if (tipo == "abierta")
    {
        // La extensión abrió la página que le pidió Atarraya (en una pestaña que abrió ella misma)
        auto it = j.find("id");
        if (it == j.end() || !it->is_number_integer()) return;
        auto a = m_porAbrir.find(it->get<int64_t>());
        if (a == m_porAbrir.end()) return;
        Registrar("de una en una: parte " + std::to_string(a->second.parte + 1) + ": la extensión abrió su página");
        m_porAbrir.erase(a);
        return;
    }
    if (tipo == "sinConfirmar")
    {
        // La extensión no pudo cancelar la del navegador ni saber cómo quedó: pueden estar bajando las dos. La de
        // Atarraya se pausa y la parte no sigue hasta saberlo (la extensión lo sigue mirando).
        int i = m_sitios.ParteDeToma(campo("toma"));
        uint64_t pausar = m_sitios.MarcarSinConfirmar(i);
        if (!pausar) return;
        m_motor.Pausar(pausar);
        GuardarCola();
        size_t cuantas = m_sitios.Partes().size();
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": no se pudo confirmar si el navegador sigue bajándola; "
                  "Atarraya pausó la suya hasta saberlo");
        Avisar(wxString::Format(L"Parte %d de %zu: no se pudo confirmar", i + 1, cuantas),
               L"No se pudo saber si tu navegador sigue bajando esta parte. Atarraya pausó la suya hasta saberlo, para no bajarla dos "
               L"veces. Mira la lista de descargas de tu navegador.",
               true);
        ActualizarBarraSitios();
        Refrescar();
        return;
    }
    if (tipo == "confirmada")
    {
        // Ya se sabe: la del navegador no sigue. La de Atarraya sigue donde iba.
        int i = m_sitios.ParteDeToma(campo("toma"));
        if (i < 0 || campo("estado") != "cancelada") return;
        uint64_t reanudar = m_sitios.Confirmar(i);
        GuardarCola();
        if (reanudar) m_motor.Reanudar(reanudar);
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": se confirmó que el navegador ya no la baja; sigue la de Atarraya");
        ActualizarBarraSitios();
        Refrescar();
        return;
    }
    if (tipo == "soltar")
    {
        // A la extensión se le acabó el plazo: que siga el navegador, si Atarraya todavía no la tomó. Decide Atarraya
        // (la extensión espera su respuesta), así nunca bajan las dos.
        int i = m_sitios.ParteDeToma(campo("toma"));
        if (i >= 0 && m_sitios.Partes()[i].estadoToma == EstadoToma::Probando)
        {
            Registrar("de una en una: parte " + std::to_string(i + 1) + ": la extensión pidió que siga el navegador");
            TomaANavegador(i, false);
        }
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

    // ¿Es la descarga de una página abierta «de una en una»? Si es seguro, se toma sin preguntar (la persona ya eligió
    // la carpeta); si viene del sitio pero hay duda, se le pregunta. Si no, sigue como cualquier descarga. «paginaCola»
    // es la página que abrió Atarraya en la pestaña donde nació la descarga (la extensión la recuerda aunque la pestaña
    // haya cambiado de página después del captcha).
    std::string paginaCola = campo("paginaCola");
    std::string toma = campo("toma");
    auto parte = m_sitios.Emparejar(d.url, d.referer, paginaCola.empty() ? campo("pagina") : paginaCola, nombre);
    if (!toma.empty())
    {
        // La extensión dejó la descarga en el navegador mientras Atarraya prueba seguirla. Solo si es una parte, sin
        // duda: si no, que siga el navegador (así no se baja dos veces ni se pierde el captcha).
        if (m_sitios.ParteDeToma(toma) >= 0) return;   // la misma otra vez
        if (parte.parte < 0 || !parte.segura)
        {
            if (m_tomasSinParte.size() >= 1000) m_tomasSinParte.clear();
            m_tomasSinParte.insert(toma);
            return;
        }
        if (m_sitiosNavegador.Necesita(m_sitios.Partes()[parte.parte].url))
        {
            m_sitios.EnNavegador(parte.parte, toma);
            GuardarCola();
            Registrar("de una en una: parte " + std::to_string(parte.parte + 1) + ": la baja el navegador (" + SoloSitio(d.url) + ")");
            ActualizarBarraSitios();
            return;
        }
        TomarParte(parte.parte, d, toma);
        return;
    }
    if (parte.parte >= 0 && (parte.segura || PreguntarSiEsParte(parte.parte, d.url, nombre)))
    {
        TomarParte(parte.parte, d);
        return;
    }

    if (m_ajustes.preguntarNavegador) NuevaDescarga(d, true, tamano);
    else
    {
        d.carpeta = m_ajustes.CarpetaEfectiva();
        m_motor.Agregar(d, true);
        Refrescar();
    }
}

void VentanaPrincipal::PedidoVideo(const std::string& formato, const std::string& url, const std::wstring& titulo,
                                   const std::string& referer, const std::string& pagina, const std::string& cookies,
                                   const std::string& agente, const std::string& tipoMime, int64_t tamano)
{
    // Lo mismo que revisa la extensión, otra vez aquí: cualquier programa de esta PC puede mandar un pedido
    std::string bajo = url.substr(0, 8);
    std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (!sistema::EsDireccion(Ancho(url)) || (bajo.rfind("http://", 0) != 0 && bajo.rfind("https://", 0) != 0))
    {
        Registrar("video rechazado: dirección no válida");
        return;
    }
    if (enlaces::NoPermitido(url) || enlaces::NoPermitido(referer) || enlaces::NoPermitido(pagina))
    {
        Registrar("video rechazado: sitio que no permite bajar sus videos");
        Avisar(L"Atarraya", enlaces::TextoNoPermitido);
        return;
    }
    if (enlaces::SitioProtegido(url) || enlaces::SitioProtegido(referer) || enlaces::SitioProtegido(pagina))
    {
        Registrar("video rechazado: sitio con DRM");
        Avisar(L"Atarraya", enlaces::TextoSitioProtegido);
        return;
    }
    if (formato == "archivo")
    {
        // Un archivo de video o de audio entero: como cualquier descarga, con el título de la página como nombre
        DatosDescarga d;
        d.url = url;
        d.referer = referer;
        d.cookies = cookies;
        d.agente = agente;
        d.conexiones = m_ajustes.conexiones;
        d.sinProxy = true;   // los videos de las páginas van directo, nunca por proxy (como las consultas del recolector)
        // Como las listas: nada de esta PC ni de su red, tampoco por una redirección (el filtro del socket ve cada
        // conexión). Si el video o su página son de ahí, se pregunta antes; con un sí, se conecta ahí.
        std::string deLaCasa;
        for (const std::string* u : { &url, &referer, &pagina })
        {
            if (deLaCasa.empty() && EsDeLaCasa(*u)) deLaCasa = *u;
        }
        if (!deLaCasa.empty())
        {
            Registrar("video: archivo de la red de la casa (" + SoloSitio(deLaCasa) + "): se pregunta");
            if (!PreguntarVideoDeRed(IsShown() ? this : nullptr, deLaCasa))
            {
                Registrar("video: la persona no quiso bajarlo de su red");
                return;
            }
        }
        d.soloAfuera = deLaCasa.empty();
        std::wstring deUrl = NombreDeUrl(url);
        std::wstring t = wxString(titulo).Strip(wxString::both).ToStdWstring();
        if (!t.empty())
        {
            auto punto = deUrl.find_last_of(L'.');
            std::wstring ext = punto != std::wstring::npos && deUrl.size() - punto <= 6 ? deUrl.substr(punto) : std::wstring();
            d.nombre = ext.empty() ? AgregarExtension(LimpiarNombre(t), tipoMime) : LimpiarNombre(t) + ext;
        }
        Registrar("video: archivo de " + SoloSitio(url));
        if (m_ajustes.preguntarNavegador) NuevaDescarga(d, true, tamano);
        else
        {
            d.carpeta = m_ajustes.CarpetaEfectiva();
            m_motor.Agregar(d, true);
            Refrescar();
        }
        return;
    }
    if (formato != "hls" && formato != "dash") return;
    Registrar("video: " + formato + " de " + SoloSitio(url));
    DialogoVideo dialogo(IsShown() ? this : nullptr, m_ajustes, { formato, url, referer, cookies, agente, titulo });
    dialogo.Raise();
    int respuesta = dialogo.ShowModal();
    Registrar("ventana «Bajar video»: " + std::string(respuesta == wxID_OK ? "aceptada" : "cancelada"));
    if (respuesta != wxID_OK) return;
    auto descargas = dialogo.Descargas();
    for (auto& d : descargas) m_motor.Agregar(d, dialogo.EmpezarAhora());
    if (descargas.size() > 1) Registrar("video: " + std::to_string(descargas.size()) + " archivos (video y audio aparte)");
    Mostrar();
    Refrescar();
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
        // Archivos que se suman a la selección: si ya hay otros con ese nombre en la carpeta, se pregunta
        std::vector<bool> nuevos(elegidos.size(), false);
        for (size_t i = 0; i < elegidos.size() && i < info.archivos.size(); i++)
            nuevos[i] = elegidos[i] && !info.archivos[i].bajar;   // lo bajado de un archivo no elegido puede ser de las piezas vecinas
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
        // El sitio respondió con una página: se puede abrir en el navegador
        if (f.datos.estado == Estado::Error && f.datos.error == TextoRespondioPagina) m.Append(ID_ABRIR_EN_NAVEGADOR, L"Abrir en el navegador");
    }
    m.Append(ID_HUELLA, L"Calcular la huella SHA-256");
    m.AppendSeparator();
    m.Append(ID_QUITAR, L"Quitar...");
    PopupMenu(&m);
}

void VentanaPrincipal::Avisar(const wxString& titulo, const wxString& texto, bool error)
{
    bool hay = m_bandeja && m_bandeja->IsIconInstalled();
    Registrar("aviso: " + Utf8(titulo.ToStdWstring()) + (hay ? "" : " (sin ícono junto al reloj: no se mostró)"));
    if (hay) m_bandeja->ShowBalloon(titulo, texto, 6000, error ? wxICON_WARNING : wxICON_INFORMATION);
}

void VentanaPrincipal::AlTerminar(const hnd::Foto& f)
{
    // Una descarga con la que Atarraya probaba seguir la del navegador: si no pudo, la parte sigue en el navegador
    // (no es un error de la parte ni se avisa como tal)
    for (size_t i = 0; i < m_sitios.Partes().size(); i++)
    {
        const auto& p = m_sitios.Partes()[i];
        if (p.descarga != f.datos.id || p.estadoToma != EstadoToma::Probando || p.estado != EstadoParte::Tomada) continue;
        if (f.datos.estado != Estado::Completada)
        {
            TomaANavegador((int)i, true);
            return;
        }
        m_sitios.Decidir((int)i, true);
        GuardarCola();
        break;
    }
    wxString nombre = NombreVisible(f.datos);
    // De una en una: si era una parte, se marca ya (así se sabe si su paquete quedó completo). La siguiente se abre al
    // final.
    int parte = m_sitios.Terminar(f.datos.id, f.datos.estado == Estado::Completada);
    if (f.datos.estado == Estado::Completada)
    {
        std::wstring ruta = f.datos.carpeta + L"\\" + f.datos.nombre;
        VentanaPrincipal* yo = this;
        hilos::Vivo vivo = m_vivo;
        // Jícaro, si con esta terminó todo su paquete. Se abre cuando Windows ya marcó todas sus partes como bajadas de
        // internet: así lo que salga del comprimido queda marcado igual.
        m_marcando.insert(Minusculas(ruta));
        EncolarJicaro(PrepararJicaro(f.datos));
        uint64_t id = f.datos.id;
        sistema::MarcarComoDescargado(ruta, f.datos.url, f.datos.referer, [yo, vivo, nombre, ruta, id](bool existe) {
            hilos::Enviar([yo, vivo, nombre, ruta, id, existe]() {
                if (!*vivo) return;
                yo->m_marcando.erase(Minusculas(ruta));
                if (existe) yo->GuardarIdentidad(id, ruta);   // ya con la marca: así queda el archivo que bajó Atarraya
                yo->SiguienteJicaro();
                if (existe) return;
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

    // El sitio respondió con una página en vez del archivo: se ofrece abrirla en el navegador. (Las partes de «de una
    // en una» ya tienen su camino: vuelven al recolector.)
    if (f.datos.estado == Estado::Error && f.datos.error == TextoRespondioPagina && !m_sitios.TieneDescarga(f.datos.id))
    {
        m_paginasPorAbrir.push_back(f.datos.id);
        CallAfter([this]() { OfrecerAbrirPaginas(); });
    }

    // De una en una: si era una parte, se abre la siguiente (este aviso va al último, para que sea el que se ve)
    if (parte >= 0)
    {
        GuardarCola();
        Registrar("de una en una: parte " + std::to_string(parte + 1) + (f.datos.estado == Estado::Completada ? " lista" : " con error"));
        Avanzar();
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
    GuardarCola();
    Destroy();
}

// --- De una en una ------------------------------------------------------------------------------------------------
// Atarraya solo abre la página del sitio en el navegador y recibe lo que el navegador le pasa: no hace clics, no
// resuelve captchas, no se salta esperas ni usa cuentas premium.

bool VentanaPrincipal::EmpezarDeUnaEnUna(const std::vector<const EnlaceRecolectado*>& sitios, const std::wstring& carpeta,
                                         bool porPaquete, bool abrir, wxWindow* padre, jicaro::Tanda* tanda)
{
    jicaro::Tanda propia;
    if (!tanda) tanda = &propia;
    // Sin la extensión, lo que se baje en esas páginas se queda en el navegador
    bool conectada = false;
    for (const auto& n : navegadores::Estado()) conectada = conectada || n.conectado;
    if (!conectada)
    {
        wxMessageDialog p(padre,
                          L"La extensión de Atarraya no está conectada con ningún navegador. Sin ella, lo que bajes en esas páginas "
                          L"se queda en el navegador y Atarraya no puede abrir la siguiente sola.\n\n"
                          L"Conéctala en «Navegadores» y vuelve a pulsar «Descargar los marcados». También puedes empezar igual y "
                          L"pasar de una página a otra con los botones.",
                          L"De una en una", wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxICON_WARNING);
        p.SetYesNoCancelLabels(L"Abrir «Navegadores»", L"Empezar igual", L"Cancelar");
        int r = p.ShowModal();
        Registrar(std::string("de una en una: la extensión no está conectada; la persona eligió ")
                  + (r == wxID_YES ? "abrir Navegadores" : r == wxID_NO ? "empezar igual" : "cancelar"));
        if (r == wxID_YES)
        {
            DialogoNavegadores d(padre);
            d.ShowModal();
            return false;   // los enlaces se quedan en el recolector
        }
        if (r != wxID_NO) return false;
    }

    std::vector<Parte> partes;
    for (const auto* e : sitios)
    {
        Parte p;
        p.url = e->url;
        p.referer = e->referer;
        p.nombre = NombreDeUrl(e->url);
        p.paquete = PaqueteDe(*e);
        p.grupo = tanda->Grupo(p.paquete);
        p.carpeta = porPaquete ? carpeta + L"\\" + LimpiarNombre(p.paquete) : carpeta;
        partes.push_back(std::move(p));
    }
    // La cola se guarda antes de que los enlaces salgan del recolector: si no se puede guardar, se quedan ahí
    if (!m_sitios.Agregar(std::move(partes)))
    {
        Registrar("de una en una: no se pudo guardar la cola; los enlaces se quedan en el recolector");
        ayuda::Aviso(L"No se pudo guardar la cola de «de una en una» en " + wxString(CarpetaDatos())
                         + L". Los enlaces se quedan en el recolector: prueba otra vez en un momento.",
                     L"De una en una", wxOK | wxICON_WARNING, padre);
        return false;
    }
    m_resumenSitios.clear();
    Registrar("de una en una: " + std::to_string(sitios.size()) + (sitios.size() == 1 ? " página" : " páginas") + " en la cola");
    int i = abrir && !m_sitios.HayAbierta() ? m_sitios.AbrirSiguiente() : -1;
    if (i >= 0) GuardarCola();
    Mostrar();   // la barra con los botones está en la ventana principal
    if (i >= 0) AbrirParte(i, false);
    ActualizarBarraSitios();
    return true;
}

void VentanaPrincipal::CrearBarraSitios(wxWindow* padre)
{
    m_barraSitios = new wxPanel(padre);
    bool oscuro = TemaOscuro();
    m_barraSitios->SetBackgroundColour(oscuro ? wxColour(0x3A, 0x33, 0x1C) : wxColour(0xFF, 0xF4, 0xCE));
    auto* caja = new wxBoxSizer(wxVERTICAL);
    m_textoSitios = new wxStaticText(m_barraSitios, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_textoSitios->SetForegroundColour(oscuro ? wxColour(0xF2, 0xE6, 0xC2) : wxColour(0x3D, 0x2E, 0x00));
    caja->Add(m_textoSitios, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    auto* botones = new wxBoxSizer(wxHORIZONTAL);
    auto boton = [&](const wxString& texto, const wxString& ayuda) {
        auto* b = new wxButton(m_barraSitios, wxID_ANY, texto);
        b->SetToolTip(ayuda);
        botones->Add(b, 0, wxRIGHT, FromDIP(6));
        return b;
    };
    m_bOtraVez = boton(L"Abrir otra vez", L"Vuelve a abrir esta página en el navegador (por ejemplo, si cerraste la pestaña)");
    m_bSiguiente = boton(L"Abrir la siguiente ahora", L"Abre la página siguiente sin esperar a que termine la que se está bajando");
    m_bSaltar = boton(L"Saltar esta", L"Deja esta página para después: al final vuelve al recolector. Se abre la siguiente.");
    m_bParar = boton(L"Parar", L"Las páginas que faltan vuelven al recolector. Lo que ya se está bajando sigue.");
    m_bCerrarSitios = boton(L"Cerrar", L"Quitar este aviso");
    caja->Add(botones, 0, wxALL, FromDIP(8));
    m_barraSitios->SetSizer(caja);
    m_barraSitios->Hide();

    m_bOtraVez->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int i = m_sitios.Actual();
        if (i < 0) return;
        auto e = m_sitios.Partes()[i].estado;
        if (e != EstadoParte::Abierta && e != EstadoParte::Espera) return;
        // También una que espera: la persona decide abrirla ya
        m_sitios.AbrirOtraVez(i);
        GuardarCola();
        AbrirParte(i, false);
        ActualizarBarraSitios();
    });
    m_bSiguiente->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int i = m_sitios.AbrirSiguiente();
        if (i < 0) return;
        GuardarCola();
        AbrirParte(i, false);
        ActualizarBarraSitios();
    });
    m_bSaltar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int i = m_sitios.Actual();
        auto e = i < 0 ? EstadoParte::Lista : m_sitios.Partes()[i].estado;
        if (e != EstadoParte::Abierta && e != EstadoParte::Espera && e != EstadoParte::Navegador) return;
        m_sitios.Saltar(i);
        GuardarCola();
        Registrar("de una en una: parte " + std::to_string(i + 1) + " saltada");
        Avanzar();
    });
    m_bParar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        size_t vuelven = 0;
        if (!DevolverYVaciar(vuelven))
        {
            ayuda::Aviso(L"No se pudo guardar el recolector, así que la cola sigue como estaba y no se perdió ninguna página. "
                         L"Prueba otra vez en un momento.",
                         L"De una en una", wxOK | wxICON_WARNING, this);
            return;
        }
        Registrar("de una en una: parada; " + std::to_string(vuelven) + " vuelven al recolector");
        if (vuelven == 0) m_resumenSitios = L"Paraste la cola.";
        else if (vuelven == 1) m_resumenSitios = L"Paraste la cola: la página que faltaba volvió al recolector.";
        else m_resumenSitios = wxString::Format(L"Paraste la cola: las %zu páginas que faltaban volvieron al recolector.", vuelven);
        ActualizarBarraSitios();
        Refrescar();
    });
    m_bCerrarSitios->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_resumenSitios.clear();
        ActualizarBarraSitios();
    });
}

void VentanaPrincipal::ActualizarBarraSitios()
{
    if (!m_barraSitios) return;
    const auto& partes = m_sitios.Partes();
    wxString texto;
    bool otraVez = false, saltar = false, activa = !partes.empty();
    if (!activa) texto = m_resumenSitios;
    else
    {
        int i = m_sitios.Actual();
        size_t total = partes.size();
        int64_t ahora = RelojEsperas();
        // «9:41» o «1:02:03»
        auto reloj = [](int64_t s) {
            s = std::max<int64_t>(0, s);
            return s >= 3600 ? wxString::Format(L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60)
                             : wxString::Format(L"%lld:%02lld", s / 60, s % 60);
        };
        wxString sitio = i >= 0 ? wxString::FromUTF8(enlaces::Sitio(partes[i].url)) : wxString();
        if (i >= 0 && partes[i].estado == EstadoParte::Abierta && partes[i].hasta > ahora && !partes[i].limite)
        {
            texto = wxString::Format(L"Parte %d de %zu en tu navegador (%s): la página pide esperar %s. Después resuelve el captcha y "
                                     L"pulsa descargar; Atarraya te avisa.",
                                     i + 1, total, sitio, reloj(partes[i].hasta - ahora));
            otraVez = saltar = true;
        }
        else if (i >= 0 && partes[i].estado == EstadoParte::Abierta)
        {
            texto = wxString::Format(L"Parte %d de %zu en tu navegador (%s): resuelve el captcha y pulsa descargar. Atarraya la toma sola.",
                                     i + 1, total, sitio);
            otraVez = saltar = true;
        }
        else if (i >= 0 && partes[i].estado == EstadoParte::Navegador)
        {
            const auto& p = partes[i];
            texto = wxString::Format(L"Parte %d de %zu bajando en tu navegador", i + 1, total);
            if (p.total > 0)
                texto += wxString::Format(L" (%d %%, %s de %s)", (int)(p.bajado * 100 / p.total), wxString(TextoTamano(p.bajado)),
                                          wxString(TextoTamano(p.total)));
            else if (p.bajado > 0) texto += L" (" + wxString(TextoTamano(p.bajado)) + L")";
            texto += L": el sitio no deja que la baje otro programa. Atarraya la anota cuando termine.";
            saltar = true;   // por ejemplo, si se cerró el navegador y esa descarga ya no sigue
        }
        else if (i >= 0 && partes[i].estado == EstadoParte::Espera)
        {
            const auto& p = partes[i];
            texto = wxString::Format(L"Parte %d de %zu (%s): el sitio pide esperar entre descargas. Atarraya abre la página otra vez en %s",
                                     i + 1, total, sitio, reloj(p.hasta - ahora));
            texto += p.intentos > 0 ? wxString::Format(L" (intento %d de %d) y te avisa.", p.intentos + 1, MaxIntentosEspera)
                                    : wxString(L" y te avisa.");
            otraVez = saltar = true;
        }
        else if (i >= 0 && partes[i].estado == EstadoParte::Tomada && partes[i].sinConfirmar)
        {
            texto = wxString::Format(L"Parte %d de %zu: no se pudo confirmar si tu navegador la sigue bajando. Atarraya pausó la suya "
                                     L"hasta saberlo; mira la lista de descargas de tu navegador.",
                                     i + 1, total);
            saltar = true;
        }
        else if (i >= 0)
        {
            texto = wxString::Format(L"Parte %d de %zu bajando en Atarraya. ", i + 1, total);
            texto += m_sitios.QuedanPorAbrir() ? L"Cuando termine, se abre la siguiente en tu navegador." : L"Ya no quedan páginas por abrir.";
        }
        else if (m_sitios.Terminada())
        {
            // Terminó, pero el recolector no se pudo guardar: las que no bajaron siguen aquí
            texto = L"Terminó la cola, pero las páginas que no bajaron no pudieron volver al recolector. Pulsa «Parar» para intentarlo otra vez.";
        }
        else
        {
            size_t quedan = m_sitios.Cuantas(EstadoParte::Esperando);
            texto = quedan == 1 ? wxString(L"Queda 1 página por abrir.") : wxString::Format(L"Quedan %zu páginas por abrir.", quedan);
            texto += L" Pulsa «Abrir la siguiente ahora» para seguir.";
        }
    }
    bool mostrar = !texto.empty();
    bool cambio = mostrar != m_barraSitios->IsShown();
    if (m_textoSitios->GetLabel() != texto)
    {
        m_textoSitios->SetLabel(texto);
        m_textoSitios->SetToolTip(texto);
        cambio = true;
    }
    auto ver = [&](wxButton* b, bool si) {
        if (b->IsShown() != si)
        {
            b->Show(si);
            cambio = true;
        }
    };
    ver(m_bOtraVez, otraVez);
    ver(m_bSiguiente, activa);
    ver(m_bSaltar, activa);
    ver(m_bParar, activa);
    ver(m_bCerrarSitios, !activa && mostrar);
    m_bSiguiente->Enable(m_sitios.QuedanPorAbrir());
    m_bSaltar->Enable(saltar);
    if (cambio)
    {
        m_barraSitios->Show(mostrar);
        m_barraSitios->GetParent()->Layout();
    }
}

void VentanaPrincipal::AbrirParte(int i, bool avisar)
{
    const auto& partes = m_sitios.Partes();
    if (i < 0 || (size_t)i >= partes.size()) return;
    std::string url = partes[i].url;
    size_t total = partes.size();
    // La abre la extensión, si está conectada: abre ella misma la pestaña y solo en esas lee los avisos de espera. Si
    // no, como antes (en esa pestaña no se leen los avisos de espera).
    int64_t orden = ++m_ultimaOrden;
    bool porLaExtension = sistema::PedirALaExtension(orden, url);
    if (porLaExtension) m_porAbrir[orden] = { i, url, GetTickCount64() };
    else sistema::AbrirPaginaWeb(url);
    Registrar("de una en una: parte " + std::to_string(i + 1) + " de " + std::to_string(total) + " abierta en el navegador ("
              + SoloSitio(url)
              + (porLaExtension ? std::string(", por la extensión)") : std::string("; la extensión no está conectada: ahí no se leen los avisos de espera)")));
    if (avisar)
        Avisar(wxString::Format(L"Parte %d de %zu en tu navegador", i + 1, total), L"Resuelve el captcha y pulsa descargar. Atarraya la toma sola.");
}

void VentanaPrincipal::RevisarPausas()
{
    // Después de un cierre de golpe, una «sin confirmar» puede haber vuelto a la cola: se pausa otra vez antes de que
    // arranque. Y una ya confirmada se reanuda hasta que de verdad sigue (mientras su pausa no termina, reanudar no hace
    // nada): así nunca quedan bajando las dos ni la parte detenida.
    bool cambio = false;
    const auto& partes = m_sitios.Partes();
    for (size_t i = 0; i < partes.size(); i++)
    {
        const auto& p = partes[i];
        if (p.estado != EstadoParte::Tomada || (!p.sinConfirmar && !p.reanudar)) continue;
        hnd::Foto f;
        if (!m_motor.Buscar(p.descarga, f)) continue;   // la persona la quitó: RevisarPartes la salta
        bool antes = p.reanudar;
        AccionPausa a = m_sitios.RevisarPausa((int)i, f.datos.estado, f.pausando);
        if (a == AccionPausa::Pausar)
        {
            m_motor.Pausar(p.descarga);
            Registrar("de una en una: parte " + std::to_string(i + 1) + ": sigue sin confirmar; su descarga vuelve a la pausa");
        }
        else if (a == AccionPausa::Reanudar) m_motor.Reanudar(p.descarga);
        cambio = cambio || antes != m_sitios.Partes()[i].reanudar;
    }
    if (cambio) GuardarCola();
}

void VentanaPrincipal::RevisarPorAbrir()
{
    uint64_t ahora = GetTickCount64();
    for (auto it = m_porAbrir.begin(); it != m_porAbrir.end();)
    {
        if (ahora - it->second.desde < 6000)
        {
            ++it;
            continue;
        }
        // La extensión recibió la orden pero no contestó que abrió la página: se abre como antes
        Registrar("de una en una: parte " + std::to_string(it->second.parte + 1)
                  + ": la extensión no abrió la página; se abre como antes (ahí no se leen los avisos de espera)");
        sistema::AbrirPaginaWeb(it->second.url);
        it = m_porAbrir.erase(it);
    }
}

void VentanaPrincipal::Avanzar()
{
    if (m_sitios.Terminada())
    {
        size_t total = m_sitios.Partes().size(), listas = m_sitios.Cuantas(EstadoParte::Lista), vuelven = 0;
        // Las saltadas y las que fallaron vuelven al recolector, para intentarlas después
        if (!DevolverYVaciar(vuelven))
        {
            Avisar(L"De una en una", L"No se pudo guardar el recolector: las páginas que no bajaron siguen en la cola. Pulsa «Parar» "
                                     L"para intentarlo otra vez.", true);
            ActualizarBarraSitios();
            return;
        }
        if (listas == total) m_resumenSitios = total == 1 ? wxString(L"Listo: bajó la única parte.") : wxString::Format(L"Listo: bajaron las %zu partes.", total);
        else
        {
            m_resumenSitios = wxString::Format(L"Listo: bajaron %zu de %zu partes. ", listas, total);
            m_resumenSitios += vuelven == 1 ? wxString(L"La otra volvió al recolector, para intentarla después.")
                                            : wxString::Format(L"Las otras %zu volvieron al recolector, para intentarlas después.", vuelven);
        }
        Registrar("de una en una: terminó (" + std::to_string(listas) + " de " + std::to_string(total) + " listas)");
        Avisar(L"De una en una: listo", m_resumenSitios);
        ActualizarBarraSitios();
        return;
    }
    // Mientras una espera (el sitio pidió esperar entre descargas), no se abre otra: la siguiente pediría lo mismo
    if (!m_sitios.HayAbierta() && !m_sitios.HayEnEspera())
    {
        int i = m_sitios.AbrirSiguiente();
        if (i >= 0)
        {
            GuardarCola();
            AbrirParte(i, true);
        }
    }
    ActualizarBarraSitios();
}

void VentanaPrincipal::TomarParte(int i, DatosDescarga d, const std::string& toma)
{
    const auto& p = m_sitios.Partes()[i];
    d.carpeta = p.carpeta.empty() ? m_ajustes.CarpetaEfectiva() : p.carpeta;
    d.paquete = p.paquete;
    d.grupo = p.grupo;
    SHCreateDirectoryExW(nullptr, d.carpeta.c_str(), nullptr);
    // Una sola conexión, como el navegador: así no se pasa de lo que el sitio deja bajar sin cuenta premium
    d.conexiones = 1;
    uint64_t id = m_motor.Agregar(d, true);
    // Con «toma», el navegador sigue bajando la suya hasta que se decide quién la baja (lo pregunta la extensión). Se
    // guarda en la cola: si Atarraya se cierra a la mitad, al abrirlo queda la del navegador y nunca bajan las dos.
    if (!toma.empty()) m_sitios.Probar(i, id, toma, AhoraUnix());
    else m_sitios.Tomar(i, id);
    GuardarCola();
    size_t total = m_sitios.Partes().size();
    if (!toma.empty())
    {
        Registrar("de una en una: parte " + std::to_string(i + 1) + " de " + std::to_string(total) + ": Atarraya prueba seguirla ("
                  + SoloSitio(d.url) + ")");
    }
    else
    {
        Registrar("de una en una: parte " + std::to_string(i + 1) + " de " + std::to_string(total) + " tomada (" + SoloSitio(d.url) + ")");
        Avisar(wxString::Format(L"Parte %d de %zu: la baja Atarraya", i + 1, total),
               m_sitios.QuedanPorAbrir() ? L"Cuando termine, se abre la siguiente en tu navegador." : L"Ya no quedan páginas por abrir.");
    }
    ActualizarBarraSitios();
    Refrescar();
}

void VentanaPrincipal::TomaANavegador(int i, bool recordarSitio)
{
    const auto& partes = m_sitios.Partes();
    if (i < 0 || (size_t)i >= partes.size() || partes[i].estadoToma != EstadoToma::Probando) return;
    std::string pagina = partes[i].url;
    // La descarga de Atarraya se quita (no llegó nada del archivo, o se le pidió soltarla); la del navegador sigue y la
    // extensión la sigue: una sola descarga de esa parte
    uint64_t quitar = m_sitios.Decidir(i, false);
    if (quitar) m_motor.Quitar(quitar);
    GuardarCola();
    if (recordarSitio) m_sitiosNavegador.Anotar(pagina, AhoraUnix());
    Registrar("de una en una: parte " + std::to_string(i + 1) + ": Atarraya no pudo seguir la descarga del navegador"
              + (recordarSitio ? "; la próxima vez de " + SoloSitio(pagina) + " la deja en el navegador" : std::string())
              + ". La baja el navegador y la extensión avisa cuando termine.");
    ActualizarBarraSitios();
    Refrescar();
}

void VentanaPrincipal::RevisarTomas()
{
    int64_t ahora = AhoraUnix();
    std::vector<std::pair<int, bool>> alNavegador;
    const auto& partes = m_sitios.Partes();
    for (size_t i = 0; i < partes.size(); i++)
    {
        const auto& p = partes[i];
        if (p.estadoToma != EstadoToma::Probando || p.estado != EstadoParte::Tomada) continue;
        hnd::Foto f;
        bool hay = m_motor.Buscar(p.descarga, f);
        Toma nueva = hay ? DecidirToma(f.datos.estado, f.hecho, ahora - p.tomaDesde) : Toma::Navegador;
        if (nueva == Toma::Tomada)
        {
            m_sitios.Decidir((int)i, true);
            GuardarCola();
            Registrar("de una en una: parte " + std::to_string(i + 1) + " tomada: Atarraya siguió la descarga del navegador");
            Avisar(wxString::Format(L"Parte %d de %zu: la baja Atarraya", (int)i + 1, partes.size()),
                   m_sitios.QuedanPorAbrir() ? L"Cuando termine, se abre la siguiente en tu navegador." : L"Ya no quedan páginas por abrir.");
        }
        // El sitio no la deja seguir (página, 403, 410, enlace usado): se recuerda el sitio. Si solo tardó, no.
        else if (nueva == Toma::Navegador) alNavegador.push_back({ (int)i, hay && f.datos.estado == Estado::Error });
    }
    for (auto& [i, recordar] : alNavegador) TomaANavegador(i, recordar);
}

bool VentanaPrincipal::PreguntarSiEsParte(int i, const std::string& url, const std::wstring& nombre)
{
    const auto& p = m_sitios.Partes()[i];
    wxString archivo = nombre.empty() ? wxString(NombreDeUrl(url)) : wxString(nombre);
    wxString texto = L"Llegó una descarga de " + wxString::FromUTF8(enlaces::Sitio(url));
    if (archivo.Find(L'.') != wxNOT_FOUND) texto += L": «" + archivo + L"»";   // solo si parece un nombre de archivo
    texto += wxString::Format(L".\n\n¿Es la parte %d de %zu", i + 1, m_sitios.Partes().size());
    if (p.nombre.find(L'.') != std::wstring::npos) texto += L" («" + wxString(p.nombre) + L"»)";
    texto += L", la que abrió Atarraya en tu navegador?\n\nSi no lo es, sigue como cualquier descarga.";
    wxMessageDialog d(IsShown() ? this : nullptr, texto, L"De una en una", wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION);
    d.SetYesNoLabels(wxString::Format(L"Sí, es la parte %d", i + 1), L"No, es otra descarga");
    bool si = d.ShowModal() == wxID_YES;
    Registrar("de una en una: se preguntó si una descarga de " + SoloSitio(url) + " era la parte " + std::to_string(i + 1) + ": "
              + (si ? "sí" : "no"));
    return si;
}

bool VentanaPrincipal::DevolverYVaciar(size_t& devueltas)
{
    auto vuelven = m_sitios.Sobrantes();
    devueltas = vuelven.size();
    // 1. Primero el recolector, con las páginas que vuelven (si falla, la cola queda como estaba)
    if (!GuardarEnRecolector(vuelven, m_recolector))
    {
        Registrar("de una en una: no se pudo guardar el recolector; la cola sigue como estaba");
        return false;
    }
    if (m_ventanaRecolector) m_ventanaRecolector->Refrescar();
    // 2. Después la cola vacía. Si esto falla, las páginas quedan repetidas en los dos lados, pero no se pierden
    if (!m_sitios.Vaciar()) GuardarCola();
    return true;
}

void VentanaPrincipal::GuardarCola()
{
    if (m_sitios.Guardar())
    {
        m_colaSinGuardar = false;
        return;
    }
    Registrar("de una en una: no se pudo guardar la cola");
    if (m_colaSinGuardar) return;   // ya se avisó
    m_colaSinGuardar = true;
    Avisar(L"De una en una", L"No se pudo guardar la cola. Si cierras Atarraya ahora, puede que no siga donde iba.", true);
}

void VentanaPrincipal::MandarDeUnaEnUna(const std::vector<EnlaceRecolectado>& sitios, const std::wstring& carpeta, bool abrir)
{
    std::vector<const EnlaceRecolectado*> lista;
    for (const auto& e : sitios) lista.push_back(&e);
    if (EmpezarDeUnaEnUna(lista, carpeta, false, abrir, IsShown() ? (wxWindow*)this : nullptr)) return;
    // No empezó (por ejemplo, para conectar antes la extensión): quedan en el recolector, donde «Descargar los
    // marcados» los manda otra vez
    std::vector<std::string> urls;
    for (const auto& e : sitios) urls.push_back(e.url);
    m_recolector.Agregar(urls, L"", sitios.empty() ? std::string() : sitios.front().referer, "agregar", true);
    m_recolector.Guardar();
    AbrirRecolector(false);
}

void VentanaPrincipal::OfrecerAbrirPaginas()
{
    if (m_saliendo || m_ofreciendoPaginas || m_paginasPorAbrir.empty()) return;
    m_ofreciendoPaginas = true;   // mientras está la pregunta, las que lleguen esperan a la siguiente
    auto ids = std::move(m_paginasPorAbrir);
    m_paginasPorAbrir.clear();
    std::vector<std::pair<wxString, std::string>> paginas;   // nombre y dirección
    for (auto id : ids)
    {
        hnd::Foto f;
        if (m_motor.Buscar(id, f) && f.datos.estado == Estado::Error && f.datos.error == TextoRespondioPagina)
            paginas.push_back({ NombreVisible(f.datos), f.datos.url });
    }
    if (!paginas.empty())
    {
        bool una = paginas.size() == 1;
        wxString texto = una ? L"«" + paginas[0].first + L"»: el sitio respondió con una página, no con el archivo, así que Atarraya no la "
                                   L"guardó como si fuera el archivo."
                             : wxString::Format(L"%zu descargas: los sitios respondieron con una página, no con el archivo, así que Atarraya "
                                                L"no las guardó como si fueran los archivos.", paginas.size());
        texto += una ? L"\n\nÁbrela en el navegador: puede que pida un captcha o que pulses descargar."
                     : L"\n\nÁbrelas en el navegador: puede que pidan un captcha o que pulses descargar.";
        texto += L" Si tienes la extensión de Atarraya, la descarga llega aquí sola.";
        wxMessageDialog p(IsShown() ? this : nullptr, texto, L"El sitio respondió con una página", wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
        p.SetYesNoLabels(L"Abrir en el navegador", L"Ahora no");
        bool abrir = p.ShowModal() == wxID_YES;
        Registrar("respondió con una página: " + std::to_string(paginas.size()) + (abrir ? ", se abren en el navegador" : ", ahora no"));
        for (size_t i = 0; abrir && i < paginas.size() && i < 10; i++) sistema::AbrirPaginaWeb(paginas[i].second);
    }
    m_ofreciendoPaginas = false;
    if (!m_paginasPorAbrir.empty()) CallAfter([this]() { OfrecerAbrirPaginas(); });
}

void VentanaPrincipal::AbrirElegidasEnNavegador()
{
    for (auto id : Seleccion())
    {
        hnd::Foto f;
        if (BuscarFoto(id, f) && !f.torrent && f.datos.estado == Estado::Error && f.datos.error == TextoRespondioPagina)
            sistema::AbrirPaginaWeb(f.datos.url);
    }
}

void VentanaPrincipal::RevisarPartes()
{
    bool cambio = false;
    std::vector<DatosDescarga> listas;
    const auto& partes = m_sitios.Partes();
    for (size_t i = 0; i < partes.size(); i++)
    {
        if (partes[i].estado != EstadoParte::Tomada) continue;
        hnd::Foto f;
        if (!m_motor.Buscar(partes[i].descarga, f))
        {
            m_sitios.Saltar((int)i);   // la persona quitó su descarga de la lista
            cambio = true;
        }
        else if (f.datos.estado == Estado::Completada)
        {
            // Terminó sin que la cola se enterara (un cierre de golpe). Si no se guardó cómo era su archivo al terminar,
            // no se toma el de ahora como si fuera ese: esa parte nunca se manda a la Papelera.
            m_sitios.Terminar(partes[i].descarga, true);
            listas.push_back(f.datos);
            cambio = true;
        }
    }
    if (cambio)
    {
        // Antes de que la cola avance (y se vacíe si terminó): así se sabe si quedó completo su paquete
        for (const auto& d : listas) EncolarJicaro(PrepararJicaro(d));
        GuardarCola();
        Avanzar();
    }
}

void VentanaPrincipal::PedidoNavegador(const std::string& toma, const std::string& estado, const std::string& url,
                                       const std::string& referer, const std::string& pagina, const std::wstring& nombre,
                                       int64_t bajado, int64_t total, const std::string& ruta)
{
    if (toma.empty()) return;
    int i = m_sitios.BuscarToma(toma);
    if (i >= 0 && m_sitios.Partes()[i].estado == EstadoParte::Fallo)
    {
        // Ya se sabía que no terminó: la extensión repite el aviso final hasta que Atarraya lo confirma
        if (estado != "bajando" && estado != "completa") return;
        // Se había cortado y la persona la reanudó en el navegador: sigue siendo de la parte (si ya se había anotado lo
        // que bajó, es el mismo aviso otra vez)
        if (!m_sitios.Partes()[i].finalNavegador)
        {
            m_sitios.EnNavegador(i, toma);
            GuardarCola();
            Registrar("de una en una: parte " + std::to_string(i + 1) + ": se reanudó en el navegador");
        }
    }
    if (i < 0 && (i = m_sitios.ParteDeToma(toma)) >= 0)
    {
        // El navegador sigue con una que Atarraya había tomado o estaba probando (no se pudo cancelar, o la extensión no
        // supo de Atarraya a tiempo): queda la del navegador y Atarraya quita la suya, para que nunca bajen las dos
        const auto& p = m_sitios.Partes()[i];
        if (p.estado != EstadoParte::Tomada || (estado != "bajando" && estado != "completa")) return;
        uint64_t quitar = m_sitios.NavegadorSiguio(i);
        if (quitar) m_motor.Quitar(quitar);
        GuardarCola();
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": el navegador siguió con la suya; Atarraya quita la suya");
    }
    else if (i < 0)
    {
        // Una que la extensión dejó en el navegador desde el principio (un sitio donde Atarraya no puede seguirla):
        // es de la cola solo si se sabe, sin duda, de qué parte es
        if (estado != "bajando" && estado != "completa") return;
        auto e = m_sitios.Emparejar(url, referer, pagina, nombre);
        if (e.parte < 0 || !e.segura || m_sitios.Partes()[e.parte].estado == EstadoParte::Tomada) return;
        i = e.parte;
        m_sitios.EnNavegador(i, toma);
        GuardarCola();
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": la baja el navegador (" + SoloSitio(url) + ")");
    }
    size_t cuantas = m_sitios.Partes().size();
    if (estado == "bajando")
    {
        m_sitios.Progreso(i, bajado, total);
        ActualizarBarraSitios();
        return;
    }
    if (estado == "completa")
    {
        TerminarParteDelNavegador(i, ruta, bajado > 0 ? bajado : total);
        return;
    }
    if (estado == "fallo" || estado == "cancelada")
    {
        if (m_sitios.Partes()[i].estado != EstadoParte::Navegador) return;   // el mismo aviso otra vez
        m_sitios.TerminarEnNavegador(i, false, 0);
        GuardarCola();
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": el navegador no la terminó (" + estado + ")");
        Avisar(wxString::Format(L"Parte %d de %zu: no terminó", i + 1, cuantas),
               L"El navegador no terminó de bajarla. Al final vuelve al recolector, para intentarla otra vez.", true);
        Avanzar();
        Refrescar();
    }
}

void VentanaPrincipal::TerminarParteDelNavegador(int i, const std::string& ruta, int64_t tamano)
{
    const Parte p = m_sitios.Partes()[i];
    if (p.finalNavegador && !p.rutaNavegador.empty())
    {
        // El mismo aviso otra vez (la extensión lo repite hasta que Atarraya lo confirma): ya está anotado
        if (p.estado == EstadoParte::Navegador) MoverParteDelNavegador(i);
        return;
    }
    // 1. Al llegar el aviso: el archivo que dice el navegador, revisado (sin enlaces en la ruta, un archivo común, del
    //    tamaño que bajó) y con su identidad anotada y guardada
    IdentidadArchivo identidad;
    std::string motivo;
    std::wstring origen = Ancho(ruta);
    if (!LeerArchivoDelNavegador(origen, identidad, motivo) || (tamano > 0 && identidad.tamano != tamano) || identidad.tamano <= 0)
    {
        if (motivo.empty()) motivo = identidad.tamano <= 0 ? "el archivo está vacío" : "el archivo no tiene el tamaño que bajó el navegador";
        NoSeMovio(i, motivo);
        return;
    }
    m_sitios.AnotarArchivoDelNavegador(i, origen, identidad);
    GuardarCola();
    Registrar("de una en una: parte " + std::to_string(i + 1) + ": el navegador terminó; su archivo queda anotado");
    // 2. Pasarlo a la carpeta del paquete (si Atarraya se cierra antes, se termina al volver a abrirlo)
    MoverParteDelNavegador(i);
}

void VentanaPrincipal::NoSeMovio(int i, const std::string& motivo)
{
    size_t cuantas = m_sitios.Partes().size();
    m_sitios.TerminarEnNavegador(i, false, 0);
    GuardarCola();
    Registrar("de una en una: parte " + std::to_string(i + 1) + ": lo que bajó el navegador no se movió: " + motivo);
    Avisar(wxString::Format(L"Parte %d de %zu: no se movió", i + 1, cuantas),
           L"El archivo que bajó el navegador no se pasó a la carpeta del paquete: " + wxString::FromUTF8(motivo)
               + L". Se queda donde estaba; al final la parte vuelve al recolector.",
           true);
    Avanzar();
    Refrescar();
}

void VentanaPrincipal::MoverParteDelNavegador(int i)
{
    const Parte p = m_sitios.Partes()[i];
    if (p.estado != EstadoParte::Navegador || p.rutaNavegador.empty()) return;
    // A la carpeta de las otras partes del paquete, para que Jícaro las encuentre juntas: solo si sigue siendo el mismo
    // archivo, sin enlaces en ninguna ruta y sin pisar nada
    std::wstring carpeta = p.carpeta.empty() ? m_ajustes.CarpetaEfectiva() : p.carpeta;
    SHCreateDirectoryExW(nullptr, carpeta.c_str(), nullptr);
    std::wstring final;
    std::string motivo;
    Movida r = MoverArchivoDelNavegador(p.rutaNavegador, p.identidadNavegador, carpeta, final, motivo);
    if (r != Movida::Movida && r != Movida::OtroDisco)
    {
        NoSeMovio(i, motivo);
        return;
    }
    size_t cuantas = m_sitios.Partes().size();
    if (r == Movida::OtroDisco)
        Avisar(wxString::Format(L"Parte %d de %zu: en otro disco", i + 1, cuantas),
               L"El navegador la dejó en otro disco, así que Atarraya no la movió (sería copiarla entera). Para descomprimir el paquete, "
               L"junta sus partes en una carpeta.");
    DatosDescarga d;
    d.url = p.url;
    d.referer = p.url;
    auto b = final.find_last_of(L'\\');
    d.carpeta = final.substr(0, b);
    d.nombre = final.substr(b + 1);
    d.nombreFijo = true;
    d.tamano = p.identidadNavegador.tamano;
    d.paquete = p.paquete;
    d.grupo = p.grupo;
    uint64_t id = m_motor.AgregarTerminada(d);
    m_motor.FijarIdentidad(id, p.identidadNavegador);   // el mismo archivo (al moverlo no cambia)
    m_sitios.TerminarEnNavegador(i, true, id);
    GuardarCola();
    Registrar("de una en una: parte " + std::to_string(i + 1) + " lista: la bajó el navegador"
              + (r == Movida::OtroDisco ? std::string(" (quedó en otro disco)") : std::string(" y está en la carpeta del paquete")));
    if (m_ajustes.avisarAlTerminar) Avisar(L"Descarga terminada", wxString(d.nombre) + L"\nLa bajó el navegador; ya está en su paquete.");
    hnd::Foto f;
    if (m_motor.Buscar(id, f)) EncolarJicaro(PrepararJicaro(f.datos));
    Avanzar();
    Refrescar();
}

void VentanaPrincipal::PedidoEspera(const std::string& paginaCola, const std::string& clase, int64_t segundos)
{
    int i = m_sitios.ParteDePagina(paginaCola);
    if (i < 0) return;
    bool limite = clase == "limite";
    if (!limite && clase != "pagina") return;
    if (segundos > 7 * 24 * 3600) segundos = 7 * 24 * 3600;
    const auto& p = m_sitios.Partes()[i];
    bool yaEsperaba = p.estado == EstadoParte::Espera;
    size_t cuantas = m_sitios.Partes().size();
    std::string cuanto = segundos < 0 ? std::string("sin decir cuánto") : std::to_string(segundos) + " s";
    if (!m_sitios.Esperar(i, limite, segundos, RelojEsperas()))
    {
        GuardarCola();
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": el sitio sigue pidiendo esperar después de "
                  + std::to_string(MaxIntentosEspera) + " intentos; queda para después");
        Avisar(wxString::Format(L"Parte %d de %zu: queda para después", i + 1, cuantas),
               L"El sitio sigue pidiendo esperar. Al final vuelve al recolector, para intentarla otra vez más tarde.", true);
        Avanzar();
        return;
    }
    GuardarCola();
    if (limite)
    {
        if (!yaEsperaba)
        {
            Registrar("de una en una: parte " + std::to_string(i + 1) + ": el sitio pide esperar entre descargas (" + cuanto + ")");
            wxDateTime cuando((time_t)m_sitios.Partes()[i].hasta);
            Avisar(wxString::Format(L"Parte %d de %zu: el sitio pide esperar", i + 1, cuantas),
                   L"Atarraya abre la página otra vez a las " + cuando.Format(L"%H:%M") + L" y te avisa. No hace falta que hagas nada.");
        }
    }
    else Registrar("de una en una: parte " + std::to_string(i + 1) + ": la página tiene una cuenta regresiva (" + cuanto + ")");
    ActualizarBarraSitios();
}

void VentanaPrincipal::RevisarEsperas()
{
    int64_t ahora = RelojEsperas();
    size_t cuantas = m_sitios.Partes().size();
    auto vencidas = m_sitios.Vencidas(ahora);
    auto cuentas = m_sitios.CuentasTerminadas(ahora);
    if (vencidas.empty() && cuentas.empty()) return;
    GuardarCola();
    for (int i : vencidas)
    {
        // Terminó la espera que pidió el sitio (más un margen): se abre otra vez su página y se avisa con sonido
        AbrirParte(i, false);
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": terminó la espera; se abre otra vez (intento "
                  + std::to_string(m_sitios.Partes()[i].intentos) + ")");
        sistema::Sonar();
        Avisar(L"Toca resolver un captcha", wxString::Format(L"Parte %d de %zu: Atarraya abrió otra vez la página en tu navegador.", i + 1, cuantas));
    }
    for (int i : cuentas)
    {
        Registrar("de una en una: parte " + std::to_string(i + 1) + ": terminó la cuenta regresiva de la página");
        sistema::Sonar();
        Avisar(L"Toca resolver un captcha", wxString::Format(L"Parte %d de %zu: terminó la espera de la página.", i + 1, cuantas));
    }
    ActualizarBarraSitios();
}

std::string VentanaPrincipal::Consulta(const std::string& texto)
{
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(texto);
    }
    catch (...)
    {
        return "{}";
    }
    if (!j.is_object()) return "{}";
    auto campo = [&](const char* nombre) {
        auto it = j.find(nombre);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    std::string tipo = campo("tipo");
    const auto& partes = m_sitios.Partes();
    nlohmann::json r = nlohmann::json::object();
    if (tipo == "emparejar")
    {
        // ¿Es de una parte? La extensión solo deja la descarga en el navegador mientras Atarraya prueba si es seguro
        std::string paginaCola = campo("paginaCola");
        std::wstring nombre = Ancho(campo("nombre"));
        auto e = m_sitios.Emparejar(campo("url"), campo("referer"), paginaCola.empty() ? campo("pagina") : paginaCola, nombre);
        r["parte"] = e.parte;
        r["segura"] = e.segura;
        r["navegador"] = e.parte >= 0 && m_sitiosNavegador.Necesita(partes[e.parte].url);
    }
    else if (tipo == "toma")
    {
        // Quién baja la descarga del navegador que Atarraya probaba seguir, y si Atarraya todavía espera un aviso sobre
        // ella («espera»: la extensión guarda su aviso final y lo repite hasta que ya no). «ajena»: no es de una parte,
        // la baja el navegador.
        std::string toma = campo("toma");
        auto e = m_sitios.EstadoDeToma(toma);
        if (m_sitios.ParteDeToma(toma) < 0 && m_tomasSinParte.count(toma)) e.estado = "ajena";
        r["estado"] = e.estado;
        r["espera"] = e.espera;
        r["sinConfirmar"] = e.sinConfirmar;
    }
    else r["error"] = "consulta desconocida";
    return r.dump();
}

// --- Jícaro ---------------------------------------------------------------------------------------------------------
// Atarraya solo le pasa a Jícaro archivos que bajó, con las rutas revisadas y entre comillas. Nunca borra las partes:
// como mucho ofrece «Borrar las partes» (a la Papelera) cuando Jícaro terminó bien, y solo si la persona lo pulsa.

std::vector<VentanaPrincipal::TrabajoJicaro> VentanaPrincipal::PrepararJicaro(const DatosDescarga& d)
{
    std::vector<TrabajoJicaro> trabajos;
    if (!m_ajustes.jicaroAlTerminar || d.estado != Estado::Completada || d.nombre.empty()) return trabajos;
    std::wstring exe = jicaro::Buscar(m_ajustes.rutaJicaro);
    if (exe.empty()) return trabajos;   // sin Jícaro no se hace nada (en Ajustes, la casilla se ve apagada)

    std::vector<jicaro::ParteBajada> propias;   // solo las descargas de este paquete (por id), o el comprimido suelto
    std::string clave;
    if (!d.grupo.empty())
    {
        // Tienen que haber terminado todas las del paquete, también las de «de una en una» que todavía no se bajan
        propias = jicaro::ArchivosDelPaquete(d.grupo, m_motor.Fotos(), m_sitios.Partes());
        if (propias.empty()) return trabajos;
        clave = "paquete " + d.grupo;
    }
    else
    {
        // Suelta: solo un .rar, .zip o .7z entero. De unas partes sueltas no se sabe si ya llegaron todas (tampoco de
        // un «peli.rar» que es la primera parte del estilo viejo).
        std::wstring ruta = d.carpeta + L"\\" + d.nombre;
        if (!jicaro::EsComprimidoSuelto(d.nombre) || jicaro::EsVolumenRar(ruta)) return trabajos;
        propias.push_back({ d.id, ruta, d.identidad });
        clave = "descarga " + std::to_string(d.id);
    }
    // Una sola vez por paquete (por ejemplo, si dos partes terminaron en el mismo momento)
    if (!m_jicaroVistos.insert(clave).second) return trabajos;

    // Las carpetas que ya eligieron otros trabajos (puede que todavía no existan): no se repiten
    auto reservar = [&](const std::wstring& carpeta, const std::wstring& nombre) {
        std::wstring destino = jicaro::CarpetaDestino(carpeta, nombre);
        for (int i = 2; !destino.empty() && m_destinosJicaro.count(Minusculas(destino)) && i < 1000; i++)
            destino = jicaro::CarpetaDestino(carpeta, nombre + L" (" + std::to_wstring(i) + L")");
        if (!destino.empty()) m_destinosJicaro.insert(Minusculas(destino));
        return destino;
    };
    // Un paquete con nombre propio va todo a una carpeta con ese nombre, al lado de sus partes; si Atarraya no sabía el
    // nombre («Páginas de ...»), cada comprimido va a una con el suyo
    bool unaCarpeta = !d.paquete.empty() && jicaro::NombreDeCarpeta(d.paquete, L"") == d.paquete;
    std::map<std::wstring, std::wstring> comunes;   // carpeta de las partes → carpeta para lo descomprimido
    for (auto& plan : jicaro::Planear(propias, d.paquete))
    {
        TrabajoJicaro t;
        t.exe = exe;
        t.nombre = plan.nombre;
        t.primera = plan.primera;
        t.partes = std::move(plan.partes);
        if (unaCarpeta)
        {
            auto& comun = comunes[Minusculas(plan.carpeta)];
            if (comun.empty()) comun = reservar(plan.carpeta, plan.nombre);
            t.destino = comun;
        }
        else t.destino = reservar(plan.carpeta, plan.nombre);
        if (!t.destino.empty()) trabajos.push_back(std::move(t));
    }
    if (!trabajos.empty())
        Registrar("jícaro: terminó " + std::string(d.grupo.empty() ? "un comprimido suelto" : "un paquete") + "; "
                  + std::to_string(trabajos.size()) + (trabajos.size() == 1 ? " comprimido" : " comprimidos") + " para descomprimir");
    return trabajos;
}

void VentanaPrincipal::AplicarTanda(const jicaro::Tanda& tanda)
{
    for (const auto& e : tanda.Entran()) m_motor.CambiarGrupo(e.descarga, e.paquete, e.grupo);
    bool cola = false;
    for (const auto& u : tanda.Uniones())
    {
        m_motor.UnirGrupos(u.viejo, u.nuevo, u.paquete);
        cola = m_sitios.CambiarGrupo(u.viejo, u.nuevo) || cola;
    }
    if (cola) GuardarCola();
    if (!tanda.Entran().empty() || !tanda.Uniones().empty())
        Registrar("jícaro: " + std::to_string(tanda.Entran().size()) + " descargas que ya estaban entran a su paquete; "
                  + std::to_string(tanda.Uniones().size()) + " paquetes se juntan");
}

void VentanaPrincipal::GuardarIdentidad(uint64_t descarga, const std::wstring& ruta)
{
    IdentidadArchivo i;
    if (LeerIdentidad(ruta, i)) m_motor.FijarIdentidad(descarga, i);
}

void VentanaPrincipal::EncolarJicaro(std::vector<TrabajoJicaro> trabajos)
{
    if (trabajos.empty() || m_saliendo) return;
    for (auto& t : trabajos) m_colaJicaro.push_back(std::move(t));
    SiguienteJicaro();
}

void VentanaPrincipal::SiguienteJicaro()
{
    while (!m_jicaroOcupado && !m_colaJicaro.empty() && !m_saliendo)
    {
        // Espera a que Windows termine de marcar sus partes como bajadas de internet (al terminar, se llama otra vez)
        const auto& siguiente = m_colaJicaro.front().partes;
        if (std::any_of(siguiente.begin(), siguiente.end(), [this](const auto& p) { return m_marcando.count(Minusculas(p.ruta)) > 0; }))
            break;
        TrabajoJicaro t = std::move(m_colaJicaro.front());
        m_colaJicaro.pop_front();
        // El archivo que quedó al terminar cada parte (lo guardó al marcarla): «Borrar las partes» solo toca esos mismos
        for (auto& p : t.partes)
        {
            hnd::Foto f;
            if (m_motor.Buscar(p.descarga, f)) p.identidad = f.datos.identidad;
        }
        std::wstring orden = jicaro::ArmarOrden(t.exe, t.primera, t.destino);
        void* proceso = !orden.empty() && ExisteArchivo(t.primera) ? jicaro::Abrir(t.exe, orden) : nullptr;
        if (!proceso)
        {
            Registrar(orden.empty() ? "jícaro: una ruta no se puede pasar a Jícaro; no se abrió" : "jícaro: no se pudo abrir");
            MostrarResultadoJicaro(std::move(t), jicaro::Resultado::NoAbrio);
            continue;
        }
        m_jicaroOcupado = true;
        m_jicaroActual = t;
        Registrar("jícaro: descomprimiendo (" + std::to_string(t.partes.size()) + (t.partes.size() == 1 ? " archivo)" : " partes)"));
        // Un hilo espera a que Jícaro se cierre y le pasa su código a la ventana
        VentanaPrincipal* yo = this;
        hilos::Vivo vivo = m_vivo;
        std::thread([proceso, yo, vivo]() {
            HANDLE h = (HANDLE)proceso;
            WaitForSingleObject(h, INFINITE);
            DWORD codigo = 1;
            if (!GetExitCodeProcess(h, &codigo)) codigo = 1;
            CloseHandle(h);
            hilos::Enviar([yo, vivo, codigo]() {
                if (*vivo) yo->AlTerminarJicaro(codigo);
            });
        }).detach();
    }
    ActualizarBarraJicaro();
}

void VentanaPrincipal::AlTerminarJicaro(unsigned long codigo)
{
    if (!m_jicaroOcupado) return;
    m_jicaroOcupado = false;
    Registrar("jícaro: terminó con el código " + std::to_string(codigo));
    TrabajoJicaro t = std::move(m_jicaroActual);
    m_jicaroActual = TrabajoJicaro();
    MostrarResultadoJicaro(std::move(t), jicaro::LeerCodigo(codigo));
    SiguienteJicaro();
}

void VentanaPrincipal::MostrarResultadoJicaro(TrabajoJicaro t, jicaro::Resultado r)
{
    ResultadoJicaro res;
    res.r = r;
    res.texto = jicaro::Texto(r, t.nombre, t.destino);
    res.t = std::move(t);
    bool bien = r == jicaro::Resultado::Listo || r == jicaro::Resultado::Parcial;
    if (m_ajustes.avisarAlTerminar || !bien) Avisar(jicaro::Titulo(r), res.texto, !bien);
    m_resultadosJicaro.push_back(std::move(res));
    while (m_resultadosJicaro.size() > 20) m_resultadosJicaro.pop_front();
    ActualizarBarraJicaro();
}

void VentanaPrincipal::CrearBarraJicaro(wxWindow* padre)
{
    m_barraJicaro = new wxPanel(padre);
    bool oscuro = TemaOscuro();
    m_barraJicaro->SetBackgroundColour(oscuro ? wxColour(0x1B, 0x2F, 0x3D) : wxColour(0xE3, 0xF1, 0xFB));
    auto* caja = new wxBoxSizer(wxVERTICAL);
    m_textoJicaro = new wxStaticText(m_barraJicaro, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_textoJicaro->SetForegroundColour(oscuro ? wxColour(0xD5, 0xE8, 0xF5) : wxColour(0x0B, 0x2E, 0x45));
    caja->Add(m_textoJicaro, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    auto* botones = new wxBoxSizer(wxHORIZONTAL);
    auto boton = [&](const wxString& texto, const wxString& ayuda) {
        auto* b = new wxButton(m_barraJicaro, wxID_ANY, texto);
        b->SetToolTip(ayuda);
        botones->Add(b, 0, wxRIGHT, FromDIP(6));
        return b;
    };
    m_bCarpetaJicaro = boton(L"Abrir la carpeta", L"Abre la carpeta con lo que sacó Jícaro");
    m_bBorrarPartes = boton(L"Borrar las partes", L"Manda a la Papelera las partes que bajó Atarraya (desde ahí se pueden recuperar)");
    m_bCerrarJicaro = boton(L"Cerrar", L"Quitar este aviso");
    caja->Add(botones, 0, wxALL, FromDIP(8));
    m_barraJicaro->SetSizer(caja);
    m_barraJicaro->Hide();

    m_bCarpetaJicaro->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_resultadosJicaro.empty() && ExisteCarpeta(m_resultadosJicaro.front().t.destino)) sistema::Abrir(m_resultadosJicaro.front().t.destino);
    });
    m_bBorrarPartes->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BorrarPartes(); });
    m_bCerrarJicaro->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_resultadosJicaro.empty()) m_resultadosJicaro.pop_front();
        ActualizarBarraJicaro();
    });
}

void VentanaPrincipal::ActualizarBarraJicaro()
{
    if (!m_barraJicaro) return;
    wxString texto;
    bool carpeta = false, borrar = false, cerrar = false;
    if (!m_resultadosJicaro.empty())
    {
        const auto& r = m_resultadosJicaro.front();
        texto = r.texto;
        if (m_resultadosJicaro.size() > 1) texto += wxString::Format(L" (y %zu avisos más)", m_resultadosJicaro.size() - 1);
        carpeta = (r.r == jicaro::Resultado::Listo || r.r == jicaro::Resultado::Parcial) && ExisteCarpeta(r.t.destino);
        // «Borrar las partes» solo si Jícaro terminó bien del todo
        borrar = r.r == jicaro::Resultado::Listo && !r.borradas;
        cerrar = true;
    }
    else if (m_jicaroOcupado)
    {
        texto = L"Jícaro está descomprimiendo «" + m_jicaroActual.nombre + L"»...";
        if (!m_colaJicaro.empty()) texto += wxString::Format(L" Después siguen %zu más.", m_colaJicaro.size());
    }
    bool mostrar = !texto.empty();
    bool cambio = mostrar != m_barraJicaro->IsShown();
    if (m_textoJicaro->GetLabelText() != texto)
    {
        m_textoJicaro->SetLabelText(texto);   // tal cual: un «&» en el nombre del paquete no es un atajo
        m_textoJicaro->SetToolTip(texto);
        cambio = true;
    }
    auto ver = [&](wxButton* b, bool si) {
        if (b->IsShown() != si)
        {
            b->Show(si);
            cambio = true;
        }
    };
    ver(m_bCarpetaJicaro, carpeta);
    ver(m_bBorrarPartes, borrar);
    ver(m_bCerrarJicaro, cerrar);
    if (cambio)
    {
        m_barraJicaro->Show(mostrar);
        m_barraJicaro->GetParent()->Layout();
    }
}

void VentanaPrincipal::BorrarPartes()
{
    if (m_resultadosJicaro.empty()) return;
    auto& r = m_resultadosJicaro.front();
    if (r.r != jicaro::Resultado::Listo || r.borradas) return;
    // Solo las partes de este paquete (por id) que siguen siendo el mismo archivo que bajó Atarraya: mismo volumen, número
    // de archivo, tamaño y fecha que al terminar. Las que no coinciden o no tienen guardado cómo eran se quedan donde están.
    auto b = jicaro::BorrarPartes(r.t.partes, [](const std::wstring& ruta) { return sistema::MandarAPapelera(ruta); });
    r.borradas = true;
    Registrar("jícaro: " + std::to_string(b.mandadas) + " partes a la Papelera; " + std::to_string(b.distintas) + " distintas, "
              + std::to_string(b.sinIdentidad) + " sin identidad, " + std::to_string(b.fallaron) + " no se pudieron, "
              + std::to_string(b.faltaban) + " ya no estaban");
    wxString nombre = L"«" + r.t.nombre + L"»";
    wxString texto;
    if (b.mandadas == 1) texto = L"La parte de " + nombre + L" está en la Papelera.";
    else if (b.mandadas > 1) texto = wxString::Format(L"Las %zu partes de ", b.mandadas) + nombre + L" están en la Papelera.";
    // Las que se quedan, con el porqué: alguien las cambió o reemplazó, o no se sabe cómo eran al terminar
    wxString aviso;
    if (b.distintas > 0)
        aviso = b.distintas == 1
            ? L"Una parte de " + nombre + L" no se mandó a la Papelera: ya no es el mismo archivo que bajó Atarraya (alguien lo cambió o lo reemplazó)."
            : wxString::Format(L"%zu partes de ", b.distintas) + nombre
                  + L" no se mandaron a la Papelera: ya no son los mismos archivos que bajó Atarraya (alguien los cambió o los reemplazó).";
    if (b.sinIdentidad > 0)
        aviso += (aviso.empty() ? wxString() : wxString(L" "))
                 + (b.sinIdentidad == 1
                        ? L"Una parte de " + nombre + L" no se mandó a la Papelera: Atarraya no guardó cómo era al terminar de bajarla "
                          L"(por ejemplo, porque se bajó con una versión anterior), así que no puede saber si es la misma."
                        : wxString::Format(L"%zu partes de ", b.sinIdentidad) + nombre
                              + L" no se mandaron a la Papelera: Atarraya no guardó cómo eran al terminar de bajarlas (por ejemplo, porque se "
                                L"bajaron con una versión anterior), así que no puede saber si son las mismas.");
    size_t quedan = b.distintas + b.sinIdentidad;
    if (quedan > 0)
    {
        texto += (texto.empty() ? L"" : L" ") + aviso + (quedan == 1 ? L" Sigue en su carpeta." : L" Siguen en su carpeta.");
        Avisar(L"Borrar las partes", aviso, true);
    }
    if (b.fallaron > 0)
        texto += (texto.empty() ? wxString() : wxString(L" ")) + (b.fallaron == 1 ? wxString(L"Una parte no se pudo mandar a la Papelera")
                                                                              : wxString::Format(L"%zu partes no se pudieron mandar a la Papelera", b.fallaron))
                 + L" (puede que estén abiertas): siguen en su carpeta.";
    if (texto.empty()) texto = L"Las partes de " + nombre + L" ya no estaban en su carpeta.";
    else if (b.mandadas > 0 && quedan == 0 && b.fallaron == 0) texto += L" Lo descomprimido sigue en su carpeta.";
    r.texto = texto;
    ActualizarBarraJicaro();
}
