// Atarraya: descargas, torrents y enlaces magnet en un solo programa, gratis, de Brizna Soft.
//
// Uso: Atarraya.exe [--bandeja] [dirección | magnet | archivo.torrent]
//   --bandeja         empieza sin ventana, solo con el ícono junto al reloj (así lo abre el puente del navegador)
//   dirección         la agrega como descarga nueva (si ya estaba abierto, se la pasa a esa copia)
//   magnet            lo mismo con un enlace magnet
//   archivo.torrent   abre la ventana «Nuevo torrent» con ese archivo

#include <windows.h>

#include <wx/app.h>
#include <wx/snglinst.h>
#include <wx/utils.h>

#include <nlohmann/json.hpp>

#include <memory>

#include "ajustes.hpp"
#include "motor.hpp"
#include "nombres.hpp"
#include "recolector.hpp"
#include "sistema.hpp"
#include "torrents.hpp"
#include "util.hpp"
#include "ventana.hpp"
#include "version.h"

namespace
{
    const wchar_t* ClaseMensajes = L"Atarraya.Mensajes";
    const ULONG_PTR MarcaCopia = 0x41545231;   // «ATR1», igual que en el puente
}

class Aplicacion : public wxApp
{
public:
    bool OnInit() override;
    int OnExit() override;

    VentanaPrincipal* ventana = nullptr;

private:
    std::unique_ptr<wxSingleInstanceChecker> m_unica;
    std::unique_ptr<hnd::Motor> m_motor;
    std::unique_ptr<hnd::MotorTorrent> m_torrents;
    std::unique_ptr<hnd::Recolector> m_recolector;
    hnd::Ajustes m_ajustes;
    HWND m_mensajes = nullptr;
};

wxIMPLEMENT_APP(Aplicacion);

namespace
{
    // Ventana invisible que recibe los pedidos del puente del navegador y de otras copias del programa
    LRESULT CALLBACK AlRecibirMensaje(HWND h, UINT mensaje, WPARAM w, LPARAM l)
    {
        if (mensaje == WM_COPYDATA)
        {
            auto* datos = reinterpret_cast<const COPYDATASTRUCT*>(l);
            if (datos && datos->dwData == MarcaCopia && datos->cbData > 0 && datos->cbData < 8 * 1024 * 1024)
            {
                std::string texto(static_cast<const char*>(datos->lpData), datos->cbData);
                hnd::Registrar("mensaje recibido (" + std::to_string(datos->cbData) + " bytes)");
                wxTheApp->CallAfter([texto]() {
                    auto& app = wxGetApp();
                    if (app.ventana) app.ventana->Pedido(texto);
                });
                return TRUE;
            }
            return FALSE;
        }
        return DefWindowProcW(h, mensaje, w, l);
    }

    bool EsArchivoTorrent(const std::wstring& a)
    {
        return a.size() > 8 && hnd::Minusculas(a.substr(a.size() - 8)) == L".torrent" && hnd::ExisteArchivo(a);
    }
}

bool Aplicacion::OnInit()
{
    bool bandeja = false;
    std::string url;
    std::wstring torrent;
    for (int i = 1; i < argc; i++)
    {
        std::wstring a = argv[i].ToStdWstring();
        if (a == L"--bandeja") bandeja = true;
        else if (hnd::sistema::EsDireccion(a) || hnd::sistema::EsMagnet(a)) url = hnd::Utf8(a);
        else if (EsArchivoTorrent(a))
        {
            wchar_t completa[MAX_PATH * 4] = {};
            torrent = GetFullPathNameW(a.c_str(), MAX_PATH * 4, completa, nullptr) ? completa : a;
        }
    }
    nlohmann::json pedido;
    if (!torrent.empty()) pedido = { { "tipo", "torrent" }, { "ruta", hnd::Utf8(torrent) } };
    else if (!url.empty()) pedido = { { "tipo", "descargar" }, { "url", url } };

    // Una sola copia: si ya está abierto, se le pasa el pedido y esta copia se cierra
    m_unica = std::make_unique<wxSingleInstanceChecker>(L"Atarraya-" + wxGetUserId());
    if (m_unica->IsAnotherRunning())
    {
        HWND otra = FindWindowExW(HWND_MESSAGE, nullptr, ClaseMensajes, nullptr);
        if (otra)
        {
            std::string texto = (pedido.is_null() ? nlohmann::json{ { "tipo", "mostrar" } } : pedido).dump();
            DWORD pid = 0;
            GetWindowThreadProcessId(otra, &pid);
            AllowSetForegroundWindow(pid);
            COPYDATASTRUCT cds = { MarcaCopia, (DWORD)texto.size(), (PVOID)texto.data() };
            DWORD_PTR r = 0;
            SendMessageTimeoutW(otra, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 5000, &r);
        }
        return false;
    }

    hnd::Registrar(std::string("Atarraya ") + APP_VERSION_TEXTO + (bandeja ? " (en la bandeja)" : ""));
    MSWEnableDarkMode();   // claro u oscuro, como Windows
    SetAppDisplayName(L"Atarraya");

    m_ajustes.Cargar();
    m_motor = std::make_unique<hnd::Motor>(hnd::CarpetaDatos() + L"\\descargas.json");
    m_motor->Cargar();
    m_torrents = std::make_unique<hnd::MotorTorrent>(hnd::CarpetaDatos());
    m_torrents->Cargar();
    m_recolector = std::make_unique<hnd::Recolector>(hnd::CarpetaDatos() + L"\\recolector.json");
    m_recolector->Cargar();
    ventana = new VentanaPrincipal(*m_motor, *m_torrents, *m_recolector, m_ajustes);

    WNDCLASSEXW clase = { sizeof(clase) };
    clase.lpfnWndProc = AlRecibirMensaje;
    clase.hInstance = GetModuleHandleW(nullptr);
    clase.lpszClassName = ClaseMensajes;
    RegisterClassExW(&clase);
    m_mensajes = CreateWindowExW(0, ClaseMensajes, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, clase.hInstance, nullptr);

    if (!bandeja) ventana->Show();
    if (!pedido.is_null())
    {
        std::string texto = pedido.dump();
        CallAfter([this, texto]() {
            if (ventana) ventana->Pedido(texto);
        });
    }
    return true;
}

int Aplicacion::OnExit()
{
    if (m_mensajes) DestroyWindow(m_mensajes);
    m_mensajes = nullptr;
    m_recolector.reset();
    m_torrents.reset();
    m_motor.reset();
    return wxApp::OnExit();
}
