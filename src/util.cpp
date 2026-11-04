#include "util.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

std::wstring hnd::Ancho(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), r.data(), n);
    return r;
}

std::string hnd::Utf8(const std::wstring& texto)
{
    if (texto.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, texto.data(), (int)texto.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, texto.data(), (int)texto.size(), r.data(), n, nullptr, nullptr);
    return r;
}

std::wstring hnd::TextoTamano(int64_t bytes)
{
    if (bytes < 0) return L"?";
    const wchar_t* unidades[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    wchar_t buf[64];
    if (u == 0) swprintf(buf, 64, L"%lld B", (long long)bytes);
    else swprintf(buf, 64, v < 10 ? L"%.2f %s" : (v < 100 ? L"%.1f %s" : L"%.0f %s"), v, unidades[u]);
    return buf;
}

std::wstring hnd::TextoVelocidad(double bytesPorSegundo)
{
    if (bytesPorSegundo < 1) return L"";
    return TextoTamano((int64_t)bytesPorSegundo) + L"/s";
}

std::wstring hnd::TextoDuracion(int64_t s)
{
    if (s < 0) return L"";
    wchar_t buf[64];
    if (s < 60) swprintf(buf, 64, L"%lld s", (long long)s);
    else if (s < 3600) swprintf(buf, 64, L"%lld min %lld s", (long long)(s / 60), (long long)(s % 60));
    else if (s < 86400) swprintf(buf, 64, L"%lld h %lld min", (long long)(s / 3600), (long long)((s % 3600) / 60));
    else swprintf(buf, 64, L"%lld d %lld h", (long long)(s / 86400), (long long)((s % 86400) / 3600));
    return buf;
}

std::wstring hnd::CarpetaDescargas()
{
    PWSTR ruta = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &ruta))) r = ruta;
    CoTaskMemFree(ruta);
    if (r.empty())
    {
        wchar_t perfil[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", perfil, MAX_PATH)) r = std::wstring(perfil) + L"\\Downloads";
    }
    return r;
}

#ifdef ATARRAYA_PRUEBAS
std::wstring hnd::CarpetaDePrueba()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"ATARRAYA_PRUEBA", buf, MAX_PATH);
    std::wstring r = n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
    // Solo una ruta completa con letra de unidad («C:\...»); si no, una carpeta temporal
    if (r.size() < 4 || r[1] != L':' || (r[2] != L'\\' && r[2] != L'/'))
    {
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        r = std::wstring(temporal) + L"Atarraya-prueba";
    }
    SHCreateDirectoryExW(nullptr, r.c_str(), nullptr);
    return r;
}
#endif

std::wstring hnd::CarpetaDatos()
{
#ifdef ATARRAYA_PRUEBAS
    return CarpetaDePrueba();   // la copia de prueba nunca toca los datos de verdad
#else
    PWSTR ruta = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &ruta))) r = ruta;
    CoTaskMemFree(ruta);
    r += L"\\Atarraya";
    CreateDirectoryW(r.c_str(), nullptr);
    return r;
#endif
}

std::wstring hnd::CarpetaDelPrograma()
{
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    std::wstring r(buf.data(), n);
    auto p = r.find_last_of(L"\\/");
    return p == std::wstring::npos ? r : r.substr(0, p);
}

bool hnd::ExisteArchivo(const std::wstring& ruta)
{
    DWORD a = GetFileAttributesW(ruta.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool hnd::ExisteCarpeta(const std::wstring& ruta)
{
    DWORD a = GetFileAttributesW(ruta.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int64_t hnd::TamanoArchivo(const std::wstring& ruta)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(ruta.c_str(), GetFileExInfoStandard, &d)) return -1;
    return ((int64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

bool hnd::LeerIdentidad(const std::wstring& ruta, IdentidadArchivo& identidad)
{
    identidad = {};
    HANDLE h = CreateFileW(ruta.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION i = {};
    BOOL ok = GetFileInformationByHandle(h, &i);
    CloseHandle(h);
    if (!ok || (i.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    identidad.volumen = i.dwVolumeSerialNumber;
    identidad.indice = ((uint64_t)i.nFileIndexHigh << 32) | i.nFileIndexLow;
    identidad.tamano = ((int64_t)i.nFileSizeHigh << 32) | i.nFileSizeLow;
    identidad.modificado = ((int64_t)i.ftLastWriteTime.dwHighDateTime << 32) | i.ftLastWriteTime.dwLowDateTime;
    return true;
}

int64_t hnd::EspacioLibre(const std::wstring& carpeta)
{
    ULARGE_INTEGER libre;
    if (!GetDiskFreeSpaceExW(carpeta.c_str(), &libre, nullptr, nullptr)) return -1;
    return (int64_t)libre.QuadPart;
}

bool hnd::EscribirSeguro(const std::wstring& ruta, const std::string& contenido)
{
    std::wstring temporal = ruta + L".nuevo";
    HANDLE h = CreateFileW(temporal.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD escritos = 0;
    BOOL ok = WriteFile(h, contenido.data(), (DWORD)contenido.size(), &escritos, nullptr) && escritos == contenido.size();
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { DeleteFileW(temporal.c_str()); return false; }
    return MoveFileExW(temporal.c_str(), ruta.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool hnd::LeerArchivo(const std::wstring& ruta, std::string& contenido)
{
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER t;
    bool ok = GetFileSizeEx(h, &t) && t.QuadPart < 256LL * 1024 * 1024;
    if (ok)
    {
        contenido.resize((size_t)t.QuadPart);
        DWORD leidos = 0;
        ok = contenido.empty() || (ReadFile(h, contenido.data(), (DWORD)contenido.size(), &leidos, nullptr) && leidos == contenido.size());
    }
    CloseHandle(h);
    return ok;
}

std::string hnd::Sha256Archivo(const std::wstring& ruta)
{
    std::string r;
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return r;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
        && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0)
    {
        std::vector<unsigned char> buf(1 << 20);
        DWORD leidos = 0;
        bool ok = true;
        while (ReadFile(h, buf.data(), (DWORD)buf.size(), &leidos, nullptr) && leidos > 0)
        {
            if (BCryptHashData(hash, buf.data(), leidos, 0) != 0) { ok = false; break; }
        }
        unsigned char huella[32];
        if (ok && BCryptFinishHash(hash, huella, sizeof(huella), 0) == 0)
        {
            static const char* hex = "0123456789abcdef";
            for (unsigned char b : huella) { r += hex[b >> 4]; r += hex[b & 15]; }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(h);
    return r;
}

int64_t hnd::AhoraUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string hnd::NuevoId()
{
    unsigned char b[8] = {};
    if (BCryptGenRandom(nullptr, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    {
        // Muy raro: con la hora y un contador también sirve para no mezclar paquetes
        static std::atomic<uint64_t> cuenta{ 0 };
        uint64_t v = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ (++cuenta << 48);
        memcpy(b, &v, sizeof(b));
    }
    static const char* hex = "0123456789abcdef";
    std::string r;
    for (unsigned char c : b) { r += hex[c >> 4]; r += hex[c & 15]; }
    return r;
}

namespace
{
    std::string Hora()
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char hora[32];
        snprintf(hora, sizeof(hora), "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        return hora;
    }

    // Deja solo la mitad más nueva de un registro (desde una línea entera), con una línea que lo dice
    bool CortarALaMitad(const std::wstring& ruta)
    {
        HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER tam = {};
        bool ok = GetFileSizeEx(h, &tam) != FALSE;
        const int64_t guardar = hnd::TopeRegistro / 2;
        if (ok && tam.QuadPart > guardar)
        {
            std::string cola((size_t)guardar, '\0');
            LARGE_INTEGER desde = {};
            desde.QuadPart = tam.QuadPart - guardar;
            DWORD leidos = 0;
            ok = SetFilePointerEx(h, desde, nullptr, FILE_BEGIN) && ReadFile(h, cola.data(), (DWORD)guardar, &leidos, nullptr);
            if (ok)
            {
                cola.resize(leidos);
                size_t salto = cola.find('\n');
                if (salto != std::string::npos) cola.erase(0, salto + 1);
                std::string nuevo = Hora() + "(registro cortado a la mitad: no se pudo renombrar a .anterior)\r\n" + cola;
                LARGE_INTEGER cero = {};
                DWORD escritos = 0;
                ok = SetFilePointerEx(h, cero, nullptr, FILE_BEGIN) && WriteFile(h, nuevo.data(), (DWORD)nuevo.size(), &escritos, nullptr)
                  && escritos == nuevo.size() && SetEndOfFile(h);
            }
        }
        CloseHandle(h);
        return ok;
    }

    // Un registro no pasa del tope: el que lo pasa se renombra a «.anterior» (reemplaza al de antes). Si no se puede (otro
    // programa lo tiene abierto, por ejemplo), se corta a la mitad. Si tampoco, false y no se escribe más en él.
    bool RespetarTope(const std::wstring& ruta, DWORD& error)
    {
        if (hnd::TamanoArchivo(ruta) <= hnd::TopeRegistro) return true;
        if (MoveFileExW(ruta.c_str(), (ruta + L".anterior").c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        DWORD alRenombrar = GetLastError();
        if (CortarALaMitad(ruta)) return true;
        error = alRenombrar != ERROR_SUCCESS ? alRenombrar : ERROR_FILE_TOO_LARGE;
        return false;
    }

    // Agrega «texto» al final de un archivo (lo crea si no está), sin pasar del tope. Si no se puede, false y el código de
    // error de Windows en «error». Un archivo que otro programa tiene abierto un momento (un antivirus, el indexador de
    // Windows) se prueba otra vez.
    bool AgregarAlFinal(const std::wstring& ruta, const std::string& texto, DWORD& error)
    {
        if (!RespetarTope(ruta, error)) return false;
        HANDLE h = INVALID_HANDLE_VALUE;
        for (int intento = 0; intento < 2; intento++)
        {
            h = CreateFileW(ruta.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) break;
            error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED) return false;
            if (intento == 0) Sleep(25);
        }
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD escritos = 0;
        bool ok = WriteFile(h, texto.data(), (DWORD)texto.size(), &escritos, nullptr) != FALSE && escritos == texto.size();
        if (!ok)
        {
            error = GetLastError();
            if (error == ERROR_SUCCESS) error = ERROR_WRITE_FAULT;
        }
        CloseHandle(h);
        return ok;
    }

    // Donde va el registro si tampoco se puede escribir en la carpeta de datos: la carpeta temporal de Windows (la copia
    // de prueba, dentro de su propia carpeta)
    std::wstring CarpetaDelRespaldo()
    {
#ifdef ATARRAYA_PRUEBAS
        std::wstring r = hnd::CarpetaDePrueba() + L"\\temporal";
        CreateDirectoryW(r.c_str(), nullptr);
        return r;
#else
        wchar_t t[MAX_PATH + 1] = {};
        DWORD n = GetTempPathW(MAX_PATH + 1, t);
        std::wstring r(t, n <= MAX_PATH ? n : 0);
        while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
        return r;
#endif
    }
}

void hnd::Registrar(const std::string& texto)
{
    static std::mutex m;
    static int perdidas = 0;        // líneas que no llegaron a registro.txt desde la última que sí
    static DWORD errorPerdidas = 0;
    std::lock_guard g(m);
    std::wstring carpeta = CarpetaDatos();
    std::string linea = Hora() + texto + "\r\n";
    // Si antes no se pudo, primero se dice cuántas líneas faltan aquí, por qué y dónde quedaron
    std::string antes;
    if (perdidas > 0)
        antes = Hora() + "(faltan " + std::to_string(perdidas) + " líneas: registro.txt no se pudo escribir, error de Windows "
              + std::to_string(errorPerdidas) + "; quedaron en " + RegistroRespaldo + " o en %TEMP%\\" + RegistroTemporal + ")\r\n";
    DWORD error = 0;
    if (AgregarAlFinal(carpeta + L"\\registro.txt", antes + linea, error))
    {
        perdidas = 0;
        return;
    }
    // No se pudo: la línea va a otro archivo, con el código de error, para que quede rastro. Primero en la misma carpeta
    // (si el problema es solo registro.txt) y, si tampoco, en la carpeta temporal.
    perdidas++;
    errorPerdidas = error;
    std::string conError = Hora() + "[registro.txt: error de Windows " + std::to_string(error) + "] " + texto + "\r\n";
    DWORD otro = 0;
    if (AgregarAlFinal(carpeta + L"\\" + Ancho(RegistroRespaldo), conError, otro)) return;
    std::wstring temporal = CarpetaDelRespaldo();
    if (!temporal.empty())
    {
        std::string conLosDos = Hora() + "[registro.txt: error de Windows " + std::to_string(error) + "; " + RegistroRespaldo
                              + ": error " + std::to_string(otro) + "] " + texto + "\r\n";
        AgregarAlFinal(temporal + L"\\" + Ancho(RegistroTemporal), conLosDos, otro);
    }
}

namespace
{
    // Los códigos que la extensión puede anotar (lista cerrada, la misma que CODIGOS_REGISTRO en medios.js), los tipos
    // de error y los archivos de la extensión
    const char* const CodigosFijos[] = { "video:sin-lista", "video:sin-leer", "video:varias-listas", "video:otros-videos",
                                         "video:no-permitida", "video:sin-respuesta", "video:sin-conexion", "video:redirige",
                                         "video:cortada" };
    const char* const TiposDeError[] = { "Error", "TypeError", "RangeError", "ReferenceError", "SyntaxError", "URIError",
                                         "EvalError", "AggregateError", "DOMException", "AbortError", "NotAllowedError",
                                         "NotFoundError", "NetworkError", "SecurityError", "InvalidStateError",
                                         "QuotaExceededError", "DataCloneError", "TimeoutError" };
    const char* const ArchivosExtension[] = { "background.js", "medios.js", "origen.js", "cola-navegador.js", "esperas.js",
                                              "cola.js", "videos.js", "popup.js" };

    template <size_t N>
    bool EnLista(const std::string& x, const char* const (&lista)[N])
    {
        for (const char* l : lista)
        {
            if (x == l) return true;
        }
        return false;
    }

    bool SoloDigitos(const std::string& x)
    {
        if (x.empty()) return false;
        for (char c : x)
        {
            if (c < '0' || c > '9') return false;
        }
        return true;
    }
}

std::string hnd::CodigoDeRegistro(const std::string& codigo)
{
    if (EnLista(codigo, CodigosFijos)) return codigo;
    // «video:http-NNN»: lo que contestó el servidor
    if (codigo.size() == 14 && codigo.rfind("video:http-", 0) == 0 && SoloDigitos(codigo.substr(11)) && codigo[11] >= '1'
        && codigo[11] <= '5')
        return codigo;
    // «extension:Tipo» o «extension:Tipo@archivo.js:línea»: un error de la extensión
    if (codigo.rfind("extension:", 0) == 0)
    {
        std::string resto = codigo.substr(10);
        size_t arroba = resto.find('@');
        if (!EnLista(resto.substr(0, arroba), TiposDeError)) return "otro";
        if (arroba == std::string::npos) return codigo;
        std::string lugar = resto.substr(arroba + 1);
        size_t dos = lugar.find(':');
        if (dos == std::string::npos) return "otro";
        std::string linea = lugar.substr(dos + 1);
        if (EnLista(lugar.substr(0, dos), ArchivosExtension) && SoloDigitos(linea) && linea.size() <= 6 && linea[0] != '0') return codigo;
    }
    return "otro";
}

std::string hnd::SitioDeRegistro(const std::string& sitio)
{
    if (sitio.empty() || sitio.size() > 253) return "";
    for (char c : sitio)
    {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return "";
    }
    return sitio;
}

std::string hnd::SoloSitio(const std::string& url)
{
    auto esquema = url.find("://");
    if (esquema == std::string::npos) return "?";
    auto fin = url.find_first_of("/?#", esquema + 3);
    std::string sitio = url.substr(esquema + 3, fin == std::string::npos ? std::string::npos : fin - esquema - 3);
    auto arroba = sitio.rfind('@');   // sin usuario ni contraseña (que puede traer otra @)
    return arroba == std::string::npos ? sitio : sitio.substr(arroba + 1);
}
