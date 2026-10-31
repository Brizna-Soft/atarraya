#include "recolector.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <regex>
#include <set>

#include "enlaces.hpp"
#include "motor.hpp"
#include "nombres.hpp"
#include "util.hpp"

using json = nlohmann::json;

namespace
{
    constexpr int MaxConsultas = 4;   // consultas a la vez

    const char* NombreComprobacion(hnd::Comprobacion c)
    {
        using C = hnd::Comprobacion;
        switch (c)
        {
        case C::Pendiente: return "pendiente";
        case C::Comprobando: return "pendiente";
        case C::Archivo: return "archivo";
        case C::Pagina: return "pagina";
        case C::Sitio: return "sitio";
        case C::NoExiste: return "noexiste";
        case C::Desconocido: return "desconocido";
        case C::Torrent: return "torrent";
        case C::Local: return "local";
        }
        return "pendiente";
    }

    hnd::Comprobacion ComprobacionDeNombre(const std::string& s)
    {
        using C = hnd::Comprobacion;
        if (s == "archivo") return C::Archivo;
        if (s == "pagina") return C::Pagina;
        if (s == "sitio") return C::Sitio;
        if (s == "noexiste") return C::NoExiste;
        if (s == "desconocido") return C::Desconocido;
        if (s == "torrent") return C::Torrent;
        if (s == "local") return C::Local;
        return C::Pendiente;
    }

    bool TerminaEn(const std::string& sitio, const std::string& dominio)
    {
        return sitio == dominio
            || (sitio.size() > dominio.size() && sitio.compare(sitio.size() - dominio.size() - 1, dominio.size() + 1, "." + dominio) == 0);
    }

    bool EsWebOFtp(const std::string& url)
    {
        std::string bajo = url.substr(0, 8);
        std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return bajo.rfind("http://", 0) == 0 || bajo.rfind("https://", 0) == 0 || bajo.rfind("ftp://", 0) == 0;
    }

    // Nombre que trae un magnet en «dn=»
    std::wstring NombreDeMagnet(const std::string& magnet)
    {
        auto p = magnet.find("dn=");
        if (p == std::string::npos) return L"";
        std::string dn = magnet.substr(p + 3, magnet.find('&', p) == std::string::npos ? std::string::npos : magnet.find('&', p) - p - 3);
        std::replace(dn.begin(), dn.end(), '+', ' ');
        return hnd::LimpiarNombre(hnd::NombreDeUrl("x://x/" + dn));
    }
}

const wchar_t* hnd::TextoComprobacion(Comprobacion c)
{
    switch (c)
    {
    case Comprobacion::Pendiente: return L"Por consultar";
    case Comprobacion::Comprobando: return L"Consultando...";
    case Comprobacion::Archivo: return L"Listo para bajar";
    case Comprobacion::Pagina: return L"Página web: se abre en el navegador";
    case Comprobacion::Sitio: return L"Sitio de descargas: se abre en el navegador";
    case Comprobacion::NoExiste: return L"Ya no está en el servidor";
    case Comprobacion::Desconocido: return L"Se sabrá al empezar";
    case Comprobacion::Torrent: return L"Torrent (magnet)";
    case Comprobacion::Local: return L"Dirección de tu red: no se consulta sola";
    }
    return L"";
}

bool hnd::EsSitioDeDescargas(const std::string& url)
{
    static const char* sitios[] = {
        "rapidgator.net", "rg.to", "1fichier.com", "alterupload.com", "cjoint.net", "desfichiers.com", "dfichiers.com",
        "megadl.fr", "mesfichiers.org", "piecejointe.net", "pjointe.com", "tenvoi.com", "dl4free.com", "mega.nz", "mega.co.nz",
        "mega.io", "mediafire.com", "nitroflare.com", "turbobit.net", "hitfile.net", "katfile.com", "ddownload.com", "ddl.to",
        "uploaded.net", "ul.to", "filefactory.com", "sendspace.com", "uptobox.com", "fikper.com", "gofile.io", "pixeldrain.com",
        "krakenfiles.com", "workupload.com", "send.cm", "uploadhaven.com", "file.al", "hexupload.net", "filestore.to",
        "filecrypt.cc", "filecrypt.co", "keeplinks.org", "drive.google.com", "dropbox.com", "onedrive.live.com", "1drv.ms",
    };
    std::string s = enlaces::Sitio(url);
    for (const char* d : sitios)
    {
        if (TerminaEn(s, d)) return true;
    }
    return false;
}

bool hnd::EsDireccionLocal(const std::string& url)
{
    std::string s = enlaces::Sitio(url);
    if (s.empty()) return true;
    if (s == "localhost" || TerminaEn(s, "localhost")) return true;
    for (const char* d : { "local", "lan", "home", "internal", "intranet", "corp", "home.arpa" })
    {
        if (TerminaEn(s, d)) return true;
    }
    if (s[0] == '[')
    {
        // IPv6: la propia PC (::1), enlace local (fe80::) y direcciones privadas (fc00::/7)
        std::string ip = s.substr(1, s.size() - 2);
        return ip == "::1" || ip.rfind("fe8", 0) == 0 || ip.rfind("fe9", 0) == 0 || ip.rfind("fea", 0) == 0 || ip.rfind("feb", 0) == 0
            || ip.rfind("fc", 0) == 0 || ip.rfind("fd", 0) == 0 || ip == "::";
    }
    if (s.find('.') == std::string::npos) return true;   // un nombre sin punto es de la red local (o un número raro)
    // IPv4 escrita con números
    int partes[4] = { -1, -1, -1, -1 };
    if (sscanf_s(s.c_str(), "%d.%d.%d.%d", &partes[0], &partes[1], &partes[2], &partes[3]) == 4
        && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c) || c == '.'; }))
    {
        int a = partes[0], b = partes[1];
        return a == 127 || a == 10 || a == 0 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254)
            || (a == 100 && b >= 64 && b <= 127);
    }
    // Formas raras de escribir una IP («0x7f.1», «127.1»...): mejor no consultarlas solas
    if (s.find("0x") != std::string::npos) return true;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c) || c == '.'; });
}

bool hnd::EnlaceParaRecolectar(const std::string& url)
{
    if (enlaces::EsMagnet(url)) return true;
    if (!EsWebOFtp(url) || enlaces::NoPermitido(url)) return false;
    if (EsSitioDeDescargas(url)) return true;
    switch (enlaces::TipoDeNombre(NombreDeUrl(url)))
    {
    case enlaces::Tipo::Video:
    case enlaces::Tipo::Audio:
    case enlaces::Tipo::Documento:
    case enlaces::Tipo::Comprimido:
    case enlaces::Tipo::Programa:
    case enlaces::Tipo::Disco:
    case enlaces::Tipo::Torrent:
    case enlaces::Tipo::Subtitulos:
        return true;
    default:
        return false;   // páginas, imágenes y archivos raros: se copian a menudo y no son para bajar
    }
}

std::wstring hnd::PaqueteDe(const EnlaceRecolectado& e)
{
    if (!e.paquete.empty()) return e.paquete;
    std::wstring sitio = Ancho(enlaces::Sitio(e.url));
    if (e.estado == Comprobacion::Torrent) return e.nombre.empty() ? std::wstring(L"Torrents") : e.nombre;
    if (e.estado == Comprobacion::Pagina || e.estado == Comprobacion::Sitio) return L"Páginas de " + sitio;
    std::wstring nombre = e.nombre.empty() ? NombreDeUrl(e.url) : e.nombre;
    if (nombre.empty()) return L"Enlaces de " + sitio;
    // Las partes de un mismo archivo van juntas: «peli.part1.rar», «peli.r00», «peli.7z.001», «peli.zip.002»...
    static const std::wregex partes[] = {
        std::wregex(LR"(^(.+)\.part\d+\.rar$)", std::regex::icase),
        std::wregex(LR"(^(.+)\.(r\d{2,3}|z\d{2})$)", std::regex::icase),
        std::wregex(LR"(^(.+)\.(7z|zip|rar|tar)\.\d{3}$)", std::regex::icase),
        std::wregex(LR"(^(.+)\.\d{3}$)", std::regex::icase),
        std::wregex(LR"(^(.+)\.[A-Za-z0-9]{1,6}$)", std::regex::icase),
    };
    std::wsmatch m;
    for (const auto& r : partes)
    {
        if (std::regex_match(nombre, m, r))
        {
            std::wstring base = m[1].str();
            while (!base.empty() && (base.back() == L' ' || base.back() == L'.' || base.back() == L'_' || base.back() == L'-')) base.pop_back();
            if (!base.empty()) return base;
        }
    }
    return nombre;
}

hnd::Recolector::Recolector(std::wstring archivo) : m_archivo(std::move(archivo)) {}

hnd::Recolector::~Recolector()
{
    Detener();
}

void hnd::Recolector::Cargar()
{
    std::string texto;
    if (!LeerArchivo(m_archivo, texto)) return;
    try
    {
        json todo = json::parse(texto);
        std::lock_guard g(m_m);
        for (auto& j : todo.at("enlaces"))
        {
            if (m_lista.size() >= Maximo) break;
            EnlaceRecolectado e;
            e.id = m_siguiente++;
            e.url = j.value("url", "");
            if (e.url.empty()) continue;
            e.referer = j.value("referer", "");
            e.nombre = Ancho(j.value("nombre", ""));
            e.tamano = j.value("tamano", (int64_t)-1);
            e.estado = ComprobacionDeNombre(j.value("estado", ""));
            e.detalle = j.value("detalle", "");
            e.paquete = Ancho(j.value("paquete", ""));
            e.origen = j.value("origen", "");
            e.marcado = j.value("marcado", true);
            e.agregado = j.value("agregado", (int64_t)0);
            m_lista.push_back(std::move(e));
        }
    }
    catch (...)
    {
        CopyFileW(m_archivo.c_str(), (m_archivo + L".danado").c_str(), FALSE);
    }
    m_version++;
    m_guardada = m_version;
}

void hnd::Recolector::Guardar()
{
    json l = json::array();
    uint64_t version;
    {
        std::lock_guard g(m_m);
        version = m_version;
        for (auto& e : m_lista)
        {
            l.push_back({ { "url", e.url }, { "referer", e.referer }, { "nombre", Utf8(e.nombre) }, { "tamano", e.tamano },
                          { "estado", NombreComprobacion(e.estado) }, { "detalle", e.detalle }, { "paquete", Utf8(e.paquete) },
                          { "origen", e.origen }, { "marcado", e.marcado }, { "agregado", e.agregado } });
        }
    }
    json todo = { { "version", 1 }, { "enlaces", l } };
    if (EscribirSeguro(m_archivo, todo.dump(1))) m_guardada = version;
}

size_t hnd::Recolector::Agregar(const std::vector<std::string>& urls, const std::wstring& paquete, const std::string& referer,
                                const std::string& origen)
{
    std::lock_guard g(m_m);
    std::set<std::string> ya;
    for (auto& e : m_lista) ya.insert(e.url);
    size_t n = 0;
    for (const auto& url : urls)
    {
        if (m_lista.size() >= Maximo) break;
        bool magnet = enlaces::EsMagnet(url);
        if (url.empty() || url.size() > 8192 || (!magnet && !EsWebOFtp(url)) || (!magnet && enlaces::NoPermitido(url))) continue;
        if (!ya.insert(url).second) continue;
        EnlaceRecolectado e;
        e.id = m_siguiente++;
        e.url = url;
        e.referer = referer;
        e.paquete = paquete.substr(0, 150);
        e.origen = origen;
        e.agregado = AhoraUnix();
        if (magnet)
        {
            e.estado = Comprobacion::Torrent;
            e.nombre = NombreDeMagnet(url);
        }
        else
        {
            e.nombre = NombreDeUrl(url);
            if (EsSitioDeDescargas(url)) e.estado = Comprobacion::Sitio;
            else if (EsDireccionLocal(url)) e.estado = Comprobacion::Local;
        }
        if (e.nombre.empty()) e.nombre = Ancho(enlaces::Sitio(url));
        m_lista.push_back(std::move(e));
        n++;
    }
    if (n > 0) m_version++;
    return n;
}

std::vector<hnd::EnlaceRecolectado> hnd::Recolector::Lista()
{
    std::lock_guard g(m_m);
    return m_lista;
}

void hnd::Recolector::Marcar(const std::vector<uint64_t>& ids, bool si)
{
    std::lock_guard g(m_m);
    for (auto& e : m_lista)
    {
        if (std::find(ids.begin(), ids.end(), e.id) != ids.end()) e.marcado = si;
    }
    m_version++;
}

void hnd::Recolector::Quitar(const std::vector<uint64_t>& ids)
{
    std::lock_guard g(m_m);
    auto fin = std::remove_if(m_lista.begin(), m_lista.end(), [&](const EnlaceRecolectado& e) {
        return std::find(ids.begin(), ids.end(), e.id) != ids.end();
    });
    if (fin != m_lista.end())
    {
        m_lista.erase(fin, m_lista.end());
        m_version++;
    }
}

size_t hnd::Recolector::Cantidad()
{
    std::lock_guard g(m_m);
    return m_lista.size();
}

bool hnd::Recolector::Contiene(const std::string& url)
{
    std::lock_guard g(m_m);
    return std::any_of(m_lista.begin(), m_lista.end(), [&](const EnlaceRecolectado& e) { return e.url == url; });
}

void hnd::Recolector::Consultar(std::shared_ptr<std::atomic<bool>> fin)
{
    while (!m_parar)
    {
        uint64_t id = 0;
        std::string url, referer;
        {
            std::lock_guard g(m_m);
            for (auto& e : m_lista)
            {
                if (e.estado == Comprobacion::Pendiente)
                {
                    e.estado = Comprobacion::Comprobando;
                    id = e.id;
                    url = e.url;
                    referer = e.referer;
                    m_version++;
                    break;
                }
            }
        }
        if (id == 0) break;
        InfoEnlace info;
        ComprobarEnlace(url, referer, info, &m_parar);
        std::lock_guard g(m_m);
        auto it = std::find_if(m_lista.begin(), m_lista.end(), [&](const EnlaceRecolectado& e) { return e.id == id; });
        if (it == m_lista.end()) continue;   // la persona lo quitó mientras tanto
        auto& e = *it;
        if (m_parar)
        {
            e.estado = Comprobacion::Pendiente;
            break;
        }
        if (!info.error.empty())
        {
            e.estado = Comprobacion::Desconocido;
            e.detalle = "No se pudo consultar: " + info.error;
        }
        else if (info.codigo == 404 || info.codigo == 410)
        {
            e.estado = Comprobacion::NoExiste;
            e.detalle = "El servidor dice que ya no está (código " + std::to_string(info.codigo) + ").";
        }
        else if (info.codigo >= 400)
        {
            e.estado = Comprobacion::Desconocido;
            e.detalle = "El servidor no deja consultarlo sin bajarlo (código " + std::to_string(info.codigo) + "). Se sabrá al empezar.";
        }
        else if (info.pagina)
        {
            e.estado = Comprobacion::Pagina;
            e.detalle = "Es una página web, no un archivo: se abre en el navegador.";
        }
        else
        {
            e.estado = Comprobacion::Archivo;
            if (!info.nombre.empty()) e.nombre = info.nombre;
            e.tamano = info.tamano;
            e.detalle.clear();
        }
        m_version++;
    }
    *fin = true;
}

void hnd::Recolector::Tick()
{
    // Los hilos que ya terminaron
    for (auto it = m_hilos.begin(); it != m_hilos.end();)
    {
        if (*it->fin)
        {
            if (it->t.joinable()) it->t.join();
            it = m_hilos.erase(it);
        }
        else ++it;
    }
    size_t pendientes = 0;
    {
        std::lock_guard g(m_m);
        for (auto& e : m_lista)
        {
            if (e.estado == Comprobacion::Pendiente) pendientes++;
        }
    }
    while (!m_parar && m_hilos.size() < (size_t)MaxConsultas && pendientes > m_hilos.size())
    {
        Hilo h;
        h.fin = std::make_shared<std::atomic<bool>>(false);
        h.t = std::thread(&Recolector::Consultar, this, h.fin);
        m_hilos.push_back(std::move(h));
    }
    static int64_t ultimo = 0;
    int64_t ahora = AhoraUnix();
    if (m_version != m_guardada && ahora - ultimo >= 2)
    {
        ultimo = ahora;
        Guardar();
    }
}

void hnd::Recolector::Detener()
{
    m_parar = true;
    for (auto& h : m_hilos)
    {
        if (h.t.joinable()) h.t.join();
    }
    m_hilos.clear();
    {
        std::lock_guard g(m_m);
        for (auto& e : m_lista)
        {
            if (e.estado == Comprobacion::Comprobando) e.estado = Comprobacion::Pendiente;
        }
    }
    if (m_version != m_guardada) Guardar();
}
