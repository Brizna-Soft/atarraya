#include "nombres.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwchar>

#include "util.hpp"

namespace
{
    int ValorHex(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string DecodificarPorciento(const std::string& s)
    {
        std::string r;
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '%' && i + 2 < s.size() && ValorHex(s[i + 1]) >= 0 && ValorHex(s[i + 2]) >= 0)
            {
                r += (char)(ValorHex(s[i + 1]) * 16 + ValorHex(s[i + 2]));
                i += 2;
            }
            else r += s[i];
        }
        return r;
    }

    // Texto que puede venir en UTF-8 o en Latin-1
    std::wstring TextoDeBytes(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
        if (n > 0) return hnd::Ancho(s);
        std::wstring r;
        for (unsigned char c : s) r += (wchar_t)c;
        return r;
    }
}

std::wstring hnd::Minusculas(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}

std::wstring hnd::NombreDeContentDisposition(const std::string& valor)
{
    std::string bajo = valor;
    std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    // filename*=charset'idioma'texto-codificado (RFC 5987/6266), tiene prioridad
    auto p = bajo.find("filename*=");
    if (p != std::string::npos)
    {
        std::string v = valor.substr(p + 10);
        auto fin = v.find(';');
        if (fin != std::string::npos) v = v.substr(0, fin);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        auto c1 = v.find('\'');
        auto c2 = c1 == std::string::npos ? std::string::npos : v.find('\'', c1 + 1);
        if (c2 != std::string::npos)
        {
            std::string juego = v.substr(0, c1);
            std::transform(juego.begin(), juego.end(), juego.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            std::string bytes = DecodificarPorciento(v.substr(c2 + 1));
            std::wstring r;
            if (juego == "utf-8") r = Ancho(bytes);
            else { for (unsigned char c : bytes) r += (wchar_t)c; }
            if (!r.empty()) return r;
        }
    }

    // filename="..." o filename=token
    size_t desde = 0;
    while ((p = bajo.find("filename=", desde)) != std::string::npos)
    {
        if (p > 0 && bajo[p - 1] == '*') { desde = p + 9; continue; }
        std::string v = valor.substr(p + 9);
        std::string r;
        if (!v.empty() && v[0] == '"')
        {
            for (size_t i = 1; i < v.size(); i++)
            {
                if (v[i] == '\\' && i + 1 < v.size()) { r += v[++i]; continue; }
                if (v[i] == '"') break;
                r += v[i];
            }
        }
        else
        {
            auto fin = v.find(';');
            r = fin == std::string::npos ? v : v.substr(0, fin);
            while (!r.empty() && (r.back() == ' ' || r.back() == '\t')) r.pop_back();
        }
        // Algunos servidores mandan el nombre codificado con %
        if (r.find('%') != std::string::npos) r = DecodificarPorciento(r);
        return TextoDeBytes(r);
    }
    return {};
}

std::wstring hnd::NombreDeUrl(const std::string& url)
{
    std::string s = url;
    auto q = s.find_first_of("?#");
    if (q != std::string::npos) s = s.substr(0, q);
    auto esquema = s.find("://");
    if (esquema != std::string::npos)
    {
        auto barra = s.find('/', esquema + 3);
        s = barra == std::string::npos ? std::string() : s.substr(barra);
    }
    auto ultima = s.find_last_of('/');
    std::string trozo = ultima == std::string::npos ? s : s.substr(ultima + 1);
    return TextoDeBytes(DecodificarPorciento(trozo));
}

std::wstring hnd::LimpiarNombre(const std::wstring& nombre)
{
    std::wstring r;
    for (wchar_t c : nombre)
    {
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c)) r += L'_';
        else r += c;
    }
    // Sin puntos ni espacios al principio o al final
    while (!r.empty() && (r.back() == L'.' || r.back() == L' ')) r.pop_back();
    size_t i = 0;
    while (i < r.size() && (r[i] == L' ' || r[i] == L'.')) i++;
    r = r.substr(i);
    if (r.empty()) r = L"descarga";

    // Nombres reservados de Windows (CON, NUL, COM1...)
    std::wstring base = Minusculas(r.substr(0, r.find(L'.')));
    static const wchar_t* reservados[] = { L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3", L"com4",
        L"com5", L"com6", L"com7", L"com8", L"com9", L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6",
        L"lpt7", L"lpt8", L"lpt9" };
    for (auto* res : reservados)
    {
        if (base == res) { r = L"_" + r; break; }
    }

    // Largo máximo razonable, conservando la extensión
    const size_t maximo = 180;
    if (r.size() > maximo)
    {
        auto punto = r.find_last_of(L'.');
        std::wstring ext = (punto != std::wstring::npos && r.size() - punto <= 12) ? r.substr(punto) : L"";
        r = r.substr(0, maximo - ext.size()) + ext;
    }
    return r;
}

std::wstring hnd::AgregarExtension(const std::wstring& nombre, const std::string& tipoMime)
{
    auto punto = nombre.find_last_of(L'.');
    if (punto != std::wstring::npos && nombre.size() - punto <= 6 && punto > 0) return nombre;
    std::string t = tipoMime.substr(0, tipoMime.find(';'));
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    static const std::pair<const char*, const wchar_t*> tabla[] = {
        { "application/zip", L".zip" }, { "application/x-zip-compressed", L".zip" },
        { "application/x-7z-compressed", L".7z" }, { "application/vnd.rar", L".rar" },
        { "application/x-rar-compressed", L".rar" }, { "application/pdf", L".pdf" },
        { "application/x-msdownload", L".exe" }, { "application/x-msi", L".msi" },
        { "application/x-iso9660-image", L".iso" }, { "application/vnd.android.package-archive", L".apk" },
        { "video/mp4", L".mp4" }, { "video/x-matroska", L".mkv" }, { "video/webm", L".webm" },
        { "audio/mpeg", L".mp3" }, { "audio/mp4", L".m4a" }, { "image/jpeg", L".jpg" }, { "image/png", L".png" },
        { "application/x-bittorrent", L".torrent" }, { "text/plain", L".txt" },
    };
    for (auto& [mime, ext] : tabla)
    {
        if (t == mime) return nombre + ext;
    }
    return nombre;
}

std::wstring hnd::NombreUnico(const std::wstring& carpeta, const std::wstring& nombre, const std::set<std::wstring>& ocupados)
{
    auto libre = [&](const std::wstring& n) {
        std::wstring ruta = carpeta + L"\\" + n;
        return !ExisteArchivo(ruta) && !ExisteCarpeta(ruta) && !ExisteArchivo(ruta + L".atarraya")
            && ocupados.find(Minusculas(n)) == ocupados.end();
    };
    if (libre(nombre)) return nombre;
    auto punto = nombre.find_last_of(L'.');
    std::wstring base = (punto == std::wstring::npos || punto == 0) ? nombre : nombre.substr(0, punto);
    std::wstring ext = (punto == std::wstring::npos || punto == 0) ? L"" : nombre.substr(punto);
    for (int i = 1; i < 10000; i++)
    {
        std::wstring n = base + L" (" + std::to_wstring(i) + L")" + ext;
        if (libre(n)) return n;
    }
    return base + L" (" + std::to_wstring(AhoraUnix()) + L")" + ext;
}
