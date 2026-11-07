#include "deunaenuna.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <map>

#include <nlohmann/json.hpp>

#include "enlaces.hpp"
#include "motor.hpp"
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
        case E::Espera: return "espera";
        case E::Navegador: return "navegador";
        }
        return "esperando";
    }

    const char* NombreToma(hnd::EstadoToma e)
    {
        switch (e)
        {
        case hnd::EstadoToma::Probando: return "probando";
        case hnd::EstadoToma::Tomada: return "tomada";
        case hnd::EstadoToma::Navegador: return "navegador";
        default: return "";
        }
    }

    hnd::EstadoToma TomaDeNombre(const std::string& s)
    {
        if (s == "probando") return hnd::EstadoToma::Probando;
        if (s == "tomada") return hnd::EstadoToma::Tomada;
        if (s == "navegador") return hnd::EstadoToma::Navegador;
        return hnd::EstadoToma::Ninguna;
    }

    E EstadoDeNombre(const std::string& s)
    {
        if (s == "abierta") return E::Abierta;
        if (s == "tomada") return E::Tomada;
        if (s == "lista") return E::Lista;
        if (s == "fallo") return E::Fallo;
        if (s == "saltada") return E::Saltada;
        if (s == "espera") return E::Espera;
        if (s == "navegador") return E::Navegador;
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

bool hnd::GuardarEnRecolector(const std::vector<Parte>& partes, Recolector& recolector,
                              const std::function<std::string(const std::string& grupo)>& cifradasDe)
{
    if (partes.empty()) return true;
    // Por paquete y por página de origen, como llegaron
    std::map<std::pair<std::wstring, std::string>, std::vector<std::string>> grupos;
    for (const auto& p : partes) grupos[{ p.paquete, p.referer }].push_back(p.url);
    for (const auto& [clave, urls] : grupos) recolector.Agregar(urls, clave.first, clave.second, "de-una-en-una", true);
    // La contraseña del paquete vuelve con sus enlaces (cifrada): al confirmarlos otra vez, Jícaro la tiene
    if (cifradasDe)
    {
        std::map<std::string, std::vector<std::string>> porGrupo;
        for (const auto& p : partes)
        {
            if (!p.grupo.empty()) porGrupo[p.grupo].push_back(p.url);
        }
        for (const auto& [grupo, urls] : porGrupo)
        {
            std::string cifradas = cifradasDe(grupo);
            if (!cifradas.empty()) recolector.FijarContrasenasDe(urls, cifradas);
        }
    }
    // Tienen que estar todas (las que ya estaban cuentan); si no, no se vacía la cola
    for (const auto& p : partes)
    {
        if (!recolector.Contiene(p.url)) return false;
    }
    return recolector.Guardar();
}

hnd::DeUnaEnUna::DeUnaEnUna(std::wstring archivo) : m_archivo(std::move(archivo)) {}

hnd::DeUnaEnUna::Carga hnd::DeUnaEnUna::Cargar()
{
    m_partes.clear();
    if (GetFileAttributesW(m_archivo.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? Carga::NoHay : Carga::NoSePudo;
    }
    std::string texto;
    if (!LeerArchivo(m_archivo, texto)) return Carga::NoSePudo;   // está, pero no se pudo leer (por ejemplo, otro lo tiene abierto)
    try
    {
        if (DesdeJson(json::parse(texto))) return Carga::Bien;
    }
    catch (...)
    {
    }
    m_partes.clear();
    CopyFileW(m_archivo.c_str(), (m_archivo + L".danado").c_str(), FALSE);   // una copia para revisarla; el archivo queda igual
    return Carga::NoSePudo;
}

hnd::DeUnaEnUna::Origen hnd::DeUnaEnUna::CargarAlAbrir(const std::string& anexo)
{
    m_partes.clear();
    if (!anexo.empty())
    {
        try
        {
            json a = json::parse(anexo);
            if (a.is_object() && a.contains("cola")) return DesdeJson(a["cola"]) ? Origen::Bien : Origen::NoSePudo;
        }
        catch (...)
        {
            m_partes.clear();
            return Origen::NoSePudo;
        }
    }
    switch (Cargar())
    {
    case Carga::NoHay: return Origen::Bien;   // nada que pasar
    case Carga::Bien: return Origen::DeAntes;
    default: return Origen::NoSePudo;
    }
}

bool hnd::DeUnaEnUna::DesdeJson(const json& todo)
{
    m_partes.clear();
    try
    {
        if (!todo.is_object() || !todo.contains("partes") || !todo["partes"].is_array()) return false;
        for (auto& j : todo.at("partes"))
        {
            Parte p;
            p.url = j.value("url", "");
            if (p.url.empty() || p.url.size() > 8192) continue;
            p.referer = j.value("referer", "");
            p.nombre = Ancho(j.value("nombre", ""));
            p.paquete = Ancho(j.value("paquete", ""));
            p.grupo = j.value("grupo", "");
            p.carpeta = Ancho(j.value("carpeta", ""));
            p.estado = EstadoDeNombre(j.value("estado", ""));
            p.descarga = j.value("descarga", (uint64_t)0);
            p.hasta = j.value("hasta", (int64_t)0);
            p.limite = j.value("limite", false);
            p.intentos = std::clamp(j.value("intentos", 0), 0, 1000);
            p.avisada = j.value("avisada", false);
            p.toma = j.value("toma", "");
            if (p.toma.size() > 128) p.toma.clear();
            p.estadoToma = p.toma.empty() ? EstadoToma::Ninguna : TomaDeNombre(j.value("estadoToma", ""));
            p.tomaDesde = j.value("tomaDesde", (int64_t)0);
            p.rutaNavegador = Ancho(j.value("rutaNavegador", ""));
            if (auto id = j.find("identidadNavegador"); id != j.end() && id->is_object())
            {
                p.identidadNavegador.volumen = id->value("volumen", (uint32_t)0);
                p.identidadNavegador.indice = id->value("indice", (uint64_t)0);
                p.identidadNavegador.tamano = id->value("tamano", (int64_t)-1);
                p.identidadNavegador.modificado = id->value("modificado", (int64_t)0);
            }
            p.finalNavegador = j.value("finalNavegador", false);
            p.sinConfirmar = j.value("sinConfirmar", false) && p.estado == EstadoParte::Tomada;
            p.reanudar = j.value("reanudar", false) && p.estado == EstadoParte::Tomada && !p.sinConfirmar;
            p.alAbrir = j.value("alAbrir", false) && p.sinConfirmar;
            m_partes.push_back(std::move(p));
            if (m_partes.size() >= 5000) break;
        }
    }
    catch (...)
    {
        m_partes.clear();
        return false;
    }
    // Si ya no queda nada por hacer ni por devolver, no hay nada que mostrar. Las saltadas o con error se quedan en la
    // cola hasta que lleguen al recolector (por ejemplo, si la última vez no se pudo guardar el recolector).
    if (Terminada() && Sobrantes().empty()) m_partes.clear();
    return true;
}

json hnd::DeUnaEnUna::ComoJson() const
{
    json l = json::array();
    for (auto& p : m_partes)
    {
        l.push_back({ { "url", p.url }, { "referer", p.referer }, { "nombre", Utf8(p.nombre) }, { "paquete", Utf8(p.paquete) },
                      { "grupo", p.grupo }, { "carpeta", Utf8(p.carpeta) }, { "estado", NombreEstado(p.estado) },
                      { "descarga", p.descarga }, { "hasta", p.hasta }, { "limite", p.limite }, { "intentos", p.intentos },
                      { "avisada", p.avisada }, { "toma", p.toma }, { "estadoToma", NombreToma(p.estadoToma) },
                      { "tomaDesde", p.tomaDesde }, { "rutaNavegador", Utf8(p.rutaNavegador) },
                      { "identidadNavegador", json{ { "volumen", p.identidadNavegador.volumen }, { "indice", p.identidadNavegador.indice },
                                                    { "tamano", p.identidadNavegador.tamano },
                                                    { "modificado", p.identidadNavegador.modificado } } },
                      { "finalNavegador", p.finalNavegador }, { "sinConfirmar", p.sinConfirmar }, { "reanudar", p.reanudar },
                      { "alAbrir", p.alAbrir } });
    }
    return { { "version", 1 }, { "partes", l } };
}

bool hnd::DeUnaEnUna::Guardar() const
{
    if (guardarJunto) return guardarJunto();   // con la lista de descargas, en un solo archivo y de una vez
    return EscribirSeguro(m_archivo, ComoJson().dump(1, ' ', false, json::error_handler_t::replace));
}

bool hnd::DeUnaEnUna::Agregar(std::vector<Parte> partes, size_t* nuevas)
{
    size_t antes = m_partes.size();
    for (auto& p : partes)
    {
        // La misma página no entra dos veces (ni si ya estaba, ni si viene repetida)
        if (std::any_of(m_partes.begin(), m_partes.end(), [&](const Parte& q) { return q.url == p.url; })) continue;
        p.estado = EstadoParte::Esperando;
        p.descarga = 0;
        m_partes.push_back(std::move(p));
    }
    if (nuevas) *nuevas = m_partes.size() - antes;
    if (m_partes.size() == antes || Guardar()) return true;
    m_partes.resize(antes);   // no se pudo guardar: la cola queda como estaba
    if (nuevas) *nuevas = 0;
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
    // Una que espera también cuenta: puede que la persona bajó igual (o que el aviso era de antes)
    auto abierta = [](const Parte& p) { return p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Espera; };
    auto falta = [](const Parte& p) {
        return p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Espera || p.estado == EstadoParte::Esperando;
    };

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
    auto& p = m_partes[i];
    p.estado = EstadoParte::Tomada;
    p.descarga = descarga;
    p.hasta = 0;
    p.limite = false;
    p.toma.clear();
    p.estadoToma = EstadoToma::Ninguna;
    p.sinConfirmar = false;
    p.reanudar = false;
    p.alAbrir = false;
}

void hnd::DeUnaEnUna::Probar(int i, uint64_t descarga, const std::string& toma, int64_t ahora)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    Tomar(i, descarga);
    auto& p = m_partes[i];
    p.toma = toma;
    p.estadoToma = EstadoToma::Probando;
    p.tomaDesde = ahora;
}

uint64_t hnd::DeUnaEnUna::Decidir(int i, bool atarraya)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return 0;
    auto& p = m_partes[i];
    if (p.estadoToma != EstadoToma::Probando) return 0;
    if (atarraya)
    {
        p.estadoToma = EstadoToma::Tomada;
        return 0;
    }
    uint64_t quitar = p.descarga;
    EnNavegador(i, p.toma);
    return quitar;
}

std::vector<uint64_t> hnd::DeUnaEnUna::ResolverTomasAlAbrir()
{
    std::vector<uint64_t> quitar;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        if (m_partes[i].estadoToma != EstadoToma::Probando || m_partes[i].estado != EstadoParte::Tomada) continue;
        uint64_t d = Decidir((int)i, false);
        if (d) quitar.push_back(d);
    }
    return quitar;
}

uint64_t hnd::DeUnaEnUna::NavegadorSiguio(int i)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return 0;
    auto& p = m_partes[i];
    if (p.estado != EstadoParte::Tomada || (p.estadoToma != EstadoToma::Tomada && p.estadoToma != EstadoToma::Probando)) return 0;
    uint64_t quitar = p.descarga;
    EnNavegador(i, p.toma);
    return quitar;
}

uint64_t hnd::DeUnaEnUna::MarcarSinConfirmar(int i)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return 0;
    auto& p = m_partes[i];
    // Una retenida al abrir pasa a ser «sin confirmar» de verdad: la extensión se hizo cargo y ya no vence el plazo
    if (p.estado != EstadoParte::Tomada || p.estadoToma != EstadoToma::Tomada || (p.sinConfirmar && !p.alAbrir)) return 0;
    p.sinConfirmar = true;
    p.alAbrir = false;
    p.reanudar = false;
    return p.descarga;
}

uint64_t hnd::DeUnaEnUna::Confirmar(int i)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return 0;
    auto& p = m_partes[i];
    if (!p.sinConfirmar) return 0;
    p.sinConfirmar = false;
    p.alAbrir = false;
    if (p.estado != EstadoParte::Tomada) return 0;
    p.reanudar = true;   // hasta que de verdad siga
    return p.descarga;
}

std::string hnd::InstanciaDeToma(const std::string& toma)
{
    // «i» + 8 a 32 letras o números + «-n» + el número de la descarga del navegador + «-» + la hora
    if (toma.size() < 12 || toma[0] != 'i') return {};
    size_t n = toma.find("-n");
    if (n == std::string::npos || n < 9 || n > 33) return {};
    std::string instancia = toma.substr(1, n - 1);
    for (char c : instancia)
    {
        if (!std::isdigit((unsigned char)c) && !(c >= 'a' && c <= 'z')) return {};
    }
    return instancia;
}

void hnd::DeUnaEnUna::VolverALaCola(int i)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    auto& p = m_partes[i];
    p.estado = EstadoParte::Esperando;
    p.descarga = 0;
    p.hasta = 0;
    p.limite = false;
    p.avisada = false;
    p.toma.clear();
    p.estadoToma = EstadoToma::Ninguna;
    p.tomaDesde = 0;
    p.bajado = 0;
    p.total = -1;
    p.rutaNavegador.clear();
    p.identidadNavegador = {};
    p.finalNavegador = false;
    p.sinConfirmar = false;
    p.reanudar = false;
    p.alAbrir = false;
}

hnd::Conciliacion hnd::DeUnaEnUna::ConciliarAlAbrir(const std::function<EnLaLista(uint64_t)>& enLaLista)
{
    Conciliacion c;
    auto parte = [](size_t i) { return "parte " + std::to_string(i + 1) + ": "; };
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        auto& p = m_partes[i];
        if (p.estado != EstadoParte::Tomada) continue;
        EnLaLista a = p.descarga && enLaLista ? enLaLista(p.descarga) : EnLaLista{};
        bool completa = a.existe && a.estado == Estado::Completada;
        if (p.estadoToma == EstadoToma::Probando)
        {
            if (completa)
            {
                // Atarraya ya tiene el archivo entero: queda esa (la del navegador sobra; si la extensión todavía
                // pregunta, se le contesta «tomada» y la cancela)
                Decidir((int)i, true);
                c.motivos.push_back(parte(i) + "Atarraya ya la había terminado mientras probaba; queda la suya");
                c.cambio = true;
                continue;
            }
            // Atarraya probaba seguir la del navegador cuando se cerró. Mientras Atarraya está cerrado, la extensión no
            // recibe respuesta y deja que siga la del navegador: esa es la que sigue. Queda la del navegador (la que dio
            // el sitio después del captcha: cancelarla no tiene vuelta atrás) y se quita la de Atarraya, que solo era
            // una prueba y se puede quitar sin perder nada.
            uint64_t d = Decidir((int)i, false);
            if (d) c.quitar.push_back(d);
            c.motivos.push_back(parte(i) + "Atarraya probaba seguirla cuando se cerró; queda la del navegador");
            c.cambio = true;
            continue;
        }
        // Sin su descarga en la lista: la persona la quitó (RevisarPartes la salta, como siempre)
        if (!a.existe) continue;
        // Terminó sin que la cola se enterara: RevisarPartes la da por lista (y prepara Jícaro)
        if (completa) continue;
        if (a.estado == Estado::Error)
        {
            // Se detuvo con un error sin que la cola se enterara: como al terminar con error (al final vuelve al
            // recolector; si la persona la reanuda y termina bien, queda lista)
            p.estado = EstadoParte::Fallo;
            p.sinConfirmar = p.reanudar = p.alAbrir = false;
            c.motivos.push_back(parte(i) + "su descarga se había detenido con un error");
            c.cambio = true;
            continue;
        }
        // Atarraya ya la había tomado y la extensión tenía que cancelar la del navegador. Si Atarraya se cerró antes de
        // que la extensión preguntara, la extensión no recibió respuesta y dejó seguir la del navegador. Hasta que la
        // extensión lo cuente, la de Atarraya queda en pausa («retenida»): así nunca bajan las dos. Si ninguna extensión
        // lo aclara en un minuto (el navegador está cerrado, así que la suya tampoco baja), sigue la de Atarraya.
        if (p.estadoToma == EstadoToma::Tomada && !p.toma.empty() && !p.sinConfirmar)
        {
            p.sinConfirmar = true;
            p.alAbrir = true;
            p.reanudar = false;
            c.motivos.push_back(parte(i) + "la de Atarraya espera a que la extensión cuente si el navegador siguió con la suya");
            c.cambio = true;
        }
    }
    return c;
}

hnd::Conciliacion hnd::DeUnaEnUna::ConciliarConExtension(const Informe& informe, const std::function<EnLaLista(uint64_t)>& enLaLista)
{
    Conciliacion c;
    auto parte = [](size_t i) { return "parte " + std::to_string(i + 1) + ": "; };
    auto informada = [&](const std::string& toma) -> const TomaInformada* {
        for (const auto& t : informe.tomas)
        {
            if (t.toma == toma) return &t;
        }
        return nullptr;
    };
    // La baja (o ya la bajó) el navegador
    auto activa = [](const TomaInformada* t) { return t && (t->estado == "siguiendo" || t->estado == "completa"); };

    // 1. Las partes con una «toma» de esta extensión: solo ella sabe si sigue su descarga del navegador
    for (size_t i = 0; i < m_partes.size() && !informe.instancia.empty(); i++)
    {
        auto& p = m_partes[i];
        if (p.toma.empty() || InstanciaDeToma(p.toma) != informe.instancia) continue;
        const TomaInformada* t = informada(p.toma);
        if (p.estado == EstadoParte::Navegador && !p.finalNavegador)
        {
            if (t) continue;
            // La baja el navegador, pero su extensión ya no la conoce (por ejemplo, se borraron sus datos o pasaron días):
            // nadie la sigue ni va a avisar cuando termine. La parte no tiene ninguna descarga activa: vuelve a la cola
            // para abrirse otra vez.
            VolverALaCola((int)i);
            c.abrir.push_back((int)i);
            c.motivos.push_back(parte(i) + "la extensión ya no sigue la descarga del navegador; vuelve a abrirse");
            c.cambio = true;
            continue;
        }
        if (p.estado != EstadoParte::Tomada) continue;
        if (activa(t))
        {
            EnLaLista a = p.descarga && enLaLista ? enLaLista(p.descarga) : EnLaLista{};
            if (a.existe && a.estado == Estado::Completada)
            {
                // Las dos, pero la de Atarraya ya está entera: queda esa (la del navegador termina sola en su carpeta)
                if (p.estadoToma == EstadoToma::Probando) Decidir((int)i, true);
                p.sinConfirmar = p.alAbrir = false;
                c.motivos.push_back(parte(i) + "el navegador siguió con la suya, pero la de Atarraya ya terminó; queda la de Atarraya");
                c.cambio = true;
                continue;
            }
            // El navegador siguió con la suya (la extensión no pudo cancelarla, o no supo de Atarraya a tiempo) y la de
            // Atarraya también está: queda la del navegador, la que dio el sitio después del captcha (cancelarla no
            // tiene vuelta atrás); la de Atarraya se quita sin perder nada.
            uint64_t d = NavegadorSiguio((int)i);
            if (d) c.quitar.push_back(d);
            c.motivos.push_back(parte(i) + "el navegador siguió con la suya; queda la del navegador y se quita la de Atarraya");
            c.cambio = true;
            continue;
        }
        if (!t || t->estado == "terminada")
        {
            if (p.sinConfirmar)
            {
                // La extensión no conoce ninguna descarga del navegador de esta parte que siga (la canceló, o ya no
                // baja): la de Atarraya es la única. Sigue.
                Confirmar((int)i);
                c.motivos.push_back(parte(i) + "el navegador ya no la baja; sigue la de Atarraya");
                c.cambio = true;
            }
            else if (p.estadoToma == EstadoToma::Probando)
            {
                // Atarraya prueba seguirla, pero la extensión ya no sabe de esa descarga del navegador (perdió lo que
                // estaba decidiendo): nadie la va a cancelar ni a seguir. La de Atarraya es la única que se sigue: queda.
                Decidir((int)i, true);
                c.motivos.push_back(parte(i) + "la extensión ya no decide sobre la del navegador; queda la de Atarraya");
                c.cambio = true;
            }
            continue;
        }
        // «probando» o «sinConfirmar»: la extensión se hizo cargo (decide con Atarraya o lo sigue mirando y avisa).
        // Una retenida al abrir ya no vence: espera lo que diga la extensión.
        if (p.alAbrir)
        {
            p.alAbrir = false;
            c.cambio = true;
        }
    }

    // 2. Descargas del navegador que la extensión sigue y la cola no conoce, de la página de una parte que no tiene
    //    ninguna descarga activa (abierta o esperando): la parte pasa a esa, que es la única. Pasa si Atarraya se cerró
    //    de golpe antes de anotar que probaba seguirla.
    for (const auto& t : informe.tomas)
    {
        if (!activa(&t) || ParteDeToma(t.toma) >= 0) continue;
        int i = ParteDePagina(t.pagina);
        if (i < 0) continue;
        EnNavegador(i, t.toma);
        c.motivos.push_back(parte((size_t)i) + "la baja el navegador (la extensión la seguía y la cola no lo sabía)");
        c.cambio = true;
    }
    return c;
}

bool hnd::DeUnaEnUna::HayRetenidas() const
{
    return std::any_of(m_partes.begin(), m_partes.end(), [](const Parte& p) { return p.alAbrir && p.sinConfirmar; });
}

hnd::Conciliacion hnd::DeUnaEnUna::SoltarRetenidas()
{
    Conciliacion c;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        auto& p = m_partes[i];
        if (!p.alAbrir || !p.sinConfirmar) continue;
        // Ninguna extensión contó nada de su descarga del navegador: el navegador no está abierto (y entonces la suya
        // tampoco baja) o su extensión es de antes. Sigue la de Atarraya; si el navegador después sigue con la suya, la
        // extensión avisa y queda la del navegador (NavegadorSiguio).
        Confirmar((int)i);
        c.motivos.push_back("parte " + std::to_string(i + 1) + ": ninguna extensión contó nada en un minuto; sigue la de Atarraya");
        c.cambio = true;
    }
    return c;
}

hnd::AccionPausa hnd::DeUnaEnUna::RevisarPausa(int i, Estado estado, bool pausando) const
{
    if (i < 0 || (size_t)i >= m_partes.size()) return AccionPausa::Listo;
    const auto& p = m_partes[i];
    if (p.estado != EstadoParte::Tomada) return AccionPausa::Listo;
    bool enMarcha = estado == Estado::EnCola || estado == Estado::Iniciando || estado == Estado::Descargando;
    if (p.sinConfirmar)
    {
        // Tiene que estar en pausa mientras no se sepa: después de un cierre de golpe puede haber vuelto a la cola
        return enMarcha && !pausando ? AccionPausa::Pausar : AccionPausa::Nada;
    }
    if (!p.reanudar) return AccionPausa::Listo;
    // Mientras la pausa no termina (todavía cierra sus conexiones), reanudar no hace nada: se espera y se intenta de nuevo
    if (pausando) return AccionPausa::Nada;
    if (estado == Estado::Pausada) return AccionPausa::Reanudar;
    // Ya sigue (o terminó, o falló: eso lo atiende la cola como siempre)
    return AccionPausa::Listo;
}

bool hnd::DeUnaEnUna::RevisarPausas(const ListaParaPausas& lista, std::vector<int>* repausadas)
{
    bool cambio = false;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        auto& p = m_partes[i];
        if (!p.sinConfirmar && !p.reanudar) continue;
        if (p.estado != EstadoParte::Tomada)
        {
            // Ya no es de Atarraya (terminó, se saltó...): no hay descarga que pausar ni reanudar
            p.sinConfirmar = false;
            p.reanudar = false;
            p.alAbrir = false;
            cambio = true;
            continue;
        }
        Estado estado = Estado::EnCola;
        bool pausando = false;
        if (!lista.buscar || !lista.buscar(p.descarga, estado, pausando)) continue;   // la persona la quitó: la cola la salta
        switch (RevisarPausa((int)i, estado, pausando))
        {
        case AccionPausa::Pausar:
            if (lista.pausar) lista.pausar(p.descarga);
            if (repausadas) repausadas->push_back((int)i);
            break;
        case AccionPausa::Reanudar:
            if (lista.reanudar) lista.reanudar(p.descarga);
            break;
        case AccionPausa::Listo:
            // Ya sigue: primero se guarda la lista de descargas con la descarga reanudada y recién después se borra la
            // marca (si no se pudo guardar, la marca queda y se intenta en la próxima vuelta)
            if (p.reanudar && lista.guardar && lista.guardar())
            {
                p.reanudar = false;
                cambio = true;
            }
            break;
        default: break;
        }
    }
    return cambio;
}

int hnd::DeUnaEnUna::ParteDeToma(const std::string& toma) const
{
    if (toma.empty()) return -1;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        if (m_partes[i].toma == toma) return (int)i;
    }
    return -1;
}

hnd::DeUnaEnUna::RespuestaToma hnd::DeUnaEnUna::EstadoDeToma(const std::string& toma) const
{
    RespuestaToma r;
    int i = ParteDeToma(toma);
    if (i < 0) return r;
    const auto& p = m_partes[i];
    if (p.finalNavegador) r.estado = "terminada";
    else if (p.estadoToma == EstadoToma::Probando) r.estado = "probando";
    else if (p.estadoToma == EstadoToma::Tomada) r.estado = "tomada";
    else if (p.estadoToma == EstadoToma::Navegador) r.estado = "navegador";
    // Todavía importa lo que diga el navegador: la baja él (y no llegó su aviso final), o la baja Atarraya (si el
    // navegador siguió con la suya, queda la del navegador). Si ya terminó, falló o se saltó, ya no.
    r.espera = (p.estado == EstadoParte::Navegador && !p.finalNavegador) || p.estado == EstadoParte::Tomada;
    r.sinConfirmar = p.sinConfirmar && p.estado == EstadoParte::Tomada;
    return r;
}

std::vector<size_t> hnd::OrdenDePuertos(const std::vector<std::wstring>& navegadores, const std::wstring& predeterminado)
{
    std::vector<size_t> orden;
    for (int primero = 1; primero >= 0; primero--)
    {
        for (size_t i = 0; i < navegadores.size(); i++)
        {
            bool es = !predeterminado.empty() && Minusculas(navegadores[i]) == Minusculas(predeterminado);
            if (es == (primero == 1)) orden.push_back(i);
        }
    }
    return orden;
}

std::wstring hnd::ProgramaDeProgId(const std::wstring& progId)
{
    std::wstring p = Minusculas(progId);
    auto empieza = [&](const wchar_t* x) { return p.rfind(x, 0) == 0; };
    if (empieza(L"chromehtml") || empieza(L"chromebhtml") || empieza(L"chromedhtml") || empieza(L"chromesshtm") || empieza(L"chromiumhtm"))
        return L"chrome.exe";
    if (empieza(L"msedge")) return L"msedge.exe";
    if (empieza(L"firefoxurl") || empieza(L"firefoxhtml")) return L"firefox.exe";
    if (empieza(L"brave")) return L"brave.exe";
    if (empieza(L"opera")) return L"opera.exe";
    if (empieza(L"vivaldi")) return L"vivaldi.exe";
    return L"";
}

int64_t hnd::MargenDeEspera(int64_t segundos)
{
    return std::min<int64_t>(5 * 60, std::max<int64_t>(30, segundos / 20));
}

int hnd::DeUnaEnUna::ParteDePagina(const std::string& url) const
{
    if (url.empty()) return -1;
    for (int exacta = 1; exacta >= 0; exacta--)
    {
        for (size_t i = 0; i < m_partes.size(); i++)
        {
            const auto& p = m_partes[i];
            if (p.estado != EstadoParte::Abierta && p.estado != EstadoParte::Espera) continue;
            if (exacta ? p.url == url : MismaPagina(p.url, url)) return (int)i;
        }
    }
    return -1;
}

bool hnd::DeUnaEnUna::Esperar(int i, bool limite, int64_t segundos, int64_t ahora)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return true;
    auto& p = m_partes[i];
    if (p.estado != EstadoParte::Abierta && p.estado != EstadoParte::Espera) return true;
    if (!limite)
    {
        // La cuenta regresiva de la página: la página ya está abierta, solo se muestra y se avisa al final
        if (p.estado != EstadoParte::Abierta || segundos <= 0) return true;
        int64_t nuevo = ahora + segundos;
        if (p.hasta == 0 || p.limite || std::llabs(nuevo - p.hasta) > 2) p.avisada = false;
        p.hasta = nuevo;
        p.limite = false;
        return true;
    }
    // Un límite del sitio. Si ya se abrió otra vez demasiadas veces y sigue pidiendo esperar, se deja para después.
    if (p.intentos >= MaxIntentosEspera)
    {
        p.estado = EstadoParte::Fallo;
        p.hasta = 0;
        p.limite = false;
        return false;
    }
    if (segundos < 0) segundos = EsperaSinTiempo;
    p.estado = EstadoParte::Espera;
    p.limite = true;
    p.avisada = false;
    p.hasta = ahora + segundos + MargenDeEspera(segundos);
    return true;
}

std::vector<int> hnd::DeUnaEnUna::Vencidas(int64_t ahora)
{
    std::vector<int> r;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        auto& p = m_partes[i];
        if (p.estado != EstadoParte::Espera || p.hasta > ahora) continue;
        p.estado = EstadoParte::Abierta;
        p.intentos++;
        p.hasta = 0;
        p.limite = false;
        p.avisada = false;
        r.push_back((int)i);
    }
    return r;
}

void hnd::DeUnaEnUna::AbrirOtraVez(int i)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    auto& p = m_partes[i];
    if (p.estado != EstadoParte::Espera && p.estado != EstadoParte::Abierta) return;
    p.estado = EstadoParte::Abierta;
    p.hasta = 0;
    p.limite = false;
    p.avisada = false;
}

std::vector<int> hnd::DeUnaEnUna::CuentasTerminadas(int64_t ahora)
{
    std::vector<int> r;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        auto& p = m_partes[i];
        if (p.estado != EstadoParte::Abierta || p.limite || p.hasta == 0 || p.hasta > ahora || p.avisada) continue;
        p.avisada = true;
        r.push_back((int)i);
    }
    return r;
}

void hnd::DeUnaEnUna::EnNavegador(int i, const std::string& toma)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    auto& p = m_partes[i];
    p.estado = EstadoParte::Navegador;
    p.toma = toma;
    p.estadoToma = EstadoToma::Navegador;
    p.descarga = 0;
    p.hasta = 0;
    p.limite = false;
    p.bajado = 0;
    p.total = -1;
    p.rutaNavegador.clear();
    p.identidadNavegador = {};
    p.finalNavegador = false;
    p.sinConfirmar = false;
    p.reanudar = false;
    p.alAbrir = false;
}

int hnd::DeUnaEnUna::BuscarToma(const std::string& toma) const
{
    if (toma.empty()) return -1;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        // También una que se cortó en el navegador: la persona puede reanudarla ahí
        const auto& p = m_partes[i];
        if ((p.estado == EstadoParte::Navegador || p.estado == EstadoParte::Fallo) && p.toma == toma) return (int)i;
    }
    return -1;
}

void hnd::DeUnaEnUna::Progreso(int i, int64_t bajado, int64_t total)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    m_partes[i].bajado = std::max<int64_t>(0, bajado);
    m_partes[i].total = total > 0 ? total : -1;
}

void hnd::DeUnaEnUna::TerminarEnNavegador(int i, bool ok, uint64_t descarga)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    auto& p = m_partes[i];
    if (p.estado != EstadoParte::Navegador && !(p.estado == EstadoParte::Fallo && ok && !p.toma.empty())) return;
    p.estado = ok ? EstadoParte::Lista : EstadoParte::Fallo;
    p.descarga = descarga;
}

void hnd::DeUnaEnUna::AnotarArchivoDelNavegador(int i, const std::wstring& ruta, const IdentidadArchivo& identidad)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    auto& p = m_partes[i];
    p.rutaNavegador = ruta;
    p.identidadNavegador = identidad;
    p.finalNavegador = true;
}

std::vector<int> hnd::DeUnaEnUna::PorMover() const
{
    std::vector<int> r;
    for (size_t i = 0; i < m_partes.size(); i++)
    {
        const auto& p = m_partes[i];
        if (p.estado == EstadoParte::Navegador && p.finalNavegador && !p.rutaNavegador.empty() && p.identidadNavegador.Valida())
            r.push_back((int)i);
    }
    return r;
}

namespace
{
    // Una ruta completa con letra de unidad, con sus nombres largos (como la devuelve Windows al abrir el archivo)
    bool RutaCompleta(const std::wstring& ruta, std::wstring& completa)
    {
        if (ruta.size() < 4 || ruta.size() > 30000 || !std::iswalpha(ruta[0]) || ruta[1] != L':' || (ruta[2] != L'\\' && ruta[2] != L'/'))
            return false;
        if (ruta.find_first_of(L"*?\"<>|", 0) != std::wstring::npos || ruta.find(L':', 2) != std::wstring::npos) return false;
        std::wstring buf(32768, L'\0');
        DWORD n = GetFullPathNameW(ruta.c_str(), (DWORD)buf.size(), buf.data(), nullptr);
        if (n == 0 || n >= buf.size()) return false;
        buf.resize(n);
        std::wstring largo(32768, L'\0');
        DWORD m = GetLongPathNameW(buf.c_str(), largo.data(), (DWORD)largo.size());
        if (m > 0 && m < largo.size())
        {
            largo.resize(m);
            buf = largo;
        }
        while (buf.size() > 3 && buf.back() == L'\\') buf.pop_back();
        completa = buf;
        return true;
    }

    // Ninguna carpeta de la ruta (ni lo último) es un enlace simbólico ni un punto de unión
    bool SinEnlaces(const std::wstring& ruta)
    {
        for (size_t i = 3; i <= ruta.size(); i++)
        {
            if (i < ruta.size() && ruta[i] != L'\\') continue;
            DWORD a = GetFileAttributesW(ruta.substr(0, i).c_str());
            if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        }
        return true;
    }

    // La ruta real de lo que está abierto, sin «\\?\»
    std::wstring RutaReal(HANDLE h)
    {
        std::wstring buf(32768, L'\0');
        DWORD n = GetFinalPathNameByHandleW(h, buf.data(), (DWORD)buf.size(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n == 0 || n >= buf.size()) return {};
        buf.resize(n);
        if (buf.rfind(L"\\\\?\\", 0) == 0 && buf.rfind(L"\\\\?\\UNC\\", 0) != 0) return buf.substr(4);
        return buf;
    }

    bool MismaRuta(const std::wstring& a, const std::wstring& b)
    {
        return !a.empty() && CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
    }

    bool IdentidadDe(HANDLE h, hnd::IdentidadArchivo& id, DWORD& atributos)
    {
        BY_HANDLE_FILE_INFORMATION i = {};
        if (!GetFileInformationByHandle(h, &i)) return false;
        atributos = i.dwFileAttributes;
        id.volumen = i.dwVolumeSerialNumber;
        id.indice = ((uint64_t)i.nFileIndexHigh << 32) | i.nFileIndexLow;
        id.tamano = ((int64_t)i.nFileSizeHigh << 32) | i.nFileSizeLow;
        id.modificado = ((int64_t)i.ftLastWriteTime.dwHighDateTime << 32) | i.ftLastWriteTime.dwLowDateTime;
        return true;
    }

    struct Cerrar
    {
        HANDLE h;
        ~Cerrar()
        {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
    };
}

#ifdef ATARRAYA_PRUEBAS
std::function<void()> hnd::pruebas::AntesDeMover;
#endif

bool hnd::LeerArchivoDelNavegador(const std::wstring& ruta, IdentidadArchivo& identidad, std::string& motivo)
{
    identidad = {};
    std::wstring completa;
    if (!RutaCompleta(ruta, completa))
    {
        motivo = "no es una ruta completa de esta PC";
        return false;
    }
    if (!SinEnlaces(completa))
    {
        motivo = "la ruta pasa por un enlace simbólico o un punto de unión";
        return false;
    }
    Cerrar h{ CreateFileW(completa.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    DWORD atributos = 0;
    if (h.h == INVALID_HANDLE_VALUE || !IdentidadDe(h.h, identidad, atributos))
    {
        motivo = "no se encuentra el archivo";
        return false;
    }
    if ((atributos & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !MismaRuta(RutaReal(h.h), completa))
    {
        identidad = {};
        motivo = "no es un archivo común en esa ruta";
        return false;
    }
    return true;
}

hnd::Movida hnd::MoverArchivoDelNavegador(const std::wstring& ruta, const IdentidadArchivo& identidad, const std::wstring& carpeta,
                                          std::wstring& destino, std::string& motivo)
{
    destino.clear();
    std::wstring completa, dir;
    if (!RutaCompleta(ruta, completa) || !RutaCompleta(carpeta, dir))
    {
        motivo = "una de las rutas no es una ruta completa de esta PC";
        return Movida::NoSePudo;
    }
    // Ya se había movido: Atarraya se cerró de golpe después de moverlo y antes de anotarlo. En la carpeta del paquete,
    // con su nombre, está ese mismo archivo (el mismo número de archivo del mismo disco, el mismo tamaño y la misma
    // fecha), revisado como siempre: sin enlaces y un archivo común en esa ruta.
    if (GetFileAttributesW(completa.c_str()) == INVALID_FILE_ATTRIBUTES && identidad.Valida())
    {
        std::wstring nombre = completa.substr(completa.find_last_of(L'\\') + 1);
        IdentidadArchivo ahi;
        std::string sinUso;
        if (!nombre.empty() && LeerArchivoDelNavegador(dir + L"\\" + nombre, ahi, sinUso) && ahi == identidad)
        {
            destino = dir + L"\\" + nombre;
            return Movida::Movida;
        }
    }
    if (!SinEnlaces(completa) || !SinEnlaces(dir))
    {
        motivo = "la ruta del archivo o de la carpeta del paquete pasa por un enlace simbólico o un punto de unión";
        return Movida::ConEnlaces;
    }
    // Abierto así, nadie más lo puede cambiar ni borrar mientras se revisa y se mueve (si el antivirus todavía lo
    // revisa, se espera un poco)
    Cerrar h{ INVALID_HANDLE_VALUE };
    for (int intento = 0; intento < 10; intento++)
    {
        h.h = CreateFileW(completa.c_str(), DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h.h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) break;
        Sleep(200);
    }
    IdentidadArchivo ahora;
    DWORD atributos = 0;
    if (h.h == INVALID_HANDLE_VALUE || !IdentidadDe(h.h, ahora, atributos))
    {
        motivo = "no se pudo abrir el archivo";
        return Movida::NoSePudo;
    }
    if ((atributos & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !MismaRuta(RutaReal(h.h), completa))
    {
        motivo = "el archivo ya no es un archivo común en esa ruta";
        return Movida::ConEnlaces;
    }
    if (!(ahora == identidad))
    {
        motivo = "el archivo cambió desde que el navegador avisó que terminó";
        return Movida::NoCoincide;
    }
    // La carpeta del paquete: la de verdad (no un enlace a otra) y en el mismo disco. Queda abierta sin compartir el
    // permiso de borrar hasta que termina el movimiento: mientras tanto nadie la puede renombrar, borrar ni cambiar por
    // un enlace, y el archivo se mueve adentro de esa misma carpeta abierta (no por su ruta).
    Cerrar c{ CreateFileW(dir.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    BY_HANDLE_FILE_INFORMATION ic = {};
    if (c.h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(c.h, &ic) || !(ic.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        motivo = "no se pudo abrir la carpeta del paquete";
        return Movida::NoSePudo;
    }
    if ((ic.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !MismaRuta(RutaReal(c.h), dir))
    {
        motivo = "la carpeta del paquete es un enlace a otra carpeta";
        return Movida::ConEnlaces;
    }
    std::wstring nombre = completa.substr(completa.find_last_of(L'\\') + 1);
    if (nombre.empty() || nombre == L"." || nombre == L".." || nombre.find_first_of(L"\\/:") != std::wstring::npos)
    {
        motivo = "el archivo no tiene un nombre que sirva";
        return Movida::NoSePudo;
    }
    destino = dir + L"\\" + nombre;
    if (MismaRuta(destino, completa)) return Movida::Movida;   // ya está en la carpeta del paquete
    if (ic.dwVolumeSerialNumber != ahora.volumen)
    {
        destino = completa;
        motivo = "la carpeta del paquete está en otro disco";
        return Movida::OtroDisco;
    }
#ifdef ATARRAYA_PRUEBAS
    if (pruebas::AntesDeMover) pruebas::AntesDeMover();   // las pruebas intentan cambiar la carpeta justo aquí
#endif
    // Por el mismo archivo abierto (el que se revisó), sin reemplazar nada, con solo su nombre dentro de la carpeta
    // abierta: ninguna ruta se vuelve a buscar. SetFileInformationByHandle no acepta una carpeta abierta como raíz, así
    // que va directo a NtSetInformationFile (FileRenameInformation), que sí.
    std::vector<BYTE> buf(sizeof(FILE_RENAME_INFO) + (nombre.size() + 1) * sizeof(wchar_t), 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buf.data());
    info->ReplaceIfExists = FALSE;
    info->RootDirectory = c.h;
    info->FileNameLength = (DWORD)(nombre.size() * sizeof(wchar_t));
    memcpy(info->FileName, nombre.c_str(), info->FileNameLength);
    using NtSetInformationFileFn = LONG(NTAPI*)(HANDLE, void*, void*, ULONG, int);
    struct EstadoEs
    {
        union { LONG estado; void* puntero; };
        ULONG_PTR informacion;
    } es = {};
    auto renombrar = reinterpret_cast<NtSetInformationFileFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile")));
    LONG st = renombrar ? renombrar(h.h, &es, info, (ULONG)buf.size(), 10 /* FileRenameInformation */) : (LONG)0xC0000002;
    if (st < 0)
    {
        if ((unsigned long)st == 0xC0000035UL)   // STATUS_OBJECT_NAME_COLLISION
        {
            motivo = "ya hay un archivo con ese nombre en la carpeta del paquete";
            return Movida::YaExiste;
        }
        char codigo[16];
        snprintf(codigo, sizeof(codigo), "0x%08lX", (unsigned long)st);
        motivo = std::string("Windows no lo dejó mover (código ") + codigo + ")";
        return Movida::NoSePudo;
    }
    // Quedó donde se pidió: el mismo archivo, dentro de la misma carpeta
    std::wstring final = RutaReal(h.h);
    if (!MismaRuta(final, destino))
    {
        motivo = "después de moverlo no quedó en la carpeta del paquete";
        destino = final;
        return Movida::NoSePudo;
    }
    return Movida::Movida;
}

hnd::Toma hnd::DecidirToma(Estado estado, int64_t hecho, int64_t segundos, int64_t plazo)
{
    if (estado == Estado::Completada) return Toma::Tomada;
    if (estado == Estado::Descargando && hecho > 0) return Toma::Tomada;
    // Una página, un 403 o 410, un enlace que ya no sirve (o la persona la pausó): que siga el navegador
    if (estado == Estado::Error || estado == Estado::Pausada) return Toma::Navegador;
    if (segundos >= plazo) return Toma::Navegador;
    return Toma::Probando;
}

hnd::SitiosNavegador::SitiosNavegador(std::wstring archivo) : m_archivo(std::move(archivo)) {}

void hnd::SitiosNavegador::Cargar()
{
    m_sitios.clear();
    std::string texto;
    if (!LeerArchivo(m_archivo, texto)) return;
    try
    {
        json todo = json::parse(texto);
        for (auto& [sitio, cuando] : todo.at("sitios").items())
        {
            if (sitio.empty() || sitio.size() > 253 || !cuando.is_number_integer()) continue;
            m_sitios[sitio] = cuando.get<int64_t>();
            if (m_sitios.size() >= 1000) break;
        }
    }
    catch (...)
    {
        m_sitios.clear();
    }
}

bool hnd::SitiosNavegador::Guardar() const
{
    json s = json::object();
    for (const auto& [sitio, cuando] : m_sitios) s[sitio] = cuando;
    return EscribirSeguro(m_archivo, json{ { "version", 1 }, { "sitios", s } }.dump(1));
}

bool hnd::SitiosNavegador::Necesita(const std::string& url) const
{
    std::string b = Base(enlaces::Sitio(url));
    return !b.empty() && m_sitios.count(b) > 0;
}

void hnd::SitiosNavegador::Anotar(const std::string& url, int64_t ahora)
{
    std::string b = Base(enlaces::Sitio(url));
    if (b.empty() || m_sitios.count(b)) return;
    m_sitios[b] = ahora;
    Guardar();
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
            p.sinConfirmar = false;
            p.reanudar = false;
            p.alAbrir = false;
            return (int)i;
        }
    }
    return -1;
}

void hnd::DeUnaEnUna::Saltar(int i)
{
    if (i < 0 || (size_t)i >= m_partes.size()) return;
    auto& p = m_partes[i];
    if (p.estado == EstadoParte::Esperando || p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Tomada
        || p.estado == EstadoParte::Espera || p.estado == EstadoParte::Navegador)
    {
        p.estado = EstadoParte::Saltada;
        p.sinConfirmar = false;
        p.reanudar = false;
        p.alAbrir = false;
        p.hasta = 0;
        p.limite = false;
    }
}

bool hnd::DeUnaEnUna::CambiarGrupo(const std::string& viejo, const std::string& nuevo)
{
    bool cambio = false;
    if (viejo.empty() || viejo == nuevo) return false;
    for (auto& p : m_partes)
    {
        if (p.grupo != viejo) continue;
        p.grupo = nuevo;
        cambio = true;
    }
    return cambio;
}

std::vector<hnd::Parte> hnd::DeUnaEnUna::Sobrantes() const
{
    std::vector<Parte> vuelven;
    for (const auto& p : m_partes)
    {
        if (p.estado == EstadoParte::Esperando || p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Saltada
            || p.estado == EstadoParte::Fallo || p.estado == EstadoParte::Espera)
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
    for (EstadoParte buscado : { EstadoParte::Abierta, EstadoParte::Navegador, EstadoParte::Tomada, EstadoParte::Espera })
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

bool hnd::DeUnaEnUna::HayEnEspera() const
{
    return Cuantas(EstadoParte::Espera) > 0;
}

bool hnd::DeUnaEnUna::QuedanPorAbrir() const
{
    return Cuantas(EstadoParte::Esperando) > 0;
}

bool hnd::DeUnaEnUna::Terminada() const
{
    return !m_partes.empty() && Cuantas(EstadoParte::Esperando) == 0 && Cuantas(EstadoParte::Abierta) == 0
        && Cuantas(EstadoParte::Tomada) == 0 && Cuantas(EstadoParte::Espera) == 0 && Cuantas(EstadoParte::Navegador) == 0;
}

bool hnd::DeUnaEnUna::TieneDescarga(uint64_t descarga) const
{
    return descarga != 0 && std::any_of(m_partes.begin(), m_partes.end(), [&](const Parte& p) { return p.descarga == descarga; });
}

size_t hnd::DeUnaEnUna::Cuantas(EstadoParte e) const
{
    return (size_t)std::count_if(m_partes.begin(), m_partes.end(), [&](const Parte& p) { return p.estado == e; });
}
