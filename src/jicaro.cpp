#include "jicaro.hpp"

#include <windows.h>
#include <shlobj.h>
#include <winver.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <map>
#include <regex>
#include <set>

#pragma comment(lib, "version.lib")

#include "nombres.hpp"
#include "util.hpp"

namespace
{
    bool EmpiezaCon(const std::wstring& s, const wchar_t* inicio)
    {
        size_t n = wcslen(inicio);
        return s.size() >= n && s.compare(0, n, inicio) == 0;
    }

    bool TerminaCon(const std::wstring& bajo, const wchar_t* fin)
    {
        size_t n = wcslen(fin);
        return bajo.size() > n && bajo.compare(bajo.size() - n, n, fin) == 0;
    }

    std::wstring NombreDe(const std::wstring& ruta)
    {
        auto barra = ruta.find_last_of(L"\\/");
        return barra == std::wstring::npos ? ruta : ruta.substr(barra + 1);
    }

    std::wstring CarpetaDe(const std::wstring& ruta)
    {
        auto barra = ruta.find_last_of(L"\\/");
        return barra == std::wstring::npos ? std::wstring() : ruta.substr(0, barra);
    }

    // Sin puntos ni espacios al final (Windows no los deja en una carpeta)
    std::wstring SinPuntoAlFinal(std::wstring s)
    {
        while (!s.empty() && (s.back() == L'.' || s.back() == L' ')) s.pop_back();
        return s;
    }

    // Qué comprimido es un archivo, por su nombre: una clave igual para todas sus partes, el número de la parte (en el
    // estilo viejo, 0 es el .rar) y el nombre sin extensión. Falso si no es un comprimido.
    struct Pieza
    {
        std::wstring clave;
        int numero = 0;
        std::wstring nombre;
    };

    bool PiezaDe(const std::wstring& ruta, Pieza& p)
    {
        std::wstring carpeta = hnd::Minusculas(CarpetaDe(ruta)) + L"\\";
        std::wstring nombre = NombreDe(ruta);
        std::wstring bajo = hnd::Minusculas(nombre);
        static const std::wregex nuevo(LR"(^(.+)\.part(\d{1,4})\.rar$)", std::regex::icase);
        static const std::wregex numerado(LR"(^(.+\.(7z|zip|rar))\.(\d{3})$)", std::regex::icase);
        // .z01 y también .z001 o .z100, como los reconoce Jícaro (ver su volumenes.cpp): una «peli.z001» ajena junto a la
        // «peli.z01» propia es del mismo juego para Jícaro, así que tiene que serlo también aquí
        static const std::wregex zipPartido(LR"(^(.+)\.z(\d{2,3})$)", std::regex::icase);
        static const std::wregex viejo(LR"(^(.+)\.([r-y])(\d\d)$)", std::regex::icase);
        std::wsmatch m;
        if (std::regex_match(nombre, m, nuevo))
        {
            // «peli.part1.rar», «peli.part02.rar»...
            p.clave = L"n|" + carpeta + hnd::Minusculas(m[1].str());
            p.numero = std::stoi(m[2].str());
            p.nombre = m[1].str();
            return p.numero >= 1;
        }
        if (std::regex_match(nombre, m, numerado))
        {
            // «peli.7z.001», «peli.zip.002»...
            std::wstring base = m[1].str();
            p.clave = L"c|" + carpeta + hnd::Minusculas(base);
            p.numero = std::stoi(m[3].str());
            p.nombre = base.substr(0, base.find_last_of(L'.'));
            return p.numero >= 1;
        }
        if (TerminaCon(bajo, L".rar"))
        {
            // Un .rar entero, o la primera parte del estilo viejo
            p.clave = L"v|" + carpeta + bajo.substr(0, bajo.size() - 4);
            p.numero = 0;
            p.nombre = nombre.substr(0, nombre.size() - 4);
            return true;
        }
        if (std::regex_match(nombre, m, zipPartido))
        {
            // Un ZIP partido: «peli.z01», «peli.z02»... y al final «peli.zip», que es el que se abre
            p.clave = L"z|" + carpeta + hnd::Minusculas(m[1].str());
            p.numero = std::stoi(m[2].str());
            p.nombre = m[1].str();
            return p.numero >= 1;
        }
        if (std::regex_match(nombre, m, viejo))
        {
            // Estilo viejo: .r00 ... .r99, después .s00 ...
            wchar_t letra = (wchar_t)towlower(m[2].str()[0]);
            p.clave = L"v|" + carpeta + hnd::Minusculas(m[1].str());
            p.numero = 1 + (letra - L'r') * 100 + std::stoi(m[3].str());
            p.nombre = m[1].str();
            return true;
        }
        if (TerminaCon(bajo, L".zip"))
        {
            // Un .zip entero, o el último trozo (el que se abre) de uno partido en .z01, .z02...
            p.clave = L"z|" + carpeta + bajo.substr(0, bajo.size() - 4);
            p.numero = 0;
            p.nombre = nombre.substr(0, nombre.size() - 4);
            return true;
        }
        if (TerminaCon(bajo, L".7z"))
        {
            p.clave = L"u|" + carpeta + bajo;
            p.numero = 0;
            p.nombre = nombre.substr(0, nombre.find_last_of(L'.'));
            return true;
        }
        return false;
    }

    // ¿Tiene la extensión de un archivo que no es un comprimido («.nfo», «.mkv», «.jpg»...)? Así una parte que todavía
    // no bajó se sabe que no es de ningún juego. Sin extensión (o con una de página), no se sabe.
    bool OtroArchivo(const std::wstring& nombre)
    {
        auto punto = nombre.find_last_of(L'.');
        if (punto == std::wstring::npos || punto == 0 || nombre.size() - punto > 6) return false;
        std::wstring ext = hnd::Minusculas(nombre.substr(punto + 1));
        if (ext.empty() || ext == L"html" || ext == L"htm" || ext == L"php" || ext == L"aspx" || ext == L"asp") return false;
        return std::all_of(ext.begin(), ext.end(), [](wchar_t c) { return iswalnum(c) != 0; });
    }

    // El nombre de una parte de «de una en una» que todavía no bajó: el del final de su página, sin el «.html» que
    // agregan algunos sitios («peli.part1.rar.html»)
    std::wstring NombreDeParte(const std::wstring& nombre)
    {
        std::wstring bajo = hnd::Minusculas(nombre);
        for (const wchar_t* f : { L".html", L".htm" })
        {
            if (TerminaCon(bajo, f)) return nombre.substr(0, nombre.size() - wcslen(f));
        }
        return nombre;
    }

    // Un número de RAR 5 («vint»): 7 bits por byte; el bit alto dice que sigue otro byte
    bool LeerVint(const std::string& b, size_t& p, uint64_t& v)
    {
        v = 0;
        for (int i = 0; i < 10 && p < b.size(); i++)
        {
            unsigned char c = (unsigned char)b[p++];
            v |= (uint64_t)(c & 0x7F) << (7 * i);
            if (!(c & 0x80)) return true;
        }
        return false;
    }

    uint32_t Le32(const std::string& b, size_t p)
    {
        return (uint32_t)(unsigned char)b[p] | ((uint32_t)(unsigned char)b[p + 1] << 8) | ((uint32_t)(unsigned char)b[p + 2] << 16)
             | ((uint32_t)(unsigned char)b[p + 3] << 24);
    }

    uint64_t Le64(const std::string& b, size_t p)
    {
        return (uint64_t)Le32(b, p) | ((uint64_t)Le32(b, p + 4) << 32);
    }

    uint32_t Crc32(const char* datos, size_t n)
    {
        static const auto tabla = [] {
            std::vector<uint32_t> t(256);
            for (uint32_t i = 0; i < 256; i++)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }();
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; i++) c = tabla[(c ^ (unsigned char)datos[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    // Lee pedazos de un archivo (sin bloquearlo para los demás)
    class Lector
    {
    public:
        explicit Lector(const std::wstring& ruta)
        {
            m_h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
            LARGE_INTEGER t = {};
            if (m_h != INVALID_HANDLE_VALUE && GetFileSizeEx(m_h, &t)) m_tamano = t.QuadPart;
        }
        ~Lector()
        {
            if (m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h);
        }
        Lector(const Lector&) = delete;
        Lector& operator=(const Lector&) = delete;
        int64_t Tamano() const { return m_tamano; }
        // Hasta «n» bytes desde «desde» (menos si el archivo termina antes); vacío si no se pudo
        std::string Leer(int64_t desde, size_t n) const
        {
            std::string b;
            if (m_h == INVALID_HANDLE_VALUE || desde < 0 || desde >= m_tamano) return b;
            n = (size_t)std::min<int64_t>((int64_t)n, m_tamano - desde);
            b.resize(n);
            OVERLAPPED ov = {};
            ov.Offset = (DWORD)(desde & 0xFFFFFFFF);
            ov.OffsetHigh = (DWORD)(desde >> 32);
            DWORD leidos = 0;
            if (!ReadFile(m_h, b.data(), (DWORD)n, &leidos, &ov)) return {};
            b.resize(leidos);
            return b;
        }

    private:
        HANDLE m_h = INVALID_HANDLE_VALUE;
        int64_t m_tamano = -1;
    };

    using hnd::jicaro::Ultima;

    // RAR 5: se recorren los bloques de la parte (solo las cabeceras: los datos se saltan) hasta el del final, cuya bandera
    // 0x0001 dice que sigue otra parte. Con las cabeceras cifradas no se puede saber.
    Ultima FinalRar5(const Lector& f)
    {
        int64_t p = 8;
        for (int vuelta = 0; vuelta < 100000 && p + 6 <= f.Tamano(); vuelta++)
        {
            std::string b = f.Leer(p, 4 + 10);
            size_t q = 4;
            uint64_t tam = 0;
            if (b.size() < 6 || !LeerVint(b, q, tam) || tam == 0 || tam > 2 * 1024 * 1024) return Ultima::NoSe;
            int64_t cabecera = p + (int64_t)q;
            std::string c = f.Leer(cabecera, (size_t)tam);
            if (c.size() != tam) return Ultima::NoSe;
            size_t r = 0;
            uint64_t tipo = 0, banderas = 0, extra = 0, datos = 0;
            if (!LeerVint(c, r, tipo) || !LeerVint(c, r, banderas)) return Ultima::NoSe;
            if ((banderas & 0x0001) && !LeerVint(c, r, extra)) return Ultima::NoSe;
            if ((banderas & 0x0002) && !LeerVint(c, r, datos)) return Ultima::NoSe;
            if (tipo == 4) return Ultima::NoSe;   // cabeceras cifradas
            if (tipo == 5)
            {
                uint64_t fin = 0;
                if (!LeerVint(c, r, fin)) return Ultima::NoSe;
                return (fin & 0x0001) ? Ultima::NoEs : Ultima::Es;
            }
            if (datos > (uint64_t)f.Tamano()) return Ultima::NoSe;
            p = cabecera + (int64_t)tam + (int64_t)datos;
        }
        return Ultima::NoSe;
    }

    // RAR 4: lo mismo, con sus bloques (tipo 0x7B = el final; su bandera 0x0001 dice que sigue otra parte)
    Ultima FinalRar4(const Lector& f)
    {
        int64_t p = 7;
        for (int vuelta = 0; vuelta < 100000 && p + 7 <= f.Tamano(); vuelta++)
        {
            std::string b = f.Leer(p, 36);
            if (b.size() < 7) return Ultima::NoSe;
            unsigned tipo = (unsigned char)b[2];
            unsigned banderas = (unsigned char)b[3] | ((unsigned char)b[4] << 8);
            unsigned tam = (unsigned char)b[5] | ((unsigned char)b[6] << 8);
            if (tam < 7) return Ultima::NoSe;
            if (tipo == 0x73 && (banderas & 0x0080)) return Ultima::NoSe;   // cabeceras cifradas
            if (tipo == 0x7B) return (banderas & 0x0001) ? Ultima::NoEs : Ultima::Es;
            uint64_t datos = 0;
            if (banderas & 0x8000)
            {
                if (b.size() < 11) return Ultima::NoSe;
                datos = Le32(b, 7);
                // Un archivo de más de 4 GB: la parte alta de su tamaño va más adelante en su cabecera
                if (tipo == 0x74 && (banderas & 0x0100))
                {
                    if (b.size() < 36) return Ultima::NoSe;
                    datos |= (uint64_t)Le32(b, 32) << 32;
                }
            }
            if (datos > (uint64_t)f.Tamano()) return Ultima::NoSe;
            p += (int64_t)tam + (int64_t)datos;
        }
        return Ultima::NoSe;
    }

    Ultima FinalRar(const std::wstring& ruta)
    {
        Lector f(ruta);
        std::string inicio = f.Leer(0, 8);
        static const char rar5[] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00 };
        static const char rar4[] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x00 };
        if (inicio.size() >= sizeof(rar5) && memcmp(inicio.data(), rar5, sizeof(rar5)) == 0) return FinalRar5(f);
        if (inicio.size() >= sizeof(rar4) && memcmp(inicio.data(), rar4, sizeof(rar4)) == 0) return FinalRar4(f);
        return Ultima::NoSe;
    }

    // 7z: la cabecera de inicio (en la primera parte) dice cuánto mide el comprimido entero: sus 32 bytes más dónde está y
    // cuánto mide la cabecera del final. Tiene su propia suma de control. -1 si no se puede leer.
    int64_t TamanoDe7z(const std::wstring& primera)
    {
        Lector f(primera);
        std::string b = f.Leer(0, 32);
        static const unsigned char firma[] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
        if (b.size() < 32 || memcmp(b.data(), firma, sizeof(firma)) != 0 || Crc32(b.data() + 12, 20) != Le32(b, 8)) return -1;
        uint64_t desde = Le64(b, 12), tam = Le64(b, 20);
        if (desde > (1ull << 50) || tam > (1ull << 40)) return -1;
        return 32 + (int64_t)desde + (int64_t)tam;
    }

    // ZIP partido: el .zip es el último trozo y el final de su índice central dice su número (0 es el primero): ese es
    // también cuántas .zNN hay. -1 si no se puede leer (o es un ZIP64, que lo guarda en otro lado).
    int TrozosDelZip(const std::wstring& zip)
    {
        Lector f(zip);
        if (f.Tamano() < 22) return -1;
        int64_t leer = std::min<int64_t>(f.Tamano(), 65536 + 22);
        std::string fin = f.Leer(f.Tamano() - leer, (size_t)leer);
        for (size_t p = fin.size() >= 22 ? fin.size() - 22 : std::string::npos; p != std::string::npos; p--)
        {
            if (fin.compare(p, 4, "PK\x05\x06", 4) == 0)
            {
                unsigned este = (unsigned char)fin[p + 4] | ((unsigned char)fin[p + 5] << 8);
                return este == 0xFFFF ? -1 : (int)este;
            }
            if (p == 0) break;
        }
        return -1;
    }
}

std::wstring hnd::jicaro::RutaPredeterminada()
{
#ifdef ATARRAYA_PRUEBAS
    // Copia de prueba: nunca usa el Jícaro que instaló la persona; lo busca dentro de su propia carpeta de datos
    return CarpetaDePrueba() + L"\\Programs\\Jicaro\\Jicaro.exe";
#else
    PWSTR ruta = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &ruta)) && ruta) r = ruta;
    CoTaskMemFree(ruta);
    return r.empty() ? std::wstring() : r + L"\\Programs\\Jicaro\\Jicaro.exe";
#endif
}

std::wstring hnd::jicaro::Buscar(const std::wstring& elegido)
{
    for (const std::wstring& r : { elegido, RutaPredeterminada() })
    {
        if (EsRutaDeJicaro(r) && ExisteArchivo(r)) return r;
    }
    return {};
}

bool hnd::jicaro::RutaSegura(const std::wstring& ruta)
{
    if (ruta.size() < 4 || ruta.size() > 32000) return false;
    size_t inicio;
    wchar_t letra = ruta[0];
    if (((letra >= L'A' && letra <= L'Z') || (letra >= L'a' && letra <= L'z')) && ruta[1] == L':' && ruta[2] == L'\\') inicio = 3;
    // \\servidor\carpeta\... (no \\?\ ni \\.\, que saltan las reglas normales de las rutas)
    else if (ruta[0] == L'\\' && ruta[1] == L'\\' && ruta[2] != L'?' && ruta[2] != L'.' && ruta[2] != L'\\') inicio = 2;
    else return false;
    for (size_t i = 0; i < ruta.size(); i++)
    {
        wchar_t c = ruta[i];
        if (c < 32 || c == 127 || wcschr(L"\"*?<>|/", c)) return false;
        if (c == L':' && i != 1) return false;   // nada de «archivo:flujo»
    }
    if (ruta.back() == L'\\' || ruta.back() == L' ' || ruta.back() == L'.') return false;
    // Cada trozo entre «\»: ni vacío, ni «.», ni «..»
    for (size_t p = inicio; p <= ruta.size();)
    {
        size_t fin = ruta.find(L'\\', p);
        if (fin == std::wstring::npos) fin = ruta.size();
        std::wstring trozo = ruta.substr(p, fin - p);
        if (trozo.empty() || trozo == L"." || trozo == L"..") return false;
        p = fin + 1;
    }
    return true;
}

bool hnd::jicaro::EsRutaDeJicaro(const std::wstring& ruta)
{
    return RutaSegura(ruta) && Minusculas(NombreDe(ruta)) == L"jicaro.exe";
}

std::wstring hnd::jicaro::ArmarOrden(const std::wstring& exe, const std::wstring& archivo, const std::wstring& destino,
                                     bool conContrasena)
{
    if (!EsRutaDeJicaro(exe) || !RutaSegura(archivo) || !RutaSegura(destino)) return {};
    // Ninguna ruta trae comillas ni termina en «\»: entre comillas, cada una llega a Jícaro como un solo argumento
    std::wstring orden = L"\"" + exe + L"\" --extraer \"" + archivo + L"\" --destino \"" + destino + L"\"";
    if (conContrasena) orden += L" --contrasena-stdin";   // la contraseña va por la entrada estándar, nunca aquí
    return orden;
}

bool hnd::jicaro::LeerVersion(const std::wstring& exe, Version& v)
{
    v = {};
    DWORD nada = 0;
    DWORD largo = GetFileVersionInfoSizeW(exe.c_str(), &nada);
    if (largo == 0 || largo > 1024 * 1024) return false;
    std::vector<BYTE> datos(largo);
    if (!GetFileVersionInfoW(exe.c_str(), 0, largo, datos.data())) return false;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT n = 0;
    if (!VerQueryValueW(datos.data(), L"\\", reinterpret_cast<void**>(&info), &n) || !info || n < sizeof(VS_FIXEDFILEINFO)
        || info->dwSignature != 0xFEEF04BD)
        return false;
    v.mayor = HIWORD(info->dwFileVersionMS);
    v.menor = LOWORD(info->dwFileVersionMS);
    v.parche = HIWORD(info->dwFileVersionLS);
    return true;
}

bool hnd::jicaro::AceptaContrasena(const Version& v)
{
    return v.mayor > 0 || (v.mayor == 0 && v.menor >= 2);
}

std::vector<hnd::jicaro::Comprimido> hnd::jicaro::Comprimidos(const std::vector<std::wstring>& archivos)
{
    std::map<std::wstring, std::vector<std::pair<int, std::wstring>>> grupos;
    std::map<std::wstring, std::wstring> nombres;
    for (const auto& a : archivos)
    {
        Pieza p;
        if (!PiezaDe(a, p)) continue;
        auto& g = grupos[p.clave];
        std::wstring bajo = Minusculas(a);
        if (std::any_of(g.begin(), g.end(), [&](const auto& x) { return Minusculas(x.second) == bajo; })) continue;   // repetido
        g.push_back({ p.numero, a });
        if (!nombres.count(p.clave)) nombres[p.clave] = p.nombre;
    }
    std::vector<Comprimido> r;
    for (auto& [clave, partes] : grupos)
    {
        // Sin el .rar de base, una «.s00» o «.t00» puede ser otra cosa: solo .r00 a .r99 cuentan solas
        if (clave[0] == L'v' && std::none_of(partes.begin(), partes.end(), [](const auto& x) { return x.first == 0; }))
            partes.erase(std::remove_if(partes.begin(), partes.end(), [](const auto& x) { return x.first > 100; }), partes.end());
        // Un ZIP partido se abre por su .zip: sin él, no se puede
        if (clave[0] == L'z' && std::none_of(partes.begin(), partes.end(), [](const auto& x) { return x.first == 0; })) continue;
        if (partes.empty()) continue;
        std::stable_sort(partes.begin(), partes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        Comprimido c;
        for (auto& x : partes) c.partes.push_back(x.second);
        c.nombre = SinPuntoAlFinal(nombres[clave]);
        if (c.nombre.empty()) c.nombre = L"descomprimido";
        r.push_back(std::move(c));
    }
    return r;
}

bool hnd::jicaro::EsComprimidoSuelto(const std::wstring& nombre)
{
    Pieza p;
    if (!PiezaDe(nombre, p)) return false;
    return p.clave[0] == L'u' || ((p.clave[0] == L'v' || p.clave[0] == L'z') && p.numero == 0);
}

bool hnd::jicaro::EsZipPartido(const std::string& fin)
{
    // El final del índice central («PK\5\6»): el número de este trozo (2 bytes) y el del trozo donde empieza el
    // índice (2 bytes). En un ZIP entero, los dos son 0.
    for (size_t p = fin.size() >= 22 ? fin.size() - 22 : std::string::npos; p != std::string::npos; p--)
    {
        if (fin.compare(p, 4, "PK\x05\x06", 4) == 0)
        {
            unsigned este = (unsigned char)fin[p + 4] | ((unsigned char)fin[p + 5] << 8);
            unsigned inicio = (unsigned char)fin[p + 6] | ((unsigned char)fin[p + 7] << 8);
            return este != 0 || inicio != 0;
        }
        if (p == 0) break;
    }
    return false;
}

bool hnd::jicaro::EsZipPartido(const std::wstring& ruta)
{
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER tamano = {};
    std::string b;
    if (GetFileSizeEx(h, &tamano) && tamano.QuadPart >= 22)
    {
        // El final del índice está en los últimos 64 KB más 22 bytes (lo que puede medir su comentario)
        int64_t leer = std::min<int64_t>(tamano.QuadPart, 65536 + 22);
        LARGE_INTEGER desde = {};
        desde.QuadPart = tamano.QuadPart - leer;
        b.resize((size_t)leer);
        DWORD n = 0;
        if (!SetFilePointerEx(h, desde, nullptr, FILE_BEGIN) || !ReadFile(h, b.data(), (DWORD)leer, &n, nullptr)) b.clear();
        else b.resize(n);
    }
    CloseHandle(h);
    return EsZipPartido(b);
}

bool hnd::jicaro::EsVolumenRar(const std::string& b)
{
    static const char rar5[] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00 };
    static const char rar4[] = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x00 };
    if (b.size() >= sizeof(rar5) && memcmp(b.data(), rar5, sizeof(rar5)) == 0)
    {
        // RAR 5: CRC32, tamaño, tipo (1 = cabecera principal), banderas, [área extra], [datos] y las banderas del
        // archivo: 0x0001 = es una parte de varias
        size_t p = sizeof(rar5) + 4;
        uint64_t tamano = 0, tipo = 0, banderas = 0, extra = 0, datos = 0, archivo = 0;
        if (!LeerVint(b, p, tamano) || !LeerVint(b, p, tipo) || !LeerVint(b, p, banderas) || tipo != 1) return false;
        if ((banderas & 0x0001) && !LeerVint(b, p, extra)) return false;
        if ((banderas & 0x0002) && !LeerVint(b, p, datos)) return false;
        if (!LeerVint(b, p, archivo)) return false;
        return (archivo & 0x0001) != 0;
    }
    if (b.size() >= sizeof(rar4) + 5 && memcmp(b.data(), rar4, sizeof(rar4)) == 0)
    {
        // RAR 4: CRC (2 bytes), tipo (0x73 = cabecera principal) y banderas: 0x0001 = es una parte de varias
        size_t p = sizeof(rar4);
        unsigned char tipo = (unsigned char)b[p + 2];
        unsigned banderas = (unsigned char)b[p + 3] | ((unsigned char)b[p + 4] << 8);
        return tipo == 0x73 && (banderas & 0x0001) != 0;
    }
    return false;
}

bool hnd::jicaro::EsVolumenRar(const std::wstring& ruta)
{
    HANDLE h = CreateFileW(ruta.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char b[64] = {};
    DWORD n = 0;
    BOOL ok = ReadFile(h, b, sizeof(b), &n, nullptr);
    CloseHandle(h);
    return ok && EsVolumenRar(std::string(b, n));
}

std::vector<hnd::jicaro::ParteBajada> hnd::jicaro::ArchivosDelPaquete(const std::string& grupo, const std::vector<Foto>& descargas,
                                                                     const std::vector<Parte>& cola)
{
    std::vector<ParteBajada> r;
    if (grupo.empty()) return r;
    for (const auto& p : cola)
    {
        if (p.grupo == grupo && p.estado != EstadoParte::Lista) return {};
    }
    for (const auto& f : descargas)
    {
        if (f.datos.grupo != grupo) continue;
        if (f.datos.estado != Estado::Completada) return {};
        if (!f.datos.nombre.empty()) r.push_back({ f.datos.id, f.datos.carpeta + L"\\" + f.datos.nombre, f.datos.identidad });
    }
    return r;
}

hnd::jicaro::Ultima hnd::jicaro::LaUltima(const std::vector<std::wstring>& partes, wchar_t tipo)
{
    if (partes.empty()) return Ultima::NoSe;
    switch (tipo)
    {
    case L'n':   // «peli.part3.rar»: lo dice el final de la última
    case L'v':   // «peli.rar», «peli.r00»...: también
        return FinalRar(partes.back());
    case L'c':
    {
        // «peli.7z.001»...: la primera dice cuánto mide todo. De un «.zip.001» o «.rar.001» (un programa que corta
        // archivos) no se sabe.
        if (!TerminaCon(Minusculas(partes.front()), L".7z.001")) return Ultima::NoSe;
        int64_t total = TamanoDe7z(partes.front());
        if (total < 0) return Ultima::NoSe;
        int64_t suma = 0;
        for (const auto& p : partes)
        {
            Lector f(p);
            if (f.Tamano() < 0) return Ultima::NoSe;
            suma += f.Tamano();
        }
        return suma == total ? Ultima::Es : suma < total ? Ultima::NoEs : Ultima::NoSe;
    }
    case L'z':
    {
        // El .zip (el primero de la lista: es el que se abre) es el último trozo; su índice dice cuántas .zNN hay
        int trozos = TrozosDelZip(partes.front());
        int hay = (int)partes.size() - 1;
        if (trozos < 0 || trozos == hay) return Ultima::Es;
        return trozos > hay ? Ultima::NoEs : Ultima::NoSe;
    }
    default: return Ultima::NoSe;
    }
}

std::wstring hnd::jicaro::ParteAjena(const Juego& j, const std::vector<std::wstring>& delPaquete)
{
    if (j.carpeta.empty()) return {};
    std::set<std::wstring> propias;
    for (const auto& r : delPaquete) propias.insert(Minusculas(r));
    WIN32_FIND_DATAW d = {};
    HANDLE h = FindFirstFileExW((j.carpeta + L"\\*").c_str(), FindExInfoBasic, &d, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring ajena;
    do
    {
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring ruta = j.carpeta + L"\\" + d.cFileName;
        Pieza p;
        // Jícaro busca las partes por su nombre: una con el mismo nombre de juego que no es del paquete se mezclaría
        if (PiezaDe(ruta, p) && p.clave == j.clave && !propias.count(Minusculas(ruta))) ajena = d.cFileName;
    } while (ajena.empty() && FindNextFileW(h, &d));
    FindClose(h);
    return ajena;
}

std::wstring hnd::jicaro::ParteAjenaDe(const std::wstring& primera, const std::vector<std::wstring>& delPaquete)
{
    Pieza p;
    if (!PiezaDe(primera, p)) return {};
    Juego j;
    j.clave = p.clave;
    j.carpeta = CarpetaDe(primera);
    return ParteAjena(j, delPaquete);
}

std::vector<uint64_t> hnd::jicaro::PorRetomar(const std::vector<Foto>& descargas)
{
    std::vector<uint64_t> r;
    std::set<std::string> grupos;
    for (const auto& f : descargas)
    {
        if (f.torrent || f.datos.jicaro != EstadoJicaro::EnCurso || f.datos.estado != Estado::Completada) continue;
        if (f.datos.grupo.empty() || grupos.insert(f.datos.grupo).second) r.push_back(f.datos.id);
    }
    return r;
}

hnd::jicaro::JuegosDelPaquete hnd::jicaro::Juegos(const std::string& grupo, const std::vector<Foto>& descargas,
                                                   const std::vector<Parte>& cola,
                                                   const std::function<Ultima(const std::vector<std::wstring>&, wchar_t)>& ultima)
{
    JuegosDelPaquete r;
    if (grupo.empty()) return r;
    std::map<std::wstring, size_t> posicion;                       // clave → juego
    std::map<std::wstring, std::vector<std::pair<int, ParteBajada>>> bajadas;   // clave → (número, parte)
    std::map<std::wstring, std::set<int>> numeros;                 // clave → números que hay (bajados o no)
    auto juegoDe = [&](const Pieza& p, const std::wstring& carpeta) -> Juego& {
        auto it = posicion.find(p.clave);
        if (it == posicion.end())
        {
            Juego j;
            j.clave = p.clave;
            j.nombre = SinPuntoAlFinal(p.nombre);
            if (j.nombre.empty()) j.nombre = L"descomprimido";
            j.carpeta = carpeta;
            r.juegos.push_back(std::move(j));
            it = posicion.emplace(p.clave, r.juegos.size() - 1).first;
        }
        return r.juegos[it->second];
    };
    std::set<uint64_t> enLaLista;
    bool sigue = false;   // algo del paquete todavía baja o espera (en la lista o en la cola)
    // Las partes que ya bajaron bien (juego y número): un intento anterior de la misma parte que falló (y volvió al
    // recolector para bajarla otra vez) no la deja como faltante
    std::set<std::pair<std::wstring, int>> completas;
    for (const auto& f : descargas)
    {
        Pieza p;
        if (f.datos.grupo == grupo && f.datos.estado == Estado::Completada && !f.datos.nombre.empty()
            && PiezaDe(f.datos.carpeta + L"\\" + f.datos.nombre, p))
            completas.insert({ p.clave, p.numero });
    }
    for (const auto& f : descargas)
    {
        if (f.datos.grupo != grupo) continue;
        enLaLista.insert(f.datos.id);
        if (f.datos.estado == Estado::Error && !f.datos.nombre.empty())
        {
            Pieza p;
            if (PiezaDe(f.datos.carpeta + L"\\" + f.datos.nombre, p) && completas.count({ p.clave, p.numero })) continue;
        }
        bool bajada = f.datos.estado == Estado::Completada;
        if (f.datos.estado == Estado::EnCola || f.datos.estado == Estado::Iniciando || f.datos.estado == Estado::Descargando
            || f.datos.estado == Estado::Pausada)
            sigue = true;
        std::wstring ruta = f.datos.carpeta + L"\\" + f.datos.nombre;
        Pieza p;
        if (f.datos.nombre.empty() || !PiezaDe(ruta, p))
        {
            // Una que todavía no baja y no se sabe qué es (sin nombre, o sin una extensión conocida): puede ser de un juego
            if (!bajada && !OtroArchivo(f.datos.nombre)) r.dudosas++;
            continue;
        }
        Juego& j = juegoDe(p, f.datos.carpeta);
        numeros[p.clave].insert(p.numero);
        if (bajada) bajadas[p.clave].push_back({ p.numero, { f.datos.id, ruta, f.datos.identidad } });
        else j.faltan++;
        // Lo más avanzado de sus partes: si una ya está hecha (o en curso), el juego también
        if (f.datos.jicaro == EstadoJicaro::Hecho || (f.datos.jicaro == EstadoJicaro::EnCurso && j.jicaro == EstadoJicaro::Pendiente))
            j.jicaro = f.datos.jicaro;
    }
    for (const auto& q : cola)
    {
        if (q.grupo != grupo) continue;
        if (q.estado == EstadoParte::Esperando || q.estado == EstadoParte::Abierta || q.estado == EstadoParte::Tomada
            || q.estado == EstadoParte::Espera || q.estado == EstadoParte::Navegador)
            sigue = true;
        // Las que ya bajaron (o la está bajando Atarraya) están en la lista de descargas
        if (q.estado == EstadoParte::Lista || (q.descarga && enLaLista.count(q.descarga))) continue;
        std::wstring nombre = NombreDeParte(q.nombre);
        std::wstring carpeta = q.carpeta;
        Pieza p;
        if (nombre.empty() || carpeta.empty() || !PiezaDe(carpeta + L"\\" + nombre, p))
        {
            if (!OtroArchivo(nombre)) r.dudosas++;
            continue;
        }
        juegoDe(p, carpeta).faltan++;
        numeros[p.clave].insert(p.numero);
    }
    r.terminado = !sigue;
    for (auto& j : r.juegos)
    {
        auto& b = bajadas[j.clave];
        std::stable_sort(b.begin(), b.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        for (auto& [n, parte] : b) j.partes.push_back(std::move(parte));
        // Sin huecos: los números de sus partes (bajadas o no) van seguidos desde el primero. «.part1.rar» y «.7z.001»
        // empiezan en 1; el estilo viejo empieza con el .rar (0), un ZIP partido tiene su .zip (0), que es el que se abre,
        // y un .7z entero es el 0.
        const auto& hay = numeros[j.clave];
        wchar_t tipo = j.Tipo();
        int desde = (tipo == L'n' || tipo == L'c') ? 1 : 0;
        j.huecos = hay.empty() || *hay.begin() != desde || (int)hay.size() != *hay.rbegin() - desde + 1;
        // En partes: varias, un «.part1.rar» o «.7z.001», o un .rar o .zip solo que su cabecera dice que es de varias
        bool todas = j.faltan == 0 && !j.partes.empty() && !j.huecos;
        j.enPartes = tipo == L'n' || tipo == L'c' || hay.size() > 1;
        if (!j.enPartes && todas && (tipo == L'v' || tipo == L'z'))
            j.enPartes = tipo == L'v' ? EsVolumenRar(j.partes[0].ruta) : EsZipPartido(j.partes[0].ruta);
        if (todas && j.enPartes)
        {
            std::vector<std::wstring> rutas;
            for (const auto& p : j.partes) rutas.push_back(p.ruta);
            j.ultima = ultima ? ultima(rutas, tipo) : Ultima::NoSe;
        }
        // Completo: un comprimido entero que ya bajó, o todas las partes sin huecos y se sabe que la última es la última.
        // Si eso no se sabe, espera a que termine todo el paquete.
        if (r.dudosas == 0 && todas)
            j.completo = !j.enPartes || j.ultima == Ultima::Es || (j.ultima == Ultima::NoSe && r.terminado);
        // Ya no baja nada del paquete, se sabe de qué juego es cada parte, y a este le falta algo: no se abre (se avisa)
        j.incompleto = r.terminado && r.dudosas == 0 && !j.completo && j.faltan == 0 && !j.partes.empty();
    }
    return r;
}

std::vector<hnd::jicaro::Plan> hnd::jicaro::Planear(const std::vector<ParteBajada>& propias, const std::wstring& paquete)
{
    std::vector<std::wstring> rutas;
    std::map<std::wstring, const ParteBajada*> porRuta;
    for (const auto& p : propias)
    {
        rutas.push_back(p.ruta);
        porRuta.emplace(Minusculas(p.ruta), &p);
    }
    std::vector<Plan> r;
    for (const auto& c : Comprimidos(rutas))
    {
        Plan plan;
        plan.nombre = NombreDeCarpeta(paquete, c.nombre);
        plan.carpeta = CarpetaDe(c.Primera());
        plan.primera = c.Primera();
        for (const auto& ruta : c.partes)
        {
            auto it = porRuta.find(Minusculas(ruta));
            if (it != porRuta.end()) plan.partes.push_back(*it->second);
        }
        r.push_back(std::move(plan));
    }
    return r;
}

hnd::jicaro::Borrado hnd::jicaro::BorrarPartes(const std::vector<ParteBajada>& partes,
                                               const std::function<bool(const std::wstring&)>& aPapelera)
{
    Borrado r;
    for (const auto& p : partes)
    {
        if (GetFileAttributesW(p.ruta.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            DWORD e = GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            {
                r.faltaban++;
                continue;
            }
        }
        // Sin la identidad de cuando terminó, no se sabe si es el mismo archivo: nunca se toca
        if (!p.identidad.Valida())
        {
            r.sinIdentidad++;
            continue;
        }
        // Tiene que ser el mismo archivo que quedó al terminar de bajarlo; si no (o si no se puede revisar), no se toca
        IdentidadArchivo ahora;
        if (!LeerIdentidad(p.ruta, ahora) || !(ahora == p.identidad))
        {
            r.distintas++;
            continue;
        }
        if (aPapelera(p.ruta)) r.mandadas++;
        else r.fallaron++;
    }
    return r;
}

std::string hnd::jicaro::Tanda::Final(std::string grupo) const
{
    for (int i = 0; i < 64; i++)
    {
        auto it = std::find_if(m_uniones.begin(), m_uniones.end(), [&](const Une& u) { return u.viejo == grupo; });
        if (it == m_uniones.end()) break;
        grupo = it->nuevo;
    }
    return grupo;
}

void hnd::jicaro::Tanda::Sumar(const std::wstring& paquete, uint64_t descarga, const std::string& grupoQueTenia)
{
    std::string tenia = grupoQueTenia.empty() ? std::string() : Final(grupoQueTenia);
    auto it = m_grupos.find(paquete);
    if (it == m_grupos.end())
    {
        // El paquete toma el grupo que ya tenía esa descarga; si era suelta, uno nuevo para las dos
        std::string g = tenia.empty() ? NuevoId() : tenia;
        m_grupos[paquete] = g;
        if (tenia.empty()) m_entran.push_back({ descarga, g, paquete });
        return;
    }
    std::string g = Final(it->second);
    if (tenia == g) return;
    if (tenia.empty()) m_entran.push_back({ descarga, g, paquete });
    else m_uniones.push_back({ tenia, g, paquete });   // las de su grupo pasan a este: un solo paquete
}

std::string hnd::jicaro::Tanda::Grupo(const std::wstring& paquete)
{
    auto& g = m_grupos[paquete];
    if (g.empty()) g = NuevoId();
    return Final(g);
}

std::vector<hnd::jicaro::Tanda::Entra> hnd::jicaro::Tanda::Entran() const
{
    std::vector<Entra> r = m_entran;
    for (auto& e : r) e.grupo = Final(e.grupo);
    return r;
}

std::vector<hnd::jicaro::Tanda::Une> hnd::jicaro::Tanda::Uniones() const
{
    std::vector<Une> r = m_uniones;
    for (auto& u : r) u.nuevo = Final(u.nuevo);
    return r;
}

std::wstring hnd::jicaro::NombreDeCarpeta(const std::wstring& paquete, const std::wstring& comprimido)
{
    if (paquete.empty() || EmpiezaCon(paquete, L"Páginas de ") || EmpiezaCon(paquete, L"Enlaces de ")) return comprimido;
    return paquete;
}

std::wstring hnd::jicaro::CarpetaDestino(const std::wstring& carpeta, const std::wstring& nombre)
{
    if (carpeta.empty()) return {};
    std::wstring base = carpeta.back() == L'\\' ? carpeta : carpeta + L"\\";
    std::wstring limpio = LimpiarNombre(nombre);
    for (int i = 1; i <= 999; i++)
    {
        std::wstring ruta = base + (i == 1 ? limpio : limpio + L" (" + std::to_wstring(i) + L")");
        // Libre solo si no hay nada con ese nombre: ni archivo, ni carpeta, ni un enlace (aunque esté roto)
        if (GetFileAttributesW(ruta.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return ruta;
    }
    return {};
}

hnd::jicaro::Resultado hnd::jicaro::LeerCodigo(unsigned long codigo)
{
    switch (codigo)
    {
    case 0: return Resultado::Listo;
    case 2: return Resultado::Cancelado;
    case 3: return Resultado::FaltanPartes;
    case 4: return Resultado::Contrasena;
    case 5: return Resultado::YaExtrayendo;
    case 6: return Resultado::Parcial;
    case 7: return Resultado::ContrasenaIncorrecta;   // Jícaro 0.2.0
    default: return Resultado::Error;   // 1, o Jícaro se cerró de golpe
    }
}

bool hnd::jicaro::PideContrasena(Resultado r)
{
    return r == Resultado::Contrasena || r == Resultado::ContrasenaIncorrecta;
}

hnd::EstadoJicaro hnd::jicaro::EstadoDespues(Resultado r)
{
    // Terminó (bien o a medias) o la persona lo canceló: no se abre otra vez solo. Todo lo demás (contraseña, Jícaro
    // viejo, no se abrió, faltaba algo, falló) queda pendiente.
    return r == Resultado::Listo || r == Resultado::Parcial || r == Resultado::Cancelado ? EstadoJicaro::Hecho : EstadoJicaro::Pendiente;
}

std::wstring hnd::jicaro::Titulo(Resultado r)
{
    switch (r)
    {
    case Resultado::Listo: return L"Jícaro: listo";
    case Resultado::Parcial: return L"Jícaro: listo, con avisos";
    case Resultado::FaltanPartes: return L"Jícaro: faltan partes";
    case Resultado::Contrasena: return L"Jícaro: tiene contraseña";
    case Resultado::ContrasenaIncorrecta: return L"Jícaro: la contraseña no sirve";
    case Resultado::JicaroViejo: return L"Hace falta un Jícaro más nuevo";
    case Resultado::Cancelado: return L"Jícaro: cancelado";
    case Resultado::YaExtrayendo: return L"Jícaro ya lo está descomprimiendo";
    case Resultado::NoAbrio: return L"Jícaro no se abrió";
    case Resultado::PartesAjenas: return L"Jícaro: partes de otro paquete";
    case Resultado::Incompleto: return L"Jícaro: falta una parte";
    default: return L"Jícaro: no se pudo descomprimir";
    }
}

std::wstring hnd::jicaro::Texto(Resultado r, const std::wstring& nombre, const std::wstring& destino)
{
    std::wstring n = L"«" + nombre + L"»";
    std::wstring carpeta = L"«" + NombreDe(destino) + L"»";
    switch (r)
    {
    case Resultado::Listo: return L"Listo: " + n + L" quedó descomprimido en la carpeta " + carpeta + L".";
    case Resultado::Parcial:
        return n + L" se descomprimió en la carpeta " + carpeta + L", pero algunos archivos se saltaron (por seguridad o porque no "
               L"se pudieron sacar). Jícaro dice cuáles en su ventana.";
    case Resultado::FaltanPartes:
        return L"A " + n + L" le faltan partes. Jícaro dice cuáles en su ventana: bájalas, ponlas en la misma carpeta y ábrelo otra "
               L"vez con Jícaro.";
    case Resultado::Contrasena:
        return n + L" tiene contraseña (o un cifrado que Jícaro no abre). Pulsa «Escribir la contraseña...» y Atarraya lo descomprime. "
               L"Las partes siguen en su carpeta.";
    case Resultado::ContrasenaIncorrecta:
        return L"La contraseña de " + n + L" no sirve. Pulsa «Escribir la contraseña...», escribe otra y Atarraya lo intenta de nuevo. "
               L"Las partes siguen en su carpeta.";
    case Resultado::JicaroViejo:
        return n + L" tiene contraseña, y para pasársela a Jícaro hace falta Jícaro 0.2.0 o más nuevo. Actualiza Jícaro desde su página "
               L"y vuelve a intentarlo. Las partes siguen en su carpeta.";
    case Resultado::Cancelado: return L"Se canceló la descompresión de " + n + L". Las partes siguen en su carpeta.";
    case Resultado::YaExtrayendo: return L"Jícaro ya está descomprimiendo " + n + L" en otra ventana.";
    case Resultado::NoAbrio: return L"No se pudo abrir Jícaro para descomprimir " + n + L". Revisa en Ajustes dónde está.";
    case Resultado::PartesAjenas:
        return L"En la carpeta de " + n + L" hay partes con el mismo nombre que no son de este paquete, y Jícaro podría juntarlas. "
               L"Atarraya no lo abrió. Pon cada paquete en su carpeta (en el recolector, «Una carpeta por paquete») o quita esas partes "
               L"de ahí y ábrelo con Jícaro.";
    case Resultado::Incompleto:
        return L"Ya terminó de bajar el paquete, pero a " + n + L" le falta una parte, así que Atarraya no se lo pasó a Jícaro. "
               L"Bájala, ponla en la misma carpeta y ábrelo con Jícaro.";
    default: return L"Jícaro no pudo descomprimir " + n + L". En su ventana dice por qué. Las partes siguen en su carpeta.";
    }
}

void* hnd::jicaro::Abrir(const std::wstring& exe, const std::wstring& orden, const std::string& contrasena)
{
    if (orden.empty() || !EsRutaDeJicaro(exe) || !ExisteArchivo(exe)) return nullptr;
    std::wstring linea = orden;   // CreateProcessW puede escribir en ella
    std::wstring carpeta = CarpetaDe(exe);
    PROCESS_INFORMATION pi = {};
    if (contrasena.empty())
    {
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOWNOACTIVATE;
        // Con la ruta del programa aparte: Windows no busca otro Jicaro.exe en ningún lado
        if (!CreateProcessW(exe.c_str(), linea.data(), nullptr, nullptr, FALSE, 0, nullptr, carpeta.c_str(), &si, &pi)) return nullptr;
        CloseHandle(pi.hThread);
        return pi.hProcess;
    }

    // Con contraseña: un tubo para su entrada estándar. Jícaro hereda solo ese extremo (la lista de handles que puede
    // heredar es exactamente esa) y nada más de Atarraya.
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE leer = nullptr, escribir = nullptr;
    if (!CreatePipe(&leer, &escribir, &sa, 0)) return nullptr;
    SetHandleInformation(escribir, HANDLE_FLAG_INHERIT, 0);
    SIZE_T tam = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &tam);
    std::vector<BYTE> lista(tam);
    auto* atributos = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(lista.data());
    bool ok = tam > 0 && InitializeProcThreadAttributeList(atributos, 1, 0, &tam)
              && UpdateProcThreadAttribute(atributos, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &leer, sizeof(leer), nullptr, nullptr);
    if (ok)
    {
        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.StartupInfo.wShowWindow = SW_SHOWNOACTIVATE;
        si.StartupInfo.hStdInput = leer;
        si.lpAttributeList = atributos;
        ok = CreateProcessW(exe.c_str(), linea.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT, nullptr, carpeta.c_str(),
                            &si.StartupInfo, &pi) != FALSE;
        DeleteProcThreadAttributeList(atributos);
    }
    CloseHandle(leer);
    if (!ok)
    {
        CloseHandle(escribir);
        return nullptr;
    }
    CloseHandle(pi.hThread);
    // La contraseña, tal cual en UTF-8 y sin salto de línea; después se cierra la entrada (así Jícaro sabe que terminó).
    // Cabe entera en el tubo: se escribe aunque Jícaro todavía no la lea.
    DWORD escritos = 0;
    WriteFile(escribir, contrasena.data(), (DWORD)contrasena.size(), &escritos, nullptr);
    CloseHandle(escribir);
    return pi.hProcess;
}
