// Puente entre la extensión del navegador y Atarraya («native messaging»).
//
// El navegador abre este programa y le habla por la entrada estándar: 4 bytes con el largo y luego un JSON
// en UTF-8. El puente pasa cada pedido a Atarraya con WM_COPYDATA (lo abre si no está abierto) y responde
// de la misma forma. No guarda nada, no se conecta a internet y solo acepta direcciones http, https, ftp o magnet.

#include <windows.h>
#include <fcntl.h>
#include <io.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "canal.hpp"
#include "version.h"

namespace
{
    const wchar_t* ClaseMensajes = ATARRAYA_CLASE_MENSAJES;
    const ULONG_PTR MarcaCopia = 0x41545231;   // «ATR1», igual que en Atarraya.exe

    bool Leer(std::string& mensaje)
    {
        uint32_t largo = 0;
        if (fread(&largo, sizeof(largo), 1, stdin) != 1) return false;
        if (largo == 0 || largo > 8 * 1024 * 1024) return false;
        mensaje.resize(largo);
        return fread(mensaje.data(), 1, largo, stdin) == largo;
    }

    void Escribir(const nlohmann::json& j)
    {
        std::string s = j.dump();
        uint32_t largo = (uint32_t)s.size();
        fwrite(&largo, sizeof(largo), 1, stdout);
        fwrite(s.data(), 1, s.size(), stdout);
        fflush(stdout);
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
        auto tamano = j.find("tamano");
        if (tamano != j.end() && tamano->is_number_integer()) r["tamano"] = tamano->get<int64_t>();
        return r;
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
}

int main()
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    std::string mensaje;
    while (Leer(mensaje))
    {
        nlohmann::json respuesta;
        try
        {
            auto j = nlohmann::json::parse(mensaje);
            std::string tipo = j.value("tipo", "");
            if (tipo == "ping")
            {
                respuesta = { { "ok", true }, { "version", APP_VERSION }, { "abierto", BuscarPrograma() != nullptr } };
            }
            else if (tipo == "descargar" || tipo == "mostrar")
            {
                if (tipo == "descargar" && !DireccionValida(j.value("url", "")))
                {
                    respuesta = { { "ok", false }, { "error", "direccion no valida" } };
                }
                else
                {
                    HWND h = AbrirPrograma();
                    respuesta = { { "ok", h != nullptr && Enviar(h, SoloLoPermitido(j).dump()) } };
                    if (!h) respuesta["error"] = "no se pudo abrir Atarraya";
                }
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
