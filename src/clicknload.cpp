#include "clicknload.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "enlaces.hpp"
#include "util.hpp"

namespace
{
    constexpr size_t MaxCabeceras = 16 * 1024;
    constexpr size_t MaxCuerpo = 1024 * 1024;
    constexpr size_t MaxEnlaces = 1000;
    constexpr int MaxPorMinuto = 30;

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

    struct Respuesta
    {
        int codigo = 200;
        std::string tipo = "text/plain; charset=utf-8";
        std::string cuerpo;
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
        t += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
        t += r.cuerpo;
        size_t enviado = 0;
        while (enviado < t.size())
        {
            int n = send(s, t.data() + enviado, (int)(t.size() - enviado), 0);
            if (n <= 0) break;
            enviado += (size_t)n;
        }
    }

    std::wstring Paquete(const std::map<std::string, std::string>& f)
    {
        auto valor = [&](const char* k) {
            auto it = f.find(k);
            return it == f.end() ? std::string() : Recortar(it->second);
        };
        std::string p = valor("package");
        if (!p.empty() && p.find("://") == std::string::npos) return hnd::Ancho(p.substr(0, 150));
        std::string origen = valor("source");
        if (origen.empty()) origen = valor("referer");
        std::string sitio = hnd::enlaces::Sitio(origen);
        return sitio.empty() ? std::wstring(L"Click'n'Load") : L"Click'n'Load de " + hnd::Ancho(sitio);
    }

    std::string Origen(const std::map<std::string, std::string>& f)
    {
        for (const char* k : { "source", "referer" })
        {
            auto it = f.find(k);
            if (it == f.end()) continue;
            std::string v = Recortar(it->second);
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
    // Algunos sitios mandan el base64 sin codificar y los «+» llegan como espacios
    std::string b64 = crypted;
    std::replace(b64.begin(), b64.end(), ' ', '+');
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

std::map<std::string, std::string> hnd::cnl::LeerFormulario(const std::string& cuerpo)
{
    std::map<std::string, std::string> r;
    size_t desde = 0;
    while (desde <= cuerpo.size() && r.size() < 64)
    {
        size_t fin = cuerpo.find('&', desde);
        std::string par = cuerpo.substr(desde, fin == std::string::npos ? std::string::npos : fin - desde);
        size_t igual = par.find('=');
        std::string clave = DecodificarFormulario(par.substr(0, igual));
        std::string valor = igual == std::string::npos ? std::string() : DecodificarFormulario(par.substr(igual + 1));
        if (!clave.empty() && !r.count(clave)) r[clave] = valor;
        if (fin == std::string::npos) break;
        desde = fin + 1;
    }
    return r;
}

std::vector<std::string> hnd::cnl::Enlaces(const std::string& texto, size_t maximo)
{
    auto r = enlaces::DeTexto(Ancho(texto));
    if (r.enlaces.size() > maximo) r.enlaces.resize(maximo);
    return r.enlaces;
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
    if (m_escucha != ~(uintptr_t)0)
    {
        closesocket((SOCKET)m_escucha);
        m_escucha = ~(uintptr_t)0;
        WSACleanup();
    }
    m_activo = false;
    Registrar("Click'n'Load: apagado");
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
        if (r <= 0)
        {
            if (r < 0) Sleep(100);
            continue;
        }
        sockaddr_in de = {};
        int largo = sizeof(de);
        SOCKET c = accept(escucha, reinterpret_cast<sockaddr*>(&de), &largo);
        if (c == INVALID_SOCKET) continue;
        // Solo esta PC (127.0.0.1); el socket ya escucha solo ahí, pero se comprueba igual
        if (de.sin_family == AF_INET && ntohl(de.sin_addr.s_addr) == INADDR_LOOPBACK)
        {
            try
            {
                Atender((uintptr_t)c);
            }
            catch (...)
            {
                Registrar("Click'n'Load: un pedido no se pudo leer");
            }
        }
        closesocket(c);
    }
}

void hnd::ServidorCnL::Atender(uintptr_t conexion)
{
    SOCKET s = (SOCKET)conexion;
    DWORD espera = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&espera), sizeof(espera));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&espera), sizeof(espera));
    ULONGLONG limite = GetTickCount64() + 10000;

    // 1. Cabeceras
    std::string datos;
    size_t finCabeceras = std::string::npos;
    char buf[4096];
    while (finCabeceras == std::string::npos)
    {
        if (datos.size() > MaxCabeceras || GetTickCount64() > limite) { Enviar(s, { 431, "text/plain; charset=utf-8", "failed\r\n" }); return; }
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return;
        datos.append(buf, (size_t)n);
        finCabeceras = datos.find("\r\n\r\n");
    }
    std::string cabeceras = datos.substr(0, finCabeceras);
    std::string cuerpo = datos.substr(finCabeceras + 4);

    size_t finLinea = cabeceras.find("\r\n");
    std::string linea = cabeceras.substr(0, finLinea);
    size_t e1 = linea.find(' '), e2 = linea.find(' ', e1 == std::string::npos ? 0 : e1 + 1);
    if (e1 == std::string::npos || e2 == std::string::npos) { Enviar(s, { 400, "text/plain; charset=utf-8", "failed\r\n" }); return; }
    std::string metodo = linea.substr(0, e1);
    std::string ruta = linea.substr(e1 + 1, e2 - e1 - 1);
    ruta = ruta.substr(0, ruta.find_first_of("?#"));

    std::map<std::string, std::string> h;
    size_t p = finLinea == std::string::npos ? cabeceras.size() : finLinea + 2;
    while (p < cabeceras.size())
    {
        size_t f = cabeceras.find("\r\n", p);
        std::string l = cabeceras.substr(p, f == std::string::npos ? std::string::npos : f - p);
        size_t dp = l.find(':');
        if (dp != std::string::npos) h[Bajo(Recortar(l.substr(0, dp)))] = Recortar(l.substr(dp + 1));
        if (f == std::string::npos) break;
        p = f + 2;
    }

    // 2. Solo pedidos dirigidos a esta PC por su nombre (evita el «DNS rebinding»)
    std::string host = Bajo(h["host"]);
    std::string puerto = ":" + std::to_string(m_puerto);
    if (host != "127.0.0.1" + puerto && host != "localhost" + puerto && host != "[::1]" + puerto)
    {
        Enviar(s, { 403, "text/plain; charset=utf-8", "failed\r\n" });
        return;
    }

    if (metodo == "GET" || metodo == "HEAD")
    {
        if (ruta == "/jdcheck.js")
            Enviar(s, { 200, "text/javascript; charset=utf-8", "jdownloader=true;\r\nvar version='42707';\r\n" });
        else if (ruta == "/flash" || ruta == "/flash/")
            Enviar(s, { 200, "text/plain; charset=utf-8", "JDownloader\r\n" });
        else
            Enviar(s, { 404, "text/plain; charset=utf-8", "" });
        return;
    }
    if (metodo != "POST") { Enviar(s, { 405, "text/plain; charset=utf-8", "" }); return; }

    // 3. Cuerpo del formulario, con tope
    if (h.count("transfer-encoding")) { Enviar(s, { 411, "text/plain; charset=utf-8", "failed\r\n" }); return; }
    if (!h.count("content-length")) { Enviar(s, { 411, "text/plain; charset=utf-8", "failed\r\n" }); return; }
    const std::string& cl = h["content-length"];
    if (cl.empty() || cl.size() > 9 || !std::all_of(cl.begin(), cl.end(), [](unsigned char c) { return std::isdigit(c); }))
    {
        Enviar(s, { 400, "text/plain; charset=utf-8", "failed\r\n" });
        return;
    }
    size_t largo = (size_t)std::stoul(cl);
    if (largo > MaxCuerpo) { Enviar(s, { 413, "text/plain; charset=utf-8", "failed\r\n" }); return; }
    while (cuerpo.size() < largo)
    {
        if (GetTickCount64() > limite) return;
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return;
        cuerpo.append(buf, (size_t)n);
    }
    cuerpo.resize(largo);

    // 4. Tope de pedidos por minuto
    int64_t ahora = (int64_t)(GetTickCount64() / 1000);
    m_recientes.erase(std::remove_if(m_recientes.begin(), m_recientes.end(), [&](int64_t t) { return ahora - t >= 60; }),
                      m_recientes.end());
    if ((int)m_recientes.size() >= MaxPorMinuto) { Enviar(s, { 429, "text/plain; charset=utf-8", "failed\r\n" }); return; }
    m_recientes.push_back(ahora);

    std::string tipo = Bajo(h["content-type"]);
    if (!tipo.empty() && tipo.rfind("application/x-www-form-urlencoded", 0) != 0)
    {
        Enviar(s, { 415, "text/plain; charset=utf-8", "failed\r\n" });
        return;
    }
    auto formulario = cnl::LeerFormulario(cuerpo);
    std::vector<std::string> lista;
    if (ruta == "/flash/add")
    {
        lista = cnl::Enlaces(formulario["urls"], MaxEnlaces);
    }
    else if (ruta == "/flash/addcrypted2")
    {
        std::vector<uint8_t> clave;
        std::string texto;
        if (!cnl::ClaveDeJk(formulario["jk"], clave))
        {
            Registrar("Click'n'Load: clave con una forma que no se acepta (no se ejecuta JavaScript)");
            Enviar(s, { 400, "text/plain; charset=utf-8", "failed\r\n" });
            return;
        }
        if (!cnl::Descifrar(formulario["crypted"], clave, texto))
        {
            Registrar("Click'n'Load: no se pudieron descifrar los enlaces");
            Enviar(s, { 400, "text/plain; charset=utf-8", "failed\r\n" });
            return;
        }
        lista = cnl::Enlaces(texto, MaxEnlaces);
    }
    else if (ruta == "/flash/addcrypted")
    {
        Registrar("Click'n'Load: un contenedor DLC (no se admite)");
        Enviar(s, { 400, "text/plain; charset=utf-8", "failed\r\n" });
        return;
    }
    else
    {
        Enviar(s, { 404, "text/plain; charset=utf-8", "" });
        return;
    }
    if (lista.empty())
    {
        Enviar(s, { 400, "text/plain; charset=utf-8", "failed\r\n" });
        return;
    }
    Registrar("Click'n'Load: " + std::to_string(lista.size()) + " enlaces de " + SoloSitio(Origen(formulario)));
    if (m_alRecibir) m_alRecibir(std::move(lista), Paquete(formulario), Origen(formulario));
    Enviar(s, { 200, "text/plain; charset=utf-8", "success\r\n" });
}
