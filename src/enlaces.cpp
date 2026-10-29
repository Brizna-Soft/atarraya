#include "enlaces.hpp"

#include <windows.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <map>
#include <set>

#include "motor.hpp"
#include "nombres.hpp"
#include "util.hpp"

using json = nlohmann::json;

const wchar_t* hnd::enlaces::TextoNoPermitido =
    L"Atarraya no baja videos de YouTube: sus condiciones no lo permiten. Sí baja videos de sitios que los ofrecen "
    L"como archivo, por ejemplo Internet Archive.";

namespace
{
    using namespace hnd::enlaces;

    constexpr size_t MaximoEnlaces = 20000;

    std::string Bajo(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // ¿«s» tiene «prefijo» (en minúsculas) en la posición «desde», sin importar mayúsculas?
    bool EmpiezaEn(const std::string& s, size_t desde, const char* prefijo)
    {
        size_t n = strlen(prefijo);
        if (desde > s.size() || s.size() - desde < n) return false;
        for (size_t i = 0; i < n; i++)
        {
            if (std::tolower((unsigned char)s[desde + i]) != prefijo[i]) return false;
        }
        return true;
    }

    bool Empieza(const std::string& s, const char* prefijo) { return EmpiezaEn(s, 0, prefijo); }

    // Texto que puede venir en UTF-8 o en Latin-1 / Windows-1252
    std::wstring TextoDeBytes(const std::string& s)
    {
        if (s.empty()) return {};
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0) > 0) return hnd::Ancho(s);
        int n = MultiByteToWideChar(1252, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring r(n, L'\0');
        MultiByteToWideChar(1252, 0, s.data(), (int)s.size(), r.data(), n);
        return r;
    }

    void AgregarUtf8(std::string& r, uint32_t c)
    {
        if (c == 0 || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return;
        if (c < 0x80) r += (char)c;
        else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 0x3F)); r += (char)(0x80 | (c & 0x3F)); }
        else { r += (char)(0xF0 | (c >> 18)); r += (char)(0x80 | ((c >> 12) & 0x3F)); r += (char)(0x80 | ((c >> 6) & 0x3F)); r += (char)(0x80 | (c & 0x3F)); }
    }

    // &amp; &quot; &#39; &#x2F; ... (lo necesario para los atributos y el título)
    std::string SinEntidades(const std::string& s)
    {
        if (s.find('&') == std::string::npos) return s;
        static const std::map<std::string, uint32_t> nombres = {
            { "amp", '&' }, { "quot", '"' }, { "apos", '\'' }, { "lt", '<' }, { "gt", '>' }, { "nbsp", 0xA0 },
            { "sol", '/' }, { "colon", ':' }, { "quest", '?' }, { "equals", '=' }, { "num", '#' }, { "percnt", '%' },
            { "aacute", 0xE1 }, { "eacute", 0xE9 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA },
            { "ntilde", 0xF1 }, { "Ntilde", 0xD1 }, { "uuml", 0xFC }, { "laquo", 0xAB }, { "raquo", 0xBB },
        };
        std::string r;
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] != '&')
            {
                r += s[i];
                continue;
            }
            size_t fin = s.find(';', i + 1);
            if (fin == std::string::npos || fin - i > 12)
            {
                r += s[i];
                continue;
            }
            std::string e = s.substr(i + 1, fin - i - 1);
            uint32_t c = 0;
            if (!e.empty() && e[0] == '#')
            {
                char* final = nullptr;
                c = (e.size() > 1 && (e[1] == 'x' || e[1] == 'X')) ? strtoul(e.c_str() + 2, &final, 16) : strtoul(e.c_str() + 1, &final, 10);
                if (!final || *final) c = 0;
            }
            else
            {
                auto it = nombres.find(e);
                if (it != nombres.end()) c = it->second;
            }
            if (c == 0)
            {
                r += s[i];
                continue;
            }
            AgregarUtf8(r, c);
            i = fin;
        }
        return r;
    }

    std::string Recortar(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) a++;
        while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
        return s.substr(a, b - a);
    }

    // Une una dirección relativa con la de la página. Solo deja http, https y ftp, sin el «#...» del final.
    std::string Resolver(const std::string& base, std::string rel)
    {
        rel = Recortar(rel);
        if (rel.empty() || rel[0] == '#') return {};
        // Espacios y saltos de línea dentro del atributo: los navegadores los quitan o los codifican
        std::string limpio;
        for (char c : rel)
        {
            if (c == '\n' || c == '\r' || c == '\t') continue;
            if (c == ' ') limpio += "%20";
            else limpio += c;
        }
        std::string r;
        CURLU* u = curl_url();
        if (!u) return r;
        if (curl_url_set(u, CURLUPART_URL, base.c_str(), 0) == CURLUE_OK
            && curl_url_set(u, CURLUPART_URL, limpio.c_str(), 0) == CURLUE_OK)
        {
            curl_url_set(u, CURLUPART_FRAGMENT, nullptr, 0);
            char* esquema = nullptr;
            if (curl_url_get(u, CURLUPART_SCHEME, &esquema, 0) == CURLUE_OK && esquema)
            {
                std::string e = Bajo(esquema);
                curl_free(esquema);
                char* todo = nullptr;
                if ((e == "http" || e == "https" || e == "ftp") && curl_url_get(u, CURLUPART_URL, &todo, 0) == CURLUE_OK && todo)
                {
                    r = todo;
                    curl_free(todo);
                }
            }
        }
        curl_url_cleanup(u);
        return r;
    }

    struct Juntar
    {
        std::vector<Enlace> lista;
        std::set<std::string> vistos;

        void Agregar(const std::string& url, Tipo porEtiqueta, bool soloArchivos)
        {
            if (url.empty() || lista.size() >= MaximoEnlaces || NoPermitido(url)) return;
            if (!vistos.insert(url).second) return;
            Enlace e;
            e.url = url;
            if (EsMagnet(url))
            {
                e.tipo = Tipo::Torrent;
                auto dn = url.find("dn=");
                std::string nombre = dn == std::string::npos ? std::string() : url.substr(dn + 3, url.find('&', dn) - dn - 3);
                for (auto& c : nombre)
                {
                    if (c == '+') c = ' ';
                }
                e.nombre = nombre.empty() ? L"(enlace magnet)" : hnd::LimpiarNombre(hnd::NombreDeUrl("x://x/" + nombre));
            }
            else
            {
                e.nombre = hnd::NombreDeUrl(url);
                e.tipo = e.nombre.empty() ? Tipo::Pagina : TipoDeNombre(e.nombre);
                if (e.tipo == Tipo::Pagina && porEtiqueta != Tipo::Pagina) e.tipo = porEtiqueta;
                if (e.nombre.empty()) e.nombre = hnd::Ancho(Sitio(url));
            }
            if (soloArchivos && (e.tipo == Tipo::Pagina || e.tipo == Tipo::Imagen || e.tipo == Tipo::Otro)) return;
            lista.push_back(std::move(e));
        }
    };

    // Direcciones de archivos escritas dentro de un <script> (por ejemplo, el video de un reproductor)
    void BuscarEnScript(const std::string& script, Juntar& j)
    {
        // En JSON las barras suelen ir escritas «\/»
        std::string s;
        s.reserve(script.size());
        for (size_t p = 0; p < script.size(); p++)
        {
            if (script[p] == '\\' && p + 1 < script.size() && script[p + 1] == '/') continue;
            s += script[p];
        }
        size_t i = 0;
        while (i < s.size())
        {
            size_t a = s.find("http", i);
            if (a == std::string::npos) break;
            if (!EmpiezaEn(s, a, "https://") && !EmpiezaEn(s, a, "http://"))
            {
                i = a + 4;
                continue;
            }
            size_t b = a;
            while (b < s.size() && s[b] > ' ' && s[b] != '"' && s[b] != '\'' && s[b] != '<' && s[b] != '>' && s[b] != '\\' && s[b] != '`')
                b++;
            std::string url = Resolver(s.substr(a, b - a), s.substr(a, b - a));
            i = b;
            if (url.empty()) continue;
            Tipo t = TipoDeNombre(hnd::NombreDeUrl(url));
            if (t == Tipo::Pagina || t == Tipo::Imagen || t == Tipo::Otro) continue;
            j.Agregar(url, Tipo::Pagina, true);
        }
    }

    // Codifica cada parte de una ruta de Internet Archive (deja las «/»)
    std::string CodificarRuta(const std::string& ruta)
    {
        static const char* cifras = "0123456789ABCDEF";
        std::string r;
        for (unsigned char c : ruta)
        {
            if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') r += (char)c;
            else
            {
                r += '%';
                r += cifras[c >> 4];
                r += cifras[c & 15];
            }
        }
        return r;
    }

    int64_t Numero(const json& v)
    {
        if (v.is_number_integer()) return v.get<int64_t>();
        if (v.is_number()) return (int64_t)v.get<double>();
        if (v.is_string())
        {
            const std::string& s = v.get_ref<const std::string&>();
            if (!s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) return _atoi64(s.c_str());
        }
        return -1;
    }

    std::string Texto(const json& v)
    {
        if (v.is_string()) return v.get<std::string>();
        if (v.is_array() && !v.empty() && v[0].is_string()) return v[0].get<std::string>();
        return {};
    }
}

hnd::enlaces::Tipo hnd::enlaces::TipoDeNombre(const std::wstring& nombre)
{
    auto punto = nombre.find_last_of(L'.');
    if (punto == std::wstring::npos || punto + 1 >= nombre.size() || nombre.size() - punto > 12) return Tipo::Pagina;
    std::wstring ext = Minusculas(nombre.substr(punto + 1));
    static const std::map<std::wstring, Tipo> tabla = {
        { L"mp4", Tipo::Video }, { L"m4v", Tipo::Video }, { L"mkv", Tipo::Video }, { L"webm", Tipo::Video }, { L"avi", Tipo::Video },
        { L"mov", Tipo::Video }, { L"wmv", Tipo::Video }, { L"flv", Tipo::Video }, { L"mpg", Tipo::Video }, { L"mpeg", Tipo::Video },
        { L"ts", Tipo::Video }, { L"m2ts", Tipo::Video }, { L"3gp", Tipo::Video }, { L"ogv", Tipo::Video }, { L"vob", Tipo::Video },
        { L"mp3", Tipo::Audio }, { L"m4a", Tipo::Audio }, { L"aac", Tipo::Audio }, { L"flac", Tipo::Audio }, { L"ogg", Tipo::Audio },
        { L"oga", Tipo::Audio }, { L"opus", Tipo::Audio }, { L"wav", Tipo::Audio }, { L"wma", Tipo::Audio }, { L"aiff", Tipo::Audio },
        { L"mka", Tipo::Audio }, { L"mid", Tipo::Audio }, { L"midi", Tipo::Audio },
        { L"jpg", Tipo::Imagen }, { L"jpeg", Tipo::Imagen }, { L"png", Tipo::Imagen }, { L"gif", Tipo::Imagen }, { L"webp", Tipo::Imagen },
        { L"bmp", Tipo::Imagen }, { L"svg", Tipo::Imagen }, { L"tif", Tipo::Imagen }, { L"tiff", Tipo::Imagen }, { L"avif", Tipo::Imagen },
        { L"heic", Tipo::Imagen }, { L"ico", Tipo::Imagen },
        { L"pdf", Tipo::Documento }, { L"doc", Tipo::Documento }, { L"docx", Tipo::Documento }, { L"xls", Tipo::Documento },
        { L"xlsx", Tipo::Documento }, { L"ppt", Tipo::Documento }, { L"pptx", Tipo::Documento }, { L"odt", Tipo::Documento },
        { L"ods", Tipo::Documento }, { L"odp", Tipo::Documento }, { L"rtf", Tipo::Documento }, { L"epub", Tipo::Documento },
        { L"mobi", Tipo::Documento }, { L"azw3", Tipo::Documento }, { L"txt", Tipo::Documento }, { L"csv", Tipo::Documento },
        { L"djvu", Tipo::Documento }, { L"cbz", Tipo::Documento }, { L"cbr", Tipo::Documento }, { L"xps", Tipo::Documento },
        { L"zip", Tipo::Comprimido }, { L"rar", Tipo::Comprimido }, { L"7z", Tipo::Comprimido }, { L"tar", Tipo::Comprimido },
        { L"gz", Tipo::Comprimido }, { L"tgz", Tipo::Comprimido }, { L"bz2", Tipo::Comprimido }, { L"xz", Tipo::Comprimido },
        { L"zst", Tipo::Comprimido }, { L"cab", Tipo::Comprimido }, { L"lz", Tipo::Comprimido }, { L"lzma", Tipo::Comprimido },
        { L"exe", Tipo::Programa }, { L"msi", Tipo::Programa }, { L"msix", Tipo::Programa }, { L"msixbundle", Tipo::Programa },
        { L"appx", Tipo::Programa }, { L"apk", Tipo::Programa }, { L"xapk", Tipo::Programa }, { L"dmg", Tipo::Programa },
        { L"pkg", Tipo::Programa }, { L"deb", Tipo::Programa }, { L"rpm", Tipo::Programa }, { L"appimage", Tipo::Programa },
        { L"jar", Tipo::Programa },
        { L"iso", Tipo::Disco }, { L"img", Tipo::Disco }, { L"vhd", Tipo::Disco }, { L"vhdx", Tipo::Disco }, { L"wim", Tipo::Disco },
        { L"torrent", Tipo::Torrent },
        { L"srt", Tipo::Subtitulos }, { L"vtt", Tipo::Subtitulos }, { L"ass", Tipo::Subtitulos }, { L"ssa", Tipo::Subtitulos },
        { L"html", Tipo::Pagina }, { L"htm", Tipo::Pagina }, { L"php", Tipo::Pagina }, { L"asp", Tipo::Pagina }, { L"aspx", Tipo::Pagina },
        { L"jsp", Tipo::Pagina }, { L"cgi", Tipo::Pagina }, { L"shtml", Tipo::Pagina },
    };
    auto it = tabla.find(ext);
    if (it != tabla.end()) return it->second;
    // Sin extensión conocida: si parece una extensión de verdad es «otro archivo»; si no, una página
    bool parece = ext.size() <= 5 && std::all_of(ext.begin(), ext.end(), [](wchar_t c) { return std::iswalnum(c); });
    return parece ? Tipo::Otro : Tipo::Pagina;
}

const wchar_t* hnd::enlaces::TextoTipo(Tipo t)
{
    switch (t)
    {
    case Tipo::Video: return L"Video";
    case Tipo::Audio: return L"Audio";
    case Tipo::Imagen: return L"Imagen";
    case Tipo::Documento: return L"Documento";
    case Tipo::Comprimido: return L"Comprimido";
    case Tipo::Programa: return L"Programa";
    case Tipo::Disco: return L"Imagen de disco";
    case Tipo::Torrent: return L"Torrent";
    case Tipo::Subtitulos: return L"Subtítulos";
    case Tipo::Otro: return L"Otro archivo";
    case Tipo::Pagina: return L"Página";
    }
    return L"";
}

bool hnd::enlaces::EsMagnet(const std::string& texto)
{
    return Empieza(texto, "magnet:?") && texto.size() > 20 && texto.find_first_of(" \t\r\n\"<>") == std::string::npos;
}

std::string hnd::enlaces::Sitio(const std::string& url)
{
    auto esquema = url.find("://");
    if (esquema == std::string::npos) return {};
    auto fin = url.find_first_of("/?#", esquema + 3);
    std::string s = url.substr(esquema + 3, fin == std::string::npos ? std::string::npos : fin - esquema - 3);
    auto arroba = s.rfind('@');
    if (arroba != std::string::npos) s = s.substr(arroba + 1);
    if (!s.empty() && s[0] == '[')
    {
        auto cierre = s.find(']');
        s = cierre == std::string::npos ? s : s.substr(0, cierre + 1);
    }
    else
    {
        auto puerto = s.find(':');
        if (puerto != std::string::npos) s = s.substr(0, puerto);
    }
    while (!s.empty() && s.back() == '.') s.pop_back();
    return Bajo(s);
}

bool hnd::enlaces::NoPermitido(const std::string& url)
{
    std::string s = Sitio(url);
    for (const char* d : { "youtube.com", "youtu.be", "youtube-nocookie.com", "googlevideo.com", "youtubekids.com" })
    {
        std::string dom = d;
        if (s == dom || (s.size() > dom.size() && s.compare(s.size() - dom.size() - 1, dom.size() + 1, "." + dom) == 0)) return true;
    }
    return false;
}

hnd::enlaces::ResultadoTexto hnd::enlaces::DeTexto(const std::wstring& texto)
{
    ResultadoTexto r;
    std::set<std::string> vistos;
    std::string t = Utf8(texto);
    size_t i = 0;
    while (i < t.size() && r.enlaces.size() < MaximoEnlaces)
    {
        // El siguiente comienzo de una dirección
        size_t mejor = std::string::npos;
        for (size_t k = i; k < t.size(); k++)
        {
            char c = (char)std::tolower((unsigned char)t[k]);
            if (c != 'h' && c != 'f' && c != 'm') continue;
            if (EmpiezaEn(t, k, "https://") || EmpiezaEn(t, k, "http://") || EmpiezaEn(t, k, "ftp://") || EmpiezaEn(t, k, "magnet:?"))
            {
                // Que no sea parte de otra palabra (por ejemplo «xhttp://»)
                if (k > 0 && std::isalnum((unsigned char)t[k - 1])) continue;
                mejor = k;
                break;
            }
        }
        if (mejor == std::string::npos) break;
        size_t fin = mejor;
        while (fin < t.size() && (unsigned char)t[fin] > ' ' && t[fin] != '"' && t[fin] != '\'' && t[fin] != '<' && t[fin] != '>'
               && t[fin] != '`')
            fin++;
        std::string url = t.substr(mejor, fin - mejor);
        i = fin;
        // Signos de puntuación pegados al final («…archivo.zip.» o «(https://…)»)
        while (!url.empty() && std::string(".,;:!?").find(url.back()) != std::string::npos) url.pop_back();
        while (!url.empty() && (url.back() == ')' || url.back() == ']'))
        {
            char abre = url.back() == ')' ? '(' : '[';
            if (std::count(url.begin(), url.end(), abre) >= std::count(url.begin(), url.end(), url.back())) break;
            url.pop_back();
        }
        bool magnet = EsMagnet(url);
        if (!magnet && url.find("://") + 3 >= url.size()) continue;
        if (!magnet && NoPermitido(url))
        {
            r.noPermitidos++;
            continue;
        }
        if (!vistos.insert(url).second)
        {
            r.repetidos++;
            continue;
        }
        r.enlaces.push_back(url);
    }
    return r;
}

std::vector<hnd::enlaces::Enlace> hnd::enlaces::DeHtml(const std::string& html, const std::string& urlBase, std::wstring* titulo)
{
    Juntar j;
    std::string base = urlBase;
    size_t n = html.size();

    // <base href="..."> cambia desde dónde se resuelven las direcciones relativas
    {
        std::string bajo = Bajo(html.substr(0, std::min<size_t>(n, 256 * 1024)));
        auto b = bajo.find("<base");
        if (b != std::string::npos)
        {
            auto h = bajo.find("href", b);
            auto cierre = bajo.find('>', b);
            if (h != std::string::npos && h < cierre)
            {
                size_t p = h + 4;
                while (p < n && (html[p] == ' ' || html[p] == '=')) p++;
                char q = p < n ? html[p] : 0;
                if (q == '"' || q == '\'')
                {
                    auto e = html.find(q, p + 1);
                    if (e != std::string::npos)
                    {
                        std::string nueva = Resolver(urlBase, SinEntidades(html.substr(p + 1, e - p - 1)));
                        if (!nueva.empty()) base = nueva;
                    }
                }
            }
        }
    }

    size_t i = 0;
    while ((i = html.find('<', i)) != std::string::npos)
    {
        if (html.compare(i, 4, "<!--") == 0)
        {
            auto f = html.find("-->", i + 4);
            i = f == std::string::npos ? n : f + 3;
            continue;
        }
        size_t k = i + 1;
        while (k < n && (std::isalnum((unsigned char)html[k]) || html[k] == '-' || html[k] == ':')) k++;
        if (k == i + 1)
        {
            i++;
            continue;
        }
        std::string etiqueta = Bajo(html.substr(i + 1, k - i - 1));

        // Atributos
        std::map<std::string, std::string> at;
        size_t p = k;
        while (p < n && html[p] != '>')
        {
            if (std::isspace((unsigned char)html[p]) || html[p] == '/')
            {
                p++;
                continue;
            }
            size_t a = p;
            while (p < n && !std::isspace((unsigned char)html[p]) && html[p] != '=' && html[p] != '>' && html[p] != '/') p++;
            std::string nombre = Bajo(html.substr(a, p - a));
            while (p < n && std::isspace((unsigned char)html[p])) p++;
            std::string valor;
            if (p < n && html[p] == '=')
            {
                p++;
                while (p < n && std::isspace((unsigned char)html[p])) p++;
                if (p < n && (html[p] == '"' || html[p] == '\''))
                {
                    char q = html[p++];
                    size_t e = html.find(q, p);
                    if (e == std::string::npos) e = n;
                    valor = html.substr(p, e - p);
                    p = e < n ? e + 1 : n;
                }
                else
                {
                    size_t s = p;
                    while (p < n && !std::isspace((unsigned char)html[p]) && html[p] != '>') p++;
                    valor = html.substr(s, p - s);
                }
            }
            if (nombre.empty())
            {
                p++;
                continue;
            }
            if (!at.count(nombre)) at[nombre] = SinEntidades(valor);
        }
        i = p < n ? p + 1 : n;

        auto valor = [&](const char* nombre) {
            auto it = at.find(nombre);
            return it == at.end() ? std::string() : it->second;
        };

        if (etiqueta == "script" || etiqueta == "style" || etiqueta == "title")
        {
            std::string cierre = "</" + etiqueta;
            size_t f = i;
            // Buscar el cierre sin importar mayúsculas
            for (;;)
            {
                f = html.find("</", f);
                if (f == std::string::npos || Bajo(html.substr(f, cierre.size())) == cierre) break;
                f += 2;
            }
            if (f == std::string::npos) f = n;
            std::string dentro = html.substr(i, f - i);
            if (etiqueta == "script") BuscarEnScript(dentro, j);
            if (etiqueta == "title" && titulo && titulo->empty())
            {
                std::wstring t = TextoDeBytes(Recortar(SinEntidades(dentro)));
                for (auto& c : t)
                {
                    if (c < 32) c = L' ';
                }
                *titulo = t.substr(0, 150);
            }
            i = f;
            continue;
        }

        if (etiqueta == "a" || etiqueta == "area")
        {
            std::string href = Recortar(valor("href"));
            if (EsMagnet(href)) j.Agregar(href, Tipo::Torrent, false);
            else j.Agregar(Resolver(base, href), Tipo::Pagina, false);
        }
        else if (etiqueta == "video" || etiqueta == "audio" || etiqueta == "source" || etiqueta == "track" || etiqueta == "embed")
        {
            Tipo t = etiqueta == "audio" ? Tipo::Audio : etiqueta == "track" ? Tipo::Subtitulos : Tipo::Video;
            std::string tipo = Bajo(valor("type"));
            if (tipo.rfind("audio/", 0) == 0) t = Tipo::Audio;
            j.Agregar(Resolver(base, valor("src")), t, false);
        }
        else if (etiqueta == "img")
        {
            j.Agregar(Resolver(base, valor("src")), Tipo::Imagen, false);
        }
        else if (etiqueta == "object")
        {
            j.Agregar(Resolver(base, valor("data")), Tipo::Pagina, false);
        }
        else if (etiqueta == "meta")
        {
            std::string prop = Bajo(valor("property").empty() ? valor("name") : valor("property"));
            if (prop == "og:video" || prop == "og:video:url" || prop == "og:video:secure_url" || prop == "twitter:player:stream")
                j.Agregar(Resolver(base, valor("content")), Tipo::Video, false);
            else if (prop == "og:audio" || prop == "og:audio:url" || prop == "og:audio:secure_url")
                j.Agregar(Resolver(base, valor("content")), Tipo::Audio, false);
        }
    }
    return std::move(j.lista);
}

bool hnd::enlaces::EsInternetArchive(const std::string& url, std::string& id)
{
    id.clear();
    std::string s = Sitio(url);
    if (s != "archive.org" && s != "www.archive.org") return false;
    auto esquema = url.find("://");
    auto ruta = url.find('/', esquema + 3);
    if (ruta == std::string::npos) return false;
    std::string camino = url.substr(ruta);
    auto corte = camino.find_first_of("?#");
    if (corte != std::string::npos) camino = camino.substr(0, corte);
    bool detalles = camino.rfind("/details/", 0) == 0;
    bool bajar = camino.rfind("/download/", 0) == 0;
    if (!detalles && !bajar) return false;
    std::string resto = camino.substr(detalles ? 9 : 10);
    auto barra = resto.find('/');
    std::string ident = resto.substr(0, barra);
    if (ident.empty() || ident.size() > 100) return false;
    if (!std::all_of(ident.begin(), ident.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; }))
        return false;
    // /download/<id>/<archivo> es un archivo suelto: se baja como cualquier otro
    if (bajar && barra != std::string::npos && barra + 1 < resto.size()) return false;
    id = ident;
    return true;
}

bool hnd::enlaces::DeInternetArchive(const std::string& texto, const std::string& id, Pagina& p, std::string& error)
{
    p = {};
    p.internetArchive = true;
    json j;
    try
    {
        j = json::parse(texto);
    }
    catch (...)
    {
        error = "Internet Archive respondió algo que no se entiende. Inténtalo más tarde.";
        return false;
    }
    if (!j.is_object() || j.empty() || !j.contains("files") || !j["files"].is_array() || j.value("is_dark", false))
    {
        error = "No se encontró ese elemento en Internet Archive, o no es público.";
        return false;
    }
    if (j.contains("metadata") && j["metadata"].is_object() && j["metadata"].contains("title"))
        p.titulo = TextoDeBytes(Texto(j["metadata"]["title"])).substr(0, 150);
    if (p.titulo.empty()) p.titulo = Ancho(id);
    for (auto& f : j["files"])
    {
        if (!f.is_object() || p.enlaces.size() >= MaximoEnlaces) continue;
        std::string nombre = Texto(f.value("name", json()));
        if (nombre.empty() || nombre.find("..") != std::string::npos) continue;
        if (Texto(f.value("private", json())) == "true") continue;   // en préstamo o restringido
        Enlace e;
        e.url = "https://archive.org/download/" + id + "/" + CodificarRuta(nombre);
        e.nombre = TextoDeBytes(nombre);
        std::replace(e.nombre.begin(), e.nombre.end(), L'/', L'\\');
        std::wstring corto = e.nombre.substr(e.nombre.find_last_of(L'\\') + 1);
        e.tipo = TipoDeNombre(corto);
        if (e.tipo == Tipo::Pagina) e.tipo = Tipo::Otro;
        e.tamano = Numero(f.value("size", json()));
        std::string formato = Texto(f.value("format", json()));
        e.formato = TextoDeBytes(formato);
        // Archivos que el sitio usa por dentro: miniaturas, índices, metadatos y registros
        static const std::set<std::string> internos = { "Metadata", "Item Tile", "Thumbnail", "Storj Upload Log",
            "Storj Upload Trigger", "JSON", "Item CDX Index", "Item CDX Meta-Index", "Scandata", "OCR Page Index",
            "OCR Search Text", "Page Numbers JSON", "hOCR", "Djvu XML", "Abbyy GZ", "Archive BitTorrent" };
        e.interno = Texto(f.value("source", json())) == "metadata" || internos.count(formato) || e.tamano == 0
                    || nombre.find(".thumbs/") != std::string::npos
                    || (nombre.size() > 7 && nombre.compare(nombre.size() - 7, 7, ".asr.js") == 0);
        p.enlaces.push_back(std::move(e));
    }
    // Primero los archivos originales, luego los que hizo el sitio, y al final los internos
    std::stable_sort(p.enlaces.begin(), p.enlaces.end(), [](const Enlace& a, const Enlace& b) { return !a.interno && b.interno; });
    if (p.enlaces.empty())
    {
        error = "Ese elemento de Internet Archive no tiene archivos públicos.";
        return false;
    }
    return true;
}

bool hnd::enlaces::Buscar(const std::string& url, Pagina& p, std::string& error, const std::atomic<bool>* parar)
{
    p = {};
    if (NoPermitido(url))
    {
        error = Utf8(TextoNoPermitido);
        return false;
    }
    std::string id, cuerpo, final, tipo;
    if (EsInternetArchive(url, id))
    {
        if (!BajarTexto("https://archive.org/metadata/" + id, 48ull * 1024 * 1024, cuerpo, final, tipo, error, parar)) return false;
        return DeInternetArchive(cuerpo, id, p, error);
    }
    if (!BajarTexto(url, 12ull * 1024 * 1024, cuerpo, final, tipo, error, parar)) return false;
    std::string t = Bajo(tipo);
    bool pareceHtml = t.find("html") != std::string::npos || t.find("xml") != std::string::npos
                      || Bajo(cuerpo.substr(0, 2048)).find("<html") != std::string::npos
                      || Bajo(cuerpo.substr(0, 2048)).find("<!doctype") != std::string::npos;
    if (!pareceHtml)
    {
        error = "Esa dirección no es una página web sino un archivo. Para bajarlo usa «Agregar enlace».";
        return false;
    }
    p.enlaces = DeHtml(cuerpo, final.empty() ? url : final, &p.titulo);
    if (p.titulo.empty()) p.titulo = Ancho(Sitio(url));
    return true;
}
