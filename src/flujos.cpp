#include "flujos.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "enlaces.hpp"
#include "util.hpp"

const char* const hnd::flujos::TextoProtegido = "Este video está protegido (DRM o cifrado): Atarraya no lo baja.";
const char* const hnd::flujos::TextoEnVivo = "Es una transmisión en vivo: Atarraya solo baja videos completos.";
const char* const hnd::flujos::TextoVariasPartes = "Este video DASH viene en varios períodos: Atarraya todavía no lo baja.";
const char* const hnd::flujos::TextoRedLocal =
    "La lista del video pide archivos de esta PC o de la red de tu casa: Atarraya no los baja.";

namespace
{
    using namespace hnd::flujos;

    // Topes, por si una lista viene mal hecha (o hecha para molestar)
    constexpr size_t MaximoTrozos = 200000;
    constexpr size_t MaximoNodos = 200000;
    constexpr int MaximaProfundidad = 64;

    std::string Bajo(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    std::string Recortar(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) a++;
        while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
        return s.substr(a, b - a);
    }

    bool Empieza(const std::string& s, const char* inicio)
    {
        size_t n = strlen(inicio);
        return s.size() >= n && s.compare(0, n, inicio) == 0;
    }

    int64_t Numero(const std::string& s, int64_t siNo = 0)
    {
        if (s.empty()) return siNo;
        char* fin = nullptr;
        long long v = std::strtoll(s.c_str(), &fin, 10);
        return fin == s.c_str() ? siNo : (int64_t)v;
    }

    double Decimal(const std::string& s, double siNo = 0)
    {
        if (s.empty()) return siNo;
        char* fin = nullptr;
        double v = std::strtod(s.c_str(), &fin);
        return fin == s.c_str() || !std::isfinite(v) ? siNo : v;
    }

    // Quita «.» y «..» de la ruta (RFC 3986, 5.2.4)
    std::string SinPuntos(const std::string& ruta)
    {
        std::vector<std::string> partes;
        size_t i = 0;
        bool absoluta = !ruta.empty() && ruta[0] == '/';
        std::string resto = absoluta ? ruta.substr(1) : ruta;
        bool barraFinal = false;
        while (i <= resto.size())
        {
            size_t fin = resto.find('/', i);
            if (fin == std::string::npos) fin = resto.size();
            std::string p = resto.substr(i, fin - i);
            barraFinal = false;
            if (p == "..")
            {
                if (!partes.empty()) partes.pop_back();
                barraFinal = true;
            }
            else if (p == ".") barraFinal = true;
            else partes.push_back(p);
            i = fin + 1;
        }
        std::string r = absoluta ? "/" : "";
        for (size_t k = 0; k < partes.size(); k++) r += (k ? "/" : "") + partes[k];
        if (barraFinal && (r.empty() || r.back() != '/')) r += '/';
        return r;
    }

    // ---- Atributos de HLS: CLAVE=valor,CLAVE="valor, con comas" ----
    std::map<std::string, std::string> Atributos(const std::string& s)
    {
        std::map<std::string, std::string> r;
        size_t i = 0;
        while (i < s.size())
        {
            size_t igual = s.find('=', i);
            if (igual == std::string::npos) break;
            std::string clave = Recortar(s.substr(i, igual - i));
            std::string valor;
            size_t j = igual + 1;
            if (j < s.size() && s[j] == '"')
            {
                size_t cierre = s.find('"', j + 1);
                if (cierre == std::string::npos) cierre = s.size();
                valor = s.substr(j + 1, cierre - j - 1);
                j = cierre + 1;
                size_t coma = s.find(',', j);
                i = coma == std::string::npos ? s.size() : coma + 1;
            }
            else
            {
                size_t coma = s.find(',', j);
                valor = Recortar(s.substr(j, coma == std::string::npos ? std::string::npos : coma - j));
                i = coma == std::string::npos ? s.size() : coma + 1;
            }
            if (!clave.empty()) r[Bajo(clave)] = valor;
        }
        return r;
    }

    // «n@o» de EXT-X-BYTERANGE o del BYTERANGE de EXT-X-MAP. Sin «@o», «desde» queda en -2: sigue donde terminó el
    // trozo anterior del mismo archivo.
    bool LeerRango(const std::string& s, int64_t& desde, int64_t& largo)
    {
        auto arroba = s.find('@');
        largo = Numero(Recortar(s.substr(0, arroba)), -1);
        desde = arroba == std::string::npos ? -2 : Numero(Recortar(s.substr(arroba + 1)), -1);
        return largo > 0 && desde >= -2 && desde != -1;
    }

    // ---- Un XML mínimo para los MPD: elementos, atributos y texto. Los prefijos de los nombres se quitan. ----
    struct Nodo
    {
        std::string nombre;
        std::map<std::string, std::string> atributos;
        std::vector<std::unique_ptr<Nodo>> hijos;
        std::string texto;

        const std::string* Atributo(const char* clave) const
        {
            auto it = atributos.find(clave);
            return it == atributos.end() ? nullptr : &it->second;
        }
        std::string Valor(const char* clave, const std::string& siNo = std::string()) const
        {
            auto* v = Atributo(clave);
            return v ? *v : siNo;
        }
        const Nodo* Hijo(const char* n) const
        {
            for (auto& h : hijos)
                if (h->nombre == n) return h.get();
            return nullptr;
        }
        std::vector<const Nodo*> Hijos(const char* n) const
        {
            std::vector<const Nodo*> r;
            for (auto& h : hijos)
                if (h->nombre == n) r.push_back(h.get());
            return r;
        }
        bool TieneEnAlgunLado(const char* n) const
        {
            if (nombre == n) return true;
            for (auto& h : hijos)
                if (h->TieneEnAlgunLado(n)) return true;
            return false;
        }
    };

    std::string SinPrefijo(const std::string& n)
    {
        auto dp = n.find(':');
        return dp == std::string::npos ? n : n.substr(dp + 1);
    }

    std::string Entidades(const std::string& s)
    {
        if (s.find('&') == std::string::npos) return s;
        std::string r;
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] != '&')
            {
                r += s[i];
                continue;
            }
            size_t fin = s.find(';', i);
            if (fin == std::string::npos || fin - i > 10)
            {
                r += s[i];
                continue;
            }
            std::string e = s.substr(i + 1, fin - i - 1);
            if (e == "amp") r += '&';
            else if (e == "lt") r += '<';
            else if (e == "gt") r += '>';
            else if (e == "quot") r += '"';
            else if (e == "apos") r += '\'';
            else if (!e.empty() && e[0] == '#')
            {
                unsigned long c = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16)
                                                                               : std::strtoul(e.c_str() + 1, nullptr, 10);
                if (c == 0 || c > 0x10FFFF) { r += s.substr(i, fin - i + 1); i = fin; continue; }
                // Como UTF-8
                if (c < 0x80) r += (char)c;
                else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 0x3F)); }
                else if (c < 0x10000) { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 0x3F)); r += (char)(0x80 | (c & 0x3F)); }
                else { r += (char)(0xF0 | (c >> 18)); r += (char)(0x80 | ((c >> 12) & 0x3F)); r += (char)(0x80 | ((c >> 6) & 0x3F)); r += (char)(0x80 | (c & 0x3F)); }
            }
            else
            {
                r += s.substr(i, fin - i + 1);
            }
            i = fin;
        }
        return r;
    }

    bool LeerXml(const std::string& s, Nodo& raiz)
    {
        std::vector<Nodo*> pila{ &raiz };
        size_t nodos = 0, i = 0;
        while (i < s.size())
        {
            if (s[i] != '<')
            {
                size_t fin = s.find('<', i);
                if (fin == std::string::npos) fin = s.size();
                pila.back()->texto += Entidades(s.substr(i, fin - i));
                i = fin;
                continue;
            }
            if (s.compare(i, 4, "<!--") == 0)
            {
                size_t fin = s.find("-->", i + 4);
                if (fin == std::string::npos) return false;
                i = fin + 3;
                continue;
            }
            if (s.compare(i, 9, "<![CDATA[") == 0)
            {
                size_t fin = s.find("]]>", i + 9);
                if (fin == std::string::npos) return false;
                pila.back()->texto += s.substr(i + 9, fin - i - 9);
                i = fin + 3;
                continue;
            }
            if (s.compare(i, 2, "<?") == 0 || s.compare(i, 2, "<!") == 0)
            {
                size_t fin = s.find('>', i);
                if (fin == std::string::npos) return false;
                i = fin + 1;
                continue;
            }
            if (s.compare(i, 2, "</") == 0)
            {
                size_t fin = s.find('>', i);
                if (fin == std::string::npos) return false;
                std::string n = SinPrefijo(Recortar(s.substr(i + 2, fin - i - 2)));
                // Se cierra hasta el elemento con ese nombre (un XML un poco mal hecho no lo arruina todo)
                for (size_t k = pila.size(); k-- > 1;)
                {
                    if (pila[k]->nombre == n)
                    {
                        pila.resize(k);
                        break;
                    }
                }
                i = fin + 1;
                continue;
            }
            // Un elemento: <nombre atributos> o <nombre atributos/>
            size_t j = i + 1;
            while (j < s.size() && !std::isspace((unsigned char)s[j]) && s[j] != '>' && s[j] != '/') j++;
            auto nuevo = std::make_unique<Nodo>();
            nuevo->nombre = SinPrefijo(s.substr(i + 1, j - i - 1));
            bool cerrado = false, completo = false;
            while (j < s.size())
            {
                while (j < s.size() && std::isspace((unsigned char)s[j])) j++;
                if (j >= s.size()) return false;
                if (s[j] == '>') { j++; completo = true; break; }
                if (s[j] == '/') { cerrado = true; j++; continue; }
                size_t k = j;
                while (k < s.size() && s[k] != '=' && s[k] != '>' && !std::isspace((unsigned char)s[k])) k++;
                std::string clave = SinPrefijo(s.substr(j, k - j));
                while (k < s.size() && std::isspace((unsigned char)s[k])) k++;
                if (k < s.size() && s[k] == '=')
                {
                    k++;
                    while (k < s.size() && std::isspace((unsigned char)s[k])) k++;
                    if (k >= s.size()) return false;
                    char comilla = s[k];
                    if (comilla != '"' && comilla != '\'') return false;
                    size_t cierre = s.find(comilla, k + 1);
                    if (cierre == std::string::npos) return false;
                    if (!clave.empty()) nuevo->atributos[clave] = Entidades(s.substr(k + 1, cierre - k - 1));
                    j = cierre + 1;
                }
                else j = k;
            }
            if (!completo) return false;   // la etiqueta quedó cortada
            if (++nodos > MaximoNodos || (int)pila.size() > MaximaProfundidad) return false;
            Nodo* puntero = nuevo.get();
            pila.back()->hijos.push_back(std::move(nuevo));
            if (!cerrado) pila.push_back(puntero);
            i = j;
        }
        // Todo lo que se abrió se tiene que cerrar: un manifiesto cortado a la mitad no sirve
        return pila.size() == 1;
    }

    // «PT1H2M3.5S», «P1DT2H»: duración ISO 8601 en segundos (años y meses no se usan en los MPD)
    double Duracion(const std::string& s)
    {
        if (s.empty() || (s[0] != 'P' && s[0] != 'p')) return 0;
        double total = 0;
        bool tiempo = false;
        std::string numero;
        for (size_t i = 1; i < s.size(); i++)
        {
            char c = (char)std::toupper((unsigned char)s[i]);
            if (c == 'T') { tiempo = true; continue; }
            if (std::isdigit((unsigned char)c) || c == '.' || c == ',') { numero += c == ',' ? '.' : c; continue; }
            double v = Decimal(numero);
            numero.clear();
            if (c == 'D') total += v * 86400;
            else if (c == 'H' && tiempo) total += v * 3600;
            else if (c == 'M' && tiempo) total += v * 60;
            else if (c == 'S' && tiempo) total += v;
            else if (c == 'W') total += v * 7 * 86400;
        }
        return total;
    }

    // $RepresentationID$, $Number$, $Bandwidth$, $Time$ (con formato «%05d» si lo trae) y «$$»
    std::string Plantilla(const std::string& p, const std::string& id, int64_t numero, int64_t banda, int64_t tiempo)
    {
        std::string r;
        size_t i = 0;
        while (i < p.size())
        {
            if (p[i] != '$')
            {
                r += p[i++];
                continue;
            }
            size_t fin = p.find('$', i + 1);
            if (fin == std::string::npos)
            {
                r += p.substr(i);
                break;
            }
            std::string clave = p.substr(i + 1, fin - i - 1);
            i = fin + 1;
            if (clave.empty())
            {
                r += '$';
                continue;
            }
            std::string formato;
            auto porc = clave.find('%');
            if (porc != std::string::npos)
            {
                formato = clave.substr(porc);
                clave = clave.substr(0, porc);
            }
            auto conFormato = [&](int64_t v) {
                // Solo «%0Nd» (o «%d»): ancho con ceros a la izquierda
                int ancho = 0;
                if (formato.size() >= 3 && formato[1] == '0') ancho = std::clamp(atoi(formato.c_str() + 2), 0, 20);
                char buf[48];
                snprintf(buf, sizeof(buf), "%0*lld", ancho, (long long)v);
                return std::string(buf);
            };
            if (clave == "RepresentationID") r += id;
            else if (clave == "Number") r += conFormato(numero);
            else if (clave == "Bandwidth") r += conFormato(banda);
            else if (clave == "Time") r += conFormato(tiempo);
            else r += "$" + clave + formato + "$";   // desconocido: se deja como estaba
        }
        return r;
    }

    // Las plantillas se heredan: Period → AdaptationSet → Representation (la más cercana gana)
    struct Plantillas
    {
        std::map<std::string, std::string> atributos;
        const Nodo* lineaDeTiempo = nullptr;
        const Nodo* lista = nullptr;    // SegmentList
        const Nodo* base = nullptr;     // SegmentBase
        bool hayPlantilla = false;

        void Sumar(const Nodo* n)
        {
            if (!n) return;
            if (auto* t = n->Hijo("SegmentTemplate"))
            {
                hayPlantilla = true;
                for (auto& [k, v] : t->atributos) atributos[k] = v;
                if (auto* l = t->Hijo("SegmentTimeline")) lineaDeTiempo = l;
            }
            if (auto* l = n->Hijo("SegmentList")) lista = l;
            if (auto* b = n->Hijo("SegmentBase")) base = b;
        }
        std::string Valor(const char* k, const std::string& siNo = std::string()) const
        {
            auto it = atributos.find(k);
            return it == atributos.end() ? siNo : it->second;
        }
    };

    std::string BaseDe(const Nodo* n, const std::string& base)
    {
        if (!n) return base;
        if (auto* b = n->Hijo("BaseURL"))
        {
            std::string t = Recortar(b->texto);
            if (!t.empty()) return Resolver(base, t);
        }
        return base;
    }

    // «a-b» de los rangos de DASH
    bool RangoDash(const std::string& s, int64_t& desde, int64_t& largo)
    {
        auto guion = s.find('-');
        if (guion == std::string::npos) return false;
        int64_t a = Numero(Recortar(s.substr(0, guion)), -1), b = Numero(Recortar(s.substr(guion + 1)), -1);
        if (a < 0 || b < a) return false;
        desde = a;
        largo = b - a + 1;
        return true;
    }

    bool EsVideo(const std::string& mime, const std::string& tipo, const std::string& codecs)
    {
        if (Empieza(Bajo(mime), "video/") || Bajo(tipo) == "video") return true;
        std::string c = Bajo(codecs);
        return mime.empty() && tipo.empty() && (Empieza(c, "avc") || Empieza(c, "hvc") || Empieza(c, "hev") || Empieza(c, "vp") || Empieza(c, "av01"));
    }

    bool EsAudio(const std::string& mime, const std::string& tipo, const std::string& codecs)
    {
        if (Empieza(Bajo(mime), "audio/") || Bajo(tipo) == "audio") return true;
        std::string c = Bajo(codecs);
        return mime.empty() && tipo.empty() && (Empieza(c, "mp4a") || Empieza(c, "opus") || Empieza(c, "ac-3") || Empieza(c, "ec-3") || Empieza(c, "flac"));
    }
}

std::string hnd::flujos::Resolver(const std::string& base, const std::string& relativa)
{
    std::string rel = Recortar(relativa);
    auto almohadilla = rel.find('#');
    if (almohadilla != std::string::npos) rel = rel.substr(0, almohadilla);
    // Ya es completa: «esquema:...»
    size_t k = 0;
    while (k < rel.size() && (std::isalnum((unsigned char)rel[k]) || rel[k] == '+' || rel[k] == '-' || rel[k] == '.')) k++;
    if (k > 0 && k < rel.size() && rel[k] == ':' && std::isalpha((unsigned char)rel[0])) return rel;

    std::string b = base.substr(0, base.find('#'));
    auto esquema = b.find("://");
    if (esquema == std::string::npos) return rel;
    auto inicioRuta = b.find_first_of("/?", esquema + 3);
    std::string origen = b.substr(0, inicioRuta == std::string::npos ? b.size() : inicioRuta);
    std::string resto = inicioRuta == std::string::npos ? "/" : b.substr(inicioRuta);
    std::string ruta = resto.substr(0, resto.find('?'));
    if (ruta.empty()) ruta = "/";

    if (rel.empty()) return b;
    if (Empieza(rel, "//")) return b.substr(0, esquema) + ":" + rel;
    auto separar = [](const std::string& r, std::string& camino, std::string& consulta) {
        auto q = r.find('?');
        camino = r.substr(0, q);
        consulta = q == std::string::npos ? std::string() : r.substr(q);
    };
    std::string camino, consulta;
    if (rel[0] == '/')
    {
        separar(rel, camino, consulta);
        return origen + SinPuntos(camino) + consulta;
    }
    if (rel[0] == '?') return origen + ruta + rel;
    separar(rel, camino, consulta);
    std::string carpeta = ruta.substr(0, ruta.rfind('/') + 1);
    return origen + SinPuntos(carpeta + camino) + consulta;
}

// ------------------------------------------------------------------------------------------------- HLS

hnd::flujos::ListaHls hnd::flujos::LeerHls(const std::string& texto, const std::string& url)
{
    ListaHls l;
    std::string t = texto;
    if (Empieza(t, "\xEF\xBB\xBF")) t = t.substr(3);   // BOM de UTF-8
    size_t i = 0;
    bool primera = true, vod = false, fin = false;
    std::map<std::string, std::string> pendiente;   // EXT-X-STREAM-INF esperando su dirección
    bool esperaVariante = false;
    double duracionParte = 0;
    bool esperaParte = false;
    int64_t rangoDesde = -1, rangoLargo = -1;
    int64_t siguienteRango = 0;
    std::string ultimoArchivoRango;
    while (i <= t.size())
    {
        size_t finLinea = t.find('\n', i);
        if (finLinea == std::string::npos) finLinea = t.size();
        std::string linea = Recortar(t.substr(i, finLinea - i));
        i = finLinea + 1;
        if (linea.empty()) continue;
        if (primera)
        {
            primera = false;
            if (linea != "#EXTM3U") return l;   // no es una lista HLS
            l.valida = true;
            continue;
        }
        if (linea[0] == '#')
        {
            if (!Empieza(linea, "#EXT")) continue;   // comentario
            auto dp = linea.find(':');
            std::string etiqueta = linea.substr(0, dp);
            std::string valor = dp == std::string::npos ? std::string() : linea.substr(dp + 1);
            if (etiqueta == "#EXT-X-KEY" || etiqueta == "#EXT-X-SESSION-KEY") l.cifrada = true;   // con cualquier método
            else if (etiqueta == "#EXT-X-STREAM-INF")
            {
                pendiente = Atributos(valor);
                esperaVariante = true;
            }
            else if (etiqueta == "#EXT-X-MEDIA")
            {
                auto a = Atributos(valor);
                if (Bajo(a["type"]) == "audio" && !a["uri"].empty())
                {
                    AudioAparte au;
                    au.url = Resolver(url, a["uri"]);
                    au.grupo = a["group-id"];
                    au.nombre = a["name"];
                    au.idioma = a["language"];
                    au.porDefecto = Bajo(a["default"]) == "yes";
                    l.audios.push_back(std::move(au));
                }
            }
            else if (etiqueta == "#EXTINF")
            {
                duracionParte = Decimal(valor.substr(0, valor.find(',')));
                esperaParte = true;
            }
            else if (etiqueta == "#EXT-X-BYTERANGE")
            {
                int64_t desde = -1, largo = -1;
                if (LeerRango(valor, desde, largo))
                {
                    rangoDesde = desde;
                    rangoLargo = largo;
                }
            }
            else if (etiqueta == "#EXT-X-MAP")
            {
                auto a = Atributos(valor);
                l.inicio.url = a["uri"].empty() ? std::string() : Resolver(url, a["uri"]);
                int64_t desde = -1, largo = -1;
                if (!a["byterange"].empty() && LeerRango(a["byterange"], desde, largo))
                {
                    l.inicio.desde = desde < 0 ? 0 : desde;
                    l.inicio.largo = largo;
                }
            }
            else if (etiqueta == "#EXT-X-PLAYLIST-TYPE") vod = Bajo(Recortar(valor)) == "vod";
            else if (etiqueta == "#EXT-X-ENDLIST") fin = true;
            continue;
        }
        // Una dirección: de una calidad (en la maestra) o de una parte
        if (esperaVariante)
        {
            esperaVariante = false;
            Variante v;
            v.url = Resolver(url, linea);
            v.banda = Numero(pendiente["bandwidth"]);
            if (v.banda <= 0) v.banda = Numero(pendiente["average-bandwidth"]);
            std::string res = pendiente["resolution"];
            auto x = res.find_first_of("xX");
            if (x != std::string::npos)
            {
                v.ancho = (int)Numero(res.substr(0, x));
                v.alto = (int)Numero(res.substr(x + 1));
            }
            v.codecs = pendiente["codecs"];
            v.grupoAudio = pendiente["audio"];
            l.variantes.push_back(std::move(v));
            l.maestra = true;
            continue;
        }
        if (l.trozos.size() >= MaximoTrozos) continue;
        Trozo tr;
        tr.url = Resolver(url, linea);
        if (rangoLargo > 0)
        {
            // Sin «@desde», el rango sigue donde terminó el anterior del mismo archivo (si es otro, desde el principio)
            if (rangoDesde == -2) rangoDesde = tr.url == ultimoArchivoRango ? siguienteRango : 0;
            tr.desde = rangoDesde;
            tr.largo = rangoLargo;
            siguienteRango = rangoDesde + rangoLargo;
            ultimoArchivoRango = tr.url;
        }
        rangoDesde = rangoLargo = -1;
        if (esperaParte) l.duracion += duracionParte;
        esperaParte = false;
        l.trozos.push_back(std::move(tr));
    }
    l.enVivo = !l.maestra && !fin && !vod;
    return l;
}

int hnd::flujos::MejorVariante(const ListaHls& l)
{
    int mejor = -1;
    for (int i = 0; i < (int)l.variantes.size(); i++)
    {
        const auto& v = l.variantes[i];
        if (v.url.empty()) continue;
        if (mejor < 0) { mejor = i; continue; }
        const auto& m = l.variantes[mejor];
        int64_t area = (int64_t)v.ancho * v.alto, areaM = (int64_t)m.ancho * m.alto;
        if (v.banda > m.banda || (v.banda == m.banda && area > areaM)) mejor = i;
    }
    return mejor;
}

int hnd::flujos::AudioDeVariante(const ListaHls& l, int variante)
{
    if (variante < 0 || variante >= (int)l.variantes.size()) return -1;
    const std::string& grupo = l.variantes[variante].grupoAudio;
    if (grupo.empty()) return -1;
    int primero = -1;
    for (int i = 0; i < (int)l.audios.size(); i++)
    {
        if (l.audios[i].grupo != grupo) continue;
        if (l.audios[i].porDefecto) return i;
        if (primero < 0) primero = i;
    }
    return primero;
}

std::wstring hnd::flujos::ExtensionHls(const ListaHls& l)
{
    if (!l.inicio.url.empty()) return L".mp4";   // CMAF / fMP4: inicio + partes es un MP4 fragmentado
    if (!l.trozos.empty())
    {
        std::string u = Bajo(l.trozos.front().url.substr(0, l.trozos.front().url.find('?')));
        auto termina = [&](const char* e) { size_t n = strlen(e); return u.size() > n && u.compare(u.size() - n, n, e) == 0; };
        if (termina(".aac")) return L".aac";
        if (termina(".mp3")) return L".mp3";
    }
    return L".ts";
}

// ------------------------------------------------------------------------------------------------- DASH

hnd::flujos::ManifiestoDash hnd::flujos::LeerDash(const std::string& xml, const std::string& url)
{
    ManifiestoDash m;
    Nodo raiz;
    if (!LeerXml(xml, raiz)) return m;
    const Nodo* mpd = raiz.Hijo("MPD");
    if (!mpd) return m;
    m.valido = true;
    m.protegido = mpd->TieneEnAlgunLado("ContentProtection");
    m.enVivo = Bajo(mpd->Valor("type", "static")) == "dynamic";
    m.duracion = Duracion(mpd->Valor("mediaPresentationDuration"));
    auto periodos = mpd->Hijos("Period");
    if (periodos.size() > 1) m.variasPartes = true;
    if (periodos.empty() || m.protegido || m.enVivo) return m;
    const Nodo* periodo = periodos.front();
    double duracionPeriodo = Duracion(periodo->Valor("duration"));
    if (duracionPeriodo <= 0) duracionPeriodo = m.duracion;
    if (m.duracion <= 0) m.duracion = duracionPeriodo;

    std::string baseMpd = BaseDe(mpd, url);
    std::string basePeriodo = BaseDe(periodo, baseMpd);
    for (const Nodo* conjunto : periodo->Hijos("AdaptationSet"))
    {
        std::string baseConjunto = BaseDe(conjunto, basePeriodo);
        for (const Nodo* rep : conjunto->Hijos("Representation"))
        {
            Representacion r;
            r.id = rep->Valor("id");
            r.banda = Numero(rep->Valor("bandwidth"));
            r.ancho = (int)Numero(rep->Valor("width", conjunto->Valor("width")));
            r.alto = (int)Numero(rep->Valor("height", conjunto->Valor("height")));
            r.mime = rep->Valor("mimeType", conjunto->Valor("mimeType"));
            r.codecs = rep->Valor("codecs", conjunto->Valor("codecs"));
            std::string tipo = conjunto->Valor("contentType");
            r.video = EsVideo(r.mime, tipo, r.codecs);
            r.audio = !r.video && EsAudio(r.mime, tipo, r.codecs);
            if (!r.video && !r.audio) continue;   // subtítulos, imágenes...
            std::string base = BaseDe(rep, baseConjunto);

            Plantillas p;
            p.Sumar(periodo);
            p.Sumar(conjunto);
            p.Sumar(rep);
            if (p.hayPlantilla && !p.Valor("media").empty())
            {
                int64_t escala = std::max<int64_t>(1, Numero(p.Valor("timescale"), 1));
                int64_t numero = Numero(p.Valor("startNumber"), 1);
                std::string ini = p.Valor("initialization");
                if (!ini.empty()) r.inicio.url = Resolver(base, Plantilla(ini, r.id, numero, r.banda, 0));
                std::string media = p.Valor("media");
                if (p.lineaDeTiempo)
                {
                    int64_t t = 0;
                    int64_t finPeriodo = (int64_t)std::llround(duracionPeriodo * (double)escala);
                    for (const Nodo* s : p.lineaDeTiempo->Hijos("S"))
                    {
                        if (auto* v = s->Atributo("t")) t = Numero(*v);
                        int64_t d = Numero(s->Valor("d"));
                        if (d <= 0) break;
                        int64_t repetir = Numero(s->Valor("r"), 0);
                        if (repetir < 0)   // hasta el final del período
                            repetir = finPeriodo > t ? (finPeriodo - t + d - 1) / d - 1 : 0;
                        for (int64_t k = 0; k <= repetir && r.trozos.size() < MaximoTrozos; k++)
                        {
                            Trozo tr;
                            tr.url = Resolver(base, Plantilla(media, r.id, numero, r.banda, t));
                            r.trozos.push_back(std::move(tr));
                            t += d;
                            numero++;
                        }
                    }
                }
                else
                {
                    int64_t d = Numero(p.Valor("duration"));
                    if (d > 0 && duracionPeriodo > 0)
                    {
                        double cuantos = std::ceil(duracionPeriodo * (double)escala / (double)d - 1e-9);
                        int64_t n = (int64_t)std::min<double>(cuantos, (double)MaximoTrozos);
                        for (int64_t k = 0; k < n; k++)
                        {
                            Trozo tr;
                            tr.url = Resolver(base, Plantilla(media, r.id, numero + k, r.banda, k * d));
                            r.trozos.push_back(std::move(tr));
                        }
                    }
                }
            }
            else if (p.lista)
            {
                if (auto* ini = p.lista->Hijo("Initialization"))
                {
                    std::string fuente = ini->Valor("sourceURL");
                    r.inicio.url = fuente.empty() ? base : Resolver(base, fuente);
                    RangoDash(ini->Valor("range"), r.inicio.desde, r.inicio.largo);
                }
                for (const Nodo* su : p.lista->Hijos("SegmentURL"))
                {
                    if (r.trozos.size() >= MaximoTrozos) break;
                    Trozo tr;
                    std::string media = su->Valor("media");
                    tr.url = media.empty() ? base : Resolver(base, media);
                    RangoDash(su->Valor("mediaRange"), tr.desde, tr.largo);
                    r.trozos.push_back(std::move(tr));
                }
            }
            else
            {
                // SegmentBase o solo BaseURL: el medio entero es un archivo
                r.unArchivo = true;
                Trozo tr;
                tr.url = base;
                r.trozos.push_back(std::move(tr));
            }
            if (r.trozos.empty()) continue;
            (r.video ? m.videos : m.audios).push_back(std::move(r));
        }
    }
    return m;
}

int hnd::flujos::MejorRepresentacion(const std::vector<Representacion>& r)
{
    int mejor = -1;
    for (int i = 0; i < (int)r.size(); i++)
    {
        if (mejor < 0) { mejor = i; continue; }
        int64_t area = (int64_t)r[i].ancho * r[i].alto, areaM = (int64_t)r[mejor].ancho * r[mejor].alto;
        if (r[i].banda > r[mejor].banda || (r[i].banda == r[mejor].banda && area > areaM)) mejor = i;
    }
    return mejor;
}

int hnd::flujos::BuscarRepresentacion(const std::vector<Representacion>& r, const std::string& id)
{
    for (int i = 0; i < (int)r.size(); i++)
        if (r[i].id == id) return i;
    return -1;
}

std::wstring hnd::flujos::ExtensionDash(const Representacion& r)
{
    std::string m = Bajo(r.mime);
    if (m.find("webm") != std::string::npos) return L".webm";
    return r.audio ? L".m4a" : L".mp4";
}

// ------------------------------------------------------------------------------------------------- Comunes

std::string hnd::flujos::RevisarTrozos(const Trozo& inicio, const std::vector<Trozo>& trozos)
{
    if (trozos.empty()) return "La lista del video no trae partes para bajar.";
    auto revisar = [](const std::string& u) -> std::string {
        std::string b = Bajo(u.substr(0, 8));
        if (!Empieza(b, "http://") && !Empieza(b, "https://"))
            return "La lista del video pide archivos que no son de internet (http o https): Atarraya no los baja.";
        if (enlaces::NoPermitido(u)) return Utf8(enlaces::TextoNoPermitido);
        if (enlaces::SitioProtegido(u)) return TextoProtegido;
        return {};
    };
    if (!inicio.url.empty())
    {
        auto e = revisar(inicio.url);
        if (!e.empty()) return e;
    }
    for (const auto& t : trozos)
    {
        auto e = revisar(t.url);
        if (!e.empty()) return e;
    }
    return {};
}

std::wstring hnd::flujos::TextoCalidad(int ancho, int alto, int64_t banda, bool soloAudio)
{
    std::wstring r;
    if (soloAudio) r = L"Audio";
    else if (alto > 0) r = std::to_wstring(alto) + L"p";
    else if (ancho > 0) r = std::to_wstring(ancho) + L" de ancho";
    else r = L"Video";
    if (banda > 0)
    {
        wchar_t buf[48];
        if (banda >= 1000000) swprintf(buf, 48, L" · %.1f Mb/s", (double)banda / 1e6);
        else swprintf(buf, 48, L" · %lld kb/s", (long long)((banda + 500) / 1000));
        r += buf;
    }
    return r;
}

// ------------------------------------------------------------------------------------------------- Ensamblador

hnd::flujos::Ensamblador::Ensamblador(size_t total, size_t yaEscritas, int64_t bytesYaEscritos, Escribir escribir)
    : m_total(total), m_siguiente(std::min(yaEscritas, total)), m_bytes(bytesYaEscritos), m_escribir(std::move(escribir))
{
}

size_t hnd::flujos::Ensamblador::EnMemoria() const
{
    size_t n = 0;
    for (auto& [i, datos] : m_esperando) n += datos.size();
    return n;
}

bool hnd::flujos::Ensamblador::Vaciar(size_t parte)
{
    auto it = m_esperando.find(parte);
    if (it == m_esperando.end()) return true;
    std::string datos = std::move(it->second);
    m_esperando.erase(it);
    if (datos.empty()) return true;
    if (!m_escribir(m_bytes + m_actual, datos.data(), datos.size())) return false;
    m_actual += (int64_t)datos.size();
    return true;
}

bool hnd::flujos::Ensamblador::Pedazo(size_t parte, const char* datos, size_t n)
{
    if (parte < m_siguiente || parte >= m_total || n == 0) return true;
    if (parte == m_siguiente)
    {
        if (!Vaciar(parte)) return false;
        if (!m_escribir(m_bytes + m_actual, datos, n)) return false;
        m_actual += (int64_t)n;
        return true;
    }
    m_esperando[parte].append(datos, n);
    return true;
}

bool hnd::flujos::Ensamblador::Terminar(size_t parte)
{
    if (parte < m_siguiente || parte >= m_total) return true;
    if (parte != m_siguiente)
    {
        m_terminadas.insert(parte);
        return true;
    }
    if (!Vaciar(parte)) return false;
    m_bytes += m_actual;
    m_actual = 0;
    m_siguiente++;
    // Las que ya habían terminado y esperaban su turno
    while (m_siguiente < m_total && m_terminadas.count(m_siguiente))
    {
        m_terminadas.erase(m_siguiente);
        if (!Vaciar(m_siguiente)) return false;
        m_bytes += m_actual;
        m_actual = 0;
        m_siguiente++;
    }
    return true;
}

void hnd::flujos::Ensamblador::Descartar(size_t parte)
{
    m_esperando.erase(parte);
    m_terminadas.erase(parte);
    if (parte == m_siguiente) m_actual = 0;
}
