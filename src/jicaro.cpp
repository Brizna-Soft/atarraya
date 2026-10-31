#include "jicaro.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <map>
#include <regex>

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
        static const std::wregex viejo(LR"(^(.+)\.([r-z])(\d\d)$)", std::regex::icase);
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
        if (std::regex_match(nombre, m, viejo))
        {
            // Estilo viejo: .r00 ... .r99, después .s00 ...
            wchar_t letra = (wchar_t)towlower(m[2].str()[0]);
            p.clave = L"v|" + carpeta + hnd::Minusculas(m[1].str());
            p.numero = 1 + (letra - L'r') * 100 + std::stoi(m[3].str());
            p.nombre = m[1].str();
            return true;
        }
        if (TerminaCon(bajo, L".zip") || TerminaCon(bajo, L".7z"))
        {
            p.clave = L"u|" + carpeta + bajo;
            p.numero = 0;
            p.nombre = nombre.substr(0, nombre.find_last_of(L'.'));
            return true;
        }
        return false;
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

std::wstring hnd::jicaro::ArmarOrden(const std::wstring& exe, const std::wstring& archivo, const std::wstring& destino)
{
    if (!EsRutaDeJicaro(exe) || !RutaSegura(archivo) || !RutaSegura(destino)) return {};
    // Ninguna ruta trae comillas ni termina en «\»: entre comillas, cada una llega a Jícaro como un solo argumento
    return L"\"" + exe + L"\" --extraer \"" + archivo + L"\" --destino \"" + destino + L"\"";
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
    return p.clave[0] == L'u' || (p.clave[0] == L'v' && p.numero == 0);
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

std::vector<std::wstring> hnd::jicaro::ArchivosDelPaquete(const std::string& grupo, const std::wstring& carpeta,
                                                          const std::vector<Foto>& descargas, const std::vector<Parte>& cola)
{
    std::vector<std::wstring> r;
    if (grupo.empty()) return r;
    for (const auto& p : cola)
    {
        if (p.grupo == grupo && p.estado != EstadoParte::Lista) return {};
    }
    std::wstring bajo = Minusculas(carpeta);
    for (const auto& f : descargas)
    {
        if (f.datos.grupo != grupo) continue;
        if (f.datos.estado != Estado::Completada) return {};
        if (!f.datos.nombre.empty() && Minusculas(f.datos.carpeta) == bajo) r.push_back(f.datos.carpeta + L"\\" + f.datos.nombre);
    }
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
    default: return Resultado::Error;   // 1, o Jícaro se cerró de golpe
    }
}

std::wstring hnd::jicaro::Titulo(Resultado r)
{
    switch (r)
    {
    case Resultado::Listo: return L"Jícaro: listo";
    case Resultado::Parcial: return L"Jícaro: listo, con avisos";
    case Resultado::FaltanPartes: return L"Jícaro: faltan partes";
    case Resultado::Contrasena: return L"Jícaro: tiene contraseña";
    case Resultado::Cancelado: return L"Jícaro: cancelado";
    case Resultado::YaExtrayendo: return L"Jícaro ya lo está descomprimiendo";
    case Resultado::NoAbrio: return L"Jícaro no se abrió";
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
    case Resultado::Contrasena: return n + L" tiene contraseña o un cifrado que Jícaro no abre. Las partes siguen en su carpeta.";
    case Resultado::Cancelado: return L"Se canceló la descompresión de " + n + L". Las partes siguen en su carpeta.";
    case Resultado::YaExtrayendo: return L"Jícaro ya está descomprimiendo " + n + L" en otra ventana.";
    case Resultado::NoAbrio: return L"No se pudo abrir Jícaro para descomprimir " + n + L". Revisa en Ajustes dónde está.";
    default: return L"Jícaro no pudo descomprimir " + n + L". En su ventana dice por qué. Las partes siguen en su carpeta.";
    }
}

void* hnd::jicaro::Abrir(const std::wstring& exe, const std::wstring& orden)
{
    if (orden.empty() || !EsRutaDeJicaro(exe) || !ExisteArchivo(exe)) return nullptr;
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNOACTIVATE;
    PROCESS_INFORMATION pi = {};
    std::wstring linea = orden;   // CreateProcessW puede escribir en ella
    std::wstring carpeta = CarpetaDe(exe);
    // Con la ruta del programa aparte: Windows no busca otro Jicaro.exe en ningún lado
    if (!CreateProcessW(exe.c_str(), linea.data(), nullptr, nullptr, FALSE, 0, nullptr, carpeta.c_str(), &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}
