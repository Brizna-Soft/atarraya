#include "deunaenuna.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <map>

#include <nlohmann/json.hpp>

#include "enlaces.hpp"
#include "nombres.hpp"
#include "recolector.hpp"
#include "util.hpp"

using json = nlohmann::json;

namespace
{
    using E = hnd::EstadoParte;

    const char* NombreEstado(E e)
    {
        switch (e)
        {
        case E::Esperando: return "esperando";
        case E::Abierta: return "abierta";
        case E::Tomada: return "tomada";
        case E::Lista: return "lista";
        case E::Fallo: return "fallo";
        case E::Saltada: return "saltada";
        }
        return "esperando";
    }

    E EstadoDeNombre(const std::string& s)
    {
        if (s == "abierta") return E::Abierta;
        if (s == "tomada") return E::Tomada;
        if (s == "lista") return E::Lista;
        if (s == "fallo") return E::Fallo;
        if (s == "saltada") return E::Saltada;
        return E::Esperando;
    }

    // Sitios que usan más de un dominio: el primero de cada grupo es el que vale
    const std::vector<std::vector<std::string>>& Grupos()
    {
        static const std::vector<std::vector<std::string>> g = {
            { "ddownload.com", "ddl.to" },
            { "rapidgator.net", "rg.to" },
            { "uploaded.net", "ul.to" },
            { "mega.nz", "mega.co.nz", "mega.io" },
            { "1fichier.com", "alterupload.com", "cjoint.net", "desfichiers.com", "dfichiers.com", "megadl.fr",
              "mesfichiers.org", "piecejointe.net", "pjointe.com", "tenvoi.com", "dl4free.com" },
        };
        return g;
    }

    // El sitio sin los servidores de adelante (s12.ddownload.com → ddownload.com): las dos últimas partes del nombre,
    // o tres si termina como «co.uk» o «com.ar». Una dirección IP queda tal cual.
    std::string Base(const std::string& sitio)
    {
        if (sitio.empty()) return {};
        bool ip = sitio[0] == '[' || std::all_of(sitio.begin(), sitio.end(), [](char c) { return std::isdigit((unsigned char)c) || c == '.'; });
        if (ip) return sitio;
        std::vector<std::string> partes;
        size_t desde = 0;
        while (desde <= sitio.size())
        {
            size_t punto = sitio.find('.', desde);
            if (punto == std::string::npos) punto = sitio.size();
            partes.push_back(sitio.substr(desde, punto - desde));
            desde = punto + 1;
        }
        size_t n = partes.size();
        size_t tomar = n >= 2 ? 2 : n;
        static const char* segundos[] = { "co", "com", "net", "org", "gov", "gob", "edu", "ac" };
        if (n >= 3 && partes[n - 1].size() == 2
            && std::any_of(std::begin(segundos), std::end(segundos), [&](const char* s) { return partes[n - 2] == s; }))
            tomar = 3;
        std::string base;
        for (size_t i = n - tomar; i < n; i++) base += (base.empty() ? "" : ".") + partes[i];
        for (auto& grupo : Grupos())
        {
            if (std::find(grupo.begin(), grupo.end(), base) != grupo.end()) return grupo[0];
        }
        return base;
    }
}

std::wstring hnd::NombreComparable(const std::wstring& nombre)
{
    std::wstring n = Minusculas(nombre);
    for (const std::wstring fin : { L".html", L".htm" })
    {
        if (n.size() > fin.size() && n.compare(n.size() - fin.size(), fin.size(), fin) == 0)
        {
            n.resize(n.size() - fin.size());
            break;
        }
    }
    std::wstring r;
    for (wchar_t c : n)
    {
        if (std::iswalnum(c)) r += c;
    }
    return r;
}

bool hnd::MismoSitio(const std::string& a, const std::string& b)
{
    std::string x = Base(enlaces::Sitio(a)), y = Base(enlaces::Sitio(b));
    return !x.empty() && x == y;
}

namespace
{
    // El sitio (sin «www.») y la ruta (con «?...», sin «#...» ni «/» al final) de una dirección
    bool Partir(const std::string& url, std::string& sitio, std::string& ruta)
    {
        sitio = hnd::enlaces::Sitio(url);
        if (sitio.rfind("www.", 0) == 0) sitio = sitio.substr(4);
        auto esquema = url.find("://");
        if (sitio.empty() || esquema == std::string::npos) return false;
        auto barra = url.find_first_of("/?", esquema + 3);
        ruta = barra == std::string::npos ? "/" : url.substr(barra);
        ruta = ruta.substr(0, ruta.find('#'));
        if (ruta.empty() || ruta[0] != '/') ruta = "/" + ruta;
        while (ruta.size() > 1 && ruta.back() == '/') ruta.pop_back();
        return true;
    }

    // ¿Dice una página, y no solo el sitio? (un referer como «https://ddownload.com/» solo dice el sitio)
    bool EsPagina(const std::string& url)
    {
        std::string sitio, ruta;
        return Partir(url, sitio, ruta) && ruta != "/";
    }
}

bool hnd::MismaPagina(const std::string& a, const std::string& b)
{
    std::string s1, r1, s2, r2;
    if (!Partir(a, s1, r1) || !Partir(b, s2, r2) || s1 != s2 || r1 == "/" || r2 == "/") return false;
    if (r1 == r2) return true;
    const std::string& corta = r1.size() < r2.size() ? r1 : r2;
    const std::string& larga = r1.size() < r2.size() ? r2 : r1;
    if (larga.compare(0, corta.size(), corta) != 0 || std::string("/?&").find(larga[corta.size()]) == std::string::npos) return false;
    // Lo que tienen en común lleva un código: un trozo de 6 o más letras o números seguidos
    size_t seguidos = 0;
    for (char c : corta)
    {
        seguidos = std::isalnum((unsigned char)c) ? seguidos + 1 : 0;
        if (seguidos >= 6) return true;
    }
    return false;
}

bool hnd::GuardarEnRecolector(const std::vector<Parte>& partes, Recolector& recolector)
{
    if (partes.empty()) return true;
    // Por paquete y por página de origen, como llegaron
    std::map<std::pair<std::wstring, std::string>, std::vector<std::string>> grupos;
    for (const auto& p : partes) grupos[{ p.paquete, p.referer }].push_back(p.url);
    for (const auto& [clave, urls] : grupos) recolector.Agregar(urls, clave.first, clave.second, "de-una-en-una", true);
    // Tienen que estar todas (las que ya estaban cuentan); si no, no se vacía la cola
    for (const auto& p : partes)
    {
        if (!recolector.Contiene(p.url)) return false;
    }
    return recolector.Guardar();
}

hnd::DeUnaEnUna::DeUnaEnUna(std::wstring archivo) : m_archivo(std::move(archivo)) {}

void hnd::DeUnaEnUna::Cargar()
{
    m_partes.clear();
    std::string texto;
    if (!LeerArchivo(m_archivo, texto)) return;
    try
    {
        json todo = json::parse(texto);
        for (auto& j : todo.at("partes"))
        {
            Parte p;
            p.url = j.value("url", "");
            if (p.url.empty() || p.url.size() > 8192) continue;
            p.referer = j.value("referer", "");
            p.nombre = Ancho(j.value("nombre", ""));
            p.paquete = Ancho(j.value("paquete", ""));
            p.carpeta = Ancho(j.value("carpeta", ""));
            p.estado = EstadoDeNombre(j.value("estado", ""));
            p.descarga = j.value("descarga", (uint64_t)0);
            m_partes.push_back(std::move(p));
            if (m_partes.size() >= 5000) break;
        }
    }
    catch (...)
    {
        m_partes.clear();
        CopyFileW(m_archivo.c_str(), (m_archivo + L".danado").c_str(), FALSE);
    }
    // Si ya no queda nada por hacer ni por devolver, no hay nada que mostrar. Las saltadas o con error se quedan en la
    // cola hasta que lleguen al recolector (por ejemplo, si la última vez no se pudo guardar el recolector).
    if (Terminada() && Sobrantes().empty()) m_partes.clear();
}

bool hnd::DeUnaEnUna::Guardar() const
{
    json l = json::array();
    for (auto& p : m_partes)
    {
        l.push_back({ { "url", p.url }, { "referer", p.referer }, { "nombre", Utf8(p.nombre) }, { "paquete", Utf8(p.paquete) },
                      { "carpeta", Utf8(p.carpeta) }, { "estado", NombreEstado(p.estado) }, { "descarga", p.descarga } });
    }
    json todo = { { "version", 1 }, { "partes", l } };
    return EscribirSeguro(m_archivo, todo.dump(1, ' ', false, json::error_handler_t::replace));
}

bool hnd::DeUnaEnUna::Agregar(std::vector<Parte> partes)
{
    size_t antes = m_partes.size();
    for (auto& p : partes)
    {
        p.estado = EstadoParte::Esperando;
        p.descarga = 0;
        m_partes.push_back(std::move(p));
    }
    if (Guardar()) return true;
    m_partes.resize(antes);   // no se pudo guardar: la cola queda como estaba
    return false;
}

int hnd::DeUnaEnUna::AbrirSiguiente()
{
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        if (m_partes[i].estado == EstadoParte::Esperando)
        {
            m_partes[i].estado = EstadoParte::Abierta;
            return (int)i;
        }
    }
    return -1;
}

hnd::Emparejamiento hnd::DeUnaEnUna::Emparejar(const std::string& url, const std::string& referer, const std::string& pagina,
                                               const std::wstring& nombre) const
{
    // Los nombres de la descarga: el que dio el navegador y el que trae la dirección del archivo
    std::vector<std::wstring> nombres;
    for (const auto& n : { NombreComparable(nombre), NombreComparable(NombreDeUrl(url)) })
    {
        if (!n.empty()) nombres.push_back(n);
    }
    auto tieneSuNombre = [&](const Parte& p) {
        std::wstring n = NombreComparable(p.nombre);
        return !n.empty() && std::find(nombres.begin(), nombres.end(), n) != nombres.end();
    };
    // Viene del sitio de la parte: la página de origen, la pestaña o el servidor del archivo son de ese sitio
    auto deSuSitio = [&](const Parte& p) { return MismoSitio(referer, p.url) || MismoSitio(pagina, p.url) || MismoSitio(url, p.url); };
    // Viene de la página que Atarraya abrió para la parte
    auto deSuPagina = [&](const Parte& p) { return MismaPagina(referer, p.url) || MismaPagina(pagina, p.url); };
    // Se sabe de qué página viene (la pestaña o un referer con página, no solo con el sitio) y es otra
    std::string origen = !pagina.empty() ? pagina : (EsPagina(referer) ? referer : std::string());
    auto deOtraPagina = [&](const Parte& p) { return !origen.empty() && !MismaPagina(origen, p.url); };
    auto abierta = [](const Parte& p) { return p.estado == EstadoParte::Abierta; };
    auto falta = [](const Parte& p) { return p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Esperando; };

    // 1. Tiene el nombre de una parte abierta, viene de su sitio y no de otra página (si viene de otra, se pregunta)
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        const auto& p = m_partes[i];
        if (abierta(p) && tieneSuNombre(p) && deSuSitio(p) && !deOtraPagina(p)) return { (int)i, true };
    }
    // 2. Viene de la página de una parte abierta y no tiene el nombre de ninguna parte de la cola
    if (std::none_of(m_partes.begin(), m_partes.end(), tieneSuNombre))
    {
        for (size_t i = 0; i < m_partes.size(); i++)
        {
            if (abierta(m_partes[i]) && deSuPagina(m_partes[i])) return { (int)i, true };
        }
    }
    // 3. Hay duda (se pregunta): tiene el nombre de una parte que falta y viene de su sitio, o viene del sitio de una
    //    parte abierta
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        if (falta(m_partes[i]) && tieneSuNombre(m_partes[i]) && deSuSitio(m_partes[i])) return { (int)i, false };
    }
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        if (abierta(m_partes[i]) && deSuSitio(m_partes[i])) return { (int)i, false };
    }
    return {};
}

void hnd::DeUnaEnUna::Tomar(int i, uint64_t descarga)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    m_partes[i].estado = EstadoParte::Tomada;
    m_partes[i].descarga = descarga;
}

int hnd::DeUnaEnUna::Terminar(uint64_t descarga, bool ok)
{
    if (descarga == 0) return -1;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        auto& p = m_partes[i];
        // También una que falló y la persona reanudó: si ahora termina bien, queda lista
        if (p.descarga == descarga && (p.estado == EstadoParte::Tomada || p.estado == EstadoParte::Fallo))
        {
            p.estado = ok ? EstadoParte::Lista : EstadoParte::Fallo;
            return (int)i;
        }
    }
    return -1;
}

void hnd::DeUnaEnUna::Saltar(int i)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    auto& p = m_partes[i];
    if (p.estado == EstadoParte::Esperando || p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Tomada)
        p.estado = EstadoParte::Saltada;
}

std::vector<hnd::Parte> hnd::DeUnaEnUna::Sobrantes() const
{
    std::vector<Parte> vuelven;
    for (const auto& p : m_partes)
    {
        if (p.estado == EstadoParte::Esperando || p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Saltada
            || p.estado == EstadoParte::Fallo)
            vuelven.push_back(p);
    }
    return vuelven;
}

bool hnd::DeUnaEnUna::Vaciar()
{
    m_partes.clear();
    return Guardar();
}

int hnd::DeUnaEnUna::Actual() const
{
    for (EstadoParte buscado : { EstadoParte::Abierta, EstadoParte::Tomada })
    {
        for (size_t i = 0; i < m_partes.size(); i++)
        {
            if (m_partes[i].estado == buscado) return (int)i;
        }
    }
    return -1;
}

bool hnd::DeUnaEnUna::HayAbierta() const
{
    return Cuantas(EstadoParte::Abierta) > 0;
}

bool hnd::DeUnaEnUna::QuedanPorAbrir() const
{
    return Cuantas(EstadoParte::Esperando) > 0;
}

bool hnd::DeUnaEnUna::Terminada() const
{
    return !m_partes.empty() && Cuantas(EstadoParte::Esperando) == 0 && Cuantas(EstadoParte::Abierta) == 0
        && Cuantas(EstadoParte::Tomada) == 0;
}

size_t hnd::DeUnaEnUna::Cuantas(EstadoParte e) const
{
    return (size_t)std::count_if(m_partes.begin(), m_partes.end(), [&](const Parte& p) { return p.estado == e; });
}
