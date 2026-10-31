#include "motor.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <winhttp.h>
#include <shlobj.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <map>
#include <set>

#include "enlaces.hpp"
#include "nombres.hpp"
#include "redlocal.hpp"
#include "util.hpp"
#include "version.h"

using namespace std::chrono;
using json = nlohmann::json;

namespace
{
    // No se parte un trozo con menos de 2 MB por bajar. La parte nueva empieza al menos 1 MB más allá de donde
    // va la conexión dueña, y cada escritura es de 256 KB como mucho (CURLOPT_BUFFERSIZE): una escritura en
    // curso nunca alcanza la parte nueva.
    constexpr int64_t MinimoParaPartir = 2LL * 1024 * 1024;
    constexpr int64_t TamanoMinimoParte = 1024LL * 1024;      // al empezar, partes de al menos 1 MB
    constexpr int MaxReintentos = 8;

    struct Proxy
    {
        std::string servidor, excepciones;
    };

    struct Peticion
    {
        std::string url, referer, cookies, agente;
    };

    bool EsFtp(const std::string& url)
    {
        std::string bajo = url.substr(0, 7);
        std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return bajo.rfind("ftp://", 0) == 0 || bajo.rfind("ftps://", 0) == 0;
    }

    // Proxy configurado en Windows (si hay uno fijo). Los scripts PAC no se usan.
    Proxy ProxyDelSistema()
    {
        Proxy p;
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG c = {};
        if (!WinHttpGetIEProxyConfigForCurrentUser(&c)) return p;
        if (c.lpszProxy)
        {
            std::string s = hnd::Utf8(c.lpszProxy);
            if (s.find('=') != std::string::npos)
            {
                // «http=host:puerto;https=host:puerto»
                std::string elegido;
                size_t desde = 0;
                while (desde < s.size())
                {
                    auto fin = s.find(';', desde);
                    std::string parte = s.substr(desde, fin == std::string::npos ? std::string::npos : fin - desde);
                    if (parte.rfind("https=", 0) == 0) { elegido = parte.substr(6); break; }
                    if (parte.rfind("http=", 0) == 0 && elegido.empty()) elegido = parte.substr(5);
                    if (fin == std::string::npos) break;
                    desde = fin + 1;
                }
                p.servidor = elegido;
            }
            else p.servidor = s;
            auto sep = p.servidor.find_first_of("; ");
            if (sep != std::string::npos) p.servidor = p.servidor.substr(0, sep);
        }
        if (c.lpszProxyBypass)
        {
            std::string b = hnd::Utf8(c.lpszProxyBypass);
            size_t desde = 0;
            while (desde <= b.size())
            {
                auto fin = b.find_first_of("; ", desde);
                std::string parte = b.substr(desde, fin == std::string::npos ? std::string::npos : fin - desde);
                if (parte.rfind("*.", 0) == 0) parte = parte.substr(1);
                if (!parte.empty() && parte != "<local>" && parte.find('*') == std::string::npos)
                {
                    if (!p.excepciones.empty()) p.excepciones += ',';
                    p.excepciones += parte;
                }
                if (fin == std::string::npos) break;
                desde = fin + 1;
            }
        }
        if (c.lpszAutoConfigUrl) GlobalFree(c.lpszAutoConfigUrl);
        if (c.lpszProxy) GlobalFree(c.lpszProxy);
        if (c.lpszProxyBypass) GlobalFree(c.lpszProxyBypass);
        return p;
    }

    void Preparar(CURL* c, const Peticion& p, const Proxy& proxy)
    {
        curl_easy_setopt(c, CURLOPT_URL, p.url.c_str());
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https,ftp,ftps");
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https,ftp,ftps");
        std::string agente = p.agente.empty() ? std::string(APP_NOMBRE "/" APP_VERSION) : p.agente;
        curl_easy_setopt(c, CURLOPT_USERAGENT, agente.c_str());
        if (!p.referer.empty()) curl_easy_setopt(c, CURLOPT_REFERER, p.referer.c_str());
        // Cookies solo en memoria; cada una va solo a su dominio (formato Netscape)
        curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");
        size_t desde = 0;
        while (desde < p.cookies.size())
        {
            auto fin = p.cookies.find('\n', desde);
            std::string linea = p.cookies.substr(desde, fin == std::string::npos ? std::string::npos : fin - desde);
            if (!linea.empty()) curl_easy_setopt(c, CURLOPT_COOKIELIST, linea.c_str());
            if (fin == std::string::npos) break;
            desde = fin + 1;
        }
        // HTTP/1.1: cada parte va por su propia conexión (con HTTP/2 irían todas por una sola)
        curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
        curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
        curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 256L * 1024);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
        if (!proxy.servidor.empty())
        {
            curl_easy_setopt(c, CURLOPT_PROXY, proxy.servidor.c_str());
            if (!proxy.excepciones.empty()) curl_easy_setopt(c, CURLOPT_NOPROXY, proxy.excepciones.c_str());
        }
    }

    struct Cabeceras
    {
        long codigo = 0;
        std::map<std::string, std::string> v;   // claves en minúsculas, solo de la última respuesta
        bool sitioNoPermitido = false;          // una redirección llevaba a YouTube: se cortó
    };

    // ¿Una redirección («Location:») lleva a un sitio que no permite bajar sus videos? Las relativas se quedan en el
    // mismo sitio, que ya se revisó antes de empezar.
    bool RedirigeANoPermitido(const std::string& destino)
    {
        std::string d = destino;
        if (d.rfind("//", 0) == 0) d = "https:" + d;
        return d.find("://") != std::string::npos && hnd::enlaces::NoPermitido(d);
    }

    size_t AlRecibirCabecera(char* b, size_t t, size_t n, void* u)
    {
        auto* c = static_cast<Cabeceras*>(u);
        std::string linea(b, t * n);
        while (!linea.empty() && (linea.back() == '\r' || linea.back() == '\n')) linea.pop_back();
        if (linea.rfind("HTTP/", 0) == 0)
        {
            c->v.clear();
            auto e = linea.find(' ');
            c->codigo = e == std::string::npos ? 0 : atol(linea.c_str() + e + 1);
        }
        else
        {
            auto dp = linea.find(':');
            if (dp != std::string::npos)
            {
                std::string clave = linea.substr(0, dp);
                std::transform(clave.begin(), clave.end(), clave.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                std::string valor = linea.substr(dp + 1);
                size_t i = 0;
                while (i < valor.size() && (valor[i] == ' ' || valor[i] == '\t')) i++;
                c->v[clave] = valor.substr(i);
                if (clave == "location" && RedirigeANoPermitido(c->v[clave]))
                {
                    c->sitioNoPermitido = true;
                    return 0;   // curl corta aquí: no sigue la redirección
                }
            }
        }
        return t * n;
    }

    // Lo que AbrirSoloAfuera no dejó conectar durante una consulta
    struct Bloqueo
    {
        bool local = false;      // esta PC, su red o un rango local
        bool sinRedes = false;   // no se supo cuáles son las redes de la PC
    };

    // Para las consultas automáticas: no conectar nunca con esta PC ni con su red (rangos locales, direcciones de la
    // PC y las redes de sus tarjetas), aunque un nombre de afuera apunte ahí o una redirección lleve ahí. Si no se
    // puede saber, tampoco se conecta.
    curl_socket_t AbrirSoloAfuera(void* u, curlsocktype tipo, curl_sockaddr* a)
    {
        if (tipo == CURLSOCKTYPE_IPCXN)
        {
            auto destino = hnd::red::Destino::SinRevisar;
            try
            {
                destino = hnd::red::RevisarDestino(&a->addr);
            }
            catch (...)
            {
            }
            if (destino != hnd::red::Destino::Afuera)
            {
                auto* b = static_cast<Bloqueo*>(u);
                if (destino == hnd::red::Destino::Local) b->local = true;
                else b->sinRedes = true;
                return CURL_SOCKET_BAD;
            }
        }
        return socket(a->family, a->socktype, a->protocol);
    }

    int AlProgresar(void* u, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        return static_cast<std::atomic<bool>*>(u)->load() ? 1 : 0;
    }

    size_t CortarCuerpo(char*, size_t, size_t, void*) { return 0; }

    struct Sondeo
    {
        long codigo = 0;
        CURLcode res = CURLE_OK;
        int64_t tamano = -1;
        bool rangos = false;
        std::wstring nombre;
        std::string etag, modificado, tipo, urlFinal, errorCurl;
        std::string disposicion;    // Content-Disposition, tal cual
        bool noPermitido = false;   // una redirección llevaba a YouTube
    };

    // Pide el primer byte para saber el tamaño, el nombre y si se puede partir/reanudar.
    // Sin «conRango» pide el archivo entero y corta apenas llegan las cabeceras.
    Sondeo Sondear(const Peticion& p, const Proxy& proxy, std::atomic<bool>& parar, bool conRango = true)
    {
        Sondeo s;
        CURL* c = curl_easy_init();
        if (!c) { s.res = CURLE_FAILED_INIT; return s; }
        Preparar(c, p, proxy);
        Cabeceras cab;
        char error[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, error);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, AlRecibirCabecera);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &cab);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, CortarCuerpo);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, AlProgresar);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &parar);
        bool ftp = EsFtp(p.url);
        if (ftp) curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
        else if (conRango) curl_easy_setopt(c, CURLOPT_RANGE, "0-0");

        s.res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &s.codigo);
        char* efectiva = nullptr;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &efectiva);
        if (efectiva) s.urlFinal = efectiva;
        s.errorCurl = error[0] ? error : curl_easy_strerror(s.res);

        auto valor = [&](const char* clave) {
            auto it = cab.v.find(clave);
            return it == cab.v.end() ? std::string() : it->second;
        };
        if (ftp)
        {
            curl_off_t largo = -1;
            curl_easy_getinfo(c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &largo);
            if (s.res == CURLE_OK)
            {
                s.codigo = 200;
                s.tamano = largo;
                s.rangos = largo > 0;
            }
        }
        else
        {
            std::string rango = valor("content-range");
            auto barra = rango.find('/');
            if (s.codigo == 206)
            {
                s.rangos = true;
                if (barra != std::string::npos && rango.substr(barra + 1) != "*") s.tamano = _atoi64(rango.c_str() + barra + 1);
            }
            else if (s.codigo == 200)
            {
                std::string largo = valor("content-length");
                if (!largo.empty()) s.tamano = _atoi64(largo.c_str());
            }
            else if (s.codigo == 416 && barra != std::string::npos)
            {
                // «bytes */0»: el archivo está vacío
                s.tamano = _atoi64(rango.c_str() + barra + 1);
                s.rangos = true;
            }
            s.disposicion = valor("content-disposition");
            s.nombre = hnd::NombreDeContentDisposition(s.disposicion);
            s.noPermitido = cab.sitioNoPermitido;
            s.etag = valor("etag");
            s.modificado = valor("last-modified");
            s.tipo = valor("content-type");
        }
        curl_easy_cleanup(c);
        return s;
    }

    std::string TextoErrorHttp(long codigo)
    {
        if (codigo == 401 || codigo == 403)
            return "El servidor no dio permiso (código " + std::to_string(codigo) + "). Quizá el enlace venció: "
                   "copia uno nuevo y usa «Cambiar enlace».";
        if (codigo == 404 || codigo == 410)
            return "El archivo ya no está en el servidor (código " + std::to_string(codigo) + ").";
        if (codigo == 416) return "El servidor no aceptó la parte pedida (código 416). Prueba a empezar de nuevo.";
        if (codigo >= 500) return "El servidor tiene problemas (código " + std::to_string(codigo) + "). Inténtalo más tarde.";
        return "El servidor respondió con el código " + std::to_string(codigo) + ".";
    }

    std::string TextoErrorCurl(CURLcode res, const std::string& detalle)
    {
        switch (res)
        {
        case CURLE_COULDNT_RESOLVE_HOST: return "No se encontró el servidor. Revisa tu conexión a internet o el enlace.";
        case CURLE_COULDNT_RESOLVE_PROXY: return "No se encontró el proxy configurado en Windows.";
        case CURLE_COULDNT_CONNECT: return "No se pudo conectar con el servidor.";
        case CURLE_OPERATION_TIMEDOUT: return "El servidor dejó de responder.";
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CONNECT_ERROR: return "La conexión segura falló (el certificado del sitio no es válido).";
        case CURLE_TOO_MANY_REDIRECTS: return "El enlace redirige demasiadas veces.";
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_URL_MALFORMAT: return "El enlace no es válido (solo http, https y ftp).";
        case CURLE_LOGIN_DENIED:
        case CURLE_REMOTE_ACCESS_DENIED: return "El servidor no dio permiso para bajar el archivo.";
        case CURLE_REMOTE_FILE_NOT_FOUND: return "El archivo no está en el servidor.";
        default: return "Error de conexión: " + detalle;
        }
    }

    bool EsErrorPermanente(CURLcode res)
    {
        switch (res)
        {
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_TOO_MANY_REDIRECTS:
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_URL_MALFORMAT:
        case CURLE_LOGIN_DENIED:
        case CURLE_REMOTE_ACCESS_DENIED:
        case CURLE_REMOTE_FILE_NOT_FOUND:
            return true;
        default:
            return false;
        }
    }

    void Esperar(double segundos, const std::atomic<bool>& parar)
    {
        auto fin = steady_clock::now() + duration_cast<steady_clock::duration>(duration<double>(segundos));
        while (!parar && steady_clock::now() < fin) Sleep(100);
    }

    const char* NombreEstado(hnd::Estado e)
    {
        switch (e)
        {
        case hnd::Estado::EnCola: return "cola";
        case hnd::Estado::Iniciando: return "iniciando";
        case hnd::Estado::Descargando: return "descargando";
        case hnd::Estado::Pausada: return "pausada";
        case hnd::Estado::Completada: return "completada";
        case hnd::Estado::Error: return "error";
        case hnd::Estado::Compartiendo: return "completada";   // no se usa en las descargas normales
        }
        return "pausada";
    }

    // Para BajarTexto: junta el cuerpo en memoria, con un tope
    struct Memoria
    {
        std::string* destino = nullptr;
        size_t maximo = 0;
        bool excedido = false;
    };

    size_t AlRecibirMemoria(char* b, size_t t, size_t n, void* u)
    {
        auto* m = static_cast<Memoria*>(u);
        size_t total = t * n;
        if (m->destino->size() + total > m->maximo)
        {
            m->excedido = true;
            return 0;
        }
        m->destino->append(b, total);
        return total;
    }

    std::string TextoErrorPagina(long codigo)
    {
        if (codigo == 404 || codigo == 410) return "La página no existe (código " + std::to_string(codigo) + ").";
        if (codigo == 401 || codigo == 403)
            return "El sitio no dio permiso para leer la página (código " + std::to_string(codigo) + "). Puede que pida iniciar sesión.";
        if (codigo == 429) return "El sitio pide esperar un poco antes de volver a intentarlo (código 429).";
        if (codigo >= 500) return "El sitio tiene problemas (código " + std::to_string(codigo) + "). Inténtalo más tarde.";
        return "El sitio respondió con el código " + std::to_string(codigo) + ".";
    }

    hnd::Estado EstadoDeNombre(const std::string& s)
    {
        if (s == "cola" || s == "iniciando" || s == "descargando") return hnd::Estado::EnCola;
        if (s == "completada") return hnd::Estado::Completada;
        if (s == "error") return hnd::Estado::Error;
        return hnd::Estado::Pausada;
    }
}

namespace hnd
{
    int64_t DatosDescarga::Hecho() const
    {
        int64_t total = 0;
        for (auto& s : segmentos) total += s.hecho;
        return total;
    }

    const char* const TextoRespondioPagina =
        "El sitio respondió con una página, no con el archivo. Ábrela en el navegador: puede que pida un captcha o que pulses descargar.";

    const wchar_t* TextoEstado(Estado e)
    {
        switch (e)
        {
        case Estado::EnCola: return L"En cola";
        case Estado::Iniciando: return L"Conectando...";
        case Estado::Descargando: return L"Bajando";
        case Estado::Pausada: return L"En pausa";
        case Estado::Completada: return L"Terminada";
        case Estado::Error: return L"Error";
        case Estado::Compartiendo: return L"Compartiendo";
        }
        return L"";
    }

    bool BajarTexto(const std::string& url, size_t maximo, std::string& cuerpo, std::string& urlFinal, std::string& tipo,
                    std::string& error, const std::atomic<bool>* parar)
    {
        cuerpo.clear();
        urlFinal.clear();
        tipo.clear();
        error.clear();
        std::string bajo = url.substr(0, 8);
        std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (bajo.rfind("http://", 0) != 0 && bajo.rfind("https://", 0) != 0)
        {
            error = "La dirección tiene que empezar por http:// o https://.";
            return false;
        }
        CURL* c = curl_easy_init();
        if (!c)
        {
            error = "No se pudo preparar la conexión.";
            return false;
        }
        // Un nombre que los sitios aceptan como de navegador, sin ocultar que es Atarraya
        Peticion p{ url, "", "", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) " APP_NOMBRE "/" APP_VERSION };
        Preparar(c, p, ProxyDelSistema());
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
        Memoria m{ &cuerpo, maximo, false };
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, AlRecibirMemoria);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
        Cabeceras cab;
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, AlRecibirCabecera);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &cab);
        std::atomic<bool> nunca{ false };
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, AlProgresar);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, parar ? const_cast<std::atomic<bool>*>(parar) : &nunca);
        char detalle[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detalle);

        CURLcode res = curl_easy_perform(c);
        long codigo = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &codigo);
        char* efectiva = nullptr;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &efectiva);
        if (efectiva) urlFinal = efectiva;
        curl_easy_cleanup(c);
        auto it = cab.v.find("content-type");
        if (it != cab.v.end()) tipo = it->second;

        if (cab.sitioNoPermitido) error = Utf8(enlaces::TextoNoPermitido);
        else if (m.excedido) error = "La página es demasiado grande (más de " + Utf8(TextoTamano((int64_t)maximo)) + ").";
        else if (res == CURLE_ABORTED_BY_CALLBACK) error = "Se canceló.";
        else if (res != CURLE_OK) error = TextoErrorCurl(res, detalle[0] ? detalle : curl_easy_strerror(res));
        else if (codigo >= 400) error = TextoErrorPagina(codigo);
        if (!error.empty())
        {
            cuerpo.clear();
            return false;
        }
        return true;
    }

    void ComprobarEnlace(const std::string& url, const std::string& referer, InfoEnlace& r, const std::atomic<bool>* parar)
    {
        r = {};
        CURL* c = curl_easy_init();
        if (!c)
        {
            r.error = "No se pudo preparar la conexión.";
            return;
        }
        Peticion p{ url, referer, "", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) " APP_NOMBRE "/" APP_VERSION };
        // Sin proxy (ni el de Windows ni el de las variables de entorno): así se puede revisar cada dirección a la que
        // se conecta. En una red que solo sale por proxy, la consulta falla y el tamaño «se sabrá al empezar».
        Preparar(c, p, Proxy{});
        curl_easy_setopt(c, CURLOPT_PROXY, "");
        curl_easy_setopt(c, CURLOPT_NOBODY, 1L);   // HEAD: solo las cabeceras, nunca el archivo
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
        Bloqueo bloqueo;
        curl_easy_setopt(c, CURLOPT_OPENSOCKETFUNCTION, AbrirSoloAfuera);
        curl_easy_setopt(c, CURLOPT_OPENSOCKETDATA, &bloqueo);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
        Cabeceras cab;
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, AlRecibirCabecera);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &cab);
        std::atomic<bool> nunca{ false };
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, AlProgresar);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, parar ? const_cast<std::atomic<bool>*>(parar) : &nunca);
        char detalle[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detalle);
        CURLcode res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.codigo);
        char* efectiva = nullptr;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &efectiva);
        if (efectiva) r.urlFinal = efectiva;
        curl_off_t largo = -1;
        curl_easy_getinfo(c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &largo);
        curl_easy_cleanup(c);
        bool ftp = EsFtp(url);
        r.local = bloqueo.local;
        r.sinRevisarRed = bloqueo.sinRedes && !bloqueo.local;
        r.noPermitido = cab.sitioNoPermitido;
        if (r.local || r.sinRevisarRed || r.noPermitido) return;
        if (res != CURLE_OK)
        {
            r.error = res == CURLE_ABORTED_BY_CALLBACK ? std::string("Se canceló.") : TextoErrorCurl(res, detalle[0] ? detalle : curl_easy_strerror(res));
            return;
        }
        if (ftp) r.codigo = 200;
        if (r.codigo >= 400) return;   // quien llama decide qué significa cada código
        auto valor = [&](const char* clave) {
            auto it = cab.v.find(clave);
            return it == cab.v.end() ? std::string() : it->second;
        };
        r.tipo = valor("content-type");
        std::string disposicion = valor("content-disposition");
        std::string tipoBajo = r.tipo;
        std::transform(tipoBajo.begin(), tipoBajo.end(), tipoBajo.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        std::string dispBajo = disposicion;
        std::transform(dispBajo.begin(), dispBajo.end(), dispBajo.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        r.pagina = !ftp && (tipoBajo.find("text/html") != std::string::npos || tipoBajo.find("application/xhtml") != std::string::npos)
                   && dispBajo.find("attachment") == std::string::npos;
        r.nombre = NombreDeContentDisposition(disposicion);
        if (r.nombre.empty()) r.nombre = NombreDeUrl(r.urlFinal.empty() ? url : r.urlFinal);
        if (r.nombre.empty()) r.nombre = NombreDeUrl(url);
        if (!r.nombre.empty()) r.nombre = AgregarExtension(LimpiarNombre(r.nombre), r.tipo);
        if (!r.pagina && largo >= 0) r.tamano = largo;
    }

    // Limita la velocidad total: cada conexión «gasta» fichas y espera si se acaban
    struct Motor::Limitador
    {
        std::mutex m;
        double fichas = 0;
        steady_clock::time_point t = steady_clock::now();

        // Recarga las fichas con el límite que rige ahora (puede cambiar mientras se espera)
        void Recargar(int64_t limite)
        {
            auto ahora = steady_clock::now();
            double dt = duration<double>(ahora - t).count();
            t = ahora;
            fichas = std::min(fichas + dt * (double)limite, (double)limite);
        }

        void Consumir(int64_t n, const std::atomic<int64_t>& limite, const std::atomic<bool>& parar)
        {
            int64_t lim = limite;
            if (lim <= 0) return;
            {
                std::lock_guard g(m);
                Recargar(lim);
                fichas -= (double)n;
            }
            // Se espera en pasos cortos, mirando el límite de cada momento: si sube, la espera se acorta
            while (!parar)
            {
                lim = limite;
                if (lim <= 0) return;
                double espera;
                {
                    std::lock_guard g(m);
                    Recargar(lim);
                    if (fichas >= 0) return;
                    espera = -fichas / (double)lim;
                }
                Sleep((DWORD)(std::min(espera, 0.05) * 1000) + 1);
            }
        }
    };

    struct Motor::Descarga
    {
        DatosDescarga d;            // protegido por m
        std::mutex m;
        std::thread hilo;           // el que coordina las conexiones
        std::atomic<bool> enMarcha{ false };
        std::atomic<bool> termino{ false };
        std::atomic<bool> parar{ false };        // todas las conexiones deben cortar
        std::atomic<bool> pausaPedida{ false };  // la persona pulsó Pausar
        std::atomic<bool> sinRangos{ false };    // el servidor dejó de aceptar partes
        std::atomic<int> activas{ 0 };
        std::atomic<int64_t> bytesNuevos{ 0 };
        std::string errorTexto;     // protegido por m
        std::wstring rutaParcial;   // protegido por m
        Proxy proxy;
        double velocidad = 0;       // solo en Tick
        steady_clock::time_point ultimaMedida = steady_clock::now();
        bool avisada = false;

        void FijarError(const std::string& texto)
        {
            std::lock_guard g(m);
            if (errorTexto.empty()) errorTexto = texto;
            parar = true;
        }
    };

    namespace
    {
        // Lo que comparte una conexión con sus funciones de curl
        struct Conexion
        {
            Motor::Descarga* x = nullptr;
            CURL* c = nullptr;
            HANDLE archivo = INVALID_HANDLE_VALUE;
            size_t indice = 0;
            int64_t desde = 0;
            bool pidioRango = false;
            bool ftp = false;
            bool primera = true;
            bool rangoIgnorado = false;
            bool respondioPagina = false;   // llegó una página en vez del archivo: no se escribió nada
            bool errorDisco = false;
            int64_t recibidos = 0;
            Cabeceras cab;
            Motor* motor = nullptr;
            std::function<void(int64_t)> limitar;
        };

        size_t AlRecibirDatos(char* b, size_t t, size_t n, void* u)
        {
            auto* k = static_cast<Conexion*>(u);
            size_t total = t * n;
            if (k->x->parar) return 0;
            if (k->primera)
            {
                k->primera = false;
                long codigo = 0;
                curl_easy_getinfo(k->c, CURLINFO_RESPONSE_CODE, &codigo);
                // Cada respuesta buena se revisa antes de escribir su primer byte: una parte, un reintento o una
                // reanudación pueden recibir una página (por ejemplo, si el enlace venció) y eso no va al archivo. Las
                // de error (403, 503...) siguen su camino de siempre.
                if (!k->ftp && (codigo == 200 || codigo == 206))
                {
                    auto valor = [&](const char* clave) {
                        auto it = k->cab.v.find(clave);
                        return it == k->cab.v.end() ? std::string() : it->second;
                    };
                    std::wstring esperado;
                    {
                        std::lock_guard g(k->x->m);
                        esperado = k->x->d.nombre;
                    }
                    if (RespondioPagina(valor("content-type"), valor("content-disposition"), esperado))
                    {
                        k->respondioPagina = true;
                        return 0;   // curl corta aquí, sin escribir nada
                    }
                }
                if (!k->ftp && k->pidioRango)
                {
                    if (codigo != 206) { k->rangoIgnorado = (codigo == 200); return 0; }
                    auto it = k->cab.v.find("content-range");
                    if (it != k->cab.v.end())
                    {
                        // «bytes A-B/T»: A tiene que ser lo que pedimos
                        auto esp = it->second.find(' ');
                        int64_t a = _atoi64(it->second.c_str() + (esp == std::string::npos ? 0 : esp + 1));
                        if (a != k->desde) { k->rangoIgnorado = true; return 0; }
                    }
                }
                else if (!k->ftp && codigo != 200 && codigo != 206) return 0;
            }

            int64_t pos;
            int64_t escribir;
            {
                std::lock_guard g(k->x->m);
                Segmento& s = k->x->d.segmentos[k->indice];
                pos = s.inicio + s.hecho;
                escribir = std::min<int64_t>((int64_t)total, s.Falta());
            }
            if (escribir > 0)
            {
                OVERLAPPED ov = {};
                ov.Offset = (DWORD)(pos & 0xFFFFFFFF);
                ov.OffsetHigh = (DWORD)(pos >> 32);
                DWORD escritos = 0;
                if (!WriteFile(k->archivo, b, (DWORD)escribir, &escritos, &ov) || (int64_t)escritos != escribir)
                {
                    k->errorDisco = true;
                    return 0;
                }
                {
                    std::lock_guard g(k->x->m);
                    Segmento& s = k->x->d.segmentos[k->indice];
                    s.hecho += escritos;
                    if (s.fin >= 0) s.hecho = std::min(s.hecho, s.fin - s.inicio + 1);
                }
                k->recibidos += escritos;
                k->x->bytesNuevos += escritos;
                if (k->limitar) k->limitar(escritos);
            }
            // Si el trozo se acortó (otra conexión tomó su final) cortamos aquí: curl lo da como error de escritura
            return (size_t)escribir;
        }
    }

    Motor::Motor(std::wstring archivoEstado)
        : m_archivo(std::move(archivoEstado)), m_limitador(std::make_unique<Limitador>())
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    Motor::~Motor()
    {
        Detener();
        curl_global_cleanup();
    }

    std::shared_ptr<Motor::Descarga> Motor::Encontrar(uint64_t id)
    {
        for (auto& x : m_descargas)
        {
            if (x->d.id == id) return x;
        }
        return nullptr;
    }

    uint64_t Motor::Agregar(DatosDescarga d, bool empezar)
    {
        auto x = std::make_shared<Descarga>();
        std::lock_guard g(m_lista);
        d.id = m_siguienteId++;
        d.agregada = AhoraUnix();
        d.estado = empezar ? Estado::EnCola : Estado::Pausada;
        d.conexiones = std::clamp(d.conexiones, 1, 16);
        x->d = std::move(d);
        m_descargas.push_back(x);
        m_sucio = true;
        return x->d.id;
    }

    void Motor::Arrancar(const std::shared_ptr<Descarga>& x)
    {
        if (x->hilo.joinable()) x->hilo.join();
        x->parar = false;
        x->pausaPedida = false;
        x->sinRangos = false;
        x->termino = false;
        x->enMarcha = true;
        x->avisada = false;
        {
            std::lock_guard g(x->m);
            x->d.estado = Estado::Iniciando;
            x->d.error.clear();
            x->errorTexto.clear();
        }
        x->hilo = std::thread(&Motor::Ejecutar, this, x);
    }

    void Motor::Reanudar(uint64_t id)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x || x->enMarcha) return;
        std::lock_guard g2(x->m);
        if (x->d.estado == Estado::Pausada || x->d.estado == Estado::Error)
        {
            x->d.estado = Estado::EnCola;
            x->d.error.clear();
            m_sucio = true;
        }
    }

    void Motor::Pausar(uint64_t id)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x) return;
        if (x->enMarcha)
        {
            x->pausaPedida = true;
            x->parar = true;
        }
        else
        {
            std::lock_guard g2(x->m);
            if (x->d.estado == Estado::EnCola) x->d.estado = Estado::Pausada;
        }
        m_sucio = true;
    }

    void Motor::Quitar(uint64_t id)
    {
        std::lock_guard g(m_lista);
        auto it = std::find_if(m_descargas.begin(), m_descargas.end(), [&](auto& x) { return x->d.id == id; });
        if (it == m_descargas.end()) return;
        auto x = *it;
        m_descargas.erase(it);
        if (x->enMarcha)
        {
            x->pausaPedida = true;
            x->parar = true;
            m_retiradas.push_back(x);   // Tick espera a que cierre sus conexiones
        }
        m_sucio = true;
    }

    bool Motor::CambiarEnlace(uint64_t id, const std::string& url)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x || x->enMarcha) return false;
        std::lock_guard g2(x->m);
        if (x->d.estado == Estado::Completada) return false;
        x->d.url = url;
        x->d.cookies.clear();
        m_sucio = true;
        return true;
    }

    void Motor::PausarTodas()
    {
        std::vector<uint64_t> ids;
        {
            std::lock_guard g(m_lista);
            for (auto& x : m_descargas) ids.push_back(x->d.id);
        }
        for (auto id : ids) Pausar(id);
    }

    void Motor::ReanudarTodas()
    {
        std::vector<uint64_t> ids;
        {
            std::lock_guard g(m_lista);
            for (auto& x : m_descargas) ids.push_back(x->d.id);
        }
        for (auto id : ids) Reanudar(id);
    }

    void Motor::LimpiarTerminadas()
    {
        std::lock_guard g(m_lista);
        auto fin = std::remove_if(m_descargas.begin(), m_descargas.end(), [](auto& x) {
            std::lock_guard g2(x->m);
            return !x->enMarcha && x->d.estado == Estado::Completada;
        });
        if (fin != m_descargas.end()) m_sucio = true;
        m_descargas.erase(fin, m_descargas.end());
    }

    std::vector<Foto> Motor::Fotos()
    {
        std::vector<Foto> r;
        std::lock_guard g(m_lista);
        r.reserve(m_descargas.size());
        for (auto& x : m_descargas)
        {
            Foto f;
            {
                std::lock_guard g2(x->m);
                f.datos = x->d;
            }
            f.datos.cookies.clear();
            f.hecho = f.datos.Hecho();
            f.velocidad = x->velocidad;
            f.activas = x->activas;
            r.push_back(std::move(f));
        }
        return r;
    }

    bool Motor::Buscar(uint64_t id, Foto& f)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x) return false;
        {
            std::lock_guard g2(x->m);
            f.datos = x->d;
        }
        f.datos.cookies.clear();
        f.hecho = f.datos.Hecho();
        f.velocidad = x->velocidad;
        f.activas = x->activas;
        return true;
    }

    std::wstring Motor::RutaFinal(uint64_t id)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x) return {};
        std::lock_guard g2(x->m);
        return x->d.carpeta + L"\\" + x->d.nombre;
    }

    std::wstring Motor::RutaParcial(uint64_t id)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x) return {};
        std::lock_guard g2(x->m);
        if (x->d.nombre.empty()) return {};
        return x->d.carpeta + L"\\" + x->d.nombre + L".atarraya";
    }

    bool Motor::HayActivas()
    {
        std::lock_guard g(m_lista);
        for (auto& x : m_descargas)
        {
            if (x->enMarcha) return true;
            std::lock_guard g2(x->m);
            if (x->d.estado == Estado::EnCola) return true;
        }
        return false;
    }

    uint64_t Motor::BuscarPorUrl(const std::string& url)
    {
        std::lock_guard g(m_lista);
        for (auto& x : m_descargas)
        {
            std::lock_guard g2(x->m);
            if (x->d.url == url && x->d.estado != Estado::Completada) return x->d.id;
        }
        return 0;
    }

    void Motor::Recoger()
    {
        std::lock_guard g(m_lista);
        for (auto& x : m_descargas)
        {
            if (x->termino)
            {
                if (x->hilo.joinable()) x->hilo.join();
                x->termino = false;
                x->enMarcha = false;
                m_sucio = true;
            }
        }
    }

    void Motor::Tick()
    {
        std::vector<Foto> terminadas;
        {
            std::lock_guard g(m_lista);
            auto ahora = steady_clock::now();
            int enMarcha = 0;
            for (auto& x : m_descargas)
            {
                if (x->termino)
                {
                    if (x->hilo.joinable()) x->hilo.join();
                    x->termino = false;
                    x->enMarcha = false;
                    m_sucio = true;
                }
                // Velocidad: promedio suave de lo que llegó desde la última vez
                double dt = duration<double>(ahora - x->ultimaMedida).count();
                if (dt >= 0.25)
                {
                    double medida = (double)x->bytesNuevos.exchange(0) / dt;
                    x->velocidad = x->enMarcha ? (x->velocidad * 0.6 + medida * 0.4) : 0;
                    if (x->velocidad < 1) x->velocidad = 0;
                    x->ultimaMedida = ahora;
                }
                if (x->enMarcha) enMarcha++;

                std::lock_guard g2(x->m);
                if (!x->enMarcha && !x->avisada && (x->d.estado == Estado::Completada || x->d.estado == Estado::Error))
                {
                    x->avisada = true;
                    Foto f;
                    f.datos = x->d;
                    f.datos.cookies.clear();
                    f.hecho = f.datos.Hecho();
                    terminadas.push_back(std::move(f));
                }
            }
            // Arrancar las de la cola: las que permitió la ventana o, sin permisos, en orden hasta el máximo
            auto enCola = [](const std::shared_ptr<Descarga>& x) {
                std::lock_guard g2(x->m);
                return !x->enMarcha && x->d.estado == Estado::EnCola;
            };
            if (conPermisos)
            {
                for (uint64_t id : permitidas)
                {
                    auto x = Encontrar(id);
                    if (x && enCola(x)) Arrancar(x);
                }
                permitidas.clear();
            }
            else
            {
                for (auto& x : m_descargas)
                {
                    if (enMarcha >= maxSimultaneas) break;
                    if (enCola(x))
                    {
                        Arrancar(x);
                        enMarcha++;
                    }
                }
            }
            // Las quitadas que ya cerraron sus conexiones
            for (auto it = m_retiradas.begin(); it != m_retiradas.end();)
            {
                if ((*it)->termino)
                {
                    if ((*it)->hilo.joinable()) (*it)->hilo.join();
                    it = m_retiradas.erase(it);
                }
                else ++it;
            }
        }

        if (alTerminar)
        {
            for (auto& f : terminadas) alTerminar(f);
        }

        int64_t ahoraSeg = AhoraUnix();
        if (m_sucio || (HayActivas() && ahoraSeg - m_ultimoGuardado >= 5))
        {
            if (ahoraSeg - m_ultimoGuardado >= 2 || terminadas.size() > 0) Guardar();
        }
    }

    void Motor::Detener()
    {
        std::vector<std::shared_ptr<Descarga>> todas;
        {
            std::lock_guard g(m_lista);
            todas = m_descargas;
            todas.insert(todas.end(), m_retiradas.begin(), m_retiradas.end());
            for (auto& x : todas)
            {
                if (x->enMarcha)
                {
                    x->parar = true;
                    // Al volver a abrir el programa siguen solas: no las marcamos como pausadas por la persona
                }
            }
        }
        for (auto& x : todas)
        {
            if (x->hilo.joinable()) x->hilo.join();
            x->enMarcha = false;
            x->termino = false;
        }
        {
            std::lock_guard g(m_lista);
            m_retiradas.clear();
        }
        Guardar();
    }

    void Motor::Guardar()
    {
        json lista = json::array();
        uint64_t siguiente;
        {
            std::lock_guard g(m_lista);
            siguiente = m_siguienteId;
            for (auto& x : m_descargas)
            {
                std::lock_guard g2(x->m);
                const auto& d = x->d;
                json segs = json::array();
                for (auto& s : d.segmentos) segs.push_back({ s.inicio, s.fin, s.hecho });
                Estado e = d.estado;
                // Si estaba bajando, al abrir de nuevo sigue (salvo que la persona la haya pausado)
                if (x->enMarcha && !x->pausaPedida) e = Estado::EnCola;
                if (x->enMarcha && x->pausaPedida) e = Estado::Pausada;
                lista.push_back({
                    { "id", d.id }, { "url", d.url }, { "referer", d.referer },
                    { "cookies", d.estado == Estado::Completada ? "" : d.cookies },
                    { "agente", d.agente }, { "carpeta", Utf8(d.carpeta) }, { "nombre", Utf8(d.nombre) },
                    { "nombreFijo", d.nombreFijo }, { "tamano", d.tamano }, { "rangos", d.rangos },
                    { "etag", d.etag }, { "modificado", d.modificado }, { "tipo", d.tipoMime },
                    { "estado", NombreEstado(e) }, { "error", d.error }, { "conexiones", d.conexiones },
                    { "agregada", d.agregada }, { "terminada", d.terminada }, { "segmentos", segs },
                    { "paquete", Utf8(d.paquete) }, { "grupo", d.grupo },
                });
            }
        }
        json todo = { { "version", 1 }, { "siguienteId", siguiente }, { "descargas", lista } };
        if (EscribirSeguro(m_archivo, todo.dump(1, ' ', false, json::error_handler_t::replace)))
        {
            m_sucio = false;
            m_ultimoGuardado = AhoraUnix();
        }
    }

    void Motor::Cargar()
    {
        std::string texto;
        if (!LeerArchivo(m_archivo, texto)) return;
        try
        {
            json todo = json::parse(texto);
            std::lock_guard g(m_lista);
            m_siguienteId = todo.value("siguienteId", (uint64_t)1);
            for (auto& j : todo.at("descargas"))
            {
                auto x = std::make_shared<Descarga>();
                auto& d = x->d;
                d.id = j.value("id", (uint64_t)0);
                d.url = j.value("url", "");
                d.referer = j.value("referer", "");
                d.cookies = j.value("cookies", "");
                d.agente = j.value("agente", "");
                d.carpeta = Ancho(j.value("carpeta", ""));
                d.nombre = Ancho(j.value("nombre", ""));
                d.nombreFijo = j.value("nombreFijo", false);
                d.tamano = j.value("tamano", (int64_t)-1);
                d.rangos = j.value("rangos", false);
                d.etag = j.value("etag", "");
                d.modificado = j.value("modificado", "");
                d.tipoMime = j.value("tipo", "");
                d.estado = EstadoDeNombre(j.value("estado", "pausada"));
                d.error = j.value("error", "");
                d.conexiones = std::clamp(j.value("conexiones", 8), 1, 16);
                d.agregada = j.value("agregada", (int64_t)0);
                d.terminada = j.value("terminada", (int64_t)0);
                d.paquete = Ancho(j.value("paquete", ""));
                d.grupo = j.value("grupo", "");
                for (auto& s : j.value("segmentos", json::array()))
                {
                    if (s.is_array() && s.size() == 3)
                        d.segmentos.push_back({ s[0].get<int64_t>(), s[1].get<int64_t>(), s[2].get<int64_t>(), false });
                }
                if (d.id == 0 || d.url.empty()) continue;
                x->avisada = true;   // no volver a avisar de las ya terminadas
                m_siguienteId = std::max(m_siguienteId, d.id + 1);
                m_descargas.push_back(x);
            }
        }
        catch (...)
        {
            // Archivo dañado: se guarda una copia y se empieza con la lista vacía
            CopyFileW(m_archivo.c_str(), (m_archivo + L".danado").c_str(), FALSE);
        }
    }

    void Motor::Ejecutar(std::shared_ptr<Descarga> x)
    {
        auto terminar = [&](Estado e, const std::string& error) {
            std::lock_guard g(x->m);
            if (e == Estado::Error) Registrar("error con " + SoloSitio(x->d.url) + ": " + error);
            if (e == Estado::Completada) Registrar("terminada: " + SoloSitio(x->d.url));
            x->d.estado = e;
            x->d.error = error;
            if (e == Estado::Completada)
            {
                x->d.terminada = AhoraUnix();
                x->d.cookies.clear();   // ya no hacen falta: no se guardan
            }
            m_sucio = true;
            x->termino = true;
        };

        Peticion p;
        {
            std::lock_guard g(x->m);
            p = { x->d.url, x->d.referer, x->d.cookies, x->d.agente };
        }
        x->proxy = ProxyDelSistema();
        bool ftp = EsFtp(p.url);

        // 1. Preguntar al servidor (con reintentos si la conexión falla)
        Sondeo s;
        std::string error;
        for (int intento = 0;; intento++)
        {
            s = Sondear(p, x->proxy, x->parar);
            if (x->parar) break;
            if (s.noPermitido)
            {
                error = Utf8(enlaces::TextoNoPermitido);
                break;
            }
            // Algunos servidores rechazan cualquier pedido por partes: entonces se pide entero
            if (!ftp && (s.codigo == 400 || s.codigo == 405 || s.codigo == 501 || (s.codigo == 416 && s.tamano < 0)))
            {
                Sondeo entero = Sondear(p, x->proxy, x->parar, false);
                if (entero.codigo == 200) s = entero;
            }
            if (s.codigo == 200 || s.codigo == 206 || s.codigo == 416) break;
            bool temporal = s.codigo == 0 ? !EsErrorPermanente(s.res)
                                          : (s.codigo == 408 || s.codigo == 429 || s.codigo >= 500);
            if (!temporal || intento >= 4)
            {
                error = s.codigo ? TextoErrorHttp(s.codigo) : TextoErrorCurl(s.res, s.errorCurl);
                break;
            }
            Esperar(2.0 * (1 << intento), x->parar);
        }
        if (!error.empty()) { terminar(Estado::Error, error); return; }
        if (x->parar) { terminar(x->pausaPedida ? Estado::Pausada : Estado::EnCola, ""); return; }

        // 1b. Se pidió un archivo y el servidor respondió con una página (por ejemplo, la de un sitio de descargas que
        //     pide un captcha): no se guarda como si fuera el archivo ni se da por terminada
        if (!ftp)
        {
            std::wstring esperado;
            {
                std::lock_guard g(x->m);
                esperado = !x->d.nombre.empty() ? x->d.nombre : NombreDeUrl(x->d.url);
            }
            if (RespondioPagina(s.tipo, s.disposicion, esperado))
            {
                terminar(Estado::Error, TextoRespondioPagina);
                return;
            }
        }

        // 2. Nombres ya usados por otras descargas (para no chocar)
        std::set<std::wstring> ocupados;
        {
            std::lock_guard g(m_lista);
            for (auto& otra : m_descargas)
            {
                if (otra == x) continue;
                std::lock_guard g2(otra->m);
                if (otra->d.estado != Estado::Completada && !otra->d.nombre.empty()) ocupados.insert(Minusculas(otra->d.nombre));
            }
        }

        // 3. Decidir nombre, tamaño y partes
        std::wstring parcial, carpeta;
        int64_t tamano;
        bool rangos;
        int conexiones;
        {
            std::lock_guard g(x->m);
            auto& d = x->d;
            if (d.Hecho() > 0)
            {
                bool cambio = (d.tamano >= 0 && s.tamano >= 0 && d.tamano != s.tamano)
                    || (!d.etag.empty() && !s.etag.empty() && d.etag != s.etag) || !s.rangos;
                if (cambio) d.segmentos.clear();   // el archivo cambió o no se puede seguir: desde cero
            }
            d.tamano = s.tamano;
            d.rangos = s.rangos;
            d.etag = s.etag;
            d.modificado = s.modificado;
            d.tipoMime = s.tipo;
            if (d.carpeta.empty()) d.carpeta = CarpetaDescargas();
            if (d.nombre.empty() || (!d.nombreFijo && d.segmentos.empty()))
            {
                std::wstring n = d.nombre;
                if (!d.nombreFijo)
                {
                    if (!s.nombre.empty()) n = s.nombre;
                    else if (n.empty()) n = NombreDeUrl(s.urlFinal.empty() ? d.url : s.urlFinal);
                    if (n.empty()) n = NombreDeUrl(d.url);
                }
                n = AgregarExtension(LimpiarNombre(n), s.tipo);
                d.nombre = NombreUnico(d.carpeta, n, ocupados);
            }
            if (d.segmentos.empty())
            {
                if (d.rangos && d.tamano > 0)
                {
                    int64_t n = std::clamp(d.conexiones, 1, 16);
                    int64_t porParte = std::max(TamanoMinimoParte, (d.tamano + n - 1) / n);
                    for (int64_t ini = 0; ini < d.tamano; ini += porParte)
                        d.segmentos.push_back({ ini, std::min(ini + porParte, d.tamano) - 1, 0, false });
                }
                else if (d.tamano != 0)
                {
                    d.segmentos.push_back({ 0, d.tamano > 0 ? d.tamano - 1 : -1, 0, false });
                }
            }
            for (auto& sg : d.segmentos) sg.activo = false;
            d.estado = Estado::Descargando;
            carpeta = d.carpeta;
            parcial = d.carpeta + L"\\" + d.nombre + L".atarraya";
            x->rutaParcial = parcial;
            tamano = d.tamano;
            rangos = d.rangos;
            conexiones = d.conexiones;
            m_sucio = true;
        }

        // 4. Preparar el archivo parcial
        SHCreateDirectoryExW(nullptr, carpeta.c_str(), nullptr);
        bool nuevo = !ExisteArchivo(parcial);
        if (nuevo)
        {
            std::lock_guard g(x->m);
            for (auto& sg : x->d.segmentos) sg.hecho = 0;   // si alguien borró el parcial, desde cero
        }
        HANDLE h = CreateFileW(parcial.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            terminar(Estado::Error, "No se pudo crear el archivo en la carpeta elegida. ¿Tienes permiso para guardar ahí?");
            return;
        }
        if (nuevo)
        {
            // Archivo «disperso»: escribir lejos del principio no obliga a llenar de ceros lo anterior
            FILE_SET_SPARSE_BUFFER disperso = { TRUE };
            DWORD r = 0;
            DeviceIoControl(h, FSCTL_SET_SPARSE, &disperso, sizeof(disperso), nullptr, 0, &r, nullptr);
        }
        LARGE_INTEGER largo = {};
        if (rangos && tamano > 0) largo.QuadPart = tamano;
        bool sinReanudar = !rangos;
        if (sinReanudar || (rangos && tamano > 0))
        {
            LARGE_INTEGER actual = {};
            GetFileSizeEx(h, &actual);
            if (actual.QuadPart != largo.QuadPart)
            {
                SetFilePointerEx(h, largo, nullptr, FILE_BEGIN);
                SetEndOfFile(h);
            }
        }
        CloseHandle(h);
        if (sinReanudar)
        {
            std::lock_guard g(x->m);
            for (auto& sg : x->d.segmentos) sg.hecho = 0;
        }

        int64_t faltaBajar;
        {
            std::lock_guard g(x->m);
            faltaBajar = tamano > 0 ? tamano - x->d.Hecho() : 0;
        }
        int64_t libre = EspacioLibre(carpeta);
        if (faltaBajar > 0 && libre >= 0 && faltaBajar > libre)
        {
            terminar(Estado::Error, "No hay espacio suficiente en el disco: faltan " + Utf8(TextoTamano(faltaBajar - libre)) + ".");
            return;
        }

        // 5. Las conexiones
        auto lanzar = [&](int cuantas) {
            std::vector<std::thread> hilos;
            for (int i = 0; i < cuantas; i++) hilos.emplace_back(&Motor::Trabajar, this, x);
            for (auto& t : hilos) t.join();
        };
        if (tamano != 0) lanzar((rangos && tamano > 0) ? conexiones : 1);

        // El servidor dejó de aceptar partes a mitad de camino: una sola conexión desde el principio
        if (x->sinRangos && !x->pausaPedida)
        {
            {
                std::lock_guard g(x->m);
                x->d.rangos = false;
                x->d.segmentos.clear();
                x->d.segmentos.push_back({ 0, tamano > 0 ? tamano - 1 : -1, 0, false });
                x->errorTexto.clear();
            }
            x->sinRangos = false;
            x->parar = false;
            lanzar(1);
        }

        // 6. Resultado
        std::string errorTexto;
        bool completo;
        {
            std::lock_guard g(x->m);
            errorTexto = x->errorTexto;
            completo = true;
            for (auto& sg : x->d.segmentos)
            {
                if (sg.Falta() > 0) completo = false;
            }
            if (tamano > 0 && x->d.Hecho() != tamano) completo = false;
        }
        if (!errorTexto.empty()) { terminar(Estado::Error, errorTexto); return; }
        if (!completo)
        {
            if (x->pausaPedida || x->parar) terminar(x->pausaPedida ? Estado::Pausada : Estado::EnCola, "");
            else terminar(Estado::Error, "La descarga se detuvo sin terminar. Pulsa «Reanudar» para seguir.");
            return;
        }

        // Terminada: quitar la marca de disperso, ponerle su nombre final y avisar
        HANDLE hf = CreateFileW(parcial.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf != INVALID_HANDLE_VALUE)
        {
            FILE_SET_SPARSE_BUFFER normal = { FALSE };
            DWORD r = 0;
            DeviceIoControl(hf, FSCTL_SET_SPARSE, &normal, sizeof(normal), nullptr, 0, &r, nullptr);
            FlushFileBuffers(hf);
            CloseHandle(hf);
        }
        std::wstring nombreFinal;
        {
            std::lock_guard g(x->m);
            nombreFinal = x->d.nombre;
        }
        std::wstring destino = carpeta + L"\\" + nombreFinal;
        if (ExisteArchivo(destino) || ExisteCarpeta(destino))
        {
            nombreFinal = NombreUnico(carpeta, nombreFinal, ocupados);
            destino = carpeta + L"\\" + nombreFinal;
        }
        if (!MoveFileExW(parcial.c_str(), destino.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            terminar(Estado::Error, "Se bajó todo, pero no se pudo poner el nombre final al archivo.");
            return;
        }
        {
            std::lock_guard g(x->m);
            x->d.nombre = nombreFinal;
            if (x->d.tamano < 0) x->d.tamano = x->d.Hecho();
        }
        terminar(Estado::Completada, "");
    }

    void Motor::Trabajar(std::shared_ptr<Descarga> x)
    {
        Peticion p;
        std::wstring parcial;
        {
            std::lock_guard g(x->m);
            p = { x->d.url, x->d.referer, x->d.cookies, x->d.agente };
            parcial = x->rutaParcial;
        }
        bool ftp = EsFtp(p.url);

        CURL* c = curl_easy_init();
        HANDLE archivo = CreateFileW(parcial.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (!c || archivo == INVALID_HANDLE_VALUE)
        {
            x->FijarError("No se pudo abrir el archivo para escribir.");
            if (c) curl_easy_cleanup(c);
            if (archivo != INVALID_HANDLE_VALUE) CloseHandle(archivo);
            return;
        }
        Preparar(c, p, x->proxy);
        char errorCurl[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errorCurl);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, AlProgresar);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &x->parar);

        // If-Range: si el archivo cambió en el servidor, responde entero (200) y se empieza de nuevo, en vez de
        // mezclar partes de dos versiones. Las ETag débiles (W/) no sirven para esto.
        std::string validador;
        {
            std::lock_guard g(x->m);
            if (!x->d.etag.empty() && x->d.etag.rfind("W/", 0) != 0) validador = x->d.etag;
            else validador = x->d.modificado;
        }
        curl_slist* cabeceras = (!ftp && !validador.empty()) ? curl_slist_append(nullptr, ("If-Range: " + validador).c_str()) : nullptr;

        x->activas++;
        bool yaDescontada = false;
        int intentos = 0;
        while (!x->parar)
        {
            // 1. Tomar un trozo libre; si no hay, partir en dos el que más falta
            size_t indice = SIZE_MAX;
            int64_t desde = 0, hasta = -1;
            bool rangos;
            {
                std::lock_guard g(x->m);
                auto& v = x->d.segmentos;
                rangos = x->d.rangos;
                for (size_t i = 0; i < v.size(); i++)
                {
                    if (!v[i].activo && v[i].Falta() > 0) { indice = i; break; }
                }
                if (indice == SIZE_MAX && rangos && x->d.tamano > 0)
                {
                    size_t mayor = SIZE_MAX;
                    int64_t maxFalta = MinimoParaPartir - 1;
                    for (size_t i = 0; i < v.size(); i++)
                    {
                        if (v[i].activo && v[i].fin >= 0 && v[i].Falta() > maxFalta) { mayor = i; maxFalta = v[i].Falta(); }
                    }
                    if (mayor != SIZE_MAX)
                    {
                        Segmento& s = v[mayor];
                        int64_t mitad = s.inicio + s.hecho + maxFalta / 2;
                        Segmento nuevo{ mitad, s.fin, 0, false };
                        s.fin = mitad - 1;
                        v.push_back(nuevo);
                        indice = v.size() - 1;
                    }
                }
                if (indice == SIZE_MAX) break;
                v[indice].activo = true;
                desde = v[indice].inicio + v[indice].hecho;
                hasta = v[indice].fin;
            }

            // 2. Bajarlo
            Conexion k;
            k.x = x.get();
            k.c = c;
            k.archivo = archivo;
            k.indice = indice;
            k.desde = desde;
            k.ftp = ftp;
            k.pidioRango = rangos || desde > 0;
            k.limitar = [this, x](int64_t n) { m_limitador->Consumir(n, limiteBytesSeg, x->parar); };
            std::string rango = std::to_string(desde) + "-" + (hasta >= 0 ? std::to_string(hasta) : std::string());
            curl_easy_setopt(c, CURLOPT_RANGE, k.pidioRango ? rango.c_str() : nullptr);
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, k.pidioRango ? cabeceras : nullptr);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, AlRecibirDatos);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
            curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, AlRecibirCabecera);
            curl_easy_setopt(c, CURLOPT_HEADERDATA, &k.cab);
            errorCurl[0] = 0;
            CURLcode res = curl_easy_perform(c);
            long codigo = 0;
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &codigo);

            // 3. Ver qué pasó
            bool listo;
            {
                std::lock_guard g(x->m);
                Segmento& s = x->d.segmentos[indice];
                if (s.fin < 0 && res == CURLE_OK && !k.errorDisco && (ftp || (codigo >= 200 && codigo < 300)))
                {
                    s.fin = s.inicio + s.hecho - 1;   // tamaño desconocido: terminó cuando el servidor cerró bien
                }
                listo = s.Falta() <= 0;
                s.activo = false;
            }
            if (k.recibidos > 0) intentos = 0;
            if (listo) continue;
            if (k.respondioPagina) { x->FijarError(TextoRespondioPagina); break; }   // como en el sondeo: no se reintenta
            if (x->parar) break;
            if (k.errorDisco) { x->FijarError("No se pudo escribir en el disco. ¿Está lleno o se desconectó?"); break; }
            if (k.cab.sitioNoPermitido) { x->FijarError(Utf8(enlaces::TextoNoPermitido)); break; }
            if (k.rangoIgnorado) { x->sinRangos = true; x->parar = true; break; }

            std::string ultimoError;
            if (!ftp && codigo >= 400)
            {
                bool temporal = codigo == 408 || codigo == 429 || codigo >= 500;
                if (!temporal) { x->FijarError(TextoErrorHttp(codigo)); break; }
                // El servidor no quiere tantas conexiones: dejamos este trozo a otra conexión
                int a = x->activas.load();
                while (a > 1 && !x->activas.compare_exchange_weak(a, a - 1)) {}
                if (a > 1) { yaDescontada = true; break; }
                ultimoError = TextoErrorHttp(codigo);
            }
            else if (res != CURLE_OK && EsErrorPermanente(res))
            {
                x->FijarError(TextoErrorCurl(res, errorCurl[0] ? errorCurl : curl_easy_strerror(res)));
                break;
            }
            else
            {
                ultimoError = res == CURLE_OK ? std::string("El servidor cortó la conexión antes de tiempo.")
                                              : TextoErrorCurl(res, errorCurl[0] ? errorCurl : curl_easy_strerror(res));
            }

            if (++intentos > MaxReintentos) { x->FijarError(ultimoError + " Se intentó varias veces."); break; }
            Esperar(std::min(30.0, 1.5 * (1 << std::min(intentos, 5))), x->parar);
        }
        if (!yaDescontada) x->activas--;
        CloseHandle(archivo);
        curl_easy_cleanup(c);
        if (cabeceras) curl_slist_free_all(cabeceras);
    }
}
