// Puente entre la extensión del navegador y Atarraya («native messaging»).
//
// El navegador abre este programa y le habla por la entrada estándar: 4 bytes con el largo y luego un JSON
// en UTF-8. El puente pasa cada pedido a Atarraya con WM_COPYDATA (lo abre si no está abierto) y responde
// de la misma forma. No guarda nada, no se conecta a internet y solo acepta direcciones http, https, ftp o magnet.
//
// Las consultas de «de una en una» («emparejar»: ¿esta descarga es de una parte?, «toma»: ¿quién baja esta descarga?)
// esperan la respuesta: Atarraya la manda enseguida con WM_COPYDATA a una ventana invisible del puente. Las consultas y
// los avisos («navegador»: cómo va una descarga que sigue la extensión, «espera»: la página pide esperar, «soltar»: que
// siga el navegador, «sinConfirmar» y «confirmada»: si la del navegador sigue después de no poder cancelarla,
// «seguidas»: lo que sabe la extensión de esas descargas, al conectarse) nunca abren Atarraya. Tampoco «registro»: un
// error de la extensión, que Atarraya anota en su registro si está abierto.
//
// El «puerto»: si lo primero que llega es «hola», la extensión deja este puente abierto mientras Atarraya está abierto
// (si no lo está, contesta «adios» y termina). El puente crea una ventana invisible donde Atarraya le deja sus órdenes
// para la extensión (por ahora, «abrir»: que abra la página de una parte en una pestaña nueva) y le pasa a Atarraya lo
// que contesta la extensión («abierta»). Termina cuando Atarraya se cierra o cuando el navegador cierra el puerto.

#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "canal.hpp"
#include "version.h"

namespace
{
    const wchar_t* ClaseMensajes = ATARRAYA_CLASE_MENSAJES;
    const wchar_t* ClasePuerto = ATARRAYA_CLASE_PUERTO;
    const ULONG_PTR MarcaCopia = 0x41545231;      // «ATR1», igual que en Atarraya.exe
    const ULONG_PTR MarcaRespuesta = 0x41545232;  // «ATR2»: la respuesta de Atarraya a una consulta
    const UINT LlegoDeLaExtension = WM_APP + 1;   // lParam: un mensaje de la extensión (std::string*)
    const UINT SeCerroElPuerto = WM_APP + 2;      // el navegador cerró la entrada

    std::string g_respuesta;
    bool g_hayRespuesta = false;
    HWND g_programa = nullptr;   // en el puerto: la ventana de Atarraya

    LRESULT CALLBACK AlRecibirRespuesta(HWND h, UINT mensaje, WPARAM w, LPARAM l)
    {
        if (mensaje == WM_COPYDATA)
        {
            auto* d = reinterpret_cast<const COPYDATASTRUCT*>(l);
            if (d && d->dwData == MarcaRespuesta && d->cbData > 0 && d->cbData < 1024 * 1024)
            {
                g_respuesta.assign(static_cast<const char*>(d->lpData), d->cbData);
                g_hayRespuesta = true;
                return TRUE;
            }
            return FALSE;
        }
        return DefWindowProcW(h, mensaje, w, l);
    }

    // La ventana invisible donde Atarraya deja la respuesta de una consulta
    HWND VentanaRespuesta()
    {
        static HWND h = nullptr;
        if (h) return h;
        WNDCLASSEXW c = { sizeof(c) };
        c.lpfnWndProc = AlRecibirRespuesta;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"Atarraya.Puente.Respuesta";
        RegisterClassExW(&c);
        h = CreateWindowExW(0, c.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, c.hInstance, nullptr);
        return h;
    }

    // Con ReadFile y WriteFile, sin el búfer de la biblioteca de C: en el puerto, un hilo lee mientras el otro escribe
    bool LeerTodo(void* destino, DWORD largo)
    {
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        auto* p = static_cast<char*>(destino);
        while (largo > 0)
        {
            DWORD n = 0;
            if (!ReadFile(h, p, largo, &n, nullptr) || n == 0) return false;
            p += n;
            largo -= n;
        }
        return true;
    }

    bool Leer(std::string& mensaje)
    {
        uint32_t largo = 0;
        if (!LeerTodo(&largo, sizeof(largo))) return false;
        if (largo == 0 || largo > 8 * 1024 * 1024) return false;
        mensaje.resize(largo);
        return LeerTodo(mensaje.data(), largo);
    }

    void Escribir(const nlohmann::json& j)
    {
        std::string s = j.dump();
        uint32_t largo = (uint32_t)s.size();
        std::string todo(reinterpret_cast<const char*>(&largo), sizeof(largo));
        todo += s;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        const char* p = todo.data();
        size_t falta = todo.size();
        DWORD n = 0;
        while (falta > 0 && WriteFile(h, p, (DWORD)falta, &n, nullptr) && n > 0)
        {
            p += n;
            falta -= n;
        }
    }

    HWND BuscarPrograma()
    {
        return FindWindowExW(HWND_MESSAGE, nullptr, ClaseMensajes, nullptr);
    }

    HWND AbrirPrograma()
    {
        HWND h = BuscarPrograma();
        if (h) return h;

        wchar_t ruta[32768] = {};
        DWORD n = GetModuleFileNameW(nullptr, ruta, 32768);
        std::wstring exe(ruta, n);
        exe = exe.substr(0, exe.find_last_of(L"\\/") + 1) + ATARRAYA_PROGRAMA;
        std::wstring linea = L"\"" + exe + L"\" --bandeja";

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        // Fuera del «trabajo» del navegador, para que Atarraya no se cierre cuando el navegador cierre el puente
        BOOL ok = CreateProcessW(exe.c_str(), linea.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB,
                                 nullptr, nullptr, &si, &pi);
        if (!ok) ok = CreateProcessW(exe.c_str(), linea.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        if (!ok) return nullptr;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        for (int i = 0; i < 200 && !(h = BuscarPrograma()); i++) Sleep(100);
        return h;
    }

    bool Enviar(HWND h, const std::string& datos)
    {
        COPYDATASTRUCT cds = { MarcaCopia, (DWORD)datos.size(), (PVOID)datos.data() };
        DWORD_PTR respuesta = 0;
        return SendMessageTimeoutW(h, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 15000, &respuesta) && respuesta == 1;
    }

    // Una consulta: va con la ventana del puente y Atarraya contesta ahí antes de terminar de atenderla (mientras
    // espera, este hilo recibe ese mensaje)
    bool Consultar(HWND h, const std::string& datos, nlohmann::json& respuesta)
    {
        HWND propia = VentanaRespuesta();
        if (!propia) return false;
        g_hayRespuesta = false;
        COPYDATASTRUCT cds = { MarcaCopia, (DWORD)datos.size(), (PVOID)datos.data() };
        DWORD_PTR r = 0;
        if (!SendMessageTimeoutW(h, WM_COPYDATA, (WPARAM)propia, (LPARAM)&cds, SMTO_ABORTIFHUNG, 15000, &r) || r != 1 || !g_hayRespuesta)
            return false;
        try
        {
            respuesta = nlohmann::json::parse(g_respuesta);
        }
        catch (...)
        {
            return false;
        }
        return respuesta.is_object();
    }

    // Solo pasan los campos conocidos, como texto y con un largo máximo
    nlohmann::json SoloLoPermitido(const nlohmann::json& j)
    {
        nlohmann::json r = { { "tipo", j.value("tipo", "") } };
        auto texto = [&](const char* campo, size_t maximo) {
            auto it = j.find(campo);
            if (it != j.end() && it->is_string() && it->get_ref<const std::string&>().size() <= maximo) r[campo] = *it;
        };
        texto("url", 8192);
        texto("referer", 8192);
        texto("pagina", 8192);   // la pestaña donde se pulsó descargar
        texto("nombre", 1024);
        texto("tipoMime", 256);
        texto("agente", 1024);
        texto("cookies", 256 * 1024);
        texto("formato", 16);    // videos: «archivo», «hls» o «dash»
        texto("titulo", 1024);   // videos: el título de la pestaña
        // De una en una
        texto("paginaCola", 8192);   // la página que abrió Atarraya en esa pestaña
        texto("toma", 128);          // la descarga del navegador que la extensión deja seguir o sigue
        texto("estado", 32);         // «bajando», «completa», «fallo» o «cancelada»
        texto("clase", 16);          // espera: «limite» (entre descargas) o «pagina» (la cuenta regresiva de la página)
        texto("ruta", 32768);        // dónde dejó el archivo el navegador
        texto("texto", 4096);        // registro: un error de la extensión
        auto numero = [&](const char* campo) {
            auto it = j.find(campo);
            if (it != j.end() && it->is_number_integer()) r[campo] = it->get<int64_t>();
        };
        numero("tamano");
        numero("segundos");
        numero("bajado");
        numero("total");
        numero("id");        // abierta: la orden «abrir» de Atarraya que se cumplió
        numero("pestana");   // abierta: la pestaña que abrió la extensión
        return r;
    }

    // «seguidas»: lo que sabe la extensión de las descargas del navegador de la cola (lo cuenta al conectarse). Solo pasa
    // quién es («instancia»: letras y números) y, de cada descarga, su «toma», cómo va y la página de la parte, como texto
    // y con un largo máximo; como mucho 500.
    nlohmann::json SoloLasSeguidas(const nlohmann::json& j)
    {
        auto texto = [](const nlohmann::json& o, const char* campo, size_t maximo) {
            auto it = o.find(campo);
            return it != o.end() && it->is_string() && it->get_ref<const std::string&>().size() <= maximo ? it->get<std::string>()
                                                                                                         : std::string();
        };
        std::string instancia = texto(j, "instancia", 32);
        for (char c : instancia)
        {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) instancia.clear();
        }
        nlohmann::json tomas = nlohmann::json::array();
        auto lista = j.find("tomas");
        if (lista != j.end() && lista->is_array())
        {
            for (const auto& t : *lista)
            {
                if (!t.is_object()) continue;
                std::string toma = texto(t, "toma", 128);
                if (toma.empty()) continue;
                tomas.push_back({ { "toma", toma }, { "estado", texto(t, "estado", 32) }, { "pagina", texto(t, "pagina", 8192) } });
                if (tomas.size() >= 500) break;
            }
        }
        return { { "tipo", "seguidas" }, { "instancia", instancia }, { "tomas", tomas } };
    }

    // http, https, ftp o un enlace magnet (clic derecho en un magnet → «Descargar con Atarraya»)
    bool DireccionValida(const std::string& url)
    {
        if (url.size() > 8192 || url.find_first_of(" \t\r\n\"<>") != std::string::npos) return false;
        std::string bajo = url.substr(0, 8);
        for (auto& c : bajo) c = (char)tolower((unsigned char)c);
        return bajo.rfind("http://", 0) == 0 || bajo.rfind("https://", 0) == 0 || bajo.rfind("ftp://", 0) == 0
            || bajo.rfind("magnet:?", 0) == 0;
    }

    // Solo http o https (los videos de una página)
    bool DireccionWeb(const std::string& url)
    {
        if (!DireccionValida(url)) return false;
        std::string bajo = url.substr(0, 8);
        for (auto& c : bajo) c = (char)tolower((unsigned char)c);
        return bajo.rfind("http://", 0) == 0 || bajo.rfind("https://", 0) == 0;
    }

    // --- El puerto -------------------------------------------------------------------------------------------------

    // El navegador que abrió este puente, en minúsculas («chrome.exe», «msedge.exe», «firefox.exe»...). Chrome y Edge
    // lo abren con cmd.exe en el medio. Atarraya lo usa para preferir el navegador predeterminado si hay varios.
    std::wstring NavegadorQueAbrio()
    {
        HANDLE foto = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (foto == INVALID_HANDLE_VALUE) return L"";
        std::map<DWORD, std::pair<DWORD, std::wstring>> procesos;   // id -> (padre, programa)
        PROCESSENTRY32W pe = { sizeof(pe) };
        for (BOOL ok = Process32FirstW(foto, &pe); ok; ok = Process32NextW(foto, &pe))
            procesos[pe.th32ProcessID] = { pe.th32ParentProcessID, pe.szExeFile };
        CloseHandle(foto);
        DWORD id = GetCurrentProcessId();
        for (int i = 0; i < 4; i++)
        {
            auto yo = procesos.find(id);
            if (yo == procesos.end()) return L"";
            auto padre = procesos.find(yo->second.first);
            if (padre == procesos.end()) return L"";
            std::wstring exe = padre->second.second;
            for (auto& c : exe) c = (wchar_t)towlower(c);
            if (exe != L"cmd.exe" && exe != L"conhost.exe") return exe.size() < 64 ? exe : L"";
            id = padre->first;
        }
        return L"";
    }

    LRESULT CALLBACK AlRecibirEnElPuerto(HWND h, UINT mensaje, WPARAM w, LPARAM l)
    {
        if (mensaje == WM_COPYDATA)
        {
            // Una orden de Atarraya para la extensión: abrir la página de una parte (solo http o https)
            auto* d = reinterpret_cast<const COPYDATASTRUCT*>(l);
            if (!d || d->dwData != MarcaCopia || d->cbData == 0 || d->cbData > 64 * 1024) return FALSE;
            try
            {
                auto j = nlohmann::json::parse(std::string(static_cast<const char*>(d->lpData), d->cbData));
                auto id = j.find("id");
                std::string url = j.value("url", "");
                if (j.value("tipo", "") != "abrir" || id == j.end() || !id->is_number_integer() || !DireccionWeb(url)) return FALSE;
                Escribir({ { "tipo", "abrir" }, { "id", id->get<int64_t>() }, { "url", url } });
                return TRUE;
            }
            catch (...)
            {
                return FALSE;
            }
        }
        if (mensaje == LlegoDeLaExtension)
        {
            // Lo que contesta la extensión: «abierta» (ya abrió la página), a Atarraya
            std::unique_ptr<std::string> texto(reinterpret_cast<std::string*>(l));
            try
            {
                auto j = nlohmann::json::parse(*texto);
                if (j.is_object() && j.value("tipo", "") == "abierta" && IsWindow(g_programa)) Enviar(g_programa, SoloLoPermitido(j).dump());
            }
            catch (...)
            {
            }
            return 0;
        }
        if (mensaje == SeCerroElPuerto || (mensaje == WM_TIMER && !IsWindow(g_programa)))
        {
            // El navegador cerró el puerto, o Atarraya se cerró: la extensión vuelve a conectar cuando lo necesite
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, mensaje, w, l);
    }

    int Puerto()
    {
        g_programa = BuscarPrograma();   // nunca se abre Atarraya por esto
        HWND ventana = nullptr;
        if (g_programa)
        {
            WNDCLASSEXW c = { sizeof(c) };
            c.lpfnWndProc = AlRecibirEnElPuerto;
            c.hInstance = GetModuleHandleW(nullptr);
            c.lpszClassName = ClasePuerto;
            RegisterClassExW(&c);
            ventana = CreateWindowExW(0, ClasePuerto, NavegadorQueAbrio().c_str(), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, c.hInstance, nullptr);
        }
        if (!ventana)
        {
            Escribir({ { "tipo", "adios" } });
            return 0;
        }
        SetTimer(ventana, 1, 2000, nullptr);   // ¿sigue abierto Atarraya?
        Escribir({ { "tipo", "listo" }, { "version", APP_VERSION } });
        std::thread([ventana]() {
            std::string m;
            while (Leer(m))
            {
                auto* copia = new std::string(m);
                if (!PostMessageW(ventana, LlegoDeLaExtension, 0, (LPARAM)copia)) delete copia;
            }
            PostMessageW(ventana, SeCerroElPuerto, 0, 0);
        }).detach();
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        DestroyWindow(ventana);
        return 0;
    }
}

int main()
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    std::string mensaje;
    bool primero = true;
    while (Leer(mensaje))
    {
        nlohmann::json respuesta;
        try
        {
            auto j = nlohmann::json::parse(mensaje);
            std::string tipo = j.value("tipo", "");
            if (primero && tipo == "hola") return Puerto();
            primero = false;
            if (tipo == "ping")
            {
                respuesta = { { "ok", true }, { "version", APP_VERSION }, { "abierto", BuscarPrograma() != nullptr } };
            }
            else if (tipo == "descargar" || tipo == "mostrar" || tipo == "video")
            {
                std::string url = j.value("url", "");
                std::string formato = j.value("formato", "");
                if (tipo == "descargar" && !DireccionValida(url))
                {
                    respuesta = { { "ok", false }, { "error", "direccion no valida" } };
                }
                // Un video de la página: solo http o https, y un formato conocido
                else if (tipo == "video" && (!DireccionWeb(url) || (formato != "archivo" && formato != "hls" && formato != "dash")))
                {
                    respuesta = { { "ok", false }, { "error", "video no valido" } };
                }
                else
                {
                    HWND h = AbrirPrograma();
                    respuesta = { { "ok", h != nullptr && Enviar(h, SoloLoPermitido(j).dump()) } };
                    if (!h) respuesta["error"] = "no se pudo abrir Atarraya";
                }
            }
            else if (tipo == "emparejar" || tipo == "toma")
            {
                // Consultas: si Atarraya no está abierto, no hay cola (no se abre por esto)
                std::string url = j.value("url", "");
                HWND h = BuscarPrograma();
                nlohmann::json r;
                if (tipo == "emparejar" && !DireccionValida(url))
                    respuesta = { { "ok", false }, { "error", "direccion no valida" } };
                else if (h && Consultar(h, SoloLoPermitido(j).dump(), r))
                {
                    respuesta = r;
                    respuesta["ok"] = true;
                }
                else respuesta = { { "ok", false }, { "abierto", h != nullptr } };
            }
            else if (tipo == "navegador" || tipo == "espera" || tipo == "soltar" || tipo == "sinConfirmar" || tipo == "confirmada")
            {
                // Avisos de «de una en una»: solo si Atarraya está abierto
                std::string url = j.value("url", "");
                HWND h = BuscarPrograma();
                if (!url.empty() && !DireccionValida(url)) respuesta = { { "ok", false }, { "error", "direccion no valida" } };
                else respuesta = { { "ok", h != nullptr && Enviar(h, SoloLoPermitido(j).dump()) } };
            }
            else if (tipo == "registro")
            {
                // Un error de la extensión, para el registro de Atarraya: solo si está abierto (no se abre por esto)
                HWND h = BuscarPrograma();
                respuesta = { { "ok", h != nullptr && Enviar(h, SoloLoPermitido(j).dump()) } };
            }
            else if (tipo == "seguidas")
            {
                // Lo que sabe la extensión de las descargas del navegador de la cola: solo si Atarraya está abierto
                HWND h = BuscarPrograma();
                respuesta = { { "ok", h != nullptr && Enviar(h, SoloLasSeguidas(j).dump()) } };
            }
            else
            {
                respuesta = { { "ok", false }, { "error", "pedido desconocido" } };
            }
        }
        catch (...)
        {
            respuesta = { { "ok", false }, { "error", "mensaje no valido" } };
        }
        Escribir(respuesta);
    }
    return 0;
}
