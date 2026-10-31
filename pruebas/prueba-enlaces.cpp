// Prueba de los enlaces sin ventana (no se publica).
//
//   prueba-enlaces             pruebas fijas (sin internet; con las direcciones de esta PC, sin mostrarlas)
//   prueba-enlaces <url>       baja esa página (o la lista de Internet Archive) y muestra sus enlaces

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <bcrypt.h>

#include "clicknload.hpp"
#include "enlaces.hpp"
#include "motor.hpp"
#include "recolector.hpp"
#include "redlocal.hpp"
#include "util.hpp"

using namespace hnd;
using namespace hnd::enlaces;

namespace
{
    int fallas = 0;

    void Comprobar(bool ok, const char* que)
    {
        printf("%s  %s\n", ok ? "bien " : "FALLA", que);
        if (!ok) fallas++;
    }

    // Una dirección escrita («203.0.113.5» o «2001:db8::1»), como la recibe curl antes de conectar
    sockaddr_storage Dir(const char* texto)
    {
        sockaddr_storage s{};
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&s);
        auto* v4 = reinterpret_cast<sockaddr_in*>(&s);
        if (inet_pton(AF_INET6, texto, &v6->sin6_addr) == 1) v6->sin6_family = AF_INET6;
        else if (inet_pton(AF_INET, texto, &v4->sin_addr) == 1) v4->sin_family = AF_INET;
        return s;
    }

    const sockaddr* P(const sockaddr_storage& s) { return reinterpret_cast<const sockaddr*>(&s); }

    red::Subred Red(const char* texto, int prefijo)
    {
        auto s = Dir(texto);
        red::Subred r;
        r.v6 = s.ss_family == AF_INET6;
        if (r.v6) memcpy(r.direccion, &reinterpret_cast<sockaddr_in6*>(&s)->sin6_addr, 16);
        else memcpy(r.direccion, &reinterpret_cast<sockaddr_in*>(&s)->sin_addr, 4);
        r.prefijo = prefijo;
        return r;
    }

    bool En(const char* destino, const red::Subred& r)
    {
        auto s = Dir(destino);
        return red::EnSubred(P(s), r);
    }

    bool Rango(const char* destino)
    {
        auto s = Dir(destino);
        return red::EsRangoLocal(P(s));
    }

    // Las redes: prefijos de ejemplo (direcciones de documentación, que no son de nadie)
    void Redes()
    {
        auto r28 = Red("203.0.113.37", 28);   // 203.0.113.32 a 203.0.113.47
        Comprobar(En("203.0.113.32", r28) && En("203.0.113.47", r28) && !En("203.0.113.31", r28) && !En("203.0.113.48", r28),
                  "red IPv4 /28: los dos bordes");
        auto r24 = Red("198.51.100.20", 24);
        Comprobar(En("198.51.100.1", r24) && En("198.51.100.254", r24) && !En("198.51.101.1", r24), "red IPv4 /24");
        auto r32 = Red("198.51.100.20", 32);
        Comprobar(En("198.51.100.20", r32) && !En("198.51.100.21", r32), "red IPv4 /32: solo la propia PC");
        auto r64 = Red("2001:db8:4404:51a0:1c2d:3e4f:5a6b:7c8d", 64);
        Comprobar(En("2001:db8:4404:51a0::1", r64) && En("2001:db8:4404:51a0:ffff:ffff:ffff:ffff", r64)
                  && !En("2001:db8:4404:51a1::1", r64) && !En("2001:db8:4404:519f:ffff::1", r64), "red IPv6 /64");
        auto r60 = Red("2001:db8:abcd:12f0::5", 60);   // 2001:db8:abcd:12f0:: a 2001:db8:abcd:12ff:ffff:...
        Comprobar(En("2001:db8:abcd:12f0::", r60) && En("2001:db8:abcd:12ff:1::1", r60) && !En("2001:db8:abcd:12ef::1", r60)
                  && !En("2001:db8:abcd:1300::1", r60), "red IPv6 /60 (el prefijo corta un byte)");
        auto r128 = Red("2001:db8::7", 128);
        Comprobar(En("2001:db8::7", r128) && !En("2001:db8::8", r128), "red IPv6 /128");
        Comprobar(En("::ffff:203.0.113.40", r28) && En("64:ff9b::203.0.113.40", r28) && En("2002:cb00:7128::1", r28)
                  && !En("::ffff:203.0.113.50", r28), "red: una IPv4 escrita como IPv6 cuenta como IPv4");
        Comprobar(!En("203.0.113.40", r64) && !En("2001:db8:4404:51a0::1", r28), "red: IPv4 e IPv6 no se mezclan");

        // La red de cada dirección de la PC, según el prefijo que da Windows
        red::Subred t;
        auto temporal = Dir("2001:db8:4404:51a0:aaaa::1");
        Comprobar(red::SubredDeTarjeta(P(temporal), 128, t) && t.prefijo == 64 && En("2001:db8:4404:51a0::99", t),
                  "red: una IPv6 pública que llega como /128 cuenta con todo su /64");
        Comprobar(red::SubredDeTarjeta(P(temporal), 56, t) && t.prefijo == 56, "red: un prefijo IPv6 más corto se respeta");
        auto v4 = Dir("198.51.100.20");
        Comprobar(red::SubredDeTarjeta(P(v4), 0, t) && t.prefijo == 32 && red::SubredDeTarjeta(P(v4), 22, t) && t.prefijo == 22,
                  "red: IPv4 sin prefijo queda solo la dirección; con prefijo, su red");

        // Rangos que nunca se consultan solos
        Comprobar(Rango("127.0.0.1") && Rango("10.1.2.3") && Rango("192.168.1.1") && Rango("100.64.0.1") && Rango("169.254.1.1")
                  && Rango("192.0.2.1") && Rango("255.255.255.255") && !Rango("1.1.1.1") && !Rango("207.241.224.2"),
                  "rangos IPv4");
        Comprobar(Rango("::1") && Rango("::") && Rango("fe80::1") && Rango("fec0::1") && Rango("fd12:3456::1") && Rango("ff02::1")
                  && Rango("::ffff:192.168.1.1") && Rango("64:ff9b::10.0.0.1") && Rango("64:ff9b:1::1") && Rango("2002:c0a8:101::1")
                  && Rango("2001:db8::1") && Rango("2001::1") && Rango("3fff::1") && Rango("5f00::1") && Rango("100::1"),
                  "rangos IPv6 locales y reservados");
        Comprobar(!Rango("2606:4700:4700::1111") && !Rango("2001:4860:4860::8888") && !Rango("::ffff:1.1.1.1")
                  && !Rango("64:ff9b::1.1.1.1") && !Rango("2002:101:101::1"), "rangos IPv6 de afuera");
    }

    // La dirección de una red de la PC, como la recibe curl
    sockaddr_storage DeSubred(const red::Subred& r)
    {
        sockaddr_storage s{};
        if (r.v6)
        {
            auto* a = reinterpret_cast<sockaddr_in6*>(&s);
            a->sin6_family = AF_INET6;
            memcpy(&a->sin6_addr, r.direccion, 16);
        }
        else
        {
            auto* a = reinterpret_cast<sockaddr_in*>(&s);
            a->sin_family = AF_INET;
            memcpy(&a->sin_addr, r.direccion, 4);
        }
        return s;
    }

    red::Destino Revisar(const sockaddr_storage& s) { return red::RevisarDestino(P(s)); }

    // Con las direcciones de verdad de esta PC (sin mostrarlas): cada una y otra de su misma red cuentan como locales,
    // y la consulta del recolector hacia ellas no sale
    void RedDeEstaPc()
    {
        bool ok = false;
        auto redes = red::LeerSubredesDeLaPc(&ok);
        Comprobar(ok && !redes.empty(), "PC: Windows da sus direcciones");
        auto inicio = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; i++) red::LeerSubredesDeLaPc();
        printf("   leer las redes de la PC tarda %.2f ms (se lee antes de cada conexión)\n",
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio).count() / 50);
        int publicas = 0;
        bool propias = true, vecinas = true, consultas = true;
        for (auto& r : redes)
        {
            sockaddr_storage s = DeSubred(r);
            if (Revisar(s) != red::Destino::Local) propias = false;
            if (red::EsRangoLocal(P(s))) continue;   // a esta ya la frenaba el filtro de rangos
            publicas++;
            // Otra dirección de la misma red: el último byte cambiado, si el prefijo deja lugar
            int bits = r.v6 ? 128 : 32;
            sockaddr_storage v = s;
            if (r.prefijo <= bits - 8)
            {
                uint8_t* b = r.v6 ? reinterpret_cast<sockaddr_in6*>(&v)->sin6_addr.u.Byte : reinterpret_cast<uint8_t*>(&reinterpret_cast<sockaddr_in*>(&v)->sin_addr);
                b[bits / 8 - 1] ^= 0x5A;
                if (Revisar(v) != red::Destino::Local) vecinas = false;
            }
            // La consulta entera, como la hace el recolector (al puerto 9, que descarta todo)
            for (const sockaddr_storage* x : { &s, &v })
            {
                char texto[INET6_ADDRSTRLEN] = {};
                if (r.v6) inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(x)->sin6_addr, texto, sizeof(texto));
                else inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(x)->sin_addr, texto, sizeof(texto));
                std::string url = r.v6 ? "http://[" + std::string(texto) + "]:9/prueba.zip" : "http://" + std::string(texto) + ":9/prueba.zip";
                InfoEnlace info;
                ComprobarEnlace(url, "", info);
                if (!info.local) consultas = false;
            }
        }
        printf("   esta PC tiene %zu direcciones; %d públicas (a esas solo las frena lo nuevo)\n", redes.size(), publicas);
        Comprobar(propias, "PC: sus direcciones cuentan como locales");
        if (publicas)
        {
            Comprobar(vecinas, "PC: otra dirección de su misma red también");
            Comprobar(consultas, "PC: la consulta del recolector hacia ellas no sale");
        }
        else printf("   (sin direcciones públicas: lo nuevo se probó con los prefijos de ejemplo)\n");
        auto g4 = Dir("8.8.8.8"), g6 = Dir("2001:4860:4860::8888");
        Comprobar(Revisar(g4) == red::Destino::Afuera && Revisar(g6) == red::Destino::Afuera,
                  "PC: una dirección de afuera sí se puede consultar");
    }

    // Si Windows no da las redes de la PC, no se consulta nada solo (ni hacia afuera) y el recolector lo dice
    void SinRedes()
    {
        red::SimularFallaAlLeerRedes(true);
        bool ok = true;
        auto redes = red::LeerSubredesDeLaPc(&ok);
        Comprobar(!ok && redes.empty(), "sin redes: la lectura falla");
        auto afuera = Dir("1.1.1.1"), casa = Dir("192.168.1.1");
        Comprobar(Revisar(afuera) == red::Destino::SinRevisar && Revisar(casa) == red::Destino::Local,
                  "sin redes: una dirección de afuera queda sin revisar; un rango local sigue siendo local");
        InfoEnlace info;
        ComprobarEnlace("http://1.1.1.1:9/prueba.zip", "", info);
        Comprobar(info.sinRevisarRed && !info.local && info.codigo == 0, "sin redes: la consulta no sale");

        // El recolector entero: el enlace queda con un estado claro, sin consultar
        wchar_t carpeta[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, carpeta);
        std::wstring archivo = std::wstring(carpeta) + L"atarraya-prueba-recolector-" + std::to_wstring(GetCurrentProcessId()) + L".json";
        {
            Recolector rec(archivo);
            rec.Agregar({ "http://1.1.1.1:9/prueba-sin-redes.zip" }, L"", "", "prueba");
            Comprobacion estado = Comprobacion::Pendiente;
            for (int i = 0; i < 100; i++)
            {
                rec.Tick();
                auto lista = rec.Lista();
                estado = lista.empty() ? Comprobacion::Pendiente : lista[0].estado;
                if (estado != Comprobacion::Pendiente && estado != Comprobacion::Comprobando) break;
                Sleep(50);
            }
            Comprobar(estado == Comprobacion::SinRevisar
                      && std::wstring(TextoComprobacion(estado)) == L"No se pudo revisar la red: se consulta al bajar",
                      "sin redes: el recolector dice «No se pudo revisar la red: se consulta al bajar»");
            rec.Detener();
        }
        DeleteFileW(archivo.c_str());
        red::SimularFallaAlLeerRedes(false);
        Comprobar(Revisar(afuera) == red::Destino::Afuera, "sin redes: cuando Windows vuelve a dar la lista, se consulta otra vez");
    }

    // Varias consultas a la vez: cada una lee las redes por su cuenta, sin nada compartido que pueda chocar
    void AlMismoTiempo()
    {
        std::vector<sockaddr_storage> propias;
        for (auto& r : red::LeerSubredesDeLaPc()) propias.push_back(DeSubred(r));
        auto afuera4 = Dir("8.8.8.8"), afuera6 = Dir("2001:4860:4860::8888"), casa = Dir("10.0.0.1");
        std::atomic<int> errores{ 0 }, revisiones{ 0 };
        std::vector<std::thread> hilos;
        for (int h = 0; h < 8; h++)
        {
            hilos.emplace_back([&] {
                for (int i = 0; i < 20; i++)
                {
                    if (Revisar(afuera4) != red::Destino::Afuera) errores++;
                    if (Revisar(afuera6) != red::Destino::Afuera) errores++;
                    if (Revisar(casa) != red::Destino::Local) errores++;
                    for (auto& s : propias)
                    {
                        if (Revisar(s) != red::Destino::Local) errores++;
                    }
                    revisiones += 3 + (int)propias.size();
                }
            });
        }
        for (auto& t : hilos) t.join();
        printf("   %d revisiones en 8 hilos a la vez\n", revisiones.load());
        Comprobar(errores == 0, "a la vez: 8 hilos revisan sin chocar ni equivocarse");

        // Y consultas enteras del recolector a la vez, hacia esta PC y la red de la casa: ninguna sale
        std::atomic<int> salieron{ 0 };
        std::vector<std::thread> consultas;
        for (int h = 0; h < 4; h++)
        {
            consultas.emplace_back([&] {
                for (const char* url : { "http://127.0.0.1:9/a.zip", "http://[::1]:9/b.zip", "http://10.0.0.1:9/c.zip" })
                {
                    InfoEnlace info;
                    ComprobarEnlace(url, "", info);
                    if (!info.local) salieron++;
                }
            });
        }
        for (auto& t : consultas) t.join();
        Comprobar(salieron == 0, "a la vez: 4 consultas del recolector juntas, ninguna sale hacia la red de la casa");
    }

    bool Tiene(const std::vector<Enlace>& v, const std::string& url, Tipo t)
    {
        for (auto& e : v)
        {
            if (e.url == url) return e.tipo == t;
        }
        return false;
    }

    void Fijas()
    {
        // Listas de enlaces
        auto r = DeTexto(L"Bajar esto:\r\n1. https://ejemplo.org/a.zip\r\n2) (https://ejemplo.org/b.iso).\n"
                         L"https://ejemplo.org/a.zip\nftp://ftp.ejemplo.org/c.tar.gz, y "
                         L"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=prueba\n"
                         L"https://www.youtube.com/watch?v=abc https://es.wikipedia.org/wiki/Atarraya_(red)\n"
                         L"xhttp://no.cuenta/1 http://\n");
        Comprobar(r.enlaces.size() == 5, "lista: 5 enlaces");
        Comprobar(r.enlaces.size() > 1 && r.enlaces[1] == "https://ejemplo.org/b.iso", "lista: sin paréntesis ni punto final");
        Comprobar(r.repetidos == 1, "lista: 1 repetido");
        Comprobar(r.noPermitidos == 1, "lista: YouTube no se agrega");
        Comprobar(r.enlaces.size() == 5 && r.enlaces[4] == "https://es.wikipedia.org/wiki/Atarraya_(red)", "lista: paréntesis de Wikipedia");
        Comprobar(r.enlaces.size() > 3 && EsMagnet(r.enlaces[3]), "lista: magnet");

        // Página
        std::string html = R"html(<!doctype html><html><head><title>Mis &amp; archivos</title>
            <base href="https://cdn.ejemplo.org/archivos/">
            <meta property="og:video" content="https://cdn.ejemplo.org/v/clip.mp4">
            <script>var p = {"src":"https:\/\/cdn.ejemplo.org\/v\/otro.webm","x":"https://ads.ejemplo.org/a.js"};</script>
            </head><body>
            <a href="manual.pdf">Manual</a> <A HREF='../programa.exe'>Programa</A>
            <a href="/acerca.html">Acerca</a> <a href="#arriba">Arriba</a> <a href="javascript:void(0)">x</a>
            <a href="mailto:hola@ejemplo.org">correo</a> <a href=sin%20comillas.zip>zip</a>
            <a href="https://youtu.be/abc">video ajeno</a>
            <a href="magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&amp;dn=Mi+torrent">magnet</a>
            <video src="pelicula?id=7"><source src="pelicula.mp4" type="video/mp4"><track src="subs.vtt"></video>
            <img src="foto.jpg"> <!-- <a href="comentado.zip">no</a> -->
            <a href="archivo con espacios.mp3">audio</a>
            </body></html>)html";
        std::wstring titulo;
        auto v = DeHtml(html, "https://ejemplo.org/pagina/index.html", &titulo);
        for (auto& e : v) printf("   %-12s %s\n", Utf8(TextoTipo(e.tipo)).c_str(), e.url.c_str());
        Comprobar(titulo == L"Mis & archivos", "página: título");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/archivos/manual.pdf", Tipo::Documento), "página: relativa con <base>");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/programa.exe", Tipo::Programa), "página: ../ y mayúsculas");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/acerca.html", Tipo::Pagina), "página: enlace a otra página");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/archivos/sin%20comillas.zip", Tipo::Comprimido), "página: sin comillas");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/v/clip.mp4", Tipo::Video), "página: og:video");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/v/otro.webm", Tipo::Video), "página: video dentro de un script");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/archivos/pelicula?id=7", Tipo::Video), "página: <video> sin extensión");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/archivos/subs.vtt", Tipo::Subtitulos), "página: subtítulos");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/archivos/foto.jpg", Tipo::Imagen), "página: imagen");
        Comprobar(Tiene(v, "https://cdn.ejemplo.org/archivos/archivo%20con%20espacios.mp3", Tipo::Audio), "página: espacios");
        Comprobar(Tiene(v, "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=Mi+torrent", Tipo::Torrent), "página: magnet");
        bool sinBasura = true;
        for (auto& e : v)
        {
            if (e.url.find("youtu") != std::string::npos || e.url.find("comentado") != std::string::npos || e.url.find("ads.") != std::string::npos
                || e.url.find("mailto") != std::string::npos || e.url.find("javascript") != std::string::npos || e.url.find('#') != std::string::npos)
                sinBasura = false;
        }
        Comprobar(sinBasura, "página: sin YouTube, comentarios, scripts .js, correos ni anclas");

        // Internet Archive
        std::string id;
        Comprobar(EsInternetArchive("https://archive.org/details/una_pelicula", id) && id == "una_pelicula", "IA: details");
        Comprobar(EsInternetArchive("https://www.archive.org/download/una_pelicula/", id) && id == "una_pelicula", "IA: download sin archivo");
        Comprobar(!EsInternetArchive("https://archive.org/download/una_pelicula/video.mp4", id), "IA: un archivo suelto no es lista");
        Comprobar(!EsInternetArchive("https://archive.org.malo.com/details/x", id), "IA: otro sitio");
        std::string json = R"({"files":[{"name":"video.mp4","source":"original","format":"h.264","size":"1048576"},
            {"name":"carpeta/sub título.ogv","source":"derivative","format":"Ogg Video","size":"2000"},
            {"name":"una_pelicula_meta.xml","source":"metadata","format":"Metadata"},
            {"name":"privado.mp4","source":"original","private":"true"},
            {"name":"../malo.mp4","source":"original"}],
            "metadata":{"title":["Una película"]}})";
        Pagina p;
        std::string error;
        Comprobar(DeInternetArchive(json, "una_pelicula", p, error) && p.enlaces.size() == 3, "IA: 3 archivos públicos");
        Comprobar(p.titulo == L"Una película", "IA: título");
        Comprobar(!p.enlaces.empty() && p.enlaces[0].tamano == 1048576 && p.enlaces[0].tipo == Tipo::Video, "IA: tamaño y tipo");
        Comprobar(p.enlaces.size() > 1 && p.enlaces[1].url == "https://archive.org/download/una_pelicula/carpeta/sub%20t%C3%ADtulo.ogv",
                  "IA: dirección codificada");
        Comprobar(p.enlaces.size() > 2 && p.enlaces[2].interno, "IA: metadatos al final y marcados");
        Comprobar(!DeInternetArchive("{}", "nada", p, error), "IA: elemento que no existe");

        // Sitios no permitidos
        Comprobar(NoPermitido("https://m.youtube.com/watch?v=1") && NoPermitido("https://rr3---sn.googlevideo.com/videoplayback")
                  && NoPermitido("https://youtu.be/x") && !NoPermitido("https://notyoutube.com/a.mp4"), "YouTube fuera");
        Comprobar(Sitio("https://usuario:clave@Ejemplo.ORG:8080/x") == "ejemplo.org", "sitio sin usuario ni puerto");
        Comprobar(NoPermitido("https://%79outube.com/watch?v=1") && NoPermitido("https://WWW.YOUTUBE.COM./x"), "YouTube escrito con %79 o con punto final");

        // Click'n'Load: la clave sin ejecutar JavaScript
        std::vector<uint8_t> clave;
        Comprobar(cnl::ClaveDeJk("function f(){ return '31323334353637383930393837363534';}", clave) && clave.size() == 16 && clave[0] == '1',
                  "CnL: jk de la página de JDownloader");
        Comprobar(cnl::ClaveDeJk("function f(){return \"3132333435363738\" + '3930393837363534'}", clave), "CnL: jk en dos trozos");
        Comprobar(!cnl::ClaveDeJk("function f(){ return atob('MTIz'); }", clave), "CnL: rechaza código");
        Comprobar(!cnl::ClaveDeJk("function f(){ return '3132'; }", clave), "CnL: rechaza clave corta");
        Comprobar(!cnl::ClaveDeJk("function f(){ return '31323334353637383930393837363534'; } alert(1)", clave), "CnL: rechaza lo que sigue");
        Comprobar(!cnl::ClaveDeJk("function f(){ return 'zz323334353637383930393837363534'; }", clave), "CnL: rechaza letras que no son hex");
        auto form = cnl::LeerFormulario("jk=function+f()%7B+return+'61'%3B%7D&crypted=ab+cd%2B&source=https%3A%2F%2Fejemplo.org%2Fc");
        Comprobar(form["jk"] == "function f(){ return '61';}" && form["crypted"] == "ab cd+" && form["source"] == "https://ejemplo.org/c",
                  "CnL: formulario");

        // Cifrar como lo haría el sitio (AES-128-CBC, la clave también es el vector inicial, relleno con ceros) y descifrar
        std::string claro = "https://ejemplo.org/a.zip\r\nhttps://ejemplo.org/b.zip\r\nhttps://ejemplo.org/a.zip";
        std::string relleno = claro + std::string(16 - claro.size() % 16, '\0');
        std::vector<uint8_t> k = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '9', '8', '7', '6', '5', '4' };
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_KEY_HANDLE hk = nullptr;
        BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
        BCryptGenerateSymmetricKey(alg, &hk, nullptr, 0, k.data(), 16, 0);
        UCHAR iv[16];
        memcpy(iv, k.data(), 16);
        std::string cifrado(relleno.size(), '\0');
        ULONG n = 0;
        BCryptEncrypt(hk, (PUCHAR)relleno.data(), (ULONG)relleno.size(), nullptr, iv, 16, (PUCHAR)cifrado.data(), (ULONG)cifrado.size(), &n, 0);
        BCryptDestroyKey(hk);
        BCryptCloseAlgorithmProvider(alg, 0);
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string base64;
        for (size_t i = 0; i < cifrado.size(); i += 3)
        {
            uint32_t w = (uint8_t)cifrado[i] << 16 | (i + 1 < cifrado.size() ? (uint8_t)cifrado[i + 1] << 8 : 0) | (i + 2 < cifrado.size() ? (uint8_t)cifrado[i + 2] : 0);
            base64 += b64[(w >> 18) & 63];
            base64 += b64[(w >> 12) & 63];
            base64 += i + 1 < cifrado.size() ? b64[(w >> 6) & 63] : '=';
            base64 += i + 2 < cifrado.size() ? b64[w & 63] : '=';
        }
        std::string sinMas = base64;
        for (auto& c : sinMas)
        {
            if (c == '+') c = ' ';   // como llega si el sitio no codifica los «+»
        }
        std::string texto;
        Comprobar(cnl::Descifrar(sinMas, k, texto) && texto == claro, "CnL: descifrar (y «+» que llegan como espacios)");
        auto lista = cnl::Enlaces(texto, 1000);
        Comprobar(lista.size() == 2, "CnL: enlaces sin repetir");
        Comprobar(!cnl::Descifrar("AAAA", k, texto), "CnL: rechaza datos que no son bloques de 16");
        std::vector<uint8_t> otra(16, 'x');
        Comprobar(!cnl::Descifrar(base64, otra, texto) || texto != claro, "CnL: con otra clave no sale lo mismo");

        // Recolector: paquetes y direcciones locales
        EnlaceRecolectado e1, e2, e3;
        e1.nombre = L"Mi película.part1.rar";
        e2.nombre = L"Mi película.part2.rar";
        e3.nombre = L"Mi película.r00";
        Comprobar(PaqueteDe(e1) == L"Mi película" && PaqueteDe(e2) == L"Mi película" && PaqueteDe(e3) == L"Mi película",
                  "recolector: partes en el mismo paquete");
        EnlaceRecolectado e4;
        e4.nombre = L"programa.7z.003";
        Comprobar(PaqueteDe(e4) == L"programa", "recolector: .7z.003");
        Comprobar(EsDireccionLocal("http://192.168.1.1/x.zip") && EsDireccionLocal("http://localhost:8080/a.mp4")
                  && EsDireccionLocal("http://nas/peli.mkv") && EsDireccionLocal("http://[::1]/a") && EsDireccionLocal("http://0x7f.1/a")
                  && !EsDireccionLocal("https://archive.org/download/x/y.mp4") && !EsDireccionLocal("https://cafe.be/a.zip"),
                  "recolector: direcciones de la red local");
        Comprobar(EnlaceParaRecolectar("https://ejemplo.org/a.zip") && EnlaceParaRecolectar("https://rapidgator.net/file/abc")
                  && !EnlaceParaRecolectar("https://ejemplo.org/noticia.html") && !EnlaceParaRecolectar("https://youtu.be/x")
                  && !EnlaceParaRecolectar("https://ejemplo.org/foto.jpg"),
                  "recolector: qué se toma del portapapeles");
    }
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (argc > 1)
    {
        Pagina p;
        std::string error;
        std::atomic<bool> parar{ false };
        if (!Buscar(Utf8(argv[1]), p, error, &parar))
        {
            printf("ERROR: %s\n", error.c_str());
            return 1;
        }
        printf("TÍTULO: %s\nINTERNET ARCHIVE: %s\nENLACES: %zu\n", Utf8(p.titulo).c_str(), p.internetArchive ? "sí" : "no", p.enlaces.size());
        for (auto& e : p.enlaces)
            printf("%-12s %12lld %s %s\n", Utf8(TextoTipo(e.tipo)).c_str(), (long long)e.tamano, e.interno ? "(interno)" : "", e.url.c_str());
        return 0;
    }
    Fijas();
    Redes();
    RedDeEstaPc();
    SinRedes();
    AlMismoTiempo();
    printf(fallas ? "\n%d FALLAS\n" : "\nTODO BIEN\n", fallas);
    return fallas ? 1 : 0;
}
