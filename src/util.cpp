#include "util.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <chrono>
#include <cstdio>
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

std::wstring hnd::CarpetaDatos()
{
    PWSTR ruta = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &ruta))) r = ruta;
    CoTaskMemFree(ruta);
    r += L"\\Atarraya";
    CreateDirectoryW(r.c_str(), nullptr);
    return r;
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

void hnd::Registrar(const std::string& texto)
{
    static std::mutex m;
    std::lock_guard g(m);
    std::wstring ruta = CarpetaDatos() + L"\\registro.txt";
    if (TamanoArchivo(ruta) > 512 * 1024)
        MoveFileExW(ruta.c_str(), (ruta + L".anterior").c_str(), MOVEFILE_REPLACE_EXISTING);
    HANDLE h = CreateFileW(ruta.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char hora[32];
    snprintf(hora, sizeof(hora), "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string linea = hora + texto + "\r\n";
    DWORD escritos = 0;
    WriteFile(h, linea.data(), (DWORD)linea.size(), &escritos, nullptr);
    CloseHandle(h);
}

std::string hnd::SoloSitio(const std::string& url)
{
    auto esquema = url.find("://");
    if (esquema == std::string::npos) return "?";
    auto fin = url.find_first_of("/?#", esquema + 3);
    std::string sitio = url.substr(esquema + 3, fin == std::string::npos ? std::string::npos : fin - esquema - 3);
    auto arroba = sitio.find('@');   // sin usuario ni contraseña
    return arroba == std::string::npos ? sitio : sitio.substr(arroba + 1);
}
