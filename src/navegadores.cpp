#include "navegadores.hpp"

#include <windows.h>
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include "util.hpp"

namespace
{
    struct Datos
    {
        const wchar_t* nombre;
        const wchar_t* clave;        // dentro de HKEY_CURRENT_USER
        KNOWNFOLDERID carpetaBase;   // para saber si está instalado
        const wchar_t* perfil;
        bool firefox;
    };

    const Datos Lista[] = {
        { L"Google Chrome", L"Software\\Google\\Chrome\\NativeMessagingHosts\\", FOLDERID_LocalAppData, L"\\Google\\Chrome", false },
        { L"Microsoft Edge", L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\", FOLDERID_LocalAppData, L"\\Microsoft\\Edge", false },
        { L"Brave", L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\", FOLDERID_LocalAppData, L"\\BraveSoftware\\Brave-Browser", false },
        { L"Firefox", L"Software\\Mozilla\\NativeMessagingHosts\\", FOLDERID_RoamingAppData, L"\\Mozilla\\Firefox", true },
    };

    std::wstring Carpeta(const KNOWNFOLDERID& id)
    {
        PWSTR p = nullptr;
        std::wstring r;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
        CoTaskMemFree(p);
        return r;
    }

    std::wstring CarpetaManifiestos()
    {
        std::wstring r = hnd::CarpetaDatos() + L"\\navegadores";
        CreateDirectoryW(r.c_str(), nullptr);
        return r;
    }

    std::wstring Manifiesto(bool firefox)
    {
        return CarpetaManifiestos() + (firefox ? L"\\firefox.json" : L"\\chromium.json");
    }

    std::wstring ValorDeClave(const std::wstring& clave)
    {
        wchar_t buf[2048] = {};
        DWORD largo = sizeof(buf);
        if (RegGetValueW(HKEY_CURRENT_USER, clave.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buf, &largo) != ERROR_SUCCESS) return {};
        return buf;
    }
}

std::wstring hnd::navegadores::CarpetaExtension()
{
    return CarpetaDelPrograma() + L"\\extension";
}

std::vector<hnd::navegadores::Navegador> hnd::navegadores::Estado()
{
    std::vector<Navegador> r;
    for (auto& d : Lista)
    {
        Navegador n;
        n.nombre = d.nombre;
        n.instalado = ExisteCarpeta(Carpeta(d.carpetaBase) + d.perfil);
        std::wstring valor = ValorDeClave(std::wstring(d.clave) + NombreHost);
        n.conectado = !valor.empty() && ExisteArchivo(valor);
        r.push_back(n);
    }
    return r;
}

bool hnd::navegadores::Conectar(std::wstring& error)
{
    std::wstring puente = CarpetaDelPrograma() + L"\\atarraya-puente.exe";
    if (!ExisteArchivo(puente))
    {
        error = L"No encuentro atarraya-puente.exe junto al programa. Vuelve a descomprimir Atarraya completo.";
        return false;
    }

    nlohmann::json chromium = {
        { "name", Utf8(NombreHost) }, { "description", "Atarraya" }, { "path", Utf8(puente) }, { "type", "stdio" },
        { "allowed_origins", { std::string("chrome-extension://") + IdChrome + "/" } },
    };
    nlohmann::json firefox = {
        { "name", Utf8(NombreHost) }, { "description", "Atarraya" }, { "path", Utf8(puente) }, { "type", "stdio" },
        { "allowed_extensions", { IdFirefox } },
    };
    if (!EscribirSeguro(Manifiesto(false), chromium.dump(2)) || !EscribirSeguro(Manifiesto(true), firefox.dump(2)))
    {
        error = L"No se pudieron guardar los archivos de conexión en " + CarpetaManifiestos() + L".";
        return false;
    }

    int conectados = 0;
    for (auto& d : Lista)
    {
        if (!ExisteCarpeta(Carpeta(d.carpetaBase) + d.perfil)) continue;   // no está instalado: no se toca
        std::wstring clave = std::wstring(d.clave) + NombreHost;
        std::wstring valor = Manifiesto(d.firefox);
        HKEY h = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, clave.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS)
        {
            if (RegSetValueExW(h, nullptr, 0, REG_SZ, (const BYTE*)valor.c_str(), (DWORD)((valor.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS)
                conectados++;
            RegCloseKey(h);
        }
    }
    if (conectados == 0)
    {
        error = L"No encontré Chrome, Edge, Brave ni Firefox en esta cuenta de Windows.";
        return false;
    }
    return true;
}

void hnd::navegadores::Desconectar()
{
    for (auto& d : Lista)
    {
        std::wstring clave = std::wstring(d.clave) + NombreHost;
        RegDeleteKeyW(HKEY_CURRENT_USER, clave.c_str());
    }
    DeleteFileW(Manifiesto(false).c_str());
    DeleteFileW(Manifiesto(true).c_str());
}
