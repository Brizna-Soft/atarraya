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
#include <cstring>
#include <map>
#include <set>

#include "enlaces.hpp"
#include "flujos.hpp"
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
        // Ningún proxy, ni el de Windows ni el de las variables de entorno. Así van los videos de las páginas (sus listas,
        // todas sus partes y sus redirecciones) y las consultas del recolector: el proxy volvería a buscar el nombre y
        // podría llegar a la red de la casa sin que nadie lo revise; directo, el filtro del socket ve el destino real.
        bool ninguno = false;
    };

    Proxy SinProxy()
    {
        Proxy p;
        p.ninguno = true;
        return p;
    }

#ifdef ATARRAYA_PRUEBAS
    // Solo en las pruebas (ver hnd::pruebas): se fijan antes de bajar y no cambian mientras se baja
    curl_slist* g_resolverPrueba = nullptr;
    std::map<std::string, std::string> g_dnsPrueba;   // nombre -> IP: lo mismo, para EsDeLaCasa
    std::atomic<size_t> g_memoriaPrueba{ 0 };
    bool g_proxyFijo = false;
    Proxy g_proxyPrueba;
#endif

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

    // Proxy configurado en Windows (si hay uno fijo). Los scripts PAC no se usan. Lo usan las descargas normales.
    Proxy ProxyDelSistema()
    {
#ifdef ATARRAYA_PRUEBAS
        if (g_proxyFijo) return g_proxyPrueba;
#endif
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
#ifdef ATARRAYA_PRUEBAS
        if (g_resolverPrueba) curl_easy_setopt(c, CURLOPT_RESOLVE, g_resolverPrueba);
#endif
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
        if (proxy.ninguno) curl_easy_setopt(c, CURLOPT_PROXY, "");   // ni el de Windows ni el del entorno
        else if (!proxy.servidor.empty())
        {
            curl_easy_setopt(c, CURLOPT_PROXY, proxy.servidor.c_str());
            if (!proxy.excepciones.empty()) curl_easy_setopt(c, CURLOPT_NOPROXY, proxy.excepciones.c_str());
        }
    }

    hnd::red::Destino Revisar(const sockaddr* a)
    {
        try
        {
            return hnd::red::RevisarDestino(a);
        }
        catch (...)
        {
            return hnd::red::Destino::SinRevisar;
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

    // Videos de las páginas: a dónde se conectó cada pedido (van siempre directo, sin proxy: aquí se ve el destino
    // real). Si la lista vino de internet, ni ella ni sus partes pueden pedir nada de esta PC ni de su red (una lista no
    // elige a qué aparatos de la casa se conecta Atarraya), tampoco por una redirección.
    struct DestinoVisto
    {
        bool soloAfuera = false;   // no deja conectar con esta PC ni con su red
        bool local = false;        // se conectó (o quiso) con esta PC o su red
        bool sinRedes = false;     // no se supo cuáles son las redes de la PC
    };

    curl_socket_t AbrirMirando(void* u, curlsocktype tipo, curl_sockaddr* a)
    {
        auto* v = static_cast<DestinoVisto*>(u);
        if (tipo == CURLSOCKTYPE_IPCXN)
        {
            auto destino = Revisar(&a->addr);
            if (destino == hnd::red::Destino::Local) v->local = true;
            if (destino == hnd::red::Destino::SinRevisar) v->sinRedes = true;
            if (v->soloAfuera && destino != hnd::red::Destino::Afuera) return CURL_SOCKET_BAD;
        }
        return socket(a->family, a->socktype, a->protocol);
    }

    // Mira cada conexión (con «soloAfuera», ninguna va a esta PC ni a su red). Sin «soloAfuera» solo hace falta con
    // «siempre» (para saber si una lista era de la casa).
    void MirarDestino(CURL* c, DestinoVisto& visto, bool siempre = false)
    {
        if (!visto.soloAfuera && !siempre)
        {
            curl_easy_setopt(c, CURLOPT_OPENSOCKETFUNCTION, nullptr);
            return;
        }
        curl_easy_setopt(c, CURLOPT_OPENSOCKETFUNCTION, AbrirMirando);
        curl_easy_setopt(c, CURLOPT_OPENSOCKETDATA, &visto);
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
        bool redLocal = false;      // con «soloAfuera»: quiso conectar con esta PC o su red (no se dejó)
    };

    // Pide el primer byte para saber el tamaño, el nombre y si se puede partir/reanudar.
    // Sin «conRango» pide el archivo entero y corta apenas llegan las cabeceras. Con «soloAfuera» no se conecta con
    // esta PC ni con su red.
    Sondeo Sondear(const Peticion& p, const Proxy& proxy, std::atomic<bool>& parar, bool conRango = true, bool soloAfuera = false)
    {
        Sondeo s;
        CURL* c = curl_easy_init();
        if (!c) { s.res = CURLE_FAILED_INIT; return s; }
        Preparar(c, p, proxy);
        DestinoVisto visto;
        visto.soloAfuera = soloAfuera;
        MirarDestino(c, visto);
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
        s.redLocal = soloAfuera && (visto.local || visto.sinRedes) && s.res != CURLE_OK;

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

    bool EsPaginaWeb(const std::string& tipo)
    {
        std::string t = tipo;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return t.find("text/html") != std::string::npos || t.find("application/xhtml") != std::string::npos;
    }

    // Baja la lista de un video (HLS o DASH) a memoria. «temporal»: la falla puede pasar sola (vale la pena reintentar).
    bool BajarListaInterna(const Peticion& p, const Proxy& proxy, bool soloAfuera, std::string& texto, std::string& urlFinal,
                           bool& local, bool& temporal, std::string& error, const std::atomic<bool>& parar)
    {
        texto.clear();
        urlFinal.clear();
        error.clear();
        local = temporal = false;
        std::string bajo = p.url.substr(0, 8);
        std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (bajo.rfind("http://", 0) != 0 && bajo.rfind("https://", 0) != 0)
        {
            error = "La lista del video tiene que ser de una dirección http o https.";
            return false;
        }
        if (hnd::enlaces::NoPermitido(p.url))
        {
            error = hnd::Utf8(hnd::enlaces::TextoNoPermitido);
            return false;
        }
        // Una lista que salió de una lista de internet no lleva a esta PC ni a su red (por el nombre, antes de conectar;
        // lo que un nombre de afuera esconde lo frena AbrirMirando)
        if (soloAfuera && hnd::enlaces::DireccionLocal(p.url))
        {
            error = hnd::flujos::TextoRedLocal;
            return false;
        }
        CURL* c = curl_easy_init();
        if (!c)
        {
            error = "No se pudo preparar la conexión.";
            return false;
        }
        Preparar(c, p, proxy);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
        Memoria m{ &texto, hnd::flujos::MaximoLista, false };
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, AlRecibirMemoria);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
        Cabeceras cab;
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, AlRecibirCabecera);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &cab);
        DestinoVisto visto;
        visto.soloAfuera = soloAfuera;
        MirarDestino(c, visto, true);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, AlProgresar);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(&parar));
        char detalle[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detalle);
        CURLcode res = curl_easy_perform(c);
        long codigo = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &codigo);
        char* efectiva = nullptr;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &efectiva);
        if (efectiva) urlFinal = efectiva;
        curl_easy_cleanup(c);
        local = visto.local;
        auto it = cab.v.find("content-type");
        std::string tipo = it == cab.v.end() ? std::string() : it->second;
        if (cab.sitioNoPermitido) error = hnd::Utf8(hnd::enlaces::TextoNoPermitido);
        else if (soloAfuera && (visto.local || visto.sinRedes) && res != CURLE_OK) error = hnd::flujos::TextoRedLocal;
        else if (m.excedido) error = hnd::flujos::TextoDemasiadas;
        else if (res == CURLE_ABORTED_BY_CALLBACK) error = "Se canceló.";
        else if (res != CURLE_OK)
        {
            error = TextoErrorCurl(res, detalle[0] ? detalle : curl_easy_strerror(res));
            temporal = !EsErrorPermanente(res);
        }
        else if (codigo >= 400)
        {
            error = TextoErrorHttp(codigo);
            temporal = codigo == 408 || codigo == 429 || codigo >= 500;
        }
        // Una página web en vez de la lista (por ejemplo, la de un sitio que pide iniciar sesión)
        else if (EsPaginaWeb(tipo) && texto.find("#EXTM3U") == std::string::npos && texto.find("<MPD") == std::string::npos)
            error = hnd::TextoRespondioPagina;
        else if (!urlFinal.empty() && hnd::enlaces::NoPermitido(urlFinal)) error = hnd::Utf8(hnd::enlaces::TextoNoPermitido);
        if (!error.empty())
        {
            texto.clear();
            return false;
        }
        return true;
    }
}

namespace hnd
{
    bool EsDeLaCasa(const std::string& url)
    {
        std::string bajo = url.substr(0, 8);
        std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (bajo.rfind("http://", 0) != 0 && bajo.rfind("https://", 0) != 0) return false;
        if (enlaces::DireccionLocal(url)) return true;
        std::string host = enlaces::Sitio(url);
        if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
        if (host.empty()) return false;
#ifdef ATARRAYA_PRUEBAS
        if (auto it = g_dnsPrueba.find(host); it != g_dnsPrueba.end()) host = it->second;
#endif
        // Un nombre de afuera que lleva a la casa: se busca como lo buscaría la PC (el navegador acaba de buscarlo, así
        // que suele ser al instante). Si no se puede buscar, no se sabe: lo decide el filtro al conectar.
        addrinfo pista{};
        pista.ai_family = AF_UNSPEC;
        pista.ai_socktype = SOCK_STREAM;
        addrinfo* r = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &pista, &r) != 0 || !r) return false;
        bool casa = false;
        for (auto* a = r; a && !casa; a = a->ai_next) casa = Revisar(a->ai_addr) != red::Destino::Afuera;
        freeaddrinfo(r);
        return casa;
    }

    std::string ProxyEnUso()
    {
        // Solo «host:puerto»: nunca un usuario o una clave que vengan en la dirección del proxy
        std::string s = ProxyDelSistema().servidor;
        auto esquema = s.find("://");
        if (esquema != std::string::npos) s = s.substr(esquema + 3);
        auto arroba = s.rfind('@');
        if (arroba != std::string::npos) s = s.substr(arroba + 1);
        return s;
    }

#ifdef ATARRAYA_PRUEBAS
    void pruebas::Resolver(const std::string& reglas)
    {
        if (g_resolverPrueba) curl_slist_free_all(g_resolverPrueba);
        g_resolverPrueba = nullptr;
        g_dnsPrueba.clear();
        size_t desde = 0;
        while (desde < reglas.size())
        {
            auto fin = reglas.find(',', desde);
            std::string regla = reglas.substr(desde, fin == std::string::npos ? std::string::npos : fin - desde);
            if (!regla.empty()) g_resolverPrueba = curl_slist_append(g_resolverPrueba, regla.c_str());
            // «nombre:puerto:ip»
            auto dp1 = regla.find(':');
            auto dp2 = dp1 == std::string::npos ? std::string::npos : regla.find(':', dp1 + 1);
            if (dp2 != std::string::npos) g_dnsPrueba[regla.substr(0, dp1)] = regla.substr(dp2 + 1);
            if (fin == std::string::npos) break;
            desde = fin + 1;
        }
    }

    void pruebas::ProxyFijo(const std::string& servidor, const std::string& excepciones)
    {
        g_proxyPrueba = Proxy{ servidor, excepciones };
        g_proxyFijo = !servidor.empty();
    }

    void pruebas::TopeDeMemoria(size_t bytes)
    {
        g_memoriaPrueba = bytes;
    }
#endif

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
        Preparar(c, p, SinProxy());
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

    bool BajarLista(const std::string& url, const std::string& referer, const std::string& cookies, const std::string& agente,
                    std::string& texto, std::string& urlFinal, bool& local, std::string& error, const std::atomic<bool>* parar,
                    bool soloAfuera)
    {
        std::atomic<bool> nunca{ false };
        bool temporal = false;
        Peticion p{ url, referer, cookies, agente };
        // La primera lista la pidió el navegador y la eligió la persona (pulsó el botón): puede ser de su red, y entonces
        // la ventana le pregunta antes de bajar nada. Las listas que salen de una lista de internet, no. Nunca por proxy.
        return BajarListaInterna(p, SinProxy(), soloAfuera, texto, urlFinal, local, temporal, error, parar ? *parar : nunca);
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

        // Una parte de un video por partes: lo que llega va al Ensamblador, que lo junta en orden
        struct ParteFlujo
        {
            Motor::Descarga* x = nullptr;
            CURL* c = nullptr;
            size_t indice = 0;
            flujos::Ensamblador* ens = nullptr;
            std::mutex* mEns = nullptr;
            bool pidioRango = false;
            bool primera = true;
            bool pagina = false;          // llegó una página web: no se escribió nada
            bool rangoIgnorado = false;   // se pidió un trozo y el servidor mandó otra cosa
            bool errorDisco = false;
            bool lleno = false;           // adelantada y sin lugar en memoria: se baja otra vez cuando sea su turno
            int64_t recibidos = 0;
            Cabeceras cab;
            std::function<void(int64_t)> limitar;
        };

        size_t AlRecibirParteFlujo(char* b, size_t t, size_t n, void* u)
        {
            auto* k = static_cast<ParteFlujo*>(u);
            size_t total = t * n;
            if (k->x->parar) return 0;
            if (k->primera)
            {
                k->primera = false;
                long codigo = 0;
                curl_easy_getinfo(k->c, CURLINFO_RESPONSE_CODE, &codigo);
                if (codigo < 200 || codigo >= 300) return 0;   // los errores los ve quien llama
                if (k->pidioRango && codigo != 206)
                {
                    k->rangoIgnorado = true;
                    return 0;
                }
                auto it = k->cab.v.find("content-type");
                if (it != k->cab.v.end() && EsPaginaWeb(it->second))
                {
                    k->pagina = true;   // una parte de un video nunca es una página web
                    return 0;
                }
            }
            {
                std::lock_guard g(*k->mEns);
                auto paso = k->ens->Pedazo(k->indice, b, total);
                if (paso == flujos::Ensamblador::Paso::Disco)
                {
                    k->errorDisco = true;
                    return 0;
                }
                if (paso == flujos::Ensamblador::Paso::Lleno)
                {
                    k->lleno = true;
                    return 0;
                }
                std::lock_guard g2(k->x->m);
                if (!k->x->d.segmentos.empty()) k->x->d.segmentos[0].hecho = k->ens->BytesEscritos() + k->ens->BytesDeLaActual();
            }
            k->recibidos += (int64_t)total;
            k->x->bytesNuevos += (int64_t)total;
            if (k->limitar) k->limitar((int64_t)total);
            return total;
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

    uint64_t Motor::AgregarTerminada(DatosDescarga d)
    {
        auto x = std::make_shared<Descarga>();
        std::lock_guard g(m_lista);
        d.id = m_siguienteId++;
        d.agregada = d.terminada = AhoraUnix();
        d.estado = Estado::Completada;
        d.error.clear();
        d.cookies.clear();
        d.conexiones = 1;
        d.segmentos.clear();
        if (d.tamano > 0) d.segmentos.push_back({ 0, d.tamano - 1, d.tamano, false });
        x->d = std::move(d);
        x->avisada = true;   // no pasó por Tick: quien la agrega hace lo que haga falta al terminar
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

    void Motor::CambiarGrupo(uint64_t id, const std::wstring& paquete, const std::string& grupo)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x) return;
        std::lock_guard g2(x->m);
        x->d.paquete = paquete;
        x->d.grupo = grupo;
        m_sucio = true;
    }

    void Motor::UnirGrupos(const std::string& viejo, const std::string& nuevo, const std::wstring& paquete)
    {
        if (viejo.empty() || viejo == nuevo) return;
        std::lock_guard g(m_lista);
        for (auto& x : m_descargas)
        {
            std::lock_guard g2(x->m);
            if (x->d.grupo != viejo) continue;
            x->d.grupo = nuevo;
            x->d.paquete = paquete;
            m_sucio = true;
        }
    }

    void Motor::FijarIdentidad(uint64_t id, const IdentidadArchivo& identidad)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x) return;
        std::lock_guard g2(x->m);
        x->d.identidad = identidad;
        m_sucio = true;
    }

    void Motor::MarcarDescomprimido(uint64_t id, bool si)
    {
        std::lock_guard g(m_lista);
        auto x = Encontrar(id);
        if (!x) return;
        std::lock_guard g2(x->m);
        x->d.descomprimido = si;
        m_sucio = true;
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
                    { "identidad", d.identidad.Valida() ? json{ { "volumen", d.identidad.volumen }, { "indice", d.identidad.indice },
                                                                { "tamano", d.identidad.tamano }, { "modificado", d.identidad.modificado } }
                                                        : json() },
                    { "descomprimido", d.descomprimido },
                    { "formato", d.formato }, { "pista", d.pista }, { "flujoTotal", d.flujoTotal }, { "flujoHechos", d.flujoHechos },
                    { "flujoBytes", d.flujoBytes }, { "flujoLocal", d.flujoLocal }, { "flujoListas", d.flujoListas },
                    { "soloAfuera", d.soloAfuera }, { "sinProxy", d.sinProxy },
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
                if (auto i = j.find("identidad"); i != j.end() && i->is_object())
                {
                    d.identidad.volumen = i->value("volumen", (uint32_t)0);
                    d.identidad.indice = i->value("indice", (uint64_t)0);
                    d.identidad.tamano = i->value("tamano", (int64_t)-1);
                    d.identidad.modificado = i->value("modificado", (int64_t)0);
                }
                d.descomprimido = j.value("descomprimido", false);
                d.formato = j.value("formato", "");
                if (d.formato != "hls" && d.formato != "dash") d.formato.clear();
                d.pista = j.value("pista", "");
                d.flujoTotal = std::max(0, j.value("flujoTotal", 0));
                d.flujoHechos = std::clamp(j.value("flujoHechos", 0), 0, d.flujoTotal);
                d.flujoBytes = std::max<int64_t>(0, j.value("flujoBytes", (int64_t)0));
                d.flujoLocal = j.value("flujoLocal", false);
                for (auto& u : j.value("flujoListas", json::array()))
                {
                    if (u.is_string() && d.flujoListas.size() < flujos::MaximoListasJuntas) d.flujoListas.push_back(u.get<std::string>());
                }
                d.soloAfuera = j.value("soloAfuera", false);
                d.sinProxy = j.value("sinProxy", false);
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

        // Un video por partes (HLS o DASH) va por su propio camino
        {
            std::string formato;
            {
                std::lock_guard g(x->m);
                formato = x->d.formato;
            }
            if (!formato.empty())
            {
                EjecutarFlujo(x, terminar);
                return;
            }
        }

        Peticion p;
        bool soloAfuera;
        {
            std::lock_guard g(x->m);
            p = { x->d.url, x->d.referer, x->d.cookies, x->d.agente };
            soloAfuera = x->d.soloAfuera;
            // Un video de una página (su archivo, o el archivo único de un DASH) va directo, nunca por proxy: así el
            // filtro de la red de la casa ve a dónde se conecta de verdad. Lo demás usa el proxy de Windows, como siempre.
            x->proxy = soloAfuera || x->d.sinProxy ? SinProxy() : ProxyDelSistema();
        }
        bool ftp = EsFtp(p.url);
        // Su dirección salió de una lista de internet: nada de esta PC ni de su red (por el nombre, antes de conectar)
        if (soloAfuera && enlaces::DireccionLocal(p.url))
        {
            terminar(Estado::Error, flujos::TextoRedLocal);
            return;
        }

        // 1. Preguntar al servidor (con reintentos si la conexión falla)
        Sondeo s;
        std::string error;
        for (int intento = 0;; intento++)
        {
            s = Sondear(p, x->proxy, x->parar, true, soloAfuera);
            if (x->parar) break;
            if (s.noPermitido)
            {
                error = Utf8(enlaces::TextoNoPermitido);
                break;
            }
            if (s.redLocal)
            {
                error = flujos::TextoRedLocal;
                break;
            }
            // Algunos servidores rechazan cualquier pedido por partes: entonces se pide entero
            if (!ftp && (s.codigo == 400 || s.codigo == 405 || s.codigo == 501 || (s.codigo == 416 && s.tamano < 0)))
            {
                Sondeo entero = Sondear(p, x->proxy, x->parar, false, soloAfuera);
                if (entero.redLocal)
                {
                    error = flujos::TextoRedLocal;
                    break;
                }
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

    void Motor::EjecutarFlujo(std::shared_ptr<Descarga> x, const std::function<void(Estado, const std::string&)>& terminar)
    {
        DatosDescarga d0;
        {
            std::lock_guard g(x->m);
            d0 = x->d;
        }
        // Un video por partes nunca va por proxy (ni sus listas, ni sus partes, ni sus redirecciones): el proxy volvería
        // a buscar los nombres por su cuenta. Directo, el filtro del socket ve a dónde se conecta de verdad.
        x->proxy = SinProxy();
        Peticion p{ d0.url, d0.referer, d0.cookies, d0.agente };
        // Si la lista es de internet, nada de esta PC ni de su red (ni la lista, ni sus partes)
        bool soloAfuera = !d0.flujoLocal;

        // 1. La lista (con reintentos si la conexión falla)
        std::string texto, urlFinal, error;
        bool local = false;
        auto bajarLista = [&](const Peticion& pl, std::string& t, std::string& uf, bool& loc) {
            for (int intento = 0;; intento++)
            {
                bool temporal = false;
                if (BajarListaInterna(pl, x->proxy, soloAfuera, t, uf, loc, temporal, error, x->parar)) return true;
                if (x->parar || !temporal || intento >= 4) return false;
                Esperar(2.0 * (1 << intento), x->parar);
            }
        };
        auto fallo = [&]() {
            if (x->parar && error == "Se canceló.") terminar(x->pausaPedida ? Estado::Pausada : Estado::EnCola, "");
            else terminar(Estado::Error, error);
        };
        if (!bajarLista(p, texto, urlFinal, local))
        {
            fallo();
            return;
        }

        // 2. Qué partes bajar
        flujos::Trozo inicio;
        std::vector<flujos::Trozo> trozos;
        std::wstring extension;
        if (d0.formato == "hls")
        {
            // Las otras listas del mismo video (su audio aparte, sus subtítulos)
            std::vector<std::string> otras = d0.flujoListas;
            auto l = flujos::LeerHls(texto, urlFinal.empty() ? p.url : urlFinal);
            if (l.valida && l.maestra && !l.cifrada && !l.demasiadas)
            {
                // Llegó la lista con las calidades (la ventana guarda la que eligió la persona): la mejor, con su audio
                int v = flujos::MejorVariante(l);
                if (v < 0) error = "La lista del video no trae ninguna calidad para bajar.";
                else
                {
                    for (const auto& u : flujos::ListasJuntas(l, v))
                        if (std::find(otras.begin(), otras.end(), u) == otras.end()) otras.push_back(u);
                    Peticion pv = p;
                    pv.url = l.variantes[v].url;
                    if (!bajarLista(pv, texto, urlFinal, local))
                    {
                        fallo();
                        return;
                    }
                    l = flujos::LeerHls(texto, urlFinal.empty() ? pv.url : urlFinal);
                    if (l.maestra) error = "La lista del video no trae partes para bajar.";
                }
            }
            if (!error.empty()) {}
            else if (l.demasiadas) error = flujos::TextoDemasiadas;
            else if (!l.valida) error = "La dirección no es una lista de video HLS (.m3u8).";
            else if (l.cifrada) error = flujos::TextoProtegido;
            else if (l.enVivo) error = flujos::TextoEnVivo;
            inicio = l.inicio;
            trozos = std::move(l.trozos);
            extension = flujos::ExtensionHls(l);
            // Todas las listas que se usan se leen ANTES de bajar una sola parte: si alguna está cifrada (o en vivo, o no
            // se puede leer), el video no se baja; y las partes de todas juntas (y sus direcciones) no pueden pasar el tope
            size_t total = trozos.size() + (inicio.url.empty() ? 0 : 1);
            size_t bytesTotal = l.bytes;
            if (error.empty() && otras.size() > flujos::MaximoListasJuntas) error = flujos::TextoDemasiadas;
            for (size_t k = 0; error.empty() && k < otras.size(); k++)
            {
                std::string t2, uf2;
                bool local2 = false;
                Peticion po = p;
                po.url = otras[k];
                if (!bajarLista(po, t2, uf2, local2))
                {
                    if (x->parar)
                    {
                        fallo();
                        return;
                    }
                    error = "No se pudo revisar otra lista del mismo video (su audio o sus subtítulos): " + error;
                    break;
                }
                auto o = flujos::LeerHls(t2, uf2.empty() ? po.url : uf2);
                if (o.demasiadas) error = flujos::TextoDemasiadas;
                else if (!o.valida || o.maestra) error = "Otra lista del mismo video (su audio o sus subtítulos) no es una lista HLS.";
                else if (o.cifrada) error = flujos::TextoProtegido;
                else if (o.enVivo) error = flujos::TextoEnVivo;
                else if ((total += o.trozos.size() + (o.inicio.url.empty() ? 0 : 1)) > flujos::MaximoPartes) error = flujos::TextoDemasiadas;
                else if ((bytesTotal += o.bytes) > flujos::MaximoBytesDirecciones) error = flujos::TextoDemasiadas;
            }
        }
        else if (d0.formato == "dash")
        {
            auto m = flujos::LeerDash(texto, urlFinal.empty() ? p.url : urlFinal);
            if (m.demasiadas) error = flujos::TextoDemasiadas;
            else if (!m.valido) error = "La dirección no es un manifiesto de video DASH (.mpd).";
            else if (m.protegido) error = flujos::TextoProtegido;
            else if (m.enVivo) error = flujos::TextoEnVivo;
            else if (m.variasPartes) error = flujos::TextoVariasPartes;
            else
            {
                bool audio = d0.pista.rfind("audio", 0) == 0;
                auto dp = d0.pista.find(':');
                std::string id = dp == std::string::npos ? std::string() : d0.pista.substr(dp + 1);
                auto& lista = audio ? m.audios : m.videos;
                int i = flujos::BuscarRepresentacion(lista, id);
                if (i < 0) i = flujos::MejorRepresentacion(lista);
                if (i < 0) error = audio ? "El video DASH no trae audio." : "El video DASH no trae video.";
                else
                {
                    // Solo se arman las direcciones de esta representación, con el tope de bytes
                    size_t bytes = 0;
                    if (!flujos::ArmarTrozos(lista[i], bytes)) error = flujos::TextoDemasiadas;
                    else
                    {
                        inicio = lista[i].inicio;
                        trozos = std::move(lista[i].trozos);
                        extension = flujos::ExtensionDash(lista[i]);
                    }
                }
            }
        }
        else error = "Atarraya no sabe bajar este tipo de video.";
        // Las partes pueden ser de esta PC o de su red solo si la lista también lo es (y la persona lo confirmó)
        bool partesSoloAfuera = soloAfuera || !local;
        if (error.empty()) error = flujos::RevisarTrozos(inicio, trozos, partesSoloAfuera);
        if (!error.empty())
        {
            terminar(Estado::Error, error);
            return;
        }
        std::vector<flujos::Trozo> partes;
        if (!inicio.url.empty()) partes.push_back(inicio);
        partes.insert(partes.end(), trozos.begin(), trozos.end());
        trozos.clear();
        trozos.shrink_to_fit();

        // 3. Nombre, archivo parcial y desde dónde seguir
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
        std::wstring parcial, carpeta;
        size_t yaEscritas;
        int64_t yaBytes;
        {
            std::lock_guard g(x->m);
            auto& d = x->d;
            if (d.flujoTotal != (int)partes.size() || d.segmentos.size() != 1)
            {
                d.flujoHechos = 0;   // la lista cambió (o es la primera vez): desde el principio
                d.flujoBytes = 0;
                d.segmentos.clear();
            }
            if (d.segmentos.empty()) d.segmentos.push_back({ 0, -1, 0, false });
            if (d.carpeta.empty()) d.carpeta = CarpetaDescargas();
            if (d.flujoHechos == 0 && d.flujoBytes == 0 && d.segmentos[0].hecho == 0)
            {
                std::wstring n = LimpiarNombre(d.nombre.empty() ? NombreDeUrl(d.url) : d.nombre);
                std::wstring bajo = Minusculas(n);
                for (const wchar_t* lista : { L".m3u8", L".mpd", L".m3u" })
                {
                    size_t largo = wcslen(lista);
                    if (bajo.size() > largo && bajo.compare(bajo.size() - largo, largo, lista) == 0)
                    {
                        n = n.substr(0, n.size() - largo);
                        bajo = Minusculas(n);
                        break;
                    }
                }
                std::wstring ext = Minusculas(extension);
                if (bajo.size() < ext.size() || bajo.compare(bajo.size() - ext.size(), ext.size(), ext) != 0) n += extension;
                d.nombre = NombreUnico(d.carpeta, n, ocupados);
            }
            d.flujoTotal = (int)partes.size();
            d.tamano = -1;
            d.rangos = false;
            d.estado = Estado::Descargando;
            carpeta = d.carpeta;
            parcial = d.carpeta + L"\\" + d.nombre + L".atarraya";
            x->rutaParcial = parcial;
            yaEscritas = (size_t)std::max(0, d.flujoHechos);
            // Solo cuentan las partes que se juntaron ENTERAS («flujoBytes»), no lo que se llevaba de la siguiente
            // («segmentos[0].hecho», que es para mostrar el avance): si el programa se cerró de golpe a mitad de una
            // parte, eso se corta aquí y la parte se baja otra vez desde su principio
            yaBytes = std::max<int64_t>(0, d.flujoBytes);
            m_sucio = true;
        }

        // 4. El archivo parcial: queda con las partes enteras que ya se juntaron (lo demás se baja otra vez)
        SHCreateDirectoryExW(nullptr, carpeta.c_str(), nullptr);
        HANDLE h = CreateFileW(parcial.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            terminar(Estado::Error, "No se pudo crear el archivo en la carpeta elegida. ¿Tienes permiso para guardar ahí?");
            return;
        }
        LARGE_INTEGER actual = {};
        GetFileSizeEx(h, &actual);
        // Lo guardado no cuadra con el archivo (más corto de lo que dice, partes sin bytes o al revés): desde el principio
        if (actual.QuadPart < yaBytes || yaEscritas > partes.size() || (yaEscritas == 0) != (yaBytes == 0))
        {
            yaEscritas = 0;
            yaBytes = 0;
        }
        LARGE_INTEGER corte = {};
        corte.QuadPart = yaBytes;
        SetFilePointerEx(h, corte, nullptr, FILE_BEGIN);
        SetEndOfFile(h);
        {
            std::lock_guard g(x->m);
            x->d.flujoHechos = (int)yaEscritas;
            x->d.flujoBytes = yaBytes;
            x->d.segmentos[0].hecho = yaBytes;
        }

        // 5. Las partes: varias conexiones a la vez (las de cada descarga), con el límite de velocidad de siempre. El
        //    Ensamblador las junta en orden; las que se adelantan esperan en memoria (como mucho unas pocas).
        size_t maximoEnMemoria = flujos::MaximoEnMemoria;
#ifdef ATARRAYA_PRUEBAS
        if (g_memoriaPrueba > 0) maximoEnMemoria = g_memoriaPrueba;
#endif
        flujos::Ensamblador ens(partes.size(), yaEscritas, yaBytes, [h](int64_t posicion, const char* datos, size_t n) {
            OVERLAPPED ov = {};
            ov.Offset = (DWORD)(posicion & 0xFFFFFFFF);
            ov.OffsetHigh = (DWORD)(posicion >> 32);
            DWORD escritos = 0;
            return WriteFile(h, datos, (DWORD)n, &escritos, &ov) && escritos == n;
        }, maximoEnMemoria);
        std::mutex mEns;
        size_t proxima = yaEscritas;
        int conexiones = std::clamp(d0.conexiones, 1, 16);
        size_t ventana = (size_t)conexiones * 2;
        auto trabajar = [&]() {
            CURL* c = curl_easy_init();
            if (!c)
            {
                x->FijarError("No se pudo preparar la conexión.");
                return;
            }
            x->activas++;
            char detalle[CURL_ERROR_SIZE] = {};
            while (!x->parar)
            {
                size_t i;
                {
                    std::unique_lock g(mEns);
                    if (proxima >= partes.size()) break;
                    if (proxima >= ens.Siguiente() + ventana)
                    {
                        g.unlock();
                        Sleep(30);
                        continue;
                    }
                    i = proxima++;
                }
                const flujos::Trozo& tr = partes[i];
                std::string ultimoError;
                bool lista = false;
                for (int intento = 0; intento < MaxReintentos && !x->parar; intento++)
                {
                    ParteFlujo k;
                    k.x = x.get();
                    k.c = c;
                    k.indice = i;
                    k.ens = &ens;
                    k.mEns = &mEns;
                    k.pidioRango = tr.desde >= 0 && tr.largo > 0;
                    k.limitar = [this, x](int64_t n) { m_limitador->Consumir(n, limiteBytesSeg, x->parar); };
                    Peticion pp{ tr.url, d0.referer, d0.cookies, d0.agente };
                    Preparar(c, pp, x->proxy);
                    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
                    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
                    std::string rango = k.pidioRango ? std::to_string(tr.desde) + "-" + std::to_string(tr.desde + tr.largo - 1) : std::string();
                    curl_easy_setopt(c, CURLOPT_RANGE, k.pidioRango ? rango.c_str() : nullptr);
                    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, AlRecibirParteFlujo);
                    curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
                    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, AlRecibirCabecera);
                    curl_easy_setopt(c, CURLOPT_HEADERDATA, &k.cab);
                    DestinoVisto visto;
                    visto.soloAfuera = partesSoloAfuera;
                    MirarDestino(c, visto, true);
                    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
                    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, AlProgresar);
                    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &x->parar);
                    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detalle);
                    detalle[0] = 0;
                    CURLcode res = curl_easy_perform(c);
                    long codigo = 0;
                    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &codigo);
                    curl_off_t anunciado = -1;
                    curl_easy_getinfo(c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &anunciado);
                    // Una parte cuenta solo si llegó entera: con el largo que pidió (un trozo) o el que dijo el servidor
                    bool bien = res == CURLE_OK && codigo >= 200 && codigo < 300 && !k.pagina && !k.rangoIgnorado && !k.errorDisco
                                && !k.lleno && (!k.pidioRango || k.recibidos == tr.largo) && (anunciado < 0 || k.recibidos == anunciado);
                    if (bien)
                    {
                        std::lock_guard g(mEns);
                        if (!ens.Terminar(i))
                        {
                            x->FijarError("No se pudo escribir en el disco. ¿Está lleno o se desconectó?");
                            break;
                        }
                        std::lock_guard g2(x->m);
                        x->d.flujoHechos = (int)ens.Siguiente();
                        x->d.flujoBytes = ens.BytesEscritos();
                        x->d.segmentos[0].hecho = ens.BytesEscritos() + ens.BytesDeLaActual();
                        lista = true;
                        break;
                    }
                    {
                        std::lock_guard g(mEns);
                        ens.Descartar(i);   // lo que llegó de esta parte no sirve: se baja otra vez desde el principio
                    }
                    if (x->parar) break;
                    if (k.lleno)
                    {
                        // Adelantada y sin lugar en memoria: espera su turno y se baja otra vez, entonces directo al disco.
                        // No cuenta como un intento fallido.
                        while (!x->parar)
                        {
                            {
                                std::lock_guard g(mEns);
                                if (ens.Siguiente() >= i) break;
                            }
                            Sleep(30);
                        }
                        intento--;
                        continue;
                    }
                    // Lo que no se arregla reintentando
                    if (k.pagina) { x->FijarError(TextoRespondioPagina); break; }
                    if (k.errorDisco) { x->FijarError("No se pudo escribir en el disco. ¿Está lleno o se desconectó?"); break; }
                    if (k.cab.sitioNoPermitido) { x->FijarError(Utf8(enlaces::TextoNoPermitido)); break; }
                    if (visto.soloAfuera && (visto.local || visto.sinRedes)) { x->FijarError(flujos::TextoRedLocal); break; }
                    if (k.rangoIgnorado) { x->FijarError("El servidor no deja bajar este video por trozos."); break; }
                    if (codigo >= 400 && !(codigo == 408 || codigo == 429 || codigo >= 500)) { x->FijarError(TextoErrorHttp(codigo)); break; }
                    if (res != CURLE_OK && EsErrorPermanente(res)) { x->FijarError(TextoErrorCurl(res, detalle[0] ? detalle : curl_easy_strerror(res))); break; }
                    ultimoError = codigo >= 400 ? TextoErrorHttp(codigo)
                                                : (res != CURLE_OK ? TextoErrorCurl(res, detalle[0] ? detalle : curl_easy_strerror(res))
                                                                   : std::string("Una parte del video llegó incompleta."));
                    Esperar(std::min(30.0, 1.0 * (1 << intento)), x->parar);
                }
                if (!lista)
                {
                    if (!x->parar) x->FijarError(ultimoError.empty() ? std::string("Una parte del video no se pudo bajar.") : ultimoError);
                    break;
                }
            }
            x->activas--;
            curl_easy_cleanup(c);
        };
        {
            std::vector<std::thread> hilos;
            int cuantas = (int)std::min<size_t>((size_t)conexiones, partes.size() - std::min(partes.size(), yaEscritas));
            for (int k = 0; k < std::max(1, cuantas); k++) hilos.emplace_back(trabajar);
            for (auto& t : hilos) t.join();
        }

        // 6. Resultado: el archivo queda solo con las partes enteras (lo de una parte a medias se quita)
        bool completo;
        int64_t bytes;
        size_t escritas;
        {
            std::lock_guard g(mEns);
            completo = ens.Completo();
            bytes = ens.BytesEscritos();
            escritas = ens.Siguiente();
        }
        LARGE_INTEGER largoFinal = {};
        largoFinal.QuadPart = bytes;
        SetFilePointerEx(h, largoFinal, nullptr, FILE_BEGIN);
        SetEndOfFile(h);
        FlushFileBuffers(h);
        CloseHandle(h);
        std::string errorTexto;
        {
            std::lock_guard g(x->m);
            x->d.flujoHechos = (int)escritas;
            x->d.flujoBytes = bytes;
            x->d.segmentos[0].hecho = bytes;
            errorTexto = x->errorTexto;
        }
        if (!errorTexto.empty())
        {
            terminar(Estado::Error, errorTexto);
            return;
        }
        if (!completo)
        {
            if (x->pausaPedida || x->parar) terminar(x->pausaPedida ? Estado::Pausada : Estado::EnCola, "");
            else terminar(Estado::Error, "La descarga se detuvo sin terminar. Pulsa «Reanudar» para seguir.");
            return;
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
            x->d.tamano = bytes;
        }
        terminar(Estado::Completada, "");
    }

    void Motor::Trabajar(std::shared_ptr<Descarga> x)
    {
        Peticion p;
        std::wstring parcial;
        DestinoVisto visto;
        {
            std::lock_guard g(x->m);
            p = { x->d.url, x->d.referer, x->d.cookies, x->d.agente };
            parcial = x->rutaParcial;
            visto.soloAfuera = x->d.soloAfuera;
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
        // Si su dirección salió de una lista de internet, ninguna conexión (ni las de una redirección) va a esta PC ni a
        // su red (va directo, sin proxy: aquí se ve el destino real)
        MirarDestino(c, visto);
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
            if (visto.soloAfuera && (visto.local || visto.sinRedes) && res != CURLE_OK) { x->FijarError(flujos::TextoRedLocal); break; }
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
