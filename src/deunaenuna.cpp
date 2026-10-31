#include "deunaenuna.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>

#include <nlohmann/json.hpp>

#include "enlaces.hpp"
#include "nombres.hpp"
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
    if (Terminada()) m_partes.clear();   // ya no quedaba nada por hacer
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

void hnd::DeUnaEnUna::Agregar(std::vector<Parte> partes)
{
    for (auto& p : partes)
    {
        p.estado = EstadoParte::Esperando;
        p.descarga = 0;
        m_partes.push_back(std::move(p));
    }
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

int hnd::DeUnaEnUna::Emparejar(const std::string& url, const std::string& referer, const std::wstring& nombre) const
{
    // Por el nombre: el que dio el navegador y el que trae la dirección del archivo
    std::wstring candidatos[] = { NombreComparable(nombre), NombreComparable(NombreDeUrl(url)) };
    for (const auto& buscado : candidatos)
    {
        if (buscado.empty()) continue;
        for (size_t i = 0; i < m_partes.size(); i++)
        {
            if (m_partes[i].estado == EstadoParte::Abierta && NombreComparable(m_partes[i].nombre) == buscado) return (int)i;
        }
    }
    // Si el nombre no coincide: la primera abierta del mismo sitio (por la página de donde salió o por el servidor)
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        const auto& p = m_partes[i];
        if (p.estado == EstadoParte::Abierta && (MismoSitio(referer, p.url) || MismoSitio(url, p.url))) return (int)i;
    }
    return -1;
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

std::vector<hnd::Parte> hnd::DeUnaEnUna::Parar()
{
    std::vector<Parte> vuelven;
    for (auto& p : m_partes)
    {
        if (p.estado == EstadoParte::Esperando || p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Saltada
            || p.estado == EstadoParte::Fallo)
            vuelven.push_back(p);
    }
    m_partes.clear();
    return vuelven;
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
