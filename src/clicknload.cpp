#include "clicknload.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <string_view>
#include <set>

#include "contrasenas.hpp"
#include "enlaces.hpp"
#include "util.hpp"

// Para las funciones de abajo que están fuera de hnd (las del espacio sin nombre)
namespace contrasenas = hnd::contrasenas;

namespace
{
    using hnd::cnl::Campos;
    using hnd::cnl::MaxCampo;
    using hnd::cnl::MaxCampos;
    using hnd::cnl::MaxCuerpo;

    constexpr size_t MaxCabeceras = 16 * 1024;         // las cabeceras, sin contar la primera línea
    constexpr size_t MaxLinea = MaxCuerpo + 4096;      // la primera línea: en un GET trae los enlaces
    constexpr size_t MaxNombre = 256;                  // el nombre de un campo
    constexpr size_t MaxCabecerasDeParte = 8 * 1024;   // las cabeceras de cada parte de un multipart
    constexpr int MaxPorMinuto = 30;                   // pedidos con enlaces
    constexpr size_t MaxAnotadosPorMinuto = 20;        // rechazos en el registro: una página no puede llenarlo

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

    int ValorHex(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // «+» es un espacio y «%41» es «A», como manda el formato de los formularios
    std::string DecodificarFormulario(const std::string& s)
    {
        std::string r;
        r.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '+') r += ' ';
            else if (s[i] == '%' && i + 2 < s.size() && ValorHex(s[i + 1]) >= 0 && ValorHex(s[i + 2]) >= 0)
            {
                r += (char)(ValorHex(s[i + 1]) * 16 + ValorHex(s[i + 2]));
                i += 2;
            }
            else r += s[i];
        }
        return r;
    }

    bool Base64(const std::string& texto, std::string& salida)
    {
        salida.clear();
        int valor = 0, bits = 0;
        size_t igual = 0;
        for (char c : texto)
        {
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+' || c == '-') v = 62;
            else if (c == '/' || c == '_') v = 63;
            else if (c == '=') { igual++; continue; }
            else if (std::isspace((unsigned char)c)) continue;
            else return false;
            if (igual > 0) return false;   // algo después del relleno
            valor = (valor << 6) | v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                salida += (char)((valor >> bits) & 0xFF);
            }
        }
        return igual <= 2;
    }

    bool AesCbc(const uint8_t* clave, const uint8_t* iv, const std::string& datos, std::string& salida)
    {
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_KEY_HANDLE k = nullptr;
        bool ok = false;
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))
            && BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0))
            && BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &k, nullptr, 0, const_cast<PUCHAR>(clave), 16, 0)))
        {
            UCHAR v[16];
            memcpy(v, iv, 16);
            salida.assign(datos.size(), '\0');
            ULONG n = 0;
            ok = BCRYPT_SUCCESS(BCryptDecrypt(k, (PUCHAR)datos.data(), (ULONG)datos.size(), nullptr, v, 16, (PUCHAR)salida.data(),
                                              (ULONG)salida.size(), &n, 0));
            salida.resize(ok ? n : 0);
        }
        if (k) BCryptDestroyKey(k);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
        return ok;
    }

    bool EsUtf8(const std::string& s)
    {
        return s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0) > 0;
    }

    // Quita el relleno: PKCS#7 si está bien hecho, y además todos los ceros (algunos sitios rellenan con ceros)
    std::string SinRelleno(std::string s)
    {
        if (!s.empty())
        {
            unsigned char n = (unsigned char)s.back();
            if (n >= 1 && n <= 16 && n <= s.size()
                && std::all_of(s.end() - n, s.end(), [n](char c) { return (unsigned char)c == n; }))
                s.resize(s.size() - n);
        }
        s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
        return s;
    }

    // ---------------------------------------------------------------- Formularios

    // «1 MB», «512 KB»
    std::string Medida(size_t bytes)
    {
        return bytes % (1024 * 1024) == 0 ? std::to_string(bytes >> 20) + " MB" : std::to_string(bytes >> 10) + " KB";
    }

    std::string DemasiadoLargo()
    {
        return "un campo es demasiado largo (más de " + Medida(MaxCampo) + ")";
    }

    std::string DemasiadosCampos()
    {
        return "demasiados campos (más de " + std::to_string(MaxCampos) + ")";
    }

    // «multipart/form-data; boundary=x» → «multipart/form-data»
    std::string Medio(const std::string& tipo)
    {
        return Bajo(Recortar(tipo.substr(0, tipo.find(';'))));
    }

    // El valor de un parámetro de una cabecera («form-data; name="urls"» o «multipart/form-data; boundary=abc»)
    std::string Parametro(const std::string& v, const char* nombre)
    {
        size_t i = v.find(';');
        while (i != std::string::npos)
        {
            i++;
            size_t igual = v.find('=', i), sig = v.find(';', i);
            if (igual == std::string::npos) break;
            if (sig != std::string::npos && sig < igual)
            {
                i = sig;   // un parámetro sin «=»
                continue;
            }
            std::string clave = Bajo(Recortar(v.substr(i, igual - i)));
            size_t j = igual + 1;
            while (j < v.size() && (v[j] == ' ' || v[j] == '\t')) j++;
            std::string valor;
            if (j < v.size() && v[j] == '"')
            {
                for (j++; j < v.size() && v[j] != '"'; j++)
                {
                    if (v[j] == '\\' && j + 1 < v.size()) j++;
                    valor += v[j];
                }
                sig = v.find(';', j);
            }
            else valor = Recortar(v.substr(j, sig == std::string::npos ? std::string::npos : sig - j));
            if (clave == nombre) return valor;
            i = sig;
        }
        return {};
    }

    // Un nombre de campo de los de siempre («urls», «source», «urls[]»): sin «:» ni «/», así no se confunde con un enlace
    // (sobre una vista: así revisar una línea que no tiene nombre, por ejemplo la continuación de una contraseña con «=»,
    // no copia nada)
    bool NombreSimple(std::string_view s)
    {
        return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
                   return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '[' || c == ']';
               });
    }

    // Un campo puede ser la contraseña del paquete: lo que no entra se borra de la memoria antes de soltarlo
    std::string Agregar(Campos& campos, std::string nombre, std::string valor)
    {
        if (campos.size() >= MaxCampos || nombre.size() > MaxNombre || valor.size() > MaxCampo)
        {
            const bool muchos = campos.size() >= MaxCampos;
            contrasenas::Borrar(nombre);
            contrasenas::Borrar(valor);
            return muchos ? DemasiadosCampos() : DemasiadoLargo();
        }
        campos.emplace_back(std::move(nombre), std::move(valor));
        return {};
    }

    // «urls=a%0Ab&source=...» (también lo que va después de «?» en una dirección)
    std::string LeerUrlencoded(const std::string& s, Campos& campos)
    {
        size_t desde = 0;
        while (desde < s.size())
        {
            size_t fin = s.find('&', desde);
            if (fin == std::string::npos) fin = s.size();
            size_t igual = std::min(s.find('=', desde), fin);
            // Lo de antes de decodificar también se borra (puede ser la contraseña con «%»)
            std::string crudo = s.substr(desde, igual - desde);
            std::string nombre = DecodificarFormulario(crudo);
            contrasenas::Borrar(crudo);
            if (!nombre.empty())
            {
                std::string valor;
                if (igual < fin)
                {
                    crudo = s.substr(igual + 1, fin - igual - 1);
                    valor = DecodificarFormulario(crudo);
                    contrasenas::Borrar(crudo);
                }
                std::string motivo = Agregar(campos, std::move(nombre), std::move(valor));
                if (!motivo.empty()) return motivo;
            }
            desde = fin + 1;
        }
        return {};
    }

    // «--marca», las cabeceras de la parte, una línea en blanco, el valor; al final «--marca--»
    std::string LeerMultipart(const std::string& tipo, const std::string& cuerpo, Campos& campos)
    {
        std::string limite = Parametro(tipo, "boundary");
        if (limite.empty() || limite.size() > 200) return "formulario multipart sin un «boundary» válido";
        const std::string marca = "\r\n--" + limite;
        // La primera marca puede ir al principio, sin el salto de línea de delante; lo que haya antes se ignora
        size_t p;
        if (cuerpo.compare(0, marca.size() - 2, marca, 2, std::string::npos) == 0) p = marca.size() - 2;
        else
        {
            p = cuerpo.find(marca);
            if (p == std::string::npos) return "formulario multipart mal formado";
            p += marca.size();
        }
        for (size_t partes = 0;; partes++)
        {
            // Después de cada marca: «--» si era la última, o el salto de línea de la parte siguiente
            if (cuerpo.compare(p, 2, "--") == 0) return {};
            while (p < cuerpo.size() && (cuerpo[p] == ' ' || cuerpo[p] == '\t')) p++;
            if (cuerpo.compare(p, 2, "\r\n") != 0) return "formulario multipart mal formado";
            p += 2;
            if (partes >= MaxCampos) return DemasiadosCampos();
            // Las cabeceras de la parte (pueden faltar)
            size_t inicio;
            std::string nombre;
            if (cuerpo.compare(p, 2, "\r\n") == 0) inicio = p + 2;
            else
            {
                size_t fin = cuerpo.find("\r\n\r\n", p);
                if (fin == std::string::npos || fin - p > MaxCabecerasDeParte) return "formulario multipart mal formado";
                for (size_t a = p; a < fin;)
                {
                    size_t b = std::min(cuerpo.find("\r\n", a), fin);
                    std::string l = cuerpo.substr(a, b - a);
                    size_t dp = l.find(':');
                    if (dp != std::string::npos && Bajo(Recortar(l.substr(0, dp))) == "content-disposition")
                        nombre = Parametro(l.substr(dp + 1), "name");
                    a = b + 2;
                }
                inicio = fin + 4;
            }
            size_t sig = cuerpo.find(marca, inicio);
            if (sig == std::string::npos) return "formulario multipart incompleto";
            if (!nombre.empty())
            {
                if (sig - inicio > MaxCampo) return DemasiadoLargo();
                std::string motivo = Agregar(campos, nombre, cuerpo.substr(inicio, sig - inicio));
                if (!motivo.empty()) return motivo;
            }
            p = sig + marca.size();
        }
    }

    // «urls=https://a» y en otra línea «https://b»: una línea sin «nombre=» sigue el campo de antes
    std::string LeerTextoPlano(const std::string& cuerpo, Campos& campos)
    {
        for (size_t p = 0; p < cuerpo.size();)
        {
            size_t f = cuerpo.find('\n', p);
            if (f == std::string::npos) f = cuerpo.size();
            size_t largo = f - p;
            if (largo > 0 && cuerpo[f - 1] == '\r') largo--;
            std::string linea = cuerpo.substr(p, largo);
            p = f + 1;
            size_t igual = linea.find('=');
            std::string motivo;
            if (igual != std::string::npos && NombreSimple(std::string_view(linea).substr(0, igual)))
                motivo = Agregar(campos, linea.substr(0, igual), linea.substr(igual + 1));
            else if (!campos.empty())
            {
                std::string& v = campos.back().second;
                if (v.size() + 1 + linea.size() > MaxCampo) motivo = DemasiadoLargo();
                else
                {
                    contrasenas::Agregar(v, "\n", 1);   // si v se muda, el lugar de antes se borra
                    contrasenas::Agregar(v, linea.data(), linea.size());
                }
            }
            contrasenas::Borrar(linea);
            if (!motivo.empty()) return motivo;
        }
        return {};
    }

    // ---------------------------------------------------------------- Enlaces

    // ¿Empieza aquí una dirección? (http://, https://, ftp:// o magnet:?, sin importar mayúsculas)
    bool EmpiezaEnlace(const std::string& s, size_t i)
    {
        for (const char* e : { "http://", "https://", "ftp://", "magnet:?" })
        {
            size_t n = strlen(e);
            if (i <= s.size() && s.size() - i >= n
                && std::equal(e, e + n, s.begin() + i, [](char a, char b) { return a == (char)std::tolower((unsigned char)b); }))
                return true;
        }
        return false;
    }

    bool TieneEnlace(const std::string& s)
    {
        for (size_t i = 0; i < s.size(); i++)
        {
            char c = (char)std::tolower((unsigned char)s[i]);
            if ((c == 'h' || c == 'f' || c == 'm') && EmpiezaEnlace(s, i)) return true;
        }
        return false;
    }

    // Solo «%XX» (sin cambiar «+»): para lo que llega codificado dos veces
    std::string SinPorcentajes(const std::string& s)
    {
        std::string r;
        r.reserve(s.size());
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

    // «https%3A%2F%2F...»: enlaces que el sitio codificó antes de ponerlos en el formulario
    bool CodificadoDosVeces(const std::string& s)
    {
        std::string b = Bajo(s.substr(0, std::min<size_t>(s.size(), 64 * 1024)));
        return b.find("%3a%2f%2f") != std::string::npos || b.find("magnet%3a%3f") != std::string::npos;
    }

    // Los bytes que no forman UTF-8 válido, como «%XX» (por ejemplo, una «ñ» de una página en Latin-1)
    std::string ConPorcentajes(const std::string& s)
    {
        if (EsUtf8(s)) return s;
        static const char* hex = "0123456789ABCDEF";
        std::string r;
        for (unsigned char c : s)
        {
            if (c < 0x80) r += (char)c;
            else
            {
                r += '%';
                r += hex[c >> 4];
                r += hex[c & 15];
            }
        }
        return r;
    }

    bool Separa(unsigned char c)
    {
        return c <= ' ' || c == 0x7F;
    }

    // ---------------------------------------------------------------- Respuestas y registro

    struct Respuesta
    {
        int codigo = 200;
        std::string tipo = "text/plain; charset=utf-8";
        std::string cuerpo;
        std::string extra;   // más cabeceras, cada una con su «\r\n»
    };

    const char* TextoCodigo(int c)
    {
        switch (c)
        {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        default: return "Error";
        }
    }

    void Enviar(SOCKET s, const Respuesta& r)
    {
        std::string t = "HTTP/1.1 " + std::to_string(r.codigo) + " " + TextoCodigo(r.codigo) + "\r\n";
        t += "Content-Type: " + r.tipo + "\r\n";
        t += "Content-Length: " + std::to_string(r.cuerpo.size()) + "\r\n";
        t += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
        t += r.extra + "\r\n";
        t += r.cuerpo;
        size_t enviado = 0;
        while (enviado < t.size())
        {
            int n = send(s, t.data() + enviado, (int)(t.size() - enviado), 0);
            if (n <= 0) break;
            enviado += (size_t)n;
        }
    }

    std::string Html(const std::string& s)
    {
        std::string r;
        for (char c : s)
        {
            switch (c)
            {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            case '\'': r += "&#39;"; break;
            default: r += c;
            }
        }
        return r;
    }

    // «success» o «failed», como JDownloader. Si la persona lo ve en una pestaña (el sitio mandó el formulario ahí),
    // una página corta en español.
    Respuesta Contestar(bool pagina, int codigo, const std::string& motivo, size_t enlaces = 0)
    {
        Respuesta r;
        r.codigo = codigo;
        if (!pagina)
        {
            r.cuerpo = codigo == 200 ? "success\r\n" : "failed\r\n";
            return r;
        }
        std::string titulo, texto;
        if (codigo == 200)
        {
            titulo = enlaces == 1 ? "Listo: Atarraya recibió 1 enlace" : "Listo: Atarraya recibió " + std::to_string(enlaces) + " enlaces";
            texto = enlaces == 1 ? "Está en el recolector de Atarraya: revísalo ahí. Nada se baja solo."
                                 : "Están en el recolector de Atarraya: revísalos ahí y elige cuáles bajar. Nada se baja solo.";
            texto += " Ya puedes cerrar esta pestaña.";
        }
        else
        {
            titulo = "Atarraya no pudo recibir estos enlaces";
            texto = "Motivo: " + motivo + ". Quedó anotado en el registro de Atarraya.";
        }
        r.tipo = "text/html; charset=utf-8";
        r.extra = "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n";
        r.cuerpo = "<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
                   "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Atarraya</title></head>"
                   "<body style=\"font-family:system-ui,sans-serif;max-width:40em;margin:3em auto;padding:0 1em;line-height:1.5\">"
                   "<h1 style=\"font-size:1.4em\">" + Html(titulo) + "</h1><p>" + Html(texto) + "</p></body></html>\n";
        return r;
    }

    // Solo letras, números y los signos de «extra»; si trae otra cosa o es muy largo, vacío (no va al registro)
    std::string Limpio(const std::string& s, size_t largo, const char* extra)
    {
        if (s.empty() || s.size() > largo) return {};
        for (unsigned char c : s)
        {
            if (c == 0 || (!std::isalnum(c) && !strchr(extra, c))) return {};
        }
        return s;
    }

    // El sitio de una dirección, para el registro: «ejemplo.org» (sin usuario, sin puerto y sin el resto)
    std::string SitioParaRegistro(const std::string& url)
    {
        return Limpio(hnd::enlaces::Sitio(url), 253, ".-_[]:");
    }

    // Lo que se sabe del pedido, para el registro
    struct Pedido
    {
        std::string metodo, ruta, forma, sitio;
        bool pagina = false;   // la persona lo ve en una pestaña («Sec-Fetch-Dest: document»)
    };

    std::string TextoPedido(const Pedido& p)
    {
        std::string r;
        if (!p.metodo.empty())
        {
            // Solo las rutas que usa Click'n'Load: cualquier otra, aunque sea corta, podría traer un identificador o una clave
            static const char* const conocidas[] = { "/flash", "/flash/add", "/flash/addcrypted", "/flash/addcrypted2", "/jdcheck.js",
                                                      "/favicon.ico" };
            std::string m = Limpio(p.metodo, 16, ""), ruta;
            for (const char* c : conocidas)
                if (p.ruta == c) ruta = c;
            r = (m.empty() ? std::string("?") : m) + " " + (ruta.empty() ? std::string("(otra dirección)") : ruta);
            if (!p.forma.empty()) r += ", " + p.forma;
            r += ", ";
        }
        return r + "desde " + (p.sitio.empty() ? std::string("un sitio desconocido") : p.sitio);
    }

    // Cómo llegó el formulario, para el registro
    std::string Forma(const std::string& tipo)
    {
        std::string m = Medio(tipo);
        if (m.empty()) return "formulario sin tipo";
        if (m == "application/x-www-form-urlencoded") return "urlencoded";
        if (m == "multipart/form-data") return "multipart";
        std::string l = Limpio(m, 60, "/.+-_");
        return l.empty() ? std::string("otro tipo") : l;
    }

    // «falta el campo jk; llegaron 2 campos: crypted, source». Solo los nombres de los campos, nunca lo que traen.
    std::string FaltaElCampo(const std::string& cual, const Campos& campos)
    {
        std::string r = "falta el campo " + cual;
        if (campos.empty()) return r + "; el formulario llegó vacío";
        r += campos.size() == 1 ? std::string("; llegó 1 campo") : "; llegaron " + std::to_string(campos.size()) + " campos";
        std::vector<std::string> nombres;
        for (const auto& c : campos)
        {
            if (c.first.size() > 32 || !NombreSimple(c.first)) continue;
            if (std::find(nombres.begin(), nombres.end(), c.first) != nombres.end()) continue;
            if (nombres.size() == 8)
            {
                nombres.push_back("...");
                break;
            }
            nombres.push_back(c.first);
        }
        for (size_t i = 0; i < nombres.size(); i++) r += (i == 0 ? ": " : ", ") + nombres[i];
        return r;
    }

    std::wstring Paquete(const Campos& f)
    {
        std::string p = Recortar(hnd::cnl::Valor(f, "package"));
        if (!p.empty() && p.find("://") == std::string::npos) return hnd::Ancho(p.substr(0, 150));
        std::string origen = Recortar(hnd::cnl::Valor(f, "source"));
        if (origen.empty()) origen = Recortar(hnd::cnl::Valor(f, "referer"));
        std::string sitio = hnd::enlaces::Sitio(origen);
        return sitio.empty() ? std::wstring(L"Click'n'Load") : L"Click'n'Load de " + hnd::Ancho(sitio);
    }

    std::string Origen(const Campos& f)
    {
        for (const char* k : { "source", "referer" })
        {
            if (!hnd::cnl::Tiene(f, k)) continue;
            std::string v = Recortar(hnd::cnl::Valor(f, k));
            std::string b = Bajo(v.substr(0, 8));
            // Solo una dirección web en UTF-8 válido, sin espacios ni caracteres de control
            bool limpio = std::none_of(v.begin(), v.end(), [](char ch) { return (unsigned char)ch < 0x20 || ch == 0x7F; });
            if ((b.rfind("http://", 0) == 0 || b.rfind("https://", 0) == 0) && v.size() <= 2048 && limpio && EsUtf8(v)
                && v.find_first_of(" \t\r\n\"<>") == std::string::npos)
                return v;
        }
        return {};
    }
}

// ---------------------------------------------------------------- Partes que se pueden probar solas

bool hnd::cnl::ClaveDeJk(const std::string& jk, std::vector<uint8_t>& clave)
{
    // function NOMBRE ( ) { return 'HEX' [+ 'HEX']... [;] } [;]
    clave.clear();
    const std::string& s = jk;
    size_t i = 0;
    auto espacios = [&]() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; };
    auto palabra = [&](const char* p) {
        size_t n = strlen(p);
        if (s.compare(i, n, p) != 0) return false;
        i += n;
        return true;
    };
    if (s.size() > 4096) return false;
    espacios();
    if (!palabra("function")) return false;
    if (i >= s.size() || !std::isspace((unsigned char)s[i])) return false;
    espacios();
    size_t inicio = i;
    while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '$')) i++;
    if (i == inicio || std::isdigit((unsigned char)s[inicio])) return false;
    espacios();
    if (!palabra("(")) return false;
    espacios();
    if (!palabra(")")) return false;
    espacios();
    if (!palabra("{")) return false;
    espacios();
    if (!palabra("return")) return false;
    std::string hex;
    for (bool primero = true;; primero = false)
    {
        espacios();
        if (!primero)
        {
            if (!palabra("+")) break;
            espacios();
        }
        if (i >= s.size() || (s[i] != '\'' && s[i] != '"')) return false;
        char comilla = s[i++];
        size_t fin = s.find(comilla, i);
        if (fin == std::string::npos) return false;
        std::string trozo = s.substr(i, fin - i);
        if (!std::all_of(trozo.begin(), trozo.end(), [](unsigned char c) { return std::isxdigit(c); })) return false;
        hex += trozo;
        i = fin + 1;
    }
    espacios();
    palabra(";");
    espacios();
    if (!palabra("}")) return false;
    espacios();
    palabra(";");
    espacios();
    if (i != s.size() || hex.size() != 32) return false;
    for (size_t k = 0; k < 32; k += 2) clave.push_back((uint8_t)(ValorHex(hex[k]) * 16 + ValorHex(hex[k + 1])));
    return true;
}

bool hnd::cnl::Descifrar(const std::string& crypted, const std::vector<uint8_t>& clave, std::string& texto)
{
    texto.clear();
    if (clave.size() != 16) return false;
    // Algunos sitios mandan el base64 sin codificar y los «+» llegan como espacios; otros lo codifican dos veces
    // («%2B», «%2F», «%3D»; el base64 nunca trae «%», y pyLoad también lo acepta así)
    std::string b64 = crypted;
    std::replace(b64.begin(), b64.end(), ' ', '+');
    if (b64.find('%') != std::string::npos) b64 = SinPorcentajes(b64);
    std::string datos;
    if (!Base64(b64, datos) || datos.empty() || datos.size() % 16 != 0 || datos.size() > MaxCuerpo) return false;
    std::string salida;
    if (AesCbc(clave.data(), clave.data(), datos, salida) && EsUtf8(SinRelleno(salida)))
    {
        texto = SinRelleno(salida);
        return true;
    }
    // Como pyLoad: algunos mandan el vector inicial delante de los datos
    if (datos.size() >= 32 && AesCbc(clave.data(), reinterpret_cast<const uint8_t*>(datos.data()), datos.substr(16), salida)
        && EsUtf8(SinRelleno(salida)))
    {
        texto = SinRelleno(salida);
        return true;
    }
    return false;
}

bool hnd::cnl::TipoAdmitido(const std::string& tipo)
{
    std::string m = Medio(tipo);
    return m.empty() || m == "application/x-www-form-urlencoded" || m == "multipart/form-data" || m == "text/plain";
}

std::string hnd::cnl::LeerFormulario(const std::string& tipo, const std::string& cuerpo, Campos& campos)
{
    campos.clear();
    // Todo el lugar de entrada: si la lista se mudara, las contraseñas cortas (que van dentro de cada texto) quedarían
    // copiadas en el lugar de antes sin borrar
    campos.reserve(MaxCampos);
    std::string m = Medio(tipo);
    if (m.empty() || m == "application/x-www-form-urlencoded") return LeerUrlencoded(cuerpo, campos);
    if (m == "multipart/form-data") return LeerMultipart(tipo, cuerpo, campos);
    if (m == "text/plain") return LeerTextoPlano(cuerpo, campos);
    return "tipo de envío no admitido";
}

std::string hnd::cnl::Valor(const Campos& campos, const char* nombre)
{
    for (const auto& c : campos)
    {
        if (Bajo(c.first) == nombre) return c.second;
    }
    return {};
}

bool hnd::cnl::Tiene(const Campos& campos, const char* nombre)
{
    return std::any_of(campos.begin(), campos.end(), [&](const auto& c) { return Bajo(c.first) == nombre; });
}

bool hnd::cnl::TextoDeEnlaces(const Campos& campos, std::string& texto)
{
    texto.clear();
    bool hay = false;
    for (const auto& c : campos)
    {
        std::string n = Bajo(c.first);
        // «urls[]» o «urls[0]», como los arman algunos formularios
        size_t a = n.find('[');
        if (a != std::string::npos && n.back() == ']'
            && std::all_of(n.begin() + a + 1, n.end() - 1, [](unsigned char ch) { return std::isdigit(ch); }))
            n.resize(a);
        if (n != "urls" && n != "url" && n != "links" && n != "link") continue;
        hay = true;
        texto += c.second;
        texto += '\n';
    }
    return hay;
}

std::vector<std::string> hnd::cnl::Contrasenas(const Campos& campos)
{
    // Todas las veces que venga el campo, en orden; una por línea
    // Con el lugar justo reservado de entrada: el texto nunca se muda (dejaría una copia sin borrar)
    size_t total = 0;
    for (const auto& c : campos)
    {
        if (Bajo(c.first) == "passwords") total += c.second.size() + 1;
    }
    std::string texto;
    texto.reserve(total);
    for (const auto& c : campos)
    {
        if (Bajo(c.first) != "passwords") continue;
        contrasenas::Agregar(texto, c.second.data(), c.second.size());
        contrasenas::Agregar(texto, "\n", 1);
    }
    auto r = contrasenas::DeTexto(texto);
    contrasenas::Borrar(texto);
    return r;
}

hnd::cnl::Lista hnd::cnl::Enlaces(const std::string& recibido, size_t maximo)
{
    // Si no hay ningún enlace a la vista pero sí enlaces codificados, se decodifican una vez más
    const bool dosVeces = !TieneEnlace(recibido) && CodificadoDosVeces(recibido);
    const std::string texto = dosVeces ? SinPorcentajes(recibido) : recibido;
    Lista r;
    std::set<std::string> vistos;
    auto agregar = [&](const std::string& url) {
        r.llegaron++;
        if (!enlaces::EsMagnet(url) && enlaces::NoPermitido(url)) r.youtube++;
        else if (!vistos.insert(url).second) r.repetidos++;
        else if (r.enlaces.size() >= maximo) r.sobran++;
        else r.enlaces.push_back(url);
    };
    auto pieza = [&](const std::string& trozo) {
        // Las comas que quedaron en los bordes («a, b» deja «a,»)
        size_t a = trozo.find_first_not_of(','), b = trozo.find_last_not_of(',');
        if (a == std::string::npos) return;
        std::string p = ConPorcentajes(trozo.substr(a, b - a + 1));
        if (EmpiezaEnlace(p, 0) && p.find_first_of("\"<>`") == std::string::npos)
        {
            // Un enlace solo: tal cual llegó (también con «'» o paréntesis, que pueden ser parte de la dirección)
            bool magnet = std::tolower((unsigned char)p[0]) == 'm';
            if (magnet ? enlaces::EsMagnet(p) : p.find("://") + 3 < p.size()) agregar(p);
            else r.otros++;
            return;
        }
        if (!TieneEnlace(p))
        {
            r.otros++;
            return;
        }
        // Enlaces mezclados con otras cosas («<a href="...">», «"https://..."»): se buscan como en un texto cualquiera
        auto d = enlaces::DeTexto(Ancho(p));
        r.llegaron += (size_t)(d.repetidos + d.noPermitidos);
        r.repetidos += (size_t)d.repetidos;
        r.youtube += (size_t)d.noPermitidos;
        for (const auto& u : d.enlaces) agregar(u);
        if (d.enlaces.empty() && d.repetidos == 0 && d.noPermitidos == 0) r.otros++;
    };
    size_t i = 0;
    while (i < texto.size())
    {
        if (Separa((unsigned char)texto[i]))
        {
            i++;
            continue;
        }
        size_t fin = i;
        while (fin < texto.size() && !Separa((unsigned char)texto[fin])) fin++;
        // Dentro de un trozo sin espacios, una coma separa si después viene otro enlace («a,b»); dentro de una
        // dirección («archivo,1.zip»), no
        size_t a = i;
        for (size_t k = i; k < fin; k++)
        {
            if (texto[k] == ',' && EmpiezaEnlace(texto, k + 1))
            {
                pieza(texto.substr(a, k - a));
                a = k + 1;
            }
        }
        pieza(texto.substr(a, fin - a));
        i = fin;
    }
    return r;
}

std::string hnd::cnl::Resumen(const Lista& l)
{
    auto n = [](size_t x, const char* uno, const char* varios) { return std::to_string(x) + " " + (x == 1 ? uno : varios); };
    std::string r = std::string(l.llegaron == 1 ? "llegó " : "llegaron ") + n(l.llegaron, "enlace", "enlaces")
                    + (l.enlaces.size() == 1 ? std::string(" y quedó 1") : " y quedaron " + std::to_string(l.enlaces.size()));
    std::vector<std::string> d;
    if (l.repetidos) d.push_back(n(l.repetidos, "repetido", "repetidos"));
    if (l.youtube) d.push_back(std::to_string(l.youtube) + " de YouTube, que Atarraya no baja");
    if (l.sobran) d.push_back(std::to_string(l.sobran) + (l.sobran == 1 ? " pasó" : " pasaron") + " del tope de " + std::to_string(MaxEnlaces));
    for (size_t i = 0; i < d.size(); i++) r += (i == 0 ? " (" : ", ") + d[i];
    if (!d.empty()) r += ")";
    if (l.otros) r += "; además, " + n(l.otros, "trozo de texto que no es un enlace", "trozos de texto que no son enlaces");
    return r;
}

// ---------------------------------------------------------------- Servidor

hnd::ServidorCnL::~ServidorCnL()
{
    Detener();
}

bool hnd::ServidorCnL::Iniciar(AlRecibir alRecibir, std::string& error, uint16_t puerto)
{
    Detener();
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        error = "No se pudo preparar la red de Windows.";
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        error = "No se pudo preparar la conexión.";
        WSACleanup();
        return false;
    }
    // Que ningún otro programa pueda escuchar en el mismo puerto mientras Atarraya lo usa
    BOOL si = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&si), sizeof(si));
    sockaddr_in dir = {};
    dir.sin_family = AF_INET;
    dir.sin_port = htons(puerto);
    dir.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // solo 127.0.0.1: nada de la red puede conectarse
    if (bind(s, reinterpret_cast<sockaddr*>(&dir), sizeof(dir)) != 0 || listen(s, 8) != 0)
    {
        int e = WSAGetLastError();
        closesocket(s);
        WSACleanup();
        error = (e == WSAEADDRINUSE || e == WSAEACCES)
                    ? "El puerto " + std::to_string(puerto) + " está ocupado: quizá JDownloader o pyLoad están abiertos."
                    : "No se pudo escuchar en 127.0.0.1:" + std::to_string(puerto) + ".";
        return false;
    }
    if (puerto == 0)
    {
        sockaddr_in real = {};
        int largo = sizeof(real);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&real), &largo) == 0) puerto = ntohs(real.sin_port);
    }
    m_escucha = (uintptr_t)s;
    m_puerto = puerto;
    m_alRecibir = std::move(alRecibir);
    m_parar = false;
    m_activo = true;
    m_hilo = std::thread(&ServidorCnL::Bucle, this);
    Registrar("Click'n'Load: escuchando en 127.0.0.1:" + std::to_string(puerto));
    return true;
}

void hnd::ServidorCnL::Detener()
{
    if (!m_activo && !m_hilo.joinable()) return;
    m_parar = true;
    if (m_hilo.joinable()) m_hilo.join();
    // Las conexiones en curso ven «m_parar» en menos de un quinto de segundo
    for (auto& t : m_trabajos)
    {
        if (t.t.joinable()) t.t.join();
    }
    m_trabajos.clear();
    if (m_escucha != ~(uintptr_t)0)
    {
        closesocket((SOCKET)m_escucha);
        m_escucha = ~(uintptr_t)0;
        WSACleanup();
    }
    m_activo = false;
    Registrar("Click'n'Load: apagado");
}

void hnd::ServidorCnL::Anotar(const std::string& rechazo)
{
    bool anotar = false, callar = false;
    {
        std::lock_guard g(m_m);
        int64_t ahora = (int64_t)(GetTickCount64() / 1000);
        m_rechazos.erase(std::remove_if(m_rechazos.begin(), m_rechazos.end(), [&](int64_t t) { return ahora - t >= 60; }),
                         m_rechazos.end());
        if (m_rechazos.size() < MaxAnotadosPorMinuto)
        {
            m_rechazos.push_back(ahora);
            anotar = true;
            m_callado = false;
        }
        else if (!m_callado) callar = m_callado = true;
    }
    if (anotar) Registrar(rechazo);
    else if (callar) Registrar("Click'n'Load: llegan demasiados rechazos; los de este minuto ya no se anotan");
}

void hnd::ServidorCnL::Bucle()
{
    SOCKET escucha = (SOCKET)m_escucha;
    while (!m_parar)
    {
        fd_set f;
        FD_ZERO(&f);
        FD_SET(escucha, &f);
        timeval espera = { 0, 300000 };
        int r = select(0, &f, nullptr, nullptr, &espera);
        // Las conexiones que ya terminaron
        for (auto it = m_trabajos.begin(); it != m_trabajos.end();)
        {
            if (*it->fin)
            {
                if (it->t.joinable()) it->t.join();
                it = m_trabajos.erase(it);
            }
            else ++it;
        }
        if (r <= 0)
        {
            if (r < 0) Sleep(100);
            continue;
        }
        sockaddr_in de = {};
        int largo = sizeof(de);
        SOCKET c = accept(escucha, reinterpret_cast<sockaddr*>(&de), &largo);
        if (c == INVALID_SOCKET) continue;
        // Solo esta PC (127.0.0.1; el socket ya escucha solo ahí), como mucho 4 a la vez y 120 conexiones por minuto
        int64_t ahora = (int64_t)(GetTickCount64() / 1000);
        m_conexiones.erase(std::remove_if(m_conexiones.begin(), m_conexiones.end(), [&](int64_t t) { return ahora - t >= 60; }),
                           m_conexiones.end());
        bool local = de.sin_family == AF_INET && ntohl(de.sin_addr.s_addr) == INADDR_LOOPBACK;
        if (!local || m_trabajos.size() >= 4 || m_conexiones.size() >= 120)
        {
            closesocket(c);
            Anotar(!local ? "Click'n'Load: rechazado: una conexión que no es de esta PC"
                   : m_trabajos.size() >= 4 ? "Click'n'Load: rechazado: demasiadas conexiones a la vez (4 como mucho)"
                                            : "Click'n'Load: rechazado: demasiadas conexiones en un minuto (120 como mucho)");
            continue;
        }
        m_conexiones.push_back(ahora);
        Trabajo t;
        t.fin = std::make_shared<std::atomic<bool>>(false);
        auto fin = t.fin;
        t.t = std::thread([this, c, fin]() {
            try
            {
                Atender((uintptr_t)c);
            }
            catch (...)
            {
                Registrar("Click'n'Load: un pedido no se pudo leer");
            }
            closesocket(c);
            *fin = true;
        });
        m_trabajos.push_back(std::move(t));
    }
}

int hnd::ServidorCnL::Recibir(uintptr_t conexion, char* buf, int largo, uint64_t limite)
{
    SOCKET s = (SOCKET)conexion;
    while (!m_parar && GetTickCount64() < limite)
    {
        fd_set f;
        FD_ZERO(&f);
        FD_SET(s, &f);
        timeval espera = { 0, 200000 };
        int r = select(0, &f, nullptr, nullptr, &espera);
        if (r < 0) return -1;
        if (r == 0) continue;
        int n = recv(s, buf, largo, 0);
        return n > 0 ? n : -1;
    }
    return -1;
}

void hnd::ServidorCnL::Atender(uintptr_t conexion)
{
    SOCKET s = (SOCKET)conexion;
    DWORD espera = 2000;   // las respuestas son cortas: si no se pueden mandar en 2 s, se deja
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&espera), sizeof(espera));
    uint64_t limite = GetTickCount64() + 5000;   // todo el pedido tiene que llegar en 5 segundos
    Pedido p;
    // Cada rechazo queda en el registro con su motivo y el sitio: nunca los enlaces ni lo que trae el formulario
    auto rechazar = [&](int codigo, const std::string& motivo) {
        Anotar("Click'n'Load: rechazado: " + motivo + ". Pedido: " + TextoPedido(p));
        Enviar(s, Contestar(p.pagina, codigo, motivo));
    };
    auto incompleto = [&]() { Anotar("Click'n'Load: rechazado: el pedido llegó incompleto. Pedido: " + TextoPedido(p)); };

    // Todo lo que puede traer la contraseña del paquete (el campo «passwords» llega en el cuerpo o, en un GET, en la
    // dirección) se borra de la memoria al salir, por cualquier camino. Los textos crecen con contrasenas::Agregar, que
    // borra el lugar de antes si tiene que mudarlos.
    std::string datos, linea, cabeceras, cuerpo, destino, consulta;
    std::vector<char> buf(16 * 1024);
    cnl::Campos campos;
    struct AlSalir
    {
        std::function<void()> f;
        ~AlSalir() { f(); }
    } borrarAlSalir{ [&] {
        for (std::string* t : { &datos, &linea, &cabeceras, &cuerpo, &destino, &consulta }) contrasenas::Borrar(*t);
        contrasenas::Borrar(buf);
        for (auto& c : campos)
        {
            contrasenas::Borrar(c.first);
            contrasenas::Borrar(c.second);
        }
    } };

    // 1. La primera línea (en un GET trae los enlaces, así que puede ser larga) y las cabeceras
    size_t finLinea = std::string::npos, finCabeceras = std::string::npos;
    while (finCabeceras == std::string::npos)
    {
        int n = Recibir(conexion, buf.data(), (int)buf.size(), limite);
        if (n <= 0)
        {
            // Una conexión que se abre y no manda nada (los navegadores a veces se adelantan) no es un rechazo
            if (!datos.empty()) incompleto();
            return;
        }
        size_t antes = datos.size();
        contrasenas::Agregar(datos, buf.data(), (size_t)n);
        if (finLinea == std::string::npos)
        {
            finLinea = datos.find("\r\n", antes > 0 ? antes - 1 : 0);
            if (finLinea == std::string::npos)
            {
                if (datos.size() > MaxLinea)
                {
                    rechazar(414, "la dirección es demasiado larga (más de " + Medida(MaxCuerpo) + ")");
                    return;
                }
                continue;
            }
        }
        finCabeceras = datos.find("\r\n\r\n", std::max(finLinea, antes >= 3 ? antes - 3 : 0));
        if (finCabeceras == std::string::npos && datos.size() - finLinea > MaxCabeceras + 4)
        {
            rechazar(431, "cabeceras demasiado grandes (más de " + Medida(MaxCabeceras) + ")");
            return;
        }
    }
    linea = datos.substr(0, finLinea);
    if (finCabeceras > finLinea) cabeceras = datos.substr(finLinea + 2, finCabeceras - finLinea - 2);
    cuerpo = datos.substr(finCabeceras + 4);
    contrasenas::Borrar(datos);

    size_t e1 = linea.find(' '), e2 = linea.rfind(' ');
    if (e1 == std::string::npos || e2 == e1 || linea.compare(e2 + 1, 5, "HTTP/") != 0)
    {
        rechazar(400, "pedido mal formado");
        return;
    }
    p.metodo = linea.substr(0, e1);
    destino = linea.substr(e1 + 1, e2 - e1 - 1);
    contrasenas::Borrar(linea);
    size_t q = destino.find_first_of("?#");
    p.ruta = destino.substr(0, q);
    if (q != std::string::npos && destino[q] == '?')
    {
        size_t almohadilla = destino.find('#', q);
        consulta = destino.substr(q + 1, almohadilla == std::string::npos ? std::string::npos : almohadilla - q - 1);
    }
    contrasenas::Borrar(destino);
    if (p.ruta.size() > 1 && p.ruta.back() == '/') p.ruta.pop_back();   // «/flash/add/» es «/flash/add»

    std::map<std::string, std::string> h;
    for (size_t i = 0; i < cabeceras.size();)
    {
        size_t f = cabeceras.find("\r\n", i);
        std::string l = cabeceras.substr(i, f == std::string::npos ? std::string::npos : f - i);
        size_t dp = l.find(':');
        if (dp != std::string::npos)
        {
            std::string k = Bajo(Recortar(l.substr(0, dp)));
            if ((k == "host" || k == "content-length") && h.count(k))
            {
                rechazar(400, "pedido mal formado (cabecera repetida)");
                return;
            }
            h[k] = Recortar(l.substr(dp + 1));
        }
        if (f == std::string::npos) break;
        i = f + 2;
    }
    p.pagina = Bajo(h["sec-fetch-dest"]) == "document";
    for (const char* k : { "origin", "referer" })
    {
        if (p.sitio.empty() && h.count(k)) p.sitio = SitioParaRegistro(h[k]);
    }

    // 2. Solo pedidos dirigidos a esta PC por su nombre (evita el «DNS rebinding»)
    std::string host = Bajo(h["host"]);
    std::string puerto = ":" + std::to_string(m_puerto);
    if (host != "127.0.0.1" + puerto && host != "localhost" + puerto && host != "[::1]" + puerto)
    {
        std::string limpio = Limpio(host, 100, ".-_[]:");
        rechazar(403, host.empty() ? std::string("falta el host") : "host no permitido (" + (limpio.empty() ? std::string("otro") : limpio) + ")");
        return;
    }

    // 3. Qué pide. Los enlaces llegan en el cuerpo de un POST o en la dirección de un GET.
    bool conEnlaces = p.ruta == "/flash/add" || p.ruta == "/flash/addcrypted2" || p.ruta == "/flash/addcrypted";
    std::string tipo;
    if (p.metodo == "GET" || p.metodo == "HEAD")
    {
        if (p.ruta == "/jdcheck.js")
        {
            Enviar(s, { 200, "text/javascript; charset=utf-8", "jdownloader=true;\r\nvar version='42707';\r\n" });
            return;
        }
        if (p.ruta == "/flash")
        {
            Enviar(s, { 200, "text/plain; charset=utf-8", "JDownloader\r\n" });
            return;
        }
        if (p.ruta == "/favicon.ico")
        {
            Enviar(s, { 404, "text/plain; charset=utf-8", "" });   // el navegador lo pide solo: no es un rechazo
            return;
        }
        if (!conEnlaces)
        {
            rechazar(404, "dirección que no existe");
            return;
        }
        if (p.metodo == "HEAD")
        {
            rechazar(405, "método no admitido");
            return;
        }
        p.forma = "en la dirección";
        if (consulta.size() > MaxCuerpo)
        {
            rechazar(413, "demasiado grande (más de " + Medida(MaxCuerpo) + ")");
            return;
        }
        contrasenas::Borrar(cuerpo);
        cuerpo.swap(consulta);
        tipo = "application/x-www-form-urlencoded";
    }
    else if (p.metodo == "POST")
    {
        if (!conEnlaces)
        {
            rechazar(404, "dirección que no existe");
            return;
        }
        p.forma = Forma(h["content-type"]);
        if (h.count("transfer-encoding"))
        {
            rechazar(411, "envío en trozos (Transfer-Encoding), que no se admite");
            return;
        }
        if (!h.count("content-length"))
        {
            rechazar(411, "falta el largo del envío (Content-Length)");
            return;
        }
        const std::string& cl = h["content-length"];
        if (cl.empty() || cl.size() > 9 || !std::all_of(cl.begin(), cl.end(), [](unsigned char c) { return std::isdigit(c); }))
        {
            rechazar(400, "el largo del envío (Content-Length) no es válido");
            return;
        }
        size_t largo = (size_t)std::stoul(cl);
        if (largo > MaxCuerpo)
        {
            rechazar(413, "demasiado grande (más de " + Medida(MaxCuerpo) + ")");
            return;
        }
        while (cuerpo.size() < largo)
        {
            int n = Recibir(conexion, buf.data(), (int)buf.size(), limite);
            if (n <= 0)
            {
                incompleto();
                return;
            }
            contrasenas::Agregar(cuerpo, buf.data(), (size_t)n);
        }
        cuerpo.resize(largo);   // lo que sobra queda en su lugar reservado, que se borra entero al salir
        tipo = h["content-type"];
    }
    else
    {
        rechazar(405, "método no admitido");
        return;
    }

    // 4. Tope de envíos por minuto (las conexiones ya tienen su propio tope)
    bool demasiados = false;
    {
        std::lock_guard g(m_m);
        int64_t ahora = (int64_t)(GetTickCount64() / 1000);
        m_recientes.erase(std::remove_if(m_recientes.begin(), m_recientes.end(), [&](int64_t t) { return ahora - t >= 60; }),
                          m_recientes.end());
        if ((int)m_recientes.size() >= MaxPorMinuto) demasiados = true;
        else m_recientes.push_back(ahora);
    }
    if (demasiados)
    {
        rechazar(429, "demasiados pedidos (más de " + std::to_string(MaxPorMinuto) + " por minuto)");
        return;
    }
    if (p.ruta == "/flash/addcrypted")
    {
        rechazar(400, "un contenedor DLC, que Atarraya no abre");
        return;
    }

    // 5. El formulario
    if (!cnl::TipoAdmitido(tipo))
    {
        rechazar(415, "tipo de envío no admitido");
        return;
    }
    std::string motivo = cnl::LeerFormulario(tipo, cuerpo, campos);
    contrasenas::Borrar(cuerpo);
    if (!motivo.empty())
    {
        rechazar(400, motivo);
        return;
    }
    std::string origen = Origen(campos);
    std::string sitio = SitioParaRegistro(origen);   // el del formulario («source»), si vino; si no, el del navegador
    if (!sitio.empty()) p.sitio = sitio;

    // 6. Los enlaces
    std::string texto;
    if (p.ruta == "/flash/add")
    {
        if (!cnl::TextoDeEnlaces(campos, texto))
        {
            rechazar(400, FaltaElCampo("de los enlaces (urls, url, links o link)", campos));
            return;
        }
    }
    else
    {
        std::vector<uint8_t> clave;
        if (!cnl::Tiene(campos, "crypted") || !cnl::Tiene(campos, "jk"))
        {
            rechazar(400, FaltaElCampo(cnl::Tiene(campos, "crypted") ? "jk" : "crypted", campos));
            return;
        }
        if (!cnl::ClaveDeJk(cnl::Valor(campos, "jk"), clave))
        {
            rechazar(400, "la clave «jk» tiene una forma que no se acepta (Atarraya no ejecuta código de las páginas)");
            return;
        }
        if (!cnl::Descifrar(cnl::Valor(campos, "crypted"), clave, texto))
        {
            rechazar(400, "no se pudieron descifrar los enlaces");
            return;
        }
    }
    auto lista = cnl::Enlaces(texto, cnl::MaxEnlaces);
    if (lista.enlaces.empty())
    {
        rechazar(400, "no trae enlaces válidos: " + cnl::Resumen(lista));
        return;
    }
    // La contraseña del paquete, si vino: solo se cuenta en el registro, nunca se anota
    auto claves = cnl::Contrasenas(campos);
    for (auto& c : campos)
    {
        if (Bajo(c.first) == "passwords") contrasenas::Borrar(c.second);
    }
    Registrar("Click'n'Load: " + cnl::Resumen(lista) + ". Pedido: " + TextoPedido(p)
              + (claves.empty() ? std::string() : claves.size() == 1 ? std::string(". Trae la contraseña del paquete (no se anota)")
                                                                     : ". Trae " + std::to_string(claves.size()) + " contraseñas del paquete (no se anotan)"));
    size_t n = lista.enlaces.size();
    if (m_alRecibir) m_alRecibir(std::move(lista.enlaces), Paquete(campos), origen, std::move(claves));
    else contrasenas::Borrar(claves);
    Enviar(s, Contestar(p.pagina, 200, "", n));
}
