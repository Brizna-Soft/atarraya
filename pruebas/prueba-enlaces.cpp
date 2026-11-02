// Prueba de los enlaces sin ventana (no se publica).
//
//   prueba-enlaces             pruebas fijas (sin internet; con las direcciones de esta PC, sin mostrarlas)
//   prueba-enlaces <url>       baja esa página (o la lista de Internet Archive) y muestra sus enlaces

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <bcrypt.h>
#include <winioctl.h>

#include <deque>
#include <fstream>
#include <map>
#include <memory>

#include <nlohmann/json.hpp>
#include <shellapi.h>

#include "clicknload.hpp"
#include "contrasenas.hpp"
#include "deunaenuna.hpp"
#include "flujos.hpp"
#include "jicaro.hpp"
#include "nombres.hpp"
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

    // «De una en una»: emparejar las descargas que pasa el navegador con las páginas abiertas
    void DeUnaEnUnaPruebas()
    {
        Comprobar(NombreComparable(L"Mi Peli.part01.rar") == NombreComparable(L"mi_peli.PART01.rar")
                  && NombreComparable(L"peli.part1.rar.html") == NombreComparable(L"peli.part1.rar")
                  && NombreComparable(L"peli.part1.rar") != NombreComparable(L"peli.part01.rar"),
                  "de una en una: nombres comparables (mayúsculas, _ y .html)");
        Comprobar(MismoSitio("https://ddownload.com/abc/peli.rar", "https://s12.ddownload.com:8443/d/xyz/peli.rar")
                  && MismoSitio("https://ddownload.com/abc", "https://dl.ddl.to/x") && MismoSitio("https://rapidgator.net/file/1", "https://rg.to/2")
                  && MismoSitio("https://mega.co.nz/x", "https://mega.nz/y") && MismoSitio("https://www.ejemplo.co.uk/a", "https://cdn.ejemplo.co.uk/b")
                  && !MismoSitio("https://ddownload.com/a", "https://rapidgator.net/b") && !MismoSitio("https://uno.co.uk/a", "https://otro.co.uk/b")
                  && !MismoSitio("", "https://ddownload.com/a") && MismoSitio("http://127.0.0.1:8080/a", "http://127.0.0.1:9090/b"),
                  "de una en una: mismo sitio (sus servidores y sus otros dominios)");

        Comprobar(MismaPagina("https://ddownload.com/abc123def456", "https://www.ddownload.com/abc123def456/peli.part1.rar#arriba")
                      && MismaPagina("https://ddownload.com/abc123def456/", "https://ddownload.com/abc123def456")
                      && MismaPagina("https://1fichier.com/?abcdefgh12", "https://1fichier.com/?abcdefgh12&af=3")
                      && !MismaPagina("https://ddownload.com/", "https://ddownload.com/abc123def456")
                      && !MismaPagina("https://ddownload.com/abc123def456", "https://ddownload.com/zzz999zzz999")
                      && !MismaPagina("https://rapidgator.net/file", "https://rapidgator.net/file/aaa/x.html")
                      && !MismaPagina("https://otro.com/abc123def456", "https://ddownload.com/abc123def456"),
                  "de una en una: la misma página (con o sin el nombre; la portada sola no cuenta)");

        wchar_t carpeta[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, carpeta);
        std::wstring base = std::wstring(carpeta) + L"atarraya-prueba-" + std::to_wstring(GetCurrentProcessId());
        std::wstring archivo = base + L"-cola.json";
        auto parte = [](const char* url) {
            Parte p;
            p.url = url;
            p.nombre = NombreDeUrl(url);
            p.paquete = L"Peli";
            p.carpeta = L"C:\\Descargas\\Peli";
            return p;
        };
        auto es = [](const Emparejamiento& e, int cual, bool segura) { return e.parte == cual && (cual < 0 || e.segura == segura); };
        const std::string p1 = "https://ddownload.com/aaa111bbb/peli.part1.rar", p3 = "https://ddownload.com/eee333fff";

        DeUnaEnUna cola(archivo);
        Comprobar(cola.Agregar({ parte(p1.c_str()), parte("https://ddownload.com/ccc222ddd/peli.part2.rar"), parte(p3.c_str()) })
                      && cola.AbrirSiguiente() == 0 && cola.Actual() == 0,
                  "de una en una: se guarda la cola y se abre la primera");
        // El nombre solo cuenta si la descarga viene del sitio de esa parte
        Comprobar(es(cola.Emparejar("https://otro-sitio.org/archivos/peli.part1.rar", "https://otro-sitio.org/bajar",
                                    "https://otro-sitio.org/bajar", L"peli.part1.rar"), -1, false),
                  "de una en una: otro sitio con el mismo nombre no se toma (pregunta como siempre)");
        Comprobar(es(cola.Emparejar("https://files.cdn-ejemplo.net/x/peli.part1.rar", "https://ddownload.com/", "", L""), 0, true)
                      && es(cola.Emparejar("https://s7.ddownload.com/d/token/peli.part1.rar", "", "", L""), 0, true),
                  "de una en una: con su nombre y desde su sitio (aunque el archivo venga de otro servidor), se toma");
        // El mismo nombre, pero desde otra página de ese sitio (por la pestaña o por un referer con página): se pregunta
        Comprobar(es(cola.Emparejar("https://s7.ddownload.com/d/token/peli.part1.rar", "https://ddownload.com/",
                                    "https://ddownload.com/zzz999yyy/otra-pagina", L""), 0, false)
                      && es(cola.Emparejar("https://s7.ddownload.com/d/token/peli.part1.rar", "https://ddownload.com/zzz999yyy/otra-pagina", "", L""),
                            0, false),
                  "de una en una: el mismo nombre desde otra página del sitio: pregunta «¿Es la parte 1 de 3?»");
        Comprobar(es(cola.Emparejar("https://s7.ddownload.com/d/token/peli.part1.rar", "https://ddownload.com/", p1, L""), 0, true),
                  "de una en una: el mismo nombre desde su propia página: se toma");
        // Sin el nombre, solo si viene de la página que Atarraya abrió para esa parte
        Comprobar(es(cola.Emparejar("https://s7.ddownload.com/d/token/descarga", "https://ddownload.com/", p1, L""), 0, true)
                      && es(cola.Emparejar("https://s7.ddownload.com/d/token/descarga", p1, "", L""), 0, true),
                  "de una en una: sin el nombre, desde la página de la parte (pestaña o referer), se toma");
        Comprobar(es(cola.Emparejar("https://s7.ddownload.com/d/token/otra-cosa.zip", "https://ddownload.com/",
                                    "https://ddownload.com/zzz999yyy/otra-cosa.zip", L""), 0, false),
                  "de una en una: el mismo sitio desde otra página: se pregunta");
        Comprobar(es(cola.Emparejar("https://s7.ddownload.com/d/token/peli.part2.rar", "", p1, L""), 1, false),
                  "de una en una: desde la página de una parte pero con el nombre de otra: se pregunta por esa otra");
        Comprobar(es(cola.Emparejar("https://cdn.otro.org/f/peli.zip", "", "", L""), -1, false),
                  "de una en una: sin sitio ni página de la cola, sigue como cualquier descarga");
        cola.Tomar(0, 101);
        Comprobar(cola.Actual() == 0 && !cola.HayAbierta() && es(cola.Emparejar("https://s7.ddownload.com/d/t/descarga", "", p1, L""), -1, false),
                  "de una en una: ya tomada, no recibe otra");
        Comprobar(cola.Terminar(999, true) == -1 && cola.Terminar(101, true) == 0 && cola.Partes()[0].estado == EstadoParte::Lista,
                  "de una en una: al terminar su descarga, la parte queda lista");
        // Dos abiertas a la vez: cada descarga va a la suya
        Comprobar(cola.AbrirSiguiente() == 1 && cola.AbrirSiguiente() == 2 && cola.AbrirSiguiente() == -1,
                  "de una en una: abrir la siguiente ahora");
        Comprobar(es(cola.Emparejar("https://s1.ddownload.com/d/t/descarga", "", p3 + "/peli.part3.rar", L""), 2, true)
                      && es(cola.Emparejar("https://s1.ddownload.com/d/t/peli.part2.rar", "https://ddownload.com/", "", L""), 1, true),
                  "de una en una: dos descargas a la vez, cada una a su parte");
        cola.Tomar(2, 103);
        cola.Tomar(1, 102);
        Comprobar(cola.Terminar(103, false) == 2 && cola.Partes()[2].estado == EstadoParte::Fallo && !cola.Terminada(),
                  "de una en una: una que falla no corta la cola");
        DeUnaEnUna otra(archivo);
        cola.Guardar();
        otra.Cargar();
        Comprobar(otra.Partes().size() == 3 && otra.Partes()[1].estado == EstadoParte::Tomada && otra.Partes()[1].descarga == 102
                      && otra.Partes()[0].estado == EstadoParte::Lista && otra.Partes()[2].carpeta == L"C:\\Descargas\\Peli",
                  "de una en una: al volver a abrir Atarraya, sigue donde iba");
        Comprobar(otra.Terminar(102, true) == 1 && otra.Terminada() && otra.Cuantas(EstadoParte::Lista) == 2
                      && otra.Sobrantes().size() == 1 && otra.Sobrantes()[0].url == p3,
                  "de una en una: termina la cola y la que falló es la que vuelve");

        // Si la cola no se puede guardar, no cambia: los enlaces no salen del recolector
        DeUnaEnUna sinLugar(base + L"-carpeta-que-no-existe\\cola.json");
        Comprobar(!sinLugar.Agregar({ parte(p1.c_str()) }) && sinLugar.Partes().empty(),
                  "de una en una: si no se puede guardar la cola, queda como estaba");

        // Volver al recolector sin perder nada: primero se guarda el recolector y después la cola vacía
        std::wstring archivoCola = base + L"-cola2.json", archivoRecolector = base + L"-recolector.json";
        const std::string a = "https://rapidgator.net/file/x1/a.rar.html", b = "https://rapidgator.net/file/x2/b.rar.html";
        {
            DeUnaEnUna q(archivoCola);
            q.Agregar({ parte(a.c_str()), parte(b.c_str()) });
            q.Saltar(q.AbrirSiguiente());
            q.Guardar();
            Recolector rec(archivoRecolector);
            bool paso1 = GuardarEnRecolector(q.Sobrantes(), rec);
            // Atarraya se cierra de golpe justo aquí: al volver a abrir, las páginas están en el recolector (y siguen en la cola)
            Recolector recDespues(archivoRecolector);
            recDespues.Cargar();
            DeUnaEnUna qDespues(archivoCola);
            qDespues.Cargar();
            Comprobar(paso1 && recDespues.Contiene(a) && recDespues.Contiene(b) && qDespues.Partes().size() == 2,
                      "de una en una: un cierre entre los dos guardados no pierde ninguna página");
            bool comoSitio = true;
            for (const auto& e : recDespues.Lista()) comoSitio = comoSitio && e.estado == Comprobacion::Sitio && e.paquete == L"Peli";
            Comprobar(q.Vaciar() && comoSitio, "de una en una: después se vacía la cola; vuelven como páginas de sitios, en su paquete");
            DeUnaEnUna qFinal(archivoCola);
            qFinal.Cargar();
            Comprobar(qFinal.Partes().empty(), "de una en una: la cola vacía queda guardada");
        }
        {
            // El recolector no se puede guardar: la cola queda como estaba
            Recolector recSinLugar(base + L"-carpeta-que-no-existe\\recolector.json");
            DeUnaEnUna q(archivoCola);
            q.Agregar({ parte(a.c_str()) });
            Comprobar(!GuardarEnRecolector(q.Sobrantes(), recSinLugar) && q.Partes().size() == 1,
                      "de una en una: si el recolector no se puede guardar, la cola no se vacía");
        }
        std::wstring archivoLista = base + L"-lista.json", archivoRec = base + L"-recolector3.json";
        {
            // Del recolector a la lista de descargas sin perder nada: primero la lista se guarda con la descarga nueva y
            // recién después el enlace sale del recolector
            const std::string url = "https://ejemplo.com/peli.part1.rar";
            Recolector rec(archivoRec);
            rec.Agregar({ url }, L"Peli", "", "prueba");
            rec.Guardar();
            uint64_t enlace = rec.Lista().front().id;
            Motor m(archivoLista);
            DatosDescarga d;
            d.url = url;
            d.carpeta = base;
            m.Agregar(d, false);
            auto salen = SalenDelRecolector({ enlace }, { enlace }, [&]() { return m.Guardar(); });
            // Atarraya se cierra de golpe justo aquí, antes de guardar el recolector sin el enlace
            Motor mDespues(archivoLista);
            mDespues.Cargar();
            Recolector recDespues(archivoRec);
            recDespues.Cargar();
            Comprobar(salen == std::vector<uint64_t>{ enlace } && mDespues.BuscarPorUrl(url) != 0 && recDespues.Contiene(url),
                      "recolector: un cierre justo después de pasar un enlace a la lista no lo pierde (queda en los dos lados)");
            rec.Quitar(salen);
            rec.Guardar();
            Recolector recFinal(archivoRec);
            recFinal.Cargar();
            Comprobar(!recFinal.Contiene(url), "recolector: después el enlace sale del recolector");
            // Si la lista no se pudo guardar, el enlace se queda también en el recolector (los demás salen)
            Motor listaSinLugar(base + L"-carpeta-que-no-existe\\lista.json");
            listaSinLugar.Agregar(d, false);
            Comprobar(SalenDelRecolector({ enlace, 77 }, { enlace }, [&]() { return listaSinLugar.Guardar(); }) == std::vector<uint64_t>{ 77 },
                      "recolector: si la lista no se pudo guardar, el enlace se queda en el recolector");
        }
        for (const auto& f : { archivoLista, archivoRec })
        {
            DeleteFileW(f.c_str());
            DeleteFileW((f + L".tmp").c_str());
        }
        std::wstring archivoCola3 = base + L"-cola3.json", archivoCola4 = base + L"-cola4.json";
        {
            // Terminó con una saltada y una con error, y el recolector no se pudo guardar: al volver a abrir siguen ahí
            DeUnaEnUna q(archivoCola3);
            q.Agregar({ parte(a.c_str()), parte(b.c_str()), parte(p1.c_str()) });
            q.Saltar(q.AbrirSiguiente());
            int conError = q.AbrirSiguiente(), bien = q.AbrirSiguiente();
            q.Tomar(conError, 201);
            q.Tomar(bien, 202);
            q.Terminar(201, false);
            q.Terminar(202, true);
            q.Guardar();
            Recolector recSinLugar(base + L"-carpeta-que-no-existe\\recolector2.json");
            bool llegaron = GuardarEnRecolector(q.Sobrantes(), recSinLugar);
            DeUnaEnUna alVolver(archivoCola3);
            alVolver.Cargar();
            Comprobar(!llegaron && q.Terminada() && alVolver.Partes().size() == 3 && alVolver.Sobrantes().size() == 2 && alVolver.Terminada(),
                      "de una en una: si el recolector no se pudo guardar, al volver a abrir las saltadas y las que fallaron siguen en la cola");
            // Una cola que terminó con todo bajado no queda para la próxima vez
            DeUnaEnUna toda(archivoCola4);
            toda.Agregar({ parte(p1.c_str()) });
            toda.Tomar(toda.AbrirSiguiente(), 301);
            toda.Terminar(301, true);
            toda.Guardar();
            DeUnaEnUna despues(archivoCola4);
            despues.Cargar();
            Comprobar(despues.Partes().empty(), "de una en una: una cola que terminó con todo bajado no queda para la próxima vez");
        }
        for (const auto& f : { archivo, archivoCola, archivoRecolector, archivoCola3, archivoCola4 }) DeleteFileW(f.c_str());
    }

    // El sitio respondió con una página en vez del archivo (como la página de ddownload guardada como .rar), y los
    // enlaces de sitios de descargas que se agregan a mano
    void PaginaEnVezDeArchivo()
    {
        Comprobar(RespondioPagina("text/html; charset=UTF-8", "", L"Juego-elamigos.part1.rar")
                      && RespondioPagina("application/xhtml+xml", "", L"pelicula.mp4") && RespondioPagina("TEXT/HTML", "", L"setup.exe")
                      && RespondioPagina("text/html", "", L"disco.iso") && RespondioPagina("text/html", "", L"peli.7z.001"),
                  "página en vez del archivo: un .rar, un video, un .exe o un .iso que llega como página no se guarda");
        Comprobar(!RespondioPagina("text/html", "attachment; filename=\"PELI.rar\"", L"peli.rar")
                      && !RespondioPagina("application/octet-stream", "", L"peli.rar") && !RespondioPagina("application/x-rar-compressed", "", L"peli.rar"),
                  "página en vez del archivo: un archivo de verdad (o uno que el servidor nombra como el esperado) se baja como siempre");
        Comprobar(RespondioPagina("text/html", "inline; filename=error.html", L"peli.rar")
                      && RespondioPagina("text/html", "attachment; filename=\"error.htm\"", L"peli.rar")
                      && RespondioPagina("text/html", "attachment; filename=\"otro.zip\"", L"peli.rar") && RespondioPagina("text/html", "attachment", L"peli.rar"),
                  "página en vez del archivo: «filename=error.html», otro nombre o un adjunto sin nombre no la salvan");
        Comprobar(!RespondioPagina("text/html", "", L"index.html") && !RespondioPagina("text/html", "inline; filename=error.html", L"error.html")
                      && !RespondioPagina("text/html", "", L"noticia.php") && !RespondioPagina("text/html", "", L"descarga")
                      && !RespondioPagina("text/html", "", L""),
                  "página en vez del archivo: si se pidió una página (o no se sabe), se guarda la página");
        Comprobar(VaAlNavegador("https://ddownload.com/abc123def456/Juego-elamigos.part1.rar", false)
                      && VaAlNavegador("https://rapidgator.net/file/0123456789abcdef/peli.rar.html", false)
                      && VaAlNavegador("https://1fichier.com/?abcdefghij", false),
                  "agregar enlace: la página de un sitio de descargas va al navegador, de una en una");
        Comprobar(!VaAlNavegador("https://s12.ddownload.com/d/token/Juego-elamigos.part1.rar", true)
                      && !VaAlNavegador("https://ejemplo.org/peli.rar", false)
                      && !VaAlNavegador("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567", false),
                  "agregar enlace: lo que pasa el navegador y los enlaces de otros sitios se bajan como siempre");
    }

    // Un servidor de prueba en 127.0.0.1 (en un puerto libre): cada pedido recibe lo que diga «responder», según su
    // número (desde 1) y su cabecera Range. Lo que devuelve se manda tal cual y se cierra la conexión.
    class ServidorDePrueba
    {
    public:
        explicit ServidorDePrueba(std::function<std::string(int, const std::string&)> responder) : m_responder(std::move(responder))
        {
            Iniciar();
        }
        // Otra forma: lo que se responde depende de la ruta pedida («/hls/lista.m3u8») y de su Range
        struct PorRuta
        {
            std::function<std::string(const std::string&, const std::string&)> f;
        };
        // «ip»: otra dirección de esta PC donde escuchar (por ejemplo «127.0.0.2»)
        explicit ServidorDePrueba(PorRuta porRuta, const char* ip = nullptr) : m_porRuta(std::move(porRuta.f)), m_ip(ip)
        {
            Iniciar();
        }
        int Pedidos() const { return m_pedidos; }

    private:
        void Iniciar()
        {
            m_escucha = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (m_ip) inet_pton(AF_INET, m_ip, &a.sin_addr);
            bind(m_escucha, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            listen(m_escucha, 8);
            int largo = sizeof(a);
            getsockname(m_escucha, reinterpret_cast<sockaddr*>(&a), &largo);
            m_puerto = ntohs(a.sin_port);
            m_hilo = std::thread([this]() { Atender(); });
        }

    public:
        ~ServidorDePrueba()
        {
            m_parar = true;
            m_hilo.join();
            closesocket(m_escucha);
        }
        int Puerto() const { return m_puerto; }

    private:
        void Atender()
        {
            while (!m_parar)
            {
                fd_set listos;
                FD_ZERO(&listos);
                FD_SET(m_escucha, &listos);
                timeval espera{ 0, 100000 };
                if (select(0, &listos, nullptr, nullptr, &espera) <= 0) continue;
                SOCKET c = accept(m_escucha, nullptr, nullptr);
                if (c == INVALID_SOCKET) continue;
                std::string pedido;
                char buf[4096];
                while (pedido.find("\r\n\r\n") == std::string::npos && pedido.size() < 65536)
                {
                    int n = recv(c, buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    pedido.append(buf, n);
                }
                std::string bajo = pedido, rango;
                std::transform(bajo.begin(), bajo.end(), bajo.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                auto p = bajo.find("\r\nrange:");
                if (p != std::string::npos)
                {
                    auto fin = pedido.find("\r\n", p + 2);
                    rango = pedido.substr(p + 8, fin - (p + 8));
                    while (!rango.empty() && rango.front() == ' ') rango.erase(rango.begin());
                }
                std::string respuesta;
                if (m_porRuta)
                {
                    // «GET /ruta HTTP/1.1»
                    auto e1 = pedido.find(' ');
                    auto e2 = e1 == std::string::npos ? std::string::npos : pedido.find(' ', e1 + 1);
                    std::string ruta = e2 == std::string::npos ? std::string() : pedido.substr(e1 + 1, e2 - e1 - 1);
                    ++m_pedidos;
                    respuesta = m_porRuta(ruta, rango);
                }
                else respuesta = m_responder(++m_pedidos, rango);
                for (size_t enviado = 0; enviado < respuesta.size();)
                {
                    int n = send(c, respuesta.data() + enviado, (int)std::min<size_t>(respuesta.size() - enviado, 65536), 0);
                    if (n <= 0) break;
                    enviado += n;
                }
                shutdown(c, SD_SEND);
                closesocket(c);
            }
        }

        std::function<std::string(int, const std::string&)> m_responder;
        std::function<std::string(const std::string&, const std::string&)> m_porRuta;
        const char* m_ip = nullptr;
        SOCKET m_escucha = INVALID_SOCKET;
        int m_puerto = 0;
        std::atomic<int> m_pedidos{ 0 };
        std::atomic<bool> m_parar{ false };
        std::thread m_hilo;
    };

    // Baja «/archivos/peli.rar» con el motor de verdad, con una conexión, y espera a que termine o se detenga
    Foto BajarConMotor(int puerto, const std::wstring& carpeta, std::wstring& parcial, std::wstring& final)
    {
        Motor m(carpeta + L"\\estado.json");
        DatosDescarga d;
        d.url = "http://127.0.0.1:" + std::to_string(puerto) + "/archivos/peli.rar";
        d.carpeta = carpeta;
        d.conexiones = 1;
        uint64_t id = m.Agregar(d, true);
        Foto f;
        for (int i = 0; i < 300; i++)
        {
            m.Tick();
            if (m.Buscar(id, f) && (f.datos.estado == Estado::Error || f.datos.estado == Estado::Completada)) break;
            Sleep(100);
        }
        parcial = m.RutaParcial(id);
        final = carpeta + L"\\peli.rar";
        m.Detener();
        return f;
    }

    // Una parte, o un reintento después de que se cortó la conexión, recibe una página en vez del archivo (por
    // ejemplo, porque el enlace venció): se para sin escribirla y queda con el error de siempre
    void PaginaEnUnaParte()
    {
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        std::wstring carpeta = std::wstring(temporal) + L"atarraya-prueba-motor-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(carpeta.c_str(), nullptr);
        SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", carpeta.c_str());   // el registro de esta prueba va aquí también
        const std::string pagina = "<!doctype html><html><body>El enlace vencio: resuelve el captcha otra vez.</body></html>";
        auto tipo = [](const char* estado, const std::string& cabeceras, const std::string& cuerpo) {
            return std::string("HTTP/1.1 ") + estado + "\r\n" + cabeceras + "Content-Length: " + std::to_string(cuerpo.size())
                 + "\r\nConnection: close\r\n\r\n" + cuerpo;
        };
        const std::string sondeo = tipo("206 Partial Content", "Content-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\n"
                                                             "Content-Range: bytes 0-0/200000\r\n", "A");
        auto sinHtml = [](const std::wstring& ruta) {
            std::string datos;
            LeerArchivo(ruta, datos);
            return datos.find("<html") == std::string::npos && datos.find("vencio") == std::string::npos;
        };
        {
            // La consulta dice «archivo», pero la parte recibe una página
            ServidorDePrueba s([&](int n, const std::string&) { return n == 1 ? sondeo : tipo("200 OK", "Content-Type: text/html; charset=UTF-8\r\n", pagina); });
            std::wstring parcial, final;
            Foto f = BajarConMotor(s.Puerto(), carpeta, parcial, final);
            Comprobar(f.datos.estado == Estado::Error && f.datos.error == TextoRespondioPagina && !ExisteArchivo(final) && sinHtml(parcial),
                      "motor: una parte que recibe una página se para sin escribirla y avisa");
        }
        for (const wchar_t* f : { L"\\estado.json", L"\\peli.rar.atarraya" }) DeleteFileW((carpeta + f).c_str());
        {
            // La parte empieza bien, se corta a la mitad, y el reintento recibe una página
            std::string mitad = std::string("HTTP/1.1 206 Partial Content\r\nContent-Type: application/octet-stream\r\n"
                                            "Content-Range: bytes 0-199999/200000\r\nContent-Length: 200000\r\nConnection: close\r\n\r\n")
                              + std::string(100000, 'A');
            ServidorDePrueba s([&](int n, const std::string&) {
                if (n == 1) return sondeo;
                if (n == 2) return mitad;   // se corta antes de tiempo
                return tipo("200 OK", "Content-Type: text/html\r\nContent-Disposition: inline; filename=error.html\r\n", pagina);
            });
            std::wstring parcial, final;
            Foto f = BajarConMotor(s.Puerto(), carpeta, parcial, final);
            std::string datos;
            LeerArchivo(parcial, datos);
            bool loBueno = datos.size() >= 100000 && std::all_of(datos.begin(), datos.begin() + 100000, [](char c) { return c == 'A'; });
            Comprobar(f.datos.estado == Estado::Error && f.datos.error == TextoRespondioPagina && !ExisteArchivo(final) && loBueno && sinHtml(parcial)
                          && f.hecho == 100000,
                      "motor: un reintento que recibe una página (con «filename=error.html») no la escribe: queda lo bueno y avisa");
        }
        SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", nullptr);
        std::error_code e;
        std::filesystem::remove_all(carpeta, e);
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
        cnl::Campos form;
        Comprobar(cnl::LeerFormulario("", "jk=function+f()%7B+return+'61'%3B%7D&crypted=ab+cd%2B&source=https%3A%2F%2Fejemplo.org%2Fc", form).empty()
                      && cnl::Valor(form, "jk") == "function f(){ return '61';}" && cnl::Valor(form, "crypted") == "ab cd+"
                      && cnl::Valor(form, "source") == "https://ejemplo.org/c",
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
        Comprobar(lista.enlaces.size() == 2 && lista.repetidos == 1 && lista.llegaron == 3, "CnL: enlaces sin repetir");
        // Como pyLoad: el base64 codificado otra vez («%2B», «%2F», «%3D») también se descifra
        std::string porcentajes;
        for (char c : base64)
        {
            if (c == '+' || c == '/' || c == '=') porcentajes += c == '+' ? "%2B" : c == '/' ? "%2F" : "%3D";
            else porcentajes += c;
        }
        Comprobar(porcentajes != base64 && cnl::Descifrar(porcentajes, k, texto) && texto == claro,
                  "CnL: descifrar el base64 codificado dos veces");
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

    // 0.4.1: cada juego de un paquete se descomprime apenas está completo, sin esperar al resto, y una sola vez
    void JuegosPruebas()
    {
        using namespace hnd::jicaro;
        const std::wstring c = L"C:\\Descargas\\Lote";
        uint64_t siguiente = 1;
        auto descarga = [&](const wchar_t* nombre, bool bajada, const std::string& grupo = "g1") {
            Foto f;
            f.datos.id = siguiente++;
            f.datos.carpeta = c;
            f.datos.nombre = nombre;
            f.datos.grupo = grupo;
            f.datos.estado = bajada ? Estado::Completada : Estado::Descargando;
            return f;
        };
        auto parte = [&](const char* url, const std::string& grupo = "g1") {
            Parte p;
            p.url = url;
            p.nombre = NombreDeUrl(url);
            p.carpeta = c;
            p.grupo = grupo;
            p.estado = EstadoParte::Esperando;
            return p;
        };
        auto juego = [](const JuegosDelPaquete& j, const wchar_t* nombre) -> const Juego* {
            for (const auto& x : j.juegos)
            {
                if (x.nombre == nombre) return &x;
            }
            return nullptr;
        };

        // Aquí cada comprimido en partes dice que su última parte es la última (ver JuegosEnPartesPruebas para cuando no)
        auto siEs = [](const std::vector<std::wstring>&, wchar_t) { return Ultima::Es; };

        // Juegos de cada tipo en un mismo paquete
        std::vector<Foto> l = { descarga(L"A.part1.rar", true), descarga(L"A.part2.rar", true), descarga(L"B.rar", true),
                                descarga(L"B.r00", true), descarga(L"B.r01", false), descarga(L"C.7z.001", true), descarga(L"C.7z.002", true),
                                descarga(L"D.z01", true), descarga(L"D.zip", false), descarga(L"E.zip", true), descarga(L"F.7z", true),
                                descarga(L"leeme.nfo", false), descarga(L"G.part1.rar", true, "otro") };
        auto j = Juegos("g1", l, {}, siEs);
        const Juego *a = juego(j, L"A"), *b = juego(j, L"B"), *cc = juego(j, L"C"), *dd = juego(j, L"D"), *e = juego(j, L"E"),
                    *f = juego(j, L"F");
        Comprobar(j.juegos.size() == 6 && a && b && cc && dd && e && f && !juego(j, L"G") && j.dudosas == 0,
                  "juegos: .part1.rar, .rar + .r00, .7z.001, .zip + .z01, un .zip y un .7z enteros; nada de otro paquete");
        Comprobar(a && j.Completo(*a) && a->partes.size() == 2 && a->partes[0].ruta == c + L"\\A.part1.rar" && cc && j.Completo(*cc)
                      && cc->partes[0].ruta == c + L"\\C.7z.001" && e && j.Completo(*e) && f && j.Completo(*f),
                  "juegos: uno completo se descomprime ya, aunque al paquete le falten otros (la primera parte abre el juego)");
        Comprobar(b && !j.Completo(*b) && b->faltan == 1 && b->partes.size() == 2 && b->partes[0].ruta == c + L"\\B.rar",
                  "juegos: al de .rar + .r00 + .r01 le falta la .r01: espera");
        Comprobar(dd && !j.Completo(*dd) && dd->faltan == 1,
                  "juegos: un ZIP partido espera a su .zip (es el que se abre)");
        l[8].datos.estado = Estado::Completada;   // llegó D.zip
        auto j2 = Juegos("g1", l, {}, siEs);
        const Juego* d2 = juego(j2, L"D");
        Comprobar(d2 && j2.Completo(*d2) && d2->partes.size() == 2 && d2->partes[0].ruta == c + L"\\D.zip",
                  "juegos: con su .zip, el ZIP partido está completo y se abre por el .zip");

        // De «de una en una»: lo que todavía no bajó también cuenta
        {
            std::vector<Foto> bajadas = { descarga(L"peli.part1.rar", true), descarga(L"otra.part1.rar", true), descarga(L"otra.part2.rar", true) };
            std::vector<Parte> cola = { parte("https://ddownload.com/x1x1x1x1/peli.part2.rar"), parte("https://rapidgator.net/file/abc/peli.part3.rar.html") };
            auto jc = Juegos("g1", bajadas, cola, siEs);
            const Juego *peli = juego(jc, L"peli"), *otra = juego(jc, L"otra");
            Comprobar(peli && !jc.Completo(*peli) && peli->faltan == 2 && otra && jc.Completo(*otra),
                      "juegos: las partes que faltan en la cola (también «.rar.html») hacen esperar a su juego, no a los demás");
            cola.push_back(parte("https://1fichier.com/?k3k3k3k3"));
            auto jd = Juegos("g1", bajadas, cola, siEs);
            const Juego* otra2 = juego(jd, L"otra");
            Comprobar(jd.dudosas == 1 && otra2 && !jd.Completo(*otra2),
                      "juegos: mientras falte una de la que no se sabe el nombre, ningún juego está completo (podría ser suya)");
            cola.back() = parte("https://ddownload.com/n4n4n4n4/leeme.nfo");
            auto je = Juegos("g1", bajadas, cola, siEs);
            Comprobar(je.dudosas == 0 && juego(je, L"otra") && je.Completo(*juego(je, L"otra")),
                      "juegos: una que falta y no es un comprimido («.nfo») no hace esperar a nadie");
        }
        // Una sola vez: queda anotado con cada descarga del juego
        {
            std::vector<Foto> bajadas = { descarga(L"uno.part1.rar", true), descarga(L"uno.part2.rar", true) };
            bajadas[1].datos.jicaro = EstadoJicaro::Hecho;
            auto jf = Juegos("g1", bajadas, {}, siEs);
            bajadas[1].datos.jicaro = EstadoJicaro::EnCurso;
            auto jg = Juegos("g1", bajadas, {}, siEs);
            Comprobar(jf.juegos.size() == 1 && jf.juegos[0].jicaro == EstadoJicaro::Hecho && jg.juegos[0].jicaro == EstadoJicaro::EnCurso
                          && Juegos("", bajadas, {}).juegos.empty(),
                      "juegos: un juego que ya se le pasó a Jícaro (o que está en curso) no se descomprime otra vez solo");
        }
        // El .zip de uno partido no es un comprimido suelto
        {
            std::string entero = std::string(100, 'x') + std::string("PK\x05\x06\x00\x00\x00\x00", 8) + std::string(14, '\0');
            std::string partido = std::string(100, 'x') + std::string("PK\x05\x06\x02\x00\x00\x00", 8) + std::string(14, '\0');
            Comprobar(!EsZipPartido(entero) && EsZipPartido(partido) && !EsZipPartido(std::string("PK", 2)) && EsComprimidoSuelto(L"datos.zip")
                          && !EsComprimidoSuelto(L"datos.z01"),
                      "juegos: el .zip de un ZIP partido (su índice dice el número de trozo) no se toma como un .zip suelto");
        }
    }

    // 0.4.1 (revisión): un juego en partes está completo solo con todas sus partes, sin huecos y sabiendo cuál es la
    // última (si no se sabe, cuando termina todo el paquete); Jícaro nunca junta partes de otro paquete; lo que quedó «en
    // curso» se retoma al abrir; y el reintento con la contraseña no usa lo que ya se movió
    void JuegosEnPartesPruebas()
    {
        using namespace hnd::jicaro;
        namespace fs = std::filesystem;
        const std::wstring c = L"C:\\Descargas\\Lote";
        uint64_t siguiente = 500;
        auto descarga = [&](const std::wstring& carpeta, const wchar_t* nombre, bool bajada, const std::string& grupo = "g1") {
            Foto f;
            f.datos.id = siguiente++;
            f.datos.carpeta = carpeta;
            f.datos.nombre = nombre;
            f.datos.grupo = grupo;
            f.datos.estado = bajada ? Estado::Completada : Estado::Descargando;
            return f;
        };
        auto juego = [](const JuegosDelPaquete& j, const wchar_t* nombre) -> const Juego* {
            for (const auto& x : j.juegos)
            {
                if (x.nombre == nombre) return &x;
            }
            return nullptr;
        };
        auto con = [](Ultima u) { return [u](const std::vector<std::wstring>&, wchar_t) { return u; }; };

        // Huecos: .part1 y .part3 sin .part2 (y nada más baja): no está completo, y como ya terminó el paquete, se avisa
        {
            std::vector<Foto> l = { descarga(c, L"A.part1.rar", true), descarga(c, L"A.part3.rar", true) };
            auto j = Juegos("g1", l, {}, con(Ultima::Es));
            const Juego* a = juego(j, L"A");
            Comprobar(a && a->huecos && !j.Completo(*a) && j.terminado && a->incompleto,
                      "juegos en partes: .part1 y .part3 sin .part2 no están completos (falta una en el medio) y se avisa");
            std::vector<Foto> z = { descarga(c, L"D.zip", true), descarga(c, L"D.z01", true), descarga(c, L"D.z03", true) };
            std::vector<Foto> v = { descarga(c, L"B.r00", true), descarga(c, L"B.r01", true) };
            auto jz = Juegos("g1", z, {}, con(Ultima::Es));
            auto jv = Juegos("g1", v, {}, con(Ultima::Es));
            Comprobar(juego(jz, L"D") && juego(jz, L"D")->huecos && !jz.Completo(*juego(jz, L"D")) && juego(jv, L"B")
                          && juego(jv, L"B")->huecos && !jv.Completo(*juego(jv, L"B")),
                      "juegos en partes: un ZIP partido sin su .z02, o un RAR viejo sin su .rar, tampoco");
            z.push_back(descarga(c, L"D.z02", true));
            auto jz2 = Juegos("g1", z, {}, con(Ultima::Es));
            const Juego* d = juego(jz2, L"D");
            Comprobar(d && !d->huecos && jz2.Completo(*d) && d->partes.size() == 4 && d->partes[0].ruta == c + L"\\D.zip",
                      "juegos en partes: el ZIP partido con su .zip y todas las .zNN sin huecos está completo (Jícaro lo abre)");
        }
        // Si no se sabe cuál es la última, espera a que termine todo el paquete
        {
            std::vector<Foto> l = { descarga(c, L"X.part1.rar", true), descarga(c, L"X.part2.rar", true), descarga(c, L"Y.zip", false) };
            auto j1 = Juegos("g1", l, {}, con(Ultima::NoSe));
            l[2].datos.estado = Estado::Completada;
            auto j2 = Juegos("g1", l, {}, con(Ultima::NoSe));
            auto j3 = Juegos("g1", l, {}, con(Ultima::NoEs));
            Comprobar(juego(j1, L"X") && !j1.Completo(*juego(j1, L"X")) && !juego(j1, L"X")->incompleto && juego(j2, L"X")
                          && j2.Completo(*juego(j2, L"X")) && juego(j3, L"X") && !j3.Completo(*juego(j3, L"X")) && juego(j3, L"X")->incompleto,
                      "juegos en partes: si no se sabe cuál es la última, espera al resto del paquete; si se sabe que falta otra, no se abre");
            auto j4 = Juegos("g1", { descarga(c, L"X.part1.rar", true), descarga(c, L"X.part2.rar", true), descarga(c, L"Y.zip", false) }, {},
                             con(Ultima::Es));
            Comprobar(juego(j4, L"X") && j4.Completo(*juego(j4, L"X")) && !j4.terminado,
                      "juegos en partes: si se sabe que la última es la última, se abre ya, sin esperar al resto del paquete");
        }

        // Lo que dice el propio comprimido, leído del disco
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        const std::wstring dir = std::wstring(temporal) + L"atarraya-prueba-partes-" + std::to_wstring(GetCurrentProcessId());
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        auto escribir = [&](const wchar_t* nombre, const std::string& datos) {
            std::ofstream o(dir + L"\\" + nombre, std::ios::binary);
            o << datos;
            return dir + L"\\" + nombre;
        };
        auto vint = [](uint64_t v) {
            std::string s;
            do
            {
                unsigned char b = v & 0x7F;
                v >>= 7;
                if (v) b |= 0x80;
                s.push_back((char)b);
            } while (v);
            return s;
        };
        // Un bloque de RAR 5: CRC, tamaño, tipo, banderas, [tamaño de los datos], lo demás y sus datos
        auto bloque5 = [&](uint64_t tipo, uint64_t banderas, const std::string& resto, uint64_t datos = 0) {
            std::string h = vint(tipo) + vint(banderas) + ((banderas & 2) ? vint(datos) : std::string()) + resto;
            return std::string(4, '\0') + vint(h.size()) + h + std::string((size_t)datos, 'd');
        };
        const std::string rar5(std::string("Rar!\x1A\x07\x01\x00", 8));
        std::string sigue5 = rar5 + bloque5(1, 0, vint(0x0001)) + bloque5(2, 0x0002, std::string(12, 'f'), 300) + bloque5(5, 0, vint(0x0001));
        std::string ultima5 = rar5 + bloque5(1, 0, vint(0x0003) + vint(1)) + bloque5(2, 0x0002, std::string(12, 'f'), 120) + bloque5(5, 0, vint(0));
        std::string cifrada5 = rar5 + bloque5(4, 0, std::string(20, 'c')) + std::string(64, 'x');
        // Un bloque de RAR 4: CRC, tipo, banderas, tamaño, [tamaño de los datos], lo demás y sus datos
        auto bloque4 = [](unsigned tipo, unsigned banderas, const std::string& resto, uint32_t datos = 0) {
            std::string h(2, '\0');
            h.push_back((char)tipo);
            h.push_back((char)(banderas & 0xFF));
            h.push_back((char)(banderas >> 8));
            size_t tam = 7 + ((banderas & 0x8000) ? 4 : 0) + resto.size();
            h.push_back((char)(tam & 0xFF));
            h.push_back((char)(tam >> 8));
            if (banderas & 0x8000)
            {
                for (int i = 0; i < 4; i++) h.push_back((char)((datos >> (8 * i)) & 0xFF));
            }
            return h + resto + std::string(datos, 'd');
        };
        const std::string rar4(std::string("Rar!\x1A\x07\x00", 7));
        std::string sigue4 = rar4 + bloque4(0x73, 0x0001, std::string(6, '\0')) + bloque4(0x74, 0x8000, std::string(21, '\0'), 200) + bloque4(0x7B, 0x0001, "");
        std::string ultima4 = rar4 + bloque4(0x73, 0x0001, std::string(6, '\0')) + bloque4(0x74, 0x8000, std::string(21, '\0'), 90) + bloque4(0x7B, 0, "");
        auto p1 = escribir(L"R.part1.rar", sigue5), p2 = escribir(L"R.part2.rar", ultima5), c5 = escribir(L"C5.part1.rar", cifrada5);
        auto v0 = escribir(L"V.rar", sigue4), v1 = escribir(L"V.r00", ultima4);
        auto basura = escribir(L"basura.part1.rar", std::string(100, 'z'));
        Comprobar(LaUltima({ p1 }, L'n') == Ultima::NoEs && LaUltima({ p1, p2 }, L'n') == Ultima::Es && LaUltima({ c5 }, L'n') == Ultima::NoSe
                      && LaUltima({ basura }, L'n') == Ultima::NoSe && LaUltima({ v0 }, L'v') == Ultima::NoEs && LaUltima({ v0, v1 }, L'v') == Ultima::Es,
                  "juegos en partes: RAR 5 y RAR 4 dicen al final de cada parte si sigue otra (cifradas o raras: no se sabe)");
        // 7z partido: la primera parte dice cuánto mide todo (con su suma de control)
        auto crc = [](const std::string& s) {
            uint32_t c2 = 0xFFFFFFFFu;
            for (unsigned char ch : s)
            {
                c2 ^= ch;
                for (int k = 0; k < 8; k++) c2 = (c2 & 1) ? 0xEDB88320u ^ (c2 >> 1) : c2 >> 1;
            }
            return c2 ^ 0xFFFFFFFFu;
        };
        auto le = [](uint64_t v, int n) {
            std::string s;
            for (int i = 0; i < n; i++) s.push_back((char)((v >> (8 * i)) & 0xFF));
            return s;
        };
        std::string resto7 = le(200, 8) + le(50, 8) + le(0, 4);   // la cabecera del final: en el byte 232 y de 50 bytes
        std::string inicio7 = std::string("7z\xBC\xAF\x27\x1C\x00\x04", 8) + le(crc(resto7), 4) + resto7;
        auto s1 = escribir(L"S.7z.001", inicio7 + std::string(118, 'a')), s2 = escribir(L"S.7z.002", std::string(132, 'b'));
        std::string malo7 = inicio7;
        malo7[20] ^= 1;   // la suma de control ya no coincide
        auto s9 = escribir(L"M.7z.001", malo7 + std::string(10, 'a'));
        auto z1 = escribir(L"H.zip.001", std::string(50, 'q'));
        Comprobar(LaUltima({ s1, s2 }, L'c') == Ultima::Es && LaUltima({ s1 }, L'c') == Ultima::NoEs && LaUltima({ s9 }, L'c') == Ultima::NoSe
                      && LaUltima({ z1 }, L'c') == Ultima::NoSe,
                  "juegos en partes: un 7z partido está entero cuando sus partes suman lo que dice la primera (un .zip.001 no lo dice)");
        // ZIP partido: el índice del .zip dice cuántas .zNN hay
        std::string eocd = std::string("PK\x05\x06", 4) + le(2, 2) + le(0, 2) + le(1, 2) + le(1, 2) + le(46, 4) + le(0, 4) + le(0, 2);
        auto zz = escribir(L"Z.zip", std::string(80, 'x') + eocd), zz1 = escribir(L"Z.z01", "uno"), zz2 = escribir(L"Z.z02", "dos");
        Comprobar(LaUltima({ zz, zz1, zz2 }, L'z') == Ultima::Es && LaUltima({ zz, zz1 }, L'z') == Ultima::NoEs,
                  "juegos en partes: un ZIP partido está entero con todas las .zNN que dice su .zip");

        // Jícaro nunca junta partes de otro paquete que quedaron en la misma carpeta
        {
            escribir(L"peli.part1.rar", "a");
            escribir(L"peli.part2.rar", "b");
            escribir(L"otra.part1.rar", "c");
            std::vector<Foto> l = { descarga(dir, L"peli.part1.rar", true), descarga(dir, L"peli.part2.rar", true) };
            auto j = Juegos("g1", l, {}, con(Ultima::Es));
            const Juego* peli = juego(j, L"peli");
            std::vector<std::wstring> propias = { dir + L"\\peli.part1.rar", dir + L"\\PELI.part2.rar" };
            bool sinAjenas = peli && ParteAjena(*peli, propias).empty();
            escribir(L"peli.part3.rar", "de otro paquete");
            Comprobar(sinAjenas && peli && ParteAjena(*peli, propias) == L"peli.part3.rar",
                      "juegos en partes: si en la carpeta hay una parte con el mismo nombre que no es del paquete, no se abre");
        }

        // «En curso» al abrir: una por paquete (o la suelta), solo las que terminaron de bajar
        {
            std::vector<Foto> l = { descarga(c, L"a.part1.rar", true), descarga(c, L"a.part2.rar", true), descarga(c, L"suelto.zip", true, ""),
                                    descarga(c, L"hecho.zip", true, ""), descarga(c, L"b.part1.rar", true, "g2") };
            l[0].datos.jicaro = l[1].datos.jicaro = EstadoJicaro::EnCurso;
            l[2].datos.jicaro = EstadoJicaro::EnCurso;
            l[3].datos.jicaro = EstadoJicaro::Hecho;
            auto ids = PorRetomar(l);
            Comprobar(ids.size() == 2 && ids[0] == l[0].datos.id && ids[1] == l[2].datos.id,
                      "jícaro: al abrir se retoman los juegos que quedaron en curso (uno por paquete)");
            Comprobar(EstadoDespues(Resultado::Listo) == EstadoJicaro::Hecho && EstadoDespues(Resultado::Parcial) == EstadoJicaro::Hecho
                          && EstadoDespues(Resultado::Cancelado) == EstadoJicaro::Hecho && EstadoDespues(Resultado::Error) == EstadoJicaro::Pendiente
                          && EstadoDespues(Resultado::ContrasenaIncorrecta) == EstadoJicaro::Pendiente
                          && EstadoDespues(Resultado::JicaroViejo) == EstadoJicaro::Pendiente && EstadoDespues(Resultado::NoAbrio) == EstadoJicaro::Pendiente
                          && EstadoDespues(Resultado::FaltanPartes) == EstadoJicaro::Pendiente,
                      "jícaro: queda hecho solo cuando Jícaro terminó; si no, vuelve a pendiente");
            // Se guarda con cada descarga, y una lista de antes («descomprimido») cuenta como hecho
            std::wstring f = dir + L"\\descargas.json";
            uint64_t id = 0;
            {
                Motor m(f);
                DatosDescarga d;
                d.url = "https://ejemplo.com/a.part1.rar";
                d.carpeta = dir;
                id = m.AgregarTerminada(d);
                m.MarcarJicaro(id, EstadoJicaro::EnCurso);
                m.Guardar();
            }
            Motor m2(f);
            m2.Cargar();
            Foto g;
            bool enCurso = m2.Buscar(id, g) && g.datos.jicaro == EstadoJicaro::EnCurso;
            std::string viejo = "{\"version\":1,\"siguienteId\":9,\"descargas\":[{\"id\":7,\"url\":\"https://e.com/v.rar\",\"estado\":\"completada\","
                                "\"descomprimido\":true}]}";
            EscribirSeguro(dir + L"\\vieja.json", viejo);
            Motor m3(dir + L"\\vieja.json");
            m3.Cargar();
            Comprobar(enCurso && m3.Buscar(7, g) && g.datos.jicaro == EstadoJicaro::Hecho,
                      "jícaro: «en curso» se guarda tal cual (para retomarlo al abrir)");
        }

        // El reintento con la contraseña: la clave de un aviso que se mueve y se borra al sacarlo
        {
            struct Aviso
            {
                std::string clave;
                int r;
            };
            std::deque<Aviso> avisos = { { "grupo-con-un-nombre-largo-0001", 4 }, { "otro", 4 }, { "grupo-con-un-nombre-largo-0001", 7 },
                                         { "grupo-con-un-nombre-largo-0001", 0 } };
            auto salen = SacarLosQueEsperan(avisos, avisos.front().clave, [](const Aviso& a) -> const std::string& { return a.clave; },
                                            [](const Aviso& a) { return a.r == 4 || a.r == 7; });
            Comprobar(salen.size() == 2 && salen[0].r == 4 && salen[1].r == 7 && avisos.size() == 2 && avisos[0].clave == "otro"
                          && avisos[1].r == 0,
                      "jícaro: al escribir la contraseña desde un aviso, siguen todos los de ese paquete (la clave se copia antes de moverlos)");
        }
        fs::remove_all(dir, ec);
    }

    // 0.4.1: las contraseñas de los paquetes (Click'n'Load, cifradas con DPAPI) y Jícaro con «--contrasena-stdin»
    void ContrasenasPruebas()
    {
        // Sin JSON: en claro van con su largo, en un solo bloque, y se leen directo a cada una
        {
            using Lista = std::vector<std::string>;
            Lista claves = { "con \"comillas\" y \\barras\\", "contraseña ñ €", "corta", std::string(300, 'k') };
            std::string bloque = hnd::contrasenas::Empaquetar(claves);
            Lista vuelta;
            bool ok = hnd::contrasenas::Desempaquetar(bloque.data(), bloque.size(), 5, vuelta);
            Lista dos;
            bool maximo = hnd::contrasenas::Desempaquetar(bloque.data(), bloque.size(), 2, dos);
            Lista nada;
            std::string json = "[\"a\",\"b\"]", cortado = bloque.substr(0, bloque.size() - 3);
            bool rechaza = !hnd::contrasenas::Desempaquetar(json.data(), json.size(), 5, nada) && nada.empty()
                && !hnd::contrasenas::Desempaquetar(cortado.data(), cortado.size(), 5, nada) && nada.empty()
                && !hnd::contrasenas::Desempaquetar(nullptr, 0, 5, nada);
            Comprobar(ok && vuelta == claves && maximo && dos.size() == 2 && rechaza && bloque[0] == '\x01' && bloque.find('[') == std::string::npos
                          && hnd::contrasenas::Descifrar(hnd::contrasenas::Cifrar(claves)) == claves,
                      "contraseñas: en claro van con su largo (sin JSON ni sus copias); lo que no tiene esa forma no se lee");
            hnd::contrasenas::Borrar(bloque);
            hnd::contrasenas::Borrar(vuelta);
            hnd::contrasenas::Borrar(dos);
        }
        using hnd::contrasenas::DeTexto;
        using Lista = std::vector<std::string>;
        Comprobar(DeTexto("clave1\r\nclave2\nclave3\rclave4") == Lista{ "clave1", "clave2", "clave3", "clave4" },
                  "contraseñas: varias, separadas por saltos de línea (\\r\\n, \\n o \\r), como manda Click'n'Load");
        Comprobar(DeTexto("  con espacios  \r\n\r\n\t\r\nrepetida\nrepetida\nuna\x01mala\n" + std::string(600, 'x') + "\n\xC3\x28" + "\nbuena")
                      == Lista{ "con espacios", "repetida", "buena" },
                  "contraseñas: sin espacios a los costados, sin vacías ni repetidas, sin caracteres de control, sin texto que no es UTF-8, "
                  "de 512 bytes como mucho");
        Comprobar(DeTexto("1\n2\n3\n4\n5\n6\n7").size() == 5 && DeTexto("").empty() && DeTexto("contraseña ñ €") == Lista{ "contraseña ñ €" },
                  "contraseñas: como mucho 5, en el orden en que llegaron (y con acentos)");
        Comprobar(hnd::contrasenas::Juntar({ "a", "b" }, { "b", "c", "d", "e", "f" }) == Lista{ "a", "b", "c", "d", "e" },
                  "contraseñas: juntar dos listas sin repetir y como mucho 5");

        // Cifradas con DPAPI: nunca en claro
        std::string cifrado = hnd::contrasenas::Cifrar({ "secreto-de-prueba-1", "secreto-de-prueba-2" });
        auto vuelta = hnd::contrasenas::Descifrar(cifrado);
        std::string cambiado = cifrado;
        if (cambiado.size() > 20) cambiado[cambiado.size() / 2] = cambiado[cambiado.size() / 2] == 'A' ? 'B' : 'A';
        Comprobar(!cifrado.empty() && cifrado.find("secreto") == std::string::npos && vuelta == Lista{ "secreto-de-prueba-1", "secreto-de-prueba-2" }
                      && hnd::contrasenas::Descifrar(cambiado).empty() && hnd::contrasenas::Descifrar("no es base64 !!").empty()
                      && hnd::contrasenas::Cifrar({}).empty(),
                  "contraseñas: cifradas con DPAPI (vuelven igual; cambiadas o falsas no se descifran)");
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        std::wstring base = std::wstring(temporal) + L"atarraya-prueba-claves-" + std::to_wstring(GetCurrentProcessId());
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        {
            hnd::contrasenas::Guardadas g(base + L"\\contrasenas.json");
            g.Fijar("grupo1", { "secreto-de-prueba-3" });
            g.Fijar("grupo2", { "secreto-de-prueba-4" });
            g.Fijar("grupo3", {});
            for (int i = 0; i < 25; i++) g.Recordar({ "recordada-" + std::to_string(i) });
            g.Recordar({ "recordada-3" });
            Comprobar(g.Guardar(), "contraseñas: se guardan");
            std::string archivo;
            LeerArchivo(base + L"\\contrasenas.json", archivo);
            hnd::contrasenas::Guardadas otra(base + L"\\contrasenas.json");
            otra.Cargar();
            auto recordadas = otra.Recordadas();
            Comprobar(archivo.find("secreto") == std::string::npos && archivo.find("recordada-") == std::string::npos
                          && otra.Leer("grupo1") == Lista{ "secreto-de-prueba-3" } && otra.Leer("grupo2") == Lista{ "secreto-de-prueba-4" }
                          && !otra.Tiene("grupo3") && recordadas.size() == 20 && recordadas[0] == "recordada-3" && recordadas[1] == "recordada-24",
                      "contraseñas: en contrasenas.json van cifradas; vuelven después de cerrar Atarraya; las recordadas, la más nueva primero y "
                      "como mucho 20");
            otra.QuedarseCon({ "grupo2" });
            otra.OlvidarRecordadas();
            Comprobar(!otra.Tiene("grupo1") && otra.Tiene("grupo2") && otra.Recordadas().empty() && !otra.HayRecordadas(),
                      "contraseñas: se olvidan las de paquetes que ya no están y, si se apaga en Ajustes, las recordadas");
        }

        // Click'n'Load: el campo «passwords»
        hnd::cnl::Campos campos = { { "urls", "https://a.org/1.rar" }, { "Passwords", "clave-cnl-1\r\nclave-cnl-2" }, { "passwords", "clave-cnl-3" } };
        Comprobar(hnd::cnl::Contrasenas(campos) == Lista{ "clave-cnl-1", "clave-cnl-2", "clave-cnl-3" } && hnd::cnl::Contrasenas({}).empty(),
                  "contraseñas: del campo «passwords» de Click'n'Load (una o varias, separadas por saltos de línea)");

        // El recolector las guarda cifradas con cada enlace
        {
            hnd::Recolector r(base + L"\\recolector.json");
            r.Agregar({ "https://ejemplo.org/lote/juego.part1.rar", "https://ejemplo.org/lote/juego.part2.rar" }, L"Juego", "", "lista");
            r.FijarContrasenasDe({ "https://ejemplo.org/lote/juego.part1.rar", "https://ejemplo.org/lote/juego.part2.rar" },
                                 hnd::contrasenas::Cifrar({ "secreto-de-prueba-5" }));
            r.Guardar();
            std::string archivo;
            LeerArchivo(base + L"\\recolector.json", archivo);
            hnd::Recolector otro(base + L"\\recolector.json");
            otro.Cargar();
            auto lista = otro.Lista();
            Comprobar(archivo.find("secreto") == std::string::npos && lista.size() == 2
                          && hnd::contrasenas::Descifrar(lista[0].contrasenas) == Lista{ "secreto-de-prueba-5" }
                          && lista[0].contrasenas == lista[1].contrasenas,
                      "contraseñas: en el recolector, una sola para todo el lote, cifrada con cada enlace");
        }

        // Jícaro: con «--contrasena-stdin» la contraseña llega por la entrada estándar, nunca en la línea de comandos
        {
            using namespace hnd::jicaro;
            wchar_t propio[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, propio, MAX_PATH);
            std::wstring falso = propio;
            falso = falso.substr(0, falso.find_last_of(L'\\') + 1) + L"jicaro-falso.exe";
            std::wstring carpetaJicaro = base + L"\\Jicaro";
            std::filesystem::create_directories(carpetaJicaro, ec);
            std::wstring exe = carpetaJicaro + L"\\Jicaro.exe";
            bool copiado = CopyFileW(falso.c_str(), exe.c_str(), FALSE) != FALSE;
            Version v;
            Comprobar(copiado && LeerVersion(exe, v) && v.mayor == 0 && v.menor == 2 && AceptaContrasena(v) && !AceptaContrasena({ 0, 1, 9 })
                          && AceptaContrasena({ 1, 0, 0 }),
                      "jícaro: la contraseña se le pasa solo a Jícaro 0.2.0 o más nuevo (lo dice la versión de su archivo)");
            std::wstring archivo = base + L"\\juego.part1.rar", destino = base + L"\\juego";
            std::wstring orden = ArmarOrden(exe, archivo, destino, true);
            Comprobar(orden == ArmarOrden(exe, archivo, destino) + L" --contrasena-stdin", "jícaro: con contraseña, la orden lleva «--contrasena-stdin»");
            auto correr = [&](const std::string& clave, const std::wstring& o, std::string& informe) {
                DeleteFileW((carpetaJicaro + L"\\informe.txt").c_str());
                void* p = Abrir(exe, o, clave);
                DWORD codigo = 99;
                if (p)
                {
                    WaitForSingleObject((HANDLE)p, 20000);
                    GetExitCodeProcess((HANDLE)p, &codigo);
                    CloseHandle((HANDLE)p);
                }
                informe.clear();
                LeerArchivo(carpetaJicaro + L"\\informe.txt", informe);
                return codigo;
            };
            auto hex = [](const std::string& s) {
                std::string r;
                const char* cifras = "0123456789abcdef";
                for (unsigned char ch : s)
                {
                    r += cifras[ch >> 4];
                    r += cifras[ch & 15];
                }
                return r;
            };
            std::string informe;
            DWORD bien = correr("buena", orden, informe);
            bool lineaLimpia = informe.find("linea=") != std::string::npos && informe.find("--contrasena-stdin") != std::string::npos
                               && informe.substr(0, informe.find('\n')).find("buena") == std::string::npos;
            Comprobar(bien == 0 && lineaLimpia && informe.find("entrada=" + hex("buena") + "\n") != std::string::npos,
                      "jícaro: la contraseña llega entera por la entrada estándar (sin salto de línea) y nunca en la línea de comandos");
            DWORD mala = correr("contraseña-mala-ñ", orden, informe);
            Comprobar(mala == 7 && LeerCodigo(mala) == Resultado::ContrasenaIncorrecta && informe.find("entrada=" + hex("contraseña-mala-ñ")) != std::string::npos
                          && informe.find("contraseña-mala") == std::string::npos,
                      "jícaro: una contraseña que no sirve (en UTF-8) da «la contraseña no sirve» (código 7)");
            DWORD sinClave = correr("", ArmarOrden(exe, base + L"\\cifrado.rar", destino), informe);
            Comprobar(sinClave == 4 && LeerCodigo(sinClave) == Resultado::Contrasena && informe.find("--contrasena-stdin") == std::string::npos,
                      "jícaro: sin contraseña, la orden no lleva «--contrasena-stdin» y un comprimido cifrado da «tiene contraseña»");
        }

        // Nada de esto va al registro (este proceso escribe su registro en la carpeta de la prueba)
        std::string registro;
        LeerArchivo(hnd::CarpetaDatos() + L"\\registro.txt", registro);
        Comprobar(registro.find("secreto-de-prueba") == std::string::npos && registro.find("clave-cnl") == std::string::npos
                      && registro.find("buena") == std::string::npos,
                  "contraseñas: ninguna va al registro");
        std::filesystem::remove_all(base, ec);
    }

    // Jícaro: elegir la primera parte, saber cuándo terminó un paquete, armar la orden y leer los códigos
    void JicaroPruebas()
    {
        using namespace hnd::jicaro;
        const std::wstring d = L"C:\\Descargas\\";
        auto rutas = [&](std::initializer_list<const wchar_t*> nombres) {
            std::vector<std::wstring> r;
            for (auto* n : nombres) r.push_back(d + n);
            return r;
        };

        // 1. La primera parte de cada comprimido
        {
            std::vector<std::wstring> l;
            for (int i : { 10, 2, 7, 1, 3, 9, 4, 8, 5, 6 }) l.push_back(d + L"peli.part" + std::to_wstring(i) + L".rar");
            auto c = Comprimidos(l);
            Comprobar(c.size() == 1 && c[0].Primera() == d + L"peli.part1.rar" && c[0].partes.size() == 10
                          && c[0].partes[1] == d + L"peli.part2.rar" && c[0].partes.back() == d + L"peli.part10.rar" && c[0].nombre == L"peli",
                      "jícaro: la primera de peli.part1.rar ... part10.rar, en orden por número (no por texto)");
        }
        {
            auto c = Comprimidos(rutas({ L"Juego.part03.rar", L"Juego.part01.rar", L"Juego.part02.rar", L"Juego.nfo", L"Juego.sfv" }));
            Comprobar(c.size() == 1 && c[0].Primera() == d + L"Juego.part01.rar" && c[0].partes.size() == 3 && c[0].nombre == L"Juego",
                      "jícaro: .part01.rar con ceros; el .nfo y el .sfv quedan fuera");
        }
        {
            auto c = Comprimidos(rutas({ L"viejo.r01", L"viejo.rar", L"viejo.r00" }));
            Comprobar(c.size() == 1 && c[0].partes.size() == 3 && c[0].Primera() == d + L"viejo.rar" && c[0].partes[1] == d + L"viejo.r00",
                      "jícaro: estilo viejo .rar + .r00 + .r01: la primera es el .rar");
        }
        {
            auto c = Comprimidos(rutas({ L"datos.7z.002", L"datos.7z.001", L"datos.7z.003" }));
            auto z = Comprimidos(rutas({ L"datos.zip.002", L"datos.zip.001" }));
            Comprobar(c.size() == 1 && c[0].Primera() == d + L"datos.7z.001" && c[0].nombre == L"datos" && z.size() == 1
                          && z[0].Primera() == d + L"datos.zip.001",
                      "jícaro: trozos .7z.001 y .zip.001");
        }
        {
            auto c = Comprimidos(rutas({ L"PELI.PART2.RAR", L"peli.part1.rar" }));
            auto f = Comprimidos(rutas({ L"peli.part3.rar", L"peli.part2.rar" }));
            Comprobar(c.size() == 1 && c[0].Primera() == d + L"peli.part1.rar" && f.size() == 1 && f[0].Primera() == d + L"peli.part2.rar",
                      "jícaro: sin importar mayúsculas; si falta la parte 1 va la más baja (Jícaro dice cuál falta)");
        }
        {
            auto c = Comprimidos(rutas({ L"a.part1.rar", L"a.part2.rar", L"programa.zip", L"fotos.7z", L"video.mkv", L"video.mkv.001",
                                         L"notas.txt", L"a.part1.rar" }));
            bool ok = c.size() == 3;
            for (const auto& x : c)
            {
                if (x.nombre == L"a") ok = ok && x.partes.size() == 2 && x.Primera() == d + L"a.part1.rar";
                else if (x.nombre == L"programa") ok = ok && x.partes.size() == 1 && x.Primera() == d + L"programa.zip";
                else if (x.nombre == L"fotos") ok = ok && x.partes.size() == 1 && x.Primera() == d + L"fotos.7z";
                else ok = false;
            }
            Comprobar(ok, "jícaro: varios comprimidos en un paquete; un video, un .txt y un repetido no cuentan");
        }
        Comprobar(Comprimidos(rutas({ L"serie.s01" })).empty() && Comprimidos(rutas({ L"serie.rar", L"serie.s01" })).size() == 1
                      && Comprimidos(rutas({ L"serie.rar", L"serie.s01" }))[0].partes.size() == 2,
                  "jícaro: una .s01 sola no es un RAR; con su .rar, sí");

        // 2. Sueltos: solo un comprimido entero
        Comprobar(EsComprimidoSuelto(L"programa.zip") && EsComprimidoSuelto(L"Fotos.7Z") && EsComprimidoSuelto(L"juego.rar")
                      && !EsComprimidoSuelto(L"peli.part1.rar") && !EsComprimidoSuelto(L"peli.r00") && !EsComprimidoSuelto(L"datos.7z.001")
                      && !EsComprimidoSuelto(L"video.mp4") && !EsComprimidoSuelto(L"zip"),
                  "jícaro: suelto se descomprime un .zip, .7z o .rar entero, no una parte");
        {
            auto b = [](std::initializer_list<int> v) {
                std::string s;
                for (int x : v) s += (char)x;
                return s;
            };
            std::string rar5 = b({ 'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44 });
            std::string rar4 = b({ 'R', 'a', 'r', '!', 0x1A, 0x07, 0x00, 0x11, 0x22 });
            bool volumen5 = EsVolumenRar(rar5 + b({ 0x06, 0x01, 0x00, 0x01, 0x00 }));                // cabecera principal: «es parte»
            bool entero5 = !EsVolumenRar(rar5 + b({ 0x06, 0x01, 0x00, 0x04, 0x00 }));               // sólido, de una sola parte
            bool extra5 = EsVolumenRar(rar5 + b({ 0x0B, 0x01, 0x01, 0x85, 0x01, 0x03 }));           // con área extra (número de 2 bytes)
            bool otroTipo = !EsVolumenRar(rar5 + b({ 0x06, 0x02, 0x00, 0x01, 0x00 }));              // no es la cabecera principal
            bool volumen4 = EsVolumenRar(rar4 + b({ 0x73, 0x01, 0x01, 0x0D, 0x00 }));               // RAR 4: MHD_VOLUME
            bool entero4 = !EsVolumenRar(rar4 + b({ 0x73, 0x00, 0x00, 0x0D, 0x00 }));
            bool zip = !EsVolumenRar(b({ 'P', 'K', 3, 4, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0 }));
            bool corto = !EsVolumenRar(rar5 + b({ 0x86 })) && !EsVolumenRar(std::string("Rar!"));
            Comprobar(volumen5 && entero5 && extra5 && otroTipo && volumen4 && entero4 && zip && corto,
                      "jícaro: por su cabecera (RAR 4 y RAR 5), un .rar sabe si es una parte de varias");
        }

        // 3. ¿Terminó todo el paquete? Solo las del mismo grupo; también cuentan sus partes en «de una en una»
        {
            auto foto = [&](uint64_t id, const char* grupo, const wchar_t* nombre, Estado e, const std::wstring& carpeta = L"C:\\Descargas") {
                Foto f;
                f.datos.id = id;
                f.datos.grupo = grupo;
                f.datos.nombre = nombre;
                f.datos.carpeta = carpeta;
                f.datos.estado = e;
                return f;
            };
            // En la misma carpeta: el paquete g1, OTRO paquete (g2) con una parte que se llama igual que las de g1, una
            // suelta del mismo comprimido y otras que no tienen que ver
            std::vector<Foto> lista = { foto(1, "g1", L"peli.part1.rar", Estado::Completada), foto(2, "g1", L"peli.part2.rar", Estado::Completada),
                                        foto(3, "otro", L"otra.zip", Estado::Error), foto(4, "", L"suelto.zip", Estado::Descargando),
                                        foto(5, "g2", L"peli.part3.rar", Estado::Completada), foto(6, "", L"peli.part4.rar", Estado::Completada) };
            lista[0].datos.identidad.tamano = 100;
            auto listas = ArchivosDelPaquete("g1", lista, {});
            Comprobar(listas.size() == 2 && listas[0].descarga == 1 && listas[0].ruta == d + L"peli.part1.rar" && listas[0].identidad.tamano == 100
                          && listas[1].descarga == 2 && listas[1].ruta == d + L"peli.part2.rar",
                      "jícaro: un paquete terminado da solo sus descargas (por id), con el archivo que era cada una");
            auto planes = Planear(listas, L"Peli de prueba");
            bool soloLasSuyas = planes.size() == 1 && planes[0].primera == d + L"peli.part1.rar" && planes[0].nombre == L"Peli de prueba"
                                && planes[0].carpeta == L"C:\\Descargas" && planes[0].partes.size() == 2;
            for (const auto& plan : planes)
                for (const auto& p : plan.partes) soloLasSuyas = soloLasSuyas && (p.descarga == 1 || p.descarga == 2);
            Comprobar(soloLasSuyas, "jícaro: otro paquete en la misma carpeta (con una parte que se llama igual) no entra: ni a Jícaro ni a «Borrar las partes»");
            auto deG2 = Planear(ArchivosDelPaquete("g2", lista, {}), L"Páginas de ejemplo.org");
            Comprobar(deG2.size() == 1 && deG2[0].partes.size() == 1 && deG2[0].partes[0].descarga == 5 && deG2[0].nombre == L"peli",
                      "jícaro: y al revés: el otro paquete solo tiene la suya");
            auto enDos = lista;
            enDos[1].datos.carpeta = L"D:\\Otra";
            auto planesEnDos = Planear(ArchivosDelPaquete("g1", enDos, {}), L"Peli");
            Comprobar(planesEnDos.size() == 2 && planesEnDos[0].partes.size() == 1 && planesEnDos[1].partes.size() == 1,
                      "jícaro: partes del mismo paquete en dos carpetas: cada carpeta va por su lado");
            auto conError = lista;
            conError[1].datos.estado = Estado::Error;
            auto bajando = lista;
            bajando[0].datos.estado = Estado::Descargando;
            Parte esperando;
            esperando.grupo = "g1";
            esperando.estado = EstadoParte::Esperando;
            Parte lista3 = esperando;
            lista3.estado = EstadoParte::Lista;
            Parte deOtro = esperando;
            deOtro.grupo = "otro";
            Comprobar(ArchivosDelPaquete("g1", conError, {}).empty() && ArchivosDelPaquete("g1", bajando, {}).empty()
                          && ArchivosDelPaquete("g1", lista, { lista3, esperando }).empty()
                          && ArchivosDelPaquete("g1", lista, { lista3, deOtro }).size() == 2 && ArchivosDelPaquete("", lista, {}).empty(),
                      "jícaro: falta algo si una del grupo falló, sigue bajando o tiene una parte sin bajar en «de una en una»");

            // Una parte que ya estaba bajando cuando llegó el paquete entra a su grupo: Jícaro espera también a esa
            jicaro::Tanda tanda;
            tanda.Sumar(L"Peli", 7, "");          // suelta, bajando
            tanda.Sumar(L"Serie", 8, "viejo");    // ya era de otro paquete
            tanda.Sumar(L"Juego", 9, "A");
            tanda.Sumar(L"Juego", 10, "B");       // dos paquetes que resultan ser uno
            std::string gPeli = tanda.Grupo(L"Peli");
            auto entran = tanda.Entran();
            auto uniones = tanda.Uniones();
            Comprobar(gPeli.size() == 16 && tanda.Grupo(L"Peli") == gPeli && tanda.Grupo(L"Serie") == "viejo" && tanda.Grupo(L"Juego") == "A"
                          && entran.size() == 1 && entran[0].descarga == 7 && entran[0].grupo == gPeli && entran[0].paquete == L"Peli"
                          && uniones.size() == 1 && uniones[0].viejo == "B" && uniones[0].nuevo == "A" && uniones[0].paquete == L"Juego"
                          && tanda.Grupo(L"Otro") != gPeli && tanda.Grupo(L"Otro").size() == 16,
                      "jícaro: una parte que ya estaba bajando entra al grupo de su paquete (y dos grupos del mismo paquete quedan en uno)");
            std::vector<Foto> conLaQueBajaba = { foto(20, gPeli.c_str(), L"peli.part1.rar", Estado::Completada),
                                                 foto(7, gPeli.c_str(), L"peli.part2.rar", Estado::Descargando) };
            bool espera = ArchivosDelPaquete(gPeli, conLaQueBajaba, {}).empty();
            conLaQueBajaba[1].datos.estado = Estado::Completada;
            auto alFinal = ArchivosDelPaquete(gPeli, conLaQueBajaba, {});
            Comprobar(espera && alFinal.size() == 2 && Planear(alFinal, L"Peli").size() == 1,
                      "jícaro: el paquete espera a la que ya estaba bajando y después se descomprime una sola vez, con las dos");
        }

        // 4. La orden: cada ruta entre comillas, y Windows la lee igual
        {
            std::wstring exe = L"C:\\Users\\Ana\\AppData\\Local\\Programs\\Jicaro\\Jicaro.exe";
            std::wstring archivo = L"C:\\Users\\Ana\\Mis descargas\\Película ñ & más (2024)\\peli.part1.rar";
            std::wstring destino = L"C:\\Users\\Ana\\Mis descargas\\Película ñ & más (2024)\\Película ñ & más (2024)";
            std::wstring orden = ArmarOrden(exe, archivo, destino);
            Comprobar(orden == L"\"" + exe + L"\" --extraer \"" + archivo + L"\" --destino \"" + destino + L"\"",
                      "jícaro: la orden es \"Jicaro.exe\" --extraer \"<primera parte>\" --destino \"<carpeta>\"");
            int n = 0;
            LPWSTR* a = CommandLineToArgvW(orden.c_str(), &n);
            bool igual = a && n == 5 && exe == a[0] && std::wstring(a[1]) == L"--extraer" && archivo == a[2] && std::wstring(a[3]) == L"--destino"
                         && destino == a[4];
            if (a) LocalFree(a);
            Comprobar(igual, "jícaro: Windows la lee como 5 argumentos, con espacios, «&» y acentos en las rutas");
            Comprobar(!ArmarOrden(L"\\\\nas\\programas\\Jicaro\\Jicaro.exe", L"\\\\nas\\pelis\\peli.part1.rar", L"\\\\nas\\pelis\\peli").empty()
                          && !ArmarOrden(L"D:\\Jicaro\\JICARO.EXE", L"D:\\a.zip", L"D:\\a").empty(),
                      "jícaro: rutas de red (\\\\servidor\\carpeta) y otra unidad");
            Comprobar(ArmarOrden(exe, L"C:\\D\\x\" --aqui \"y.rar", destino).empty() && ArmarOrden(exe, archivo, destino + L"\\").empty()
                          && ArmarOrden(exe, L"peli.part1.rar", destino).empty() && ArmarOrden(exe, L"-peli.rar", destino).empty()
                          && ArmarOrden(exe, L"C:\\D\\..\\peli.rar", destino).empty() && ArmarOrden(exe, L"C:\\D\\.\\peli.rar", destino).empty()
                          && ArmarOrden(exe, L"C:\\D\\peli.rar:flujo", destino).empty() && ArmarOrden(exe, L"C:\\D\\pe\nli.rar", destino).empty()
                          && ArmarOrden(exe, L"\\\\?\\C:\\D\\peli.rar", destino).empty() && ArmarOrden(exe, L"\\\\.\\PhysicalDrive0", destino).empty()
                          && ArmarOrden(exe, L"C:\\D\\*.rar", destino).empty() && ArmarOrden(exe, L"C:/D/peli.rar", destino).empty()
                          && ArmarOrden(exe, L"C:\\D\\\\peli.rar", destino).empty() && ArmarOrden(exe, L"C:\\D\\peli.rar ", destino).empty()
                          && ArmarOrden(exe, archivo, L"C:\\D\\peli.").empty() && ArmarOrden(exe, L"", destino).empty(),
                      "jícaro: no se arma con comillas, rutas relativas, «..», flujos, caracteres de control, \\\\?\\, comodines ni «\\» al final");
            Comprobar(ArmarOrden(L"C:\\Windows\\System32\\cmd.exe", archivo, destino).empty() && ArmarOrden(L"Jicaro.exe", archivo, destino).empty()
                          && ArmarOrden(L"C:\\x\\Jicaro.exe.bat", archivo, destino).empty() && !EsRutaDeJicaro(L"C:\\x\\NoJicaro.exe"),
                      "jícaro: solo se abre un Jicaro.exe con su ruta completa");
        }

        // 5. Los códigos de salida y el texto para la persona
        Comprobar(LeerCodigo(0) == Resultado::Listo && LeerCodigo(1) == Resultado::Error && LeerCodigo(2) == Resultado::Cancelado
                      && LeerCodigo(3) == Resultado::FaltanPartes && LeerCodigo(4) == Resultado::Contrasena && LeerCodigo(5) == Resultado::YaExtrayendo
                      && LeerCodigo(6) == Resultado::Parcial && LeerCodigo(7) == Resultado::ContrasenaIncorrecta && LeerCodigo(8) == Resultado::Error
                      && LeerCodigo(0xC0000005) == Resultado::Error && LeerCodigo(259) == Resultado::Error,
                  "jícaro: códigos 0 a 7 (7: la contraseña no sirve); uno desconocido o un cierre de golpe es un error");
        Comprobar(PideContrasena(Resultado::Contrasena) && PideContrasena(Resultado::ContrasenaIncorrecta) && !PideContrasena(Resultado::Listo)
                      && !PideContrasena(Resultado::JicaroViejo) && !PideContrasena(Resultado::FaltanPartes),
                  "jícaro: se pide la contraseña (otra vez) cuando hace falta o la que se usó no sirve");
        {
            auto tiene = [](const std::wstring& t, const wchar_t* s) { return t.find(s) != std::wstring::npos; };
            Comprobar(tiene(Texto(Resultado::Listo, L"Peli", L"C:\\Descargas\\Peli (2)"), L"«Peli» quedó descomprimido en la carpeta «Peli (2)»")
                          && tiene(Texto(Resultado::FaltanPartes, L"Peli", L""), L"le faltan partes")
                          && tiene(Texto(Resultado::Contrasena, L"Peli", L""), L"contraseña") && tiene(Texto(Resultado::Cancelado, L"Peli", L""), L"canceló")
                          && tiene(Texto(Resultado::Error, L"Peli", L""), L"no pudo") && tiene(Texto(Resultado::Parcial, L"Peli", L"C:\\D\\Peli"), L"se saltaron")
                          && tiene(Texto(Resultado::YaExtrayendo, L"Peli", L""), L"otra ventana") && tiene(Texto(Resultado::NoAbrio, L"Peli", L""), L"Ajustes")
                          && Titulo(Resultado::Listo) == L"Jícaro: listo" && Titulo(Resultado::FaltanPartes) == L"Jícaro: faltan partes"
                          && Titulo(Resultado::Contrasena) == L"Jícaro: tiene contraseña" && Titulo(Resultado::Cancelado) == L"Jícaro: cancelado"
                          && Titulo(Resultado::Error) == L"Jícaro: no se pudo descomprimir",
                      "jícaro: el resultado en español: listo, faltan partes, contraseña, cancelado o error");
        }

        // 6. La carpeta de destino, al lado de las partes y sin pisar nada; dónde está Jícaro
        Comprobar(NombreDeCarpeta(L"Peli de prueba", L"peli") == L"Peli de prueba" && NombreDeCarpeta(L"Páginas de ddownload.com", L"peli") == L"peli"
                      && NombreDeCarpeta(L"Enlaces de ejemplo.org", L"peli") == L"peli" && NombreDeCarpeta(L"", L"peli") == L"peli",
                  "jícaro: la carpeta lleva el nombre del paquete (o el del comprimido, si Atarraya no sabía el del paquete)");
        {
            namespace fs = std::filesystem;
            std::wstring b = (fs::temp_directory_path() / (L"atarraya-prueba-jicaro-" + std::to_wstring(GetCurrentProcessId()))).wstring();
            std::error_code ec;
            fs::remove_all(b, ec);
            fs::create_directories(b, ec);
            std::wstring d1 = CarpetaDestino(b, L"Peli");
            CreateDirectoryW(d1.c_str(), nullptr);
            std::wstring d2 = CarpetaDestino(b + L"\\", L"Peli");
            EscribirSeguro(b + L"\\Peli (2)", "un archivo con ese nombre");
            std::wstring d3 = CarpetaDestino(b, L"Peli");
            std::wstring raro = CarpetaDestino(b, L"Peli: 2024? <hd>.");
            std::wstring reservado = CarpetaDestino(b, L"CON");
            Comprobar(d1 == b + L"\\Peli" && d2 == b + L"\\Peli (2)" && d3 == b + L"\\Peli (3)" && raro == b + L"\\Peli_ 2024_ _hd_"
                          && reservado == b + L"\\_CON" && CarpetaDestino(L"", L"Peli").empty(),
                      "jícaro: carpeta nueva «Peli»; si ya hay algo con ese nombre, «Peli (2)», «Peli (3)»...; el nombre se limpia");
            EscribirSeguro(b + L"\\viejo.rar", std::string("Rar!\x1A\x07\x00\x11\x22\x73\x01\x01\x0D\x00", 14));
            EscribirSeguro(b + L"\\entero.rar", std::string("Rar!\x1A\x07\x00\x11\x22\x73\x00\x00\x0D\x00", 14));
            Comprobar(EsVolumenRar(b + L"\\viejo.rar") && !EsVolumenRar(b + L"\\entero.rar") && !EsVolumenRar(b + L"\\no-esta.rar"),
                      "jícaro: lee la cabecera del archivo en el disco");
            // Dónde está Jícaro: el elegido, si existe y se llama Jicaro.exe; si no, el de la ruta de siempre. En las pruebas
            // la «ruta de siempre» queda dentro de su carpeta de datos (nunca el Jícaro que instaló la persona).
            SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", b.c_str());
            std::wstring siempre = b + L"\\Programs\\Jicaro\\Jicaro.exe";
            EscribirSeguro(b + L"\\Jicaro.exe", "no es un programa: solo para buscarlo");
            EscribirSeguro(b + L"\\Otro.exe", "tampoco");
            bool sinJicaro = RutaPredeterminada() == siempre && Buscar(L"").empty() && Buscar(b + L"\\Otro.exe").empty()
                             && Buscar(b + L"\\falta\\Jicaro.exe").empty();
            fs::create_directories(b + L"\\Programs\\Jicaro", ec);
            EscribirSeguro(siempre, "tampoco es un programa");
            Comprobar(sinJicaro && Buscar(b + L"\\Jicaro.exe") == b + L"\\Jicaro.exe" && Buscar(L"") == siempre
                          && Buscar(b + L"\\Otro.exe") == siempre && Buscar(b + L"\\falta\\Jicaro.exe") == siempre,
                      "jícaro: usa el Jicaro.exe elegido; si no, el de la ruta de siempre (Programs\\Jicaro); si no hay ninguno, nada");
            SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", nullptr);
            fs::remove_all(b, ec);
        }

        // 7. El paquete y su grupo se guardan con cada descarga y con cada parte de la cola
        {
            namespace fs = std::filesystem;
            std::wstring b = (fs::temp_directory_path() / (L"atarraya-prueba-grupos-" + std::to_wstring(GetCurrentProcessId()))).wstring();
            std::error_code ec;
            fs::create_directories(b, ec);
            uint64_t id = 0;
            {
                Motor m(b + L"\\descargas.json");
                DatosDescarga x;
                x.url = "http://127.0.0.1:9/peli.part1.rar";
                x.carpeta = b;
                x.paquete = L"Película ñ";
                x.grupo = NuevoId();
                id = m.Agregar(x, false);
                // La identidad del archivo al terminar, y una descarga que ya estaba y entra a otro paquete
                IdentidadArchivo ident;
                ident.volumen = 0x1234ABCD;
                ident.indice = 0x0005000000001234ULL;
                ident.tamano = 15360;
                ident.modificado = 134000000000000000LL;
                m.FijarIdentidad(id, ident);
                DatosDescarga suelta;
                suelta.url = "http://127.0.0.1:9/peli.part2.rar";
                suelta.carpeta = b;
                uint64_t idSuelta = m.Agregar(suelta, false);
                DatosDescarga deOtro = suelta;
                deOtro.url = "http://127.0.0.1:9/peli.part3.rar";
                deOtro.grupo = "otrogrupo0000000";
                uint64_t idOtro = m.Agregar(deOtro, false);
                m.CambiarGrupo(idSuelta, L"Película ñ", x.grupo);
                m.UnirGrupos("otrogrupo0000000", x.grupo, L"Película ñ");
                m.Guardar();
                Motor otro(b + L"\\descargas.json");
                otro.Cargar();
                Foto f, fs2, fs3;
                Comprobar(otro.Buscar(id, f) && f.datos.paquete == L"Película ñ" && f.datos.grupo == x.grupo && x.grupo.size() == 16
                              && x.grupo != NuevoId(),
                          "jícaro: cada descarga guarda su paquete y su grupo (16 letras al azar)");
                Comprobar(f.datos.identidad == ident && otro.Buscar(idSuelta, fs2) && !fs2.datos.identidad.Valida(),
                          "jícaro: cada descarga guarda la identidad de su archivo (volumen, número, tamaño y fecha)");
                Comprobar(fs2.datos.grupo == x.grupo && otro.Buscar(idOtro, fs3) && fs3.datos.grupo == x.grupo && fs3.datos.paquete == L"Película ñ",
                          "jícaro: la que ya estaba (suelta o de otro grupo) queda guardada en el grupo del paquete");
            }
            {
                DeUnaEnUna q(b + L"\\de-una-en-una.json");
                Parte p;
                p.url = "https://ddownload.com/abc123/peli.part1.rar";
                p.paquete = L"Peli";
                p.grupo = "0123456789abcdef";
                Parte p2 = p;
                p2.url = "https://ddownload.com/def456/peli.part2.rar";
                p2.grupo = "otrogrupo0000000";
                bool guardada = q.Agregar({ p, p2 });
                bool cambio = q.CambiarGrupo("otrogrupo0000000", "0123456789abcdef") && !q.CambiarGrupo("no-esta", "0123456789abcdef");
                q.Guardar();
                DeUnaEnUna otra(b + L"\\de-una-en-una.json");
                otra.Cargar();
                Comprobar(guardada && cambio && otra.Partes().size() == 2 && otra.Partes()[0].grupo == "0123456789abcdef"
                              && otra.Partes()[1].grupo == "0123456789abcdef",
                          "jícaro: cada parte de «de una en una» guarda su grupo (también si pasa a otro)");
            }
            fs::remove_all(b, ec);
        }

        // 8. «Borrar las partes» solo manda a la Papelera las que siguen siendo el archivo que bajó Atarraya
        {
            namespace fs = std::filesystem;
            std::wstring b = (fs::temp_directory_path() / (L"atarraya-prueba-borrar-" + std::to_wstring(GetCurrentProcessId()))).wstring();
            std::error_code ec;
            fs::remove_all(b, ec);
            fs::create_directories(b, ec);
            auto fijarFecha = [](const std::wstring& ruta, int64_t fecha) {
                HANDLE h = CreateFileW(ruta.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                FILETIME ft = { (DWORD)(fecha & 0xFFFFFFFF), (DWORD)(fecha >> 32) };
                bool ok = h != INVALID_HANDLE_VALUE && SetFileTime(h, nullptr, nullptr, &ft);
                if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
                return ok;
            };
            std::vector<ParteBajada> partes;
            for (int i = 1; i <= 4; i++)
            {
                ParteBajada p;
                p.descarga = (uint64_t)i;
                p.ruta = b + L"\\juego.part" + std::to_wstring(i) + L".rar";
                EscribirSeguro(p.ruta, std::string(1000, (char)('A' + i)));
                LeerIdentidad(p.ruta, p.identidad);
                partes.push_back(p);
            }
            // Parte 2: reemplazada por OTRO archivo del mismo tamaño, y con la misma fecha
            EscribirSeguro(partes[1].ruta, std::string(1000, 'X'));
            bool mismaFecha = fijarFecha(partes[1].ruta, partes[1].identidad.modificado);
            IdentidadArchivo reemplazo;
            LeerIdentidad(partes[1].ruta, reemplazo);
            // Parte 3: el mismo archivo, cambiado por dentro (mismo tamaño) y con otra fecha
            {
                HANDLE h = CreateFileW(partes[2].ruta.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                std::string otro(1000, 'Y');
                DWORD n = 0;
                if (h != INVALID_HANDLE_VALUE)
                {
                    WriteFile(h, otro.data(), (DWORD)otro.size(), &n, nullptr);
                    CloseHandle(h);
                }
                fijarFecha(partes[2].ruta, partes[2].identidad.modificado + 10000000);   // 1 s después
            }
            // Parte 4: sin la identidad de cuando terminó (no se puede saber si es la misma)
            partes[3].identidad = {};
            // Parte 5: ya no está
            ParteBajada falta = partes[0];
            falta.descarga = 5;
            falta.ruta = b + L"\\juego.part5.rar";
            partes.push_back(falta);
            std::vector<std::wstring> aLaPapelera;
            auto r = BorrarPartes(partes, [&](const std::wstring& ruta) {
                aLaPapelera.push_back(ruta);
                return DeleteFileW(ruta.c_str()) != 0;   // en la prueba, sin Papelera: es un archivo de la prueba
            });
            Comprobar(mismaFecha && reemplazo.tamano == partes[1].identidad.tamano && reemplazo.modificado == partes[1].identidad.modificado
                          && reemplazo.volumen == partes[1].identidad.volumen && reemplazo.indice != partes[1].identidad.indice,
                      "jícaro: una parte reemplazada por otro archivo del mismo tamaño y fecha tiene otro número de archivo");
            Comprobar(r.mandadas == 1 && r.distintas == 2 && r.sinIdentidad == 1 && r.faltaban == 1 && r.fallaron == 0 && aLaPapelera.size() == 1
                          && aLaPapelera[0] == partes[0].ruta && !ExisteArchivo(partes[0].ruta) && ExisteArchivo(partes[1].ruta)
                          && ExisteArchivo(partes[2].ruta) && ExisteArchivo(partes[3].ruta),
                      "jícaro: «Borrar las partes» no toca una parte reemplazada (mismo tamaño), una cambiada ni una sin identidad; avisa cuántas");
            auto otraVez = BorrarPartes({ partes[1] }, [](const std::wstring&) { return false; });
            IdentidadArchivo actual;
            LeerIdentidad(partes[1].ruta, actual);
            ParteBajada laDeAhora = partes[1];
            laDeAhora.identidad = actual;
            auto falla = BorrarPartes({ laDeAhora }, [](const std::wstring&) { return false; });
            Comprobar(otraVez.distintas == 1 && falla.fallaron == 1 && falla.mandadas == 0 && ExisteArchivo(partes[1].ruta),
                      "jícaro: si coincide pero Windows no la manda a la Papelera, se cuenta aparte y se queda");

            // Al recuperar la cola (o con datos de una versión anterior) una parte puede no tener guardado cómo era al
            // terminar: no se toma el archivo de ahora como si fuera ese. Nunca se manda a la Papelera, aunque alguien la
            // haya reemplazado por otro archivo del mismo tamaño o aunque nadie la haya tocado.
            ParteBajada vieja;
            vieja.descarga = 30;
            vieja.ruta = b + L"\\vieja.part1.rar";
            EscribirSeguro(vieja.ruta, std::string(1000, 'V'));
            EscribirSeguro(vieja.ruta, std::string(1000, 'W'));   // reemplazada por otro archivo del mismo tamaño
            ParteBajada intacta;
            intacta.descarga = 31;
            intacta.ruta = b + L"\\vieja.part2.rar";
            EscribirSeguro(intacta.ruta, std::string(1000, 'I'));
            bool llamada = false;
            auto sin = BorrarPartes({ vieja, intacta }, [&](const std::wstring&) {
                llamada = true;
                return true;
            });
            Comprobar(sin.sinIdentidad == 2 && sin.mandadas == 0 && sin.distintas == 0 && !llamada && ExisteArchivo(vieja.ruta)
                          && ExisteArchivo(intacta.ruta),
                      "jícaro: una parte sin la identidad de cuando terminó nunca va a la Papelera (se queda y se avisa aparte)");
            fs::remove_all(b, ec);
        }
    }

    // ---- Videos por partes: HLS y DASH ----

    std::string Http(int codigo, const std::string& tipo, const std::string& cuerpo, const std::string& extra = "")
    {
        std::string razon = codigo == 200 ? "OK" : codigo == 206 ? "Partial Content" : codigo == 404 ? "Not Found" : "X";
        return "HTTP/1.1 " + std::to_string(codigo) + " " + razon + "\r\nContent-Type: " + tipo + "\r\nContent-Length: "
               + std::to_string(cuerpo.size()) + "\r\n" + extra + "Connection: close\r\n\r\n" + cuerpo;
    }

    // Espera a que una descarga del motor termine o se detenga
    Foto EsperarDescarga(Motor& m, uint64_t id)
    {
        Foto f;
        for (int i = 0; i < 300; i++)
        {
            m.Tick();
            if (m.Buscar(id, f) && (f.datos.estado == Estado::Error || f.datos.estado == Estado::Completada)) break;
            Sleep(100);
        }
        m.Detener();
        return f;
    }

    // Baja con el motor de verdad (una descarga normal o un video por partes) y espera a que termine o se detenga
    Foto Bajar(DatosDescarga d, const std::wstring& carpeta)
    {
        Motor m(carpeta + L"\\estado-video.json");
        d.carpeta = carpeta;
        d.conexiones = 3;
        uint64_t id = m.Agregar(d, true);
        return EsperarDescarga(m, id);
    }

    Foto BajarVideo(const std::string& url, const std::string& formato, const std::string& pista, bool local, const std::wstring& carpeta,
                    const std::wstring& nombre, const std::vector<std::string>& otrasListas = {})
    {
        DatosDescarga d;
        d.url = url;
        d.formato = formato;
        d.pista = pista;
        d.flujoLocal = local;
        d.nombre = nombre;
        d.flujoListas = otrasListas;
        return Bajar(d, carpeta);
    }

    void FlujosPruebas()
    {
        using namespace hnd::flujos;

        // 1. Direcciones relativas, como un navegador
        const std::string base = "https://cdn.ejemplo.org/videos/peli/index.m3u8?token=abc";
        Comprobar(Resolver(base, "seg_001.ts") == "https://cdn.ejemplo.org/videos/peli/seg_001.ts"
                      && Resolver(base, "../otra/seg.ts?x=1") == "https://cdn.ejemplo.org/videos/otra/seg.ts?x=1"
                      && Resolver(base, "/raiz/seg.ts") == "https://cdn.ejemplo.org/raiz/seg.ts"
                      && Resolver(base, "//otro.cdn.net/a.ts") == "https://otro.cdn.net/a.ts"
                      && Resolver(base, "http://x.org/b.ts") == "http://x.org/b.ts"
                      && Resolver(base, "720p/./index.m3u8#y") == "https://cdn.ejemplo.org/videos/peli/720p/index.m3u8"
                      && Resolver("http://h:8080", "a.ts") == "http://h:8080/a.ts",
                  "videos: direcciones relativas de la lista, como un navegador");

        // 2. HLS: lista maestra con calidades y audio aparte
        std::string maestra = "\xEF\xBB\xBF#EXTM3U\r\n"
                              "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\",NAME=\"Español\",LANGUAGE=\"es\",DEFAULT=YES,URI=\"audio/es.m3u8\"\r\n"
                              "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\",NAME=\"English\",LANGUAGE=\"en\",URI=\"audio/en.m3u8\"\r\n"
                              "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS=\"avc1.4d401e,mp4a.40.2\"\r\n"
                              "360p/index.m3u8\r\n"
                              "#EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1920x1080,CODECS=\"avc1.640028,mp4a.40.2\",AUDIO=\"aac\"\r\n"
                              "1080p/index.m3u8\r\n"
                              "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=100000,URI=\"iframes.m3u8\"\r\n"
                              "#EXT-X-STREAM-INF:BANDWIDTH=2500000,RESOLUTION=1280x720\r\n"
                              "720p/index.m3u8\r\n";
        auto m = LeerHls(maestra, base);
        int mejor = MejorVariante(m);
        Comprobar(m.valida && m.maestra && !m.cifrada && !m.enVivo && m.variantes.size() == 3 && mejor == 1
                      && m.variantes[1].url == "https://cdn.ejemplo.org/videos/peli/1080p/index.m3u8" && m.variantes[1].alto == 1080
                      && m.variantes[1].banda == 5000000 && m.audios.size() == 2 && AudioDeVariante(m, 1) == 0
                      && m.audios[0].url == "https://cdn.ejemplo.org/videos/peli/audio/es.m3u8" && AudioDeVariante(m, 0) == -1,
                  "videos: HLS maestra: calidades, la mejor (1080p) y su audio aparte; las de solo cuadros clave no cuentan");
        Comprobar(LeerHls("#EXTM3U\n#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES,URI=\"skd://x\"\n#EXT-X-STREAM-INF:BANDWIDTH=1\na.m3u8\n", base).cifrada,
                  "videos: HLS maestra con EXT-X-SESSION-KEY: protegida");

        // 3. HLS: lista de partes
        std::string partes = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg0.ts\n#EXTINF:4.0,\nseg1.ts\n"
                             "#EXTINF:2.5,título\nhttps://otro.cdn.net/seg2.ts\n#EXT-X-ENDLIST\n";
        auto p = LeerHls(partes, base);
        Comprobar(p.valida && !p.maestra && !p.cifrada && !p.enVivo && p.trozos.size() == 3 && std::abs(p.duracion - 10.5) < 1e-6
                      && p.trozos[0].url == "https://cdn.ejemplo.org/videos/peli/seg0.ts" && p.trozos[2].url == "https://otro.cdn.net/seg2.ts"
                      && p.trozos[0].desde == -1 && ExtensionHls(p) == L".ts",
                  "videos: HLS de partes: direcciones, duración y extensión .ts");
        auto sinFin = LeerHls("#EXTM3U\n#EXTINF:4,\na.ts\n", base);
        auto vod = LeerHls("#EXTM3U\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:4,\na.ts\n", base);
        Comprobar(sinFin.enVivo && !vod.enVivo && !LeerHls("hola\n#EXTM3U\n", base).valida && !LeerHls("<html>", base).valida,
                  "videos: HLS sin EXT-X-ENDLIST es en vivo (salvo VOD); un texto cualquiera no es una lista");
        bool todasCifradas = true;
        for (const char* metodo : { "AES-128", "SAMPLE-AES", "SAMPLE-AES-CTR", "NONE" })
            todasCifradas = todasCifradas
                            && LeerHls(std::string("#EXTM3U\n#EXT-X-KEY:METHOD=") + metodo + ",URI=\"k.bin\"\n#EXTINF:4,\na.ts\n#EXT-X-ENDLIST\n", base).cifrada;
        Comprobar(todasCifradas, "videos: HLS con EXT-X-KEY (con cualquier método): protegida, no se baja");
        auto cmaf = LeerHls("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\",BYTERANGE=\"800@0\"\n#EXTINF:2,\nparte1.m4s\n#EXTINF:2,\nparte2.m4s\n#EXT-X-ENDLIST\n", base);
        Comprobar(cmaf.inicio.url == "https://cdn.ejemplo.org/videos/peli/init.mp4" && cmaf.inicio.desde == 0 && cmaf.inicio.largo == 800
                      && cmaf.trozos.size() == 2 && ExtensionHls(cmaf) == L".mp4",
                  "videos: HLS CMAF/fMP4: su EXT-X-MAP va primero y el archivo es .mp4");
        auto rangos = LeerHls("#EXTM3U\n#EXTINF:2,\n#EXT-X-BYTERANGE:1000@0\ntodo.ts\n#EXTINF:2,\n#EXT-X-BYTERANGE:1500\ntodo.ts\n"
                              "#EXTINF:2,\n#EXT-X-BYTERANGE:500@2500\ntodo.ts\n#EXTINF:2,\n#EXT-X-BYTERANGE:40\notro.ts\n#EXT-X-ENDLIST\n", base);
        Comprobar(rangos.trozos.size() == 4 && rangos.trozos[0].desde == 0 && rangos.trozos[0].largo == 1000 && rangos.trozos[1].desde == 1000
                      && rangos.trozos[1].largo == 1500 && rangos.trozos[2].desde == 2500 && rangos.trozos[3].desde == 0,
                  "videos: HLS con EXT-X-BYTERANGE: trozos de un archivo (sin «@» sigue donde terminó el anterior del mismo archivo)");

        // 4. DASH
        std::string mpd =
            "<?xml version=\"1.0\"?>\n<!-- un comentario -->\n"
            "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"static\" mediaPresentationDuration=\"PT0H0M10.5S\">\n"
            " <BaseURL>https://cdn.ejemplo.org/dash/</BaseURL>\n"
            " <Period>\n"
            "  <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\">\n"
            "   <SegmentTemplate timescale=\"1000\" duration=\"4000\" startNumber=\"1\" initialization=\"$RepresentationID$/init.mp4\""
            " media=\"$RepresentationID$/seg-$Number%05d$.m4s?a=1&amp;b=2\"/>\n"
            "   <Representation id=\"v360\" bandwidth=\"800000\" width=\"640\" height=\"360\" codecs=\"avc1.4d401e\"/>\n"
            "   <Representation id=\"v1080\" bandwidth=\"5000000\" width=\"1920\" height=\"1080\" codecs=\"avc1.640028\"/>\n"
            "  </AdaptationSet>\n"
            "  <AdaptationSet contentType=\"audio\" mimeType=\"audio/mp4\">\n"
            "   <Representation id=\"a128\" bandwidth=\"128000\" codecs=\"mp4a.40.2\">\n"
            "    <BaseURL>audio/</BaseURL>\n"
            "    <SegmentTemplate timescale=\"48000\" initialization=\"init-$Bandwidth$.mp4\" media=\"t$Time$.m4s\">\n"
            "     <SegmentTimeline><S t=\"0\" d=\"96000\" r=\"2\"/><S d=\"48000\"/></SegmentTimeline>\n"
            "    </SegmentTemplate>\n"
            "   </Representation>\n"
            "  </AdaptationSet>\n"
            "  <AdaptationSet mimeType=\"text/vtt\"><Representation id=\"sub\" bandwidth=\"1\"/></AdaptationSet>\n"
            " </Period>\n</MPD>\n";
        auto dash = LeerDash(mpd, "https://sitio.org/ver/manifiesto.mpd");
        int mv = MejorRepresentacion(dash.videos), ma = MejorRepresentacion(dash.audios);
        // Al leer el manifiesto no se arma ninguna dirección: solo se cuentan las partes
        bool sinArmar = dash.videos.size() == 2 && dash.videos[1].trozos.empty() && dash.videos[1].partes == 3 && dash.audios.size() == 1
                        && dash.audios[0].trozos.empty() && dash.audios[0].partes == 4;
        size_t bytesDash = 0;
        bool armadas = sinArmar && ArmarTrozos(dash.videos[1], bytesDash) && ArmarTrozos(dash.audios[0], bytesDash) && bytesDash > 0
                       && dash.videos[0].trozos.empty();
        Comprobar(sinArmar && armadas, "videos: DASH: al leer el manifiesto solo se cuentan las partes; se arman las de lo que se baja");
        Comprobar(dash.valido && !dash.protegido && !dash.enVivo && !dash.variasPartes && std::abs(dash.duracion - 10.5) < 1e-6
                      && dash.videos.size() == 2 && dash.audios.size() == 1 && mv == 1 && dash.videos[1].trozos.size() == 3
                      && dash.videos[1].inicio.url == "https://cdn.ejemplo.org/dash/v1080/init.mp4"
                      && dash.videos[1].trozos[0].url == "https://cdn.ejemplo.org/dash/v1080/seg-00001.m4s?a=1&b=2"
                      && dash.videos[1].trozos[2].url == "https://cdn.ejemplo.org/dash/v1080/seg-00003.m4s?a=1&b=2"
                      && ExtensionDash(dash.videos[1]) == L".mp4",
                  "videos: DASH con SegmentTemplate: la mejor calidad, sus partes ($Number%05d$, sin pasarse del final) y su BaseURL");
        Comprobar(ma == 0 && dash.audios[0].inicio.url == "https://cdn.ejemplo.org/dash/audio/init-128000.mp4" && dash.audios[0].trozos.size() == 4
                      && dash.audios[0].trozos[1].url == "https://cdn.ejemplo.org/dash/audio/t96000.m4s"
                      && dash.audios[0].trozos[3].url == "https://cdn.ejemplo.org/dash/audio/t288000.m4s" && ExtensionDash(dash.audios[0]) == L".m4a"
                      && BuscarRepresentacion(dash.videos, "v360") == 0 && BuscarRepresentacion(dash.videos, "no") == -1,
                  "videos: DASH con SegmentTimeline ($Time$ y repeticiones) para el audio, que va en otro archivo (.m4a)");
        std::string lista = "<MPD mediaPresentationDuration=\"PT20S\"><Period><AdaptationSet mimeType=\"video/webm\">"
                            "<Representation id=\"1\" bandwidth=\"1000\"><BaseURL>http://h.org/v.webm</BaseURL><SegmentList>"
                            "<Initialization range=\"0-499\"/><SegmentURL mediaRange=\"500-999\"/><SegmentURL media=\"p2.webm\"/></SegmentList>"
                            "</Representation></AdaptationSet><AdaptationSet mimeType=\"audio/mp4\"><Representation id=\"2\" bandwidth=\"64000\">"
                            "<BaseURL>a.m4a</BaseURL><SegmentBase indexRange=\"0-100\"/></Representation></AdaptationSet></Period></MPD>";
        auto dl = LeerDash(lista, "http://h.org/m.mpd");
        size_t bytesLista = 0;
        if (!dl.videos.empty()) ArmarTrozos(dl.videos[0], bytesLista);
        if (!dl.audios.empty()) ArmarTrozos(dl.audios[0], bytesLista);
        Comprobar(dl.valido && dl.videos.size() == 1 && dl.videos[0].inicio.url == "http://h.org/v.webm" && dl.videos[0].inicio.desde == 0
                      && dl.videos[0].inicio.largo == 500 && dl.videos[0].trozos.size() == 2 && dl.videos[0].trozos[0].desde == 500
                      && dl.videos[0].trozos[1].url == "http://h.org/p2.webm" && ExtensionDash(dl.videos[0]) == L".webm"
                      && dl.audios.size() == 1 && dl.audios[0].unArchivo && dl.audios[0].trozos.size() == 1
                      && dl.audios[0].trozos[0].url == "http://h.org/a.m4a",
                  "videos: DASH con SegmentList (rangos) y con SegmentBase (todo el medio en un archivo)");
        auto conDrm = LeerDash("<MPD><Period><AdaptationSet mimeType=\"video/mp4\"><ContentProtection schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" "
                               "value=\"cenc\"/><Representation id=\"1\" bandwidth=\"1\"><BaseURL>v.mp4</BaseURL></Representation></AdaptationSet>"
                               "</Period></MPD>", "http://h.org/m.mpd");
        auto drmEnRep = LeerDash("<mpd:MPD xmlns:mpd=\"urn:x\"><mpd:Period><mpd:AdaptationSet mimeType=\"video/mp4\"><mpd:Representation id=\"1\">"
                                 "<mpd:ContentProtection schemeIdUri=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\"><cenc:pssh>AAAA</cenc:pssh>"
                                 "</mpd:ContentProtection></mpd:Representation></mpd:AdaptationSet></mpd:Period></mpd:MPD>", "http://h.org/m.mpd");
        Comprobar(conDrm.valido && conDrm.protegido && drmEnRep.valido && drmEnRep.protegido,
                  "videos: DASH con ContentProtection (en el conjunto o en la representación, con o sin prefijo): protegido");
        auto vivo = LeerDash("<MPD type=\"dynamic\"><Period/></MPD>", "http://h.org/m.mpd");
        auto dos = LeerDash("<MPD><Period duration=\"PT5S\"/><Period duration=\"PT5S\"/></MPD>", "http://h.org/m.mpd");
        Comprobar(vivo.valido && vivo.enVivo && dos.variasPartes && !LeerDash("<html><body>hola</body></html>", "").valido
                      && !LeerDash("<MPD><Period", "").valido,
                  "videos: DASH en vivo, con varios períodos o mal hecho: no se baja");

        // 5. Lo que no se baja: YouTube, sitios con DRM y lo que no es de internet
        Trozo sin;
        std::vector<Trozo> bien = { { "https://cdn.ejemplo.org/a.ts" }, { "http://cdn.ejemplo.org/b.ts" } };
        Comprobar(RevisarTrozos(sin, bien).empty() && RevisarTrozos(sin, { { "https://rr3---sn-a5m.googlevideo.com/videoplayback?x" } }) == Utf8(enlaces::TextoNoPermitido)
                      && RevisarTrozos(sin, { { "https://www.youtube-nocookie.com/a.ts" } }) == Utf8(enlaces::TextoNoPermitido)
                      && RevisarTrozos({ "https://m.youtube.com/i.mp4" }, bien) == Utf8(enlaces::TextoNoPermitido)
                      && RevisarTrozos(sin, { { "https://ipv4-c001.1.nflxvideo.net/range/0-1" } }) == TextoProtegido
                      && !RevisarTrozos(sin, { { "file:///C:/Windows/win.ini" } }).empty() && !RevisarTrozos(sin, { { "ftp://h.org/a.ts" } }).empty()
                      && !RevisarTrozos(sin, {}).empty(),
                  "videos: partes de YouTube o de googlevideo.com (también el inicio), de un sitio con DRM o que no son http: no se bajan");
        Comprobar(enlaces::SitioProtegido("https://www.netflix.com/watch/1") && enlaces::SitioProtegido("https://www.disneyplus.com/video/x")
                      && enlaces::SitioProtegido("https://www.primevideo.com/detail") && enlaces::SitioProtegido("https://www.amazon.com/gp/video/detail/B0")
                      && enlaces::SitioProtegido("https://play.max.com/") && enlaces::SitioProtegido("https://tv.apple.com/")
                      && !enlaces::SitioProtegido("https://www.amazon.com/dp/B0") && !enlaces::SitioProtegido("https://archive.org/details/x")
                      && !enlaces::SitioProtegido("https://notmax.com/") && enlaces::NoPermitido("https://m.youtube.com/watch?v=1"),
                  "videos: sitios con DRM (Netflix, Disney+, Prime Video, Max...); una tienda de Amazon o Internet Archive no");
        Comprobar(TextoCalidad(1920, 1080, 5000000, false) == L"1080p · 5.0 Mb/s" && TextoCalidad(0, 0, 128000, true) == L"Audio · 128 kb/s"
                      && TextoCalidad(0, 0, 0, false) == L"Video",
                  "videos: la calidad para la persona («1080p · 5.0 Mb/s»)");

        // 6. Juntar en orden aunque lleguen desordenadas
        {
            std::string archivo;
            auto escribir = [&](int64_t pos, const char* d, size_t n) {
                if (archivo.size() < (size_t)pos + n) archivo.resize((size_t)pos + n);
                memcpy(&archivo[(size_t)pos], d, n);
                return true;
            };
            Ensamblador e(4, 0, 0, escribir);
            e.Pedazo(2, "CC", 2);
            e.Terminar(2);
            e.Pedazo(0, "AA", 2);
            e.Pedazo(1, "B", 1);
            e.Pedazo(0, "A", 1);
            bool enEspera = e.EnMemoria() == 3 && e.Siguiente() == 0;
            e.Terminar(0);
            e.Pedazo(1, "B", 1);
            e.Pedazo(1, "XYZ", 3);   // a mitad, falla: se descarta y se baja otra vez
            e.Descartar(1);
            e.Pedazo(1, "BB", 2);
            e.Terminar(1);
            e.Pedazo(3, "DDD", 3);
            e.Terminar(3);
            Comprobar(enEspera && e.Completo() && e.BytesEscritos() == 10 && archivo.substr(0, 10) == "AAABBCCDDD" && e.EnMemoria() == 0,
                      "videos: las partes se juntan en orden aunque lleguen desordenadas; una que falla a mitad se baja otra vez");
            std::string seguido = "AAAB";
            auto sobre = [&](int64_t pos, const char* d, size_t n) {
                if (seguido.size() < (size_t)pos + n) seguido.resize((size_t)pos + n);
                memcpy(&seguido[(size_t)pos], d, n);
                return true;
            };
            Ensamblador r(3, 2, 4, sobre);   // al volver a abrir: ya había dos partes (4 bytes)
            r.Pedazo(2, "CC", 2);
            r.Terminar(2);
            Comprobar(r.Completo() && seguido == "AAABCC", "videos: al reanudar sigue después de las partes que ya estaban juntas");
        }

        // 7. Con el motor de verdad y un servidor de prueba: bajar, juntar y lo que no se baja
        namespace fs = std::filesystem;
        std::wstring carpeta = (fs::temp_directory_path() / (L"atarraya-prueba-videos-" + std::to_wstring(GetCurrentProcessId()))).wstring();
        std::error_code ec;
        fs::remove_all(carpeta, ec);
        fs::create_directories(carpeta, ec);
        SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", carpeta.c_str());
        auto parte = [](int i) { return std::string((size_t)(900 + i * 37), (char)('a' + i)); };
        std::string todo;
        for (int i = 0; i < 6; i++) todo += parte(i);
        std::string init = "INIT-fMP4-ftyp-moov";
        std::string rango3000;
        for (int i = 0; i < 3000; i++) rango3000 += (char)('0' + i % 10);
        std::string listaHls = "#EXTM3U\n#EXT-X-PLAYLIST-TYPE:VOD\n";
        std::string listaCmaf = "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n";
        for (int i = 0; i < 6; i++)
        {
            listaHls += "#EXTINF:2,\nseg" + std::to_string(i) + ".ts\n";
            listaCmaf += "#EXTINF:2,\nseg" + std::to_string(i) + ".m4s\n";
        }
        listaHls += "#EXT-X-ENDLIST\n";
        listaCmaf += "#EXT-X-ENDLIST\n";
        std::string mpdBien = "<MPD mediaPresentationDuration=\"PT12S\"><Period><AdaptationSet mimeType=\"video/mp4\">"
                              "<SegmentTemplate duration=\"2\" initialization=\"init-$RepresentationID$.mp4\" media=\"$RepresentationID$-$Number$.m4s\"/>"
                              "<Representation id=\"v1\" bandwidth=\"1000\" width=\"320\" height=\"180\"/></AdaptationSet>"
                              "<AdaptationSet mimeType=\"audio/mp4\"><SegmentTemplate duration=\"2\" initialization=\"init-$RepresentationID$.mp4\" "
                              "media=\"$RepresentationID$-$Number$.m4s\"/><Representation id=\"a1\" bandwidth=\"64000\"/></AdaptationSet></Period></MPD>";
        std::atomic<int> pedidosDeYoutube{ 0 };
        // Para la revisión: un archivo único (DASH de un solo archivo, se baja por rangos), una lista enorme y listas de
        // audio o subtítulos, bien o cifradas
        std::string unico;
        for (int i = 0; i < 5000; i++) unico += (char)('A' + i % 26);
        std::string enorme = "#EXTM3U\n#EXT-X-PLAYLIST-TYPE:VOD\n";
        for (size_t i = 0; i <= MaximoPartes; i++) enorme += "#EXTINF:1,\ns" + std::to_string(i) + ".ts\n";
        enorme += "#EXT-X-ENDLIST\n";
        // Un DASH con 150.000 partes de direcciones de 8 KB (más de 1 GB si se armaran todas)
        std::string mpdLargo = "<MPD mediaPresentationDuration=\"PT150000S\"><Period><BaseURL>https://cdn.ejemplo.org/" + std::string(8000, 'a')
                               + "/</BaseURL><AdaptationSet mimeType=\"video/mp4\"><SegmentTemplate duration=\"1\" media=\"$Number$.m4s\"/>"
                                 "<Representation id=\"v\" bandwidth=\"1\"/></AdaptationSet></Period></MPD>";
        // El archivo único, entero o por rangos (como lo pide el motor)
        auto servirUnico = [&](const std::string& r) {
            if (r.rfind("bytes=", 0) != 0) return Http(200, "video/mp4", unico, "Accept-Ranges: bytes\r\n");
            int64_t a = _atoi64(r.c_str() + 6);
            std::string fin = r.substr(r.find('-') + 1);
            int64_t b = fin.empty() ? (int64_t)unico.size() - 1 : std::min<int64_t>(_atoi64(fin.c_str()), (int64_t)unico.size() - 1);
            return Http(206, "video/mp4", unico.substr((size_t)a, (size_t)(b - a + 1)),
                        "Accept-Ranges: bytes\r\nContent-Range: bytes " + std::to_string(a) + "-" + std::to_string(b) + "/"
                            + std::to_string(unico.size()) + "\r\n");
        };
        ServidorDePrueba web(ServidorDePrueba::PorRuta{ [&](const std::string& ruta, const std::string& r) -> std::string {
            if (ruta == "/unico/peli.mp4") return servirUnico(r);
            if (ruta == "/redir/lista.m3u8") return Http(302, "text/plain", "", "Location: /hls/lista.m3u8\r\n");
            if (ruta == "/audio/bien.m3u8")
                return Http(200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXTINF:2,\n/hls/seg0.ts\n#EXTINF:2,\n/hls/seg1.ts\n#EXT-X-ENDLIST\n");
            if (ruta == "/subs/cifrados.m3u8")
                return Http(200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"k.bin\"\n#EXTINF:2,\n/subs/s0.vtt\n#EXT-X-ENDLIST\n");
            if (ruta == "/maestra/master.m3u8")
                return Http(200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"es\",DEFAULT=YES,"
                            "URI=\"/cifrada/lista.m3u8\"\n#EXT-X-STREAM-INF:BANDWIDTH=1000,AUDIO=\"a\"\n/hls/lista.m3u8\n");
            if (ruta == "/enorme/lista.m3u8") return Http(200, "application/vnd.apple.mpegurl", enorme);
            if (ruta == "/largas/manifiesto.mpd") return Http(200, "application/dash+xml", mpdLargo);
            if (ruta == "/hls/lista.m3u8") return Http(200, "application/vnd.apple.mpegurl", listaHls);
            if (ruta == "/cmaf/lista.m3u8") return Http(200, "application/vnd.apple.mpegurl", listaCmaf);
            if (ruta == "/cmaf/init.mp4") return Http(200, "video/mp4", init);
            if (ruta.rfind("/hls/seg", 0) == 0 || ruta.rfind("/cmaf/seg", 0) == 0)
            {
                int i = atoi(ruta.c_str() + ruta.find("seg") + 3);
                return Http(200, ruta.find(".ts") != std::string::npos ? "video/mp2t" : "video/iso.segment", parte(i));
            }
            if (ruta == "/cifrada/lista.m3u8")
                return Http(200, "application/x-mpegURL", "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"clave.bin\"\n#EXTINF:2,\nseg0.ts\n#EXT-X-ENDLIST\n");
            if (ruta == "/pagina/lista.m3u8") return Http(200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXTINF:2,\nseg0.ts\n#EXTINF:2,\nseg1.ts\n#EXT-X-ENDLIST\n");
            if (ruta == "/pagina/seg0.ts") return Http(200, "video/mp2t", parte(0));
            if (ruta == "/pagina/seg1.ts") return Http(200, "text/html; charset=utf-8", "<html><body>Inicia sesion</body></html>");
            if (ruta == "/yt/lista.m3u8")
                return Http(200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXTINF:2,\nhttps://rr1---sn-x.googlevideo.com/videoplayback?a=1\n#EXT-X-ENDLIST\n");
            if (ruta == "/rango/lista.m3u8")
                return Http(200, "application/vnd.apple.mpegurl",
                            "#EXTM3U\n#EXTINF:2,\n#EXT-X-BYTERANGE:1000@0\ntodo.ts\n#EXTINF:2,\n#EXT-X-BYTERANGE:1500\ntodo.ts\n#EXTINF:2,\n"
                            "#EXT-X-BYTERANGE:500@2500\ntodo.ts\n#EXT-X-ENDLIST\n");
            if (ruta == "/rango/todo.ts" && r.rfind("bytes=", 0) == 0)
            {
                int64_t a = _atoi64(r.c_str() + 6), b = _atoi64(r.c_str() + r.find('-') + 1);
                return Http(206, "video/mp2t", rango3000.substr((size_t)a, (size_t)(b - a + 1)),
                            "Content-Range: bytes " + std::to_string(a) + "-" + std::to_string(b) + "/3000\r\n");
            }
            if (ruta == "/dash/manifiesto.mpd") return Http(200, "application/dash+xml", mpdBien);
            if (ruta == "/dash/init-v1.mp4" || ruta == "/dash/init-a1.mp4") return Http(200, "video/mp4", init + ruta.substr(11, 2));
            if (ruta.rfind("/dash/v1-", 0) == 0 || ruta.rfind("/dash/a1-", 0) == 0)
                return Http(200, "video/iso.segment", parte(atoi(ruta.c_str() + 9)) + ruta.substr(6, 2));
            if (ruta == "/dashdrm/manifiesto.mpd")
                return Http(200, "application/dash+xml", "<MPD><Period><AdaptationSet mimeType=\"video/mp4\"><ContentProtection "
                            "schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\"/><Representation id=\"v1\" bandwidth=\"1\">"
                            "<BaseURL>v.mp4</BaseURL></Representation></AdaptationSet></Period></MPD>");
            if (ruta.find("googlevideo") != std::string::npos) pedidosDeYoutube++;
            return Http(404, "text/plain", "no existe");
        } });
        std::string w = "http://127.0.0.1:" + std::to_string(web.Puerto());
        auto leer = [&](const std::wstring& nombre) {
            std::string s;
            LeerArchivo(carpeta + L"\\" + nombre, s);
            return s;
        };

        Foto hls = BajarVideo(w + "/hls/lista.m3u8", "hls", "", true, carpeta, L"Mi video");
        Comprobar(hls.datos.estado == Estado::Completada && hls.datos.nombre == L"Mi video.ts" && leer(L"Mi video.ts") == todo
                      && hls.datos.flujoTotal == 6 && hls.datos.flujoHechos == 6 && hls.datos.tamano == (int64_t)todo.size()
                      && !ExisteArchivo(carpeta + L"\\Mi video.ts.atarraya"),
                  "videos: HLS con el motor: 6 partes con 3 conexiones, juntas en orden en un .ts");
        Foto cm = BajarVideo(w + "/cmaf/lista.m3u8", "hls", "", true, carpeta, L"");
        Comprobar(cm.datos.estado == Estado::Completada && cm.datos.nombre == L"lista.mp4" && leer(L"lista.mp4") == init + todo,
                  "videos: HLS CMAF/fMP4 con el motor: el inicio y las partes en un .mp4");
        Foto rg = BajarVideo(w + "/rango/lista.m3u8", "hls", "", true, carpeta, L"Rangos");
        Comprobar(rg.datos.estado == Estado::Completada && leer(L"Rangos.ts") == rango3000,
                  "videos: HLS con EXT-X-BYTERANGE: los trozos de un archivo, pedidos por rango y juntos");
        Foto dv = BajarVideo(w + "/dash/manifiesto.mpd", "dash", "video:v1", true, carpeta, L"Peli (video)");
        Foto da = BajarVideo(w + "/dash/manifiesto.mpd", "dash", "audio:a1", true, carpeta, L"Peli (audio)");
        std::string esperadoV = init + "v1", esperadoA = init + "a1";
        for (int i = 1; i <= 6; i++)
        {
            esperadoV += parte(i) + "v1";
            esperadoA += parte(i) + "a1";
        }
        Comprobar(dv.datos.estado == Estado::Completada && dv.datos.nombre == L"Peli (video).mp4" && leer(L"Peli (video).mp4") == esperadoV
                      && da.datos.estado == Estado::Completada && da.datos.nombre == L"Peli (audio).m4a" && leer(L"Peli (audio).m4a") == esperadoA,
                  "videos: DASH con el motor: el video y el audio en dos archivos, sin unir");
        int antes = web.Pedidos();
        Foto cif = BajarVideo(w + "/cifrada/lista.m3u8", "hls", "", true, carpeta, L"Cifrada");
        Foto drm = BajarVideo(w + "/dashdrm/manifiesto.mpd", "dash", "video:v1", true, carpeta, L"Drm");
        Comprobar(cif.datos.estado == Estado::Error && cif.datos.error == TextoProtegido && drm.datos.estado == Estado::Error
                      && drm.datos.error == TextoProtegido && web.Pedidos() == antes + 2 && !ExisteArchivo(carpeta + L"\\Cifrada.ts")
                      && !ExisteArchivo(carpeta + L"\\Cifrada.ts.atarraya"),
                  "videos: HLS con EXT-X-KEY y DASH con ContentProtection: «protegido», no se baja ni una parte");
        Foto pag = BajarVideo(w + "/pagina/lista.m3u8", "hls", "", true, carpeta, L"Pagina");
        Foto yt = BajarVideo(w + "/yt/lista.m3u8", "hls", "", true, carpeta, L"Yt");
        Comprobar(pag.datos.estado == Estado::Error && pag.datos.error == TextoRespondioPagina && !ExisteArchivo(carpeta + L"\\Pagina.ts")
                      && yt.datos.estado == Estado::Error && yt.datos.error == Utf8(enlaces::TextoNoPermitido) && pedidosDeYoutube == 0,
                  "videos: una parte que llega como página web no se guarda; una lista con partes de googlevideo.com no se baja");
        antes = web.Pedidos();
        Foto lejos = BajarVideo(w + "/hls/lista.m3u8", "hls", "", false, carpeta, L"Lejos");
        Comprobar(lejos.datos.estado == Estado::Error && lejos.datos.error == TextoRedLocal && web.Pedidos() == antes,
                  "videos: una lista de internet no puede pedir nada de esta PC ni de su red (ni se conecta)");

        // 8. Un DASH de internet que apunta a esta PC o a la red de la casa: ni su archivo único, ni su inicio, ni sus partes
        {
            auto unicoDash = LeerDash("<MPD mediaPresentationDuration=\"PT10S\"><Period><AdaptationSet mimeType=\"video/mp4\"><Representation "
                                      "id=\"v\" bandwidth=\"1\"><BaseURL>http://127.0.0.1:8080/peli.mp4</BaseURL><SegmentBase indexRange=\"0-99\"/>"
                                      "</Representation></AdaptationSet></Period></MPD>", "https://cdn.ejemplo.org/m.mpd");
            bool locales = true;
            for (const char* u : { "http://127.0.0.1/a.ts", "http://192.168.1.1/a.ts", "http://10.0.0.5/a.ts", "http://172.16.0.9/a.ts",
                                   "http://router.lan/a.ts", "http://localhost./a.ts", "http://[::1]/a.ts", "http://0x7f.1/a.ts", "http://nas/a.ts" })
                locales = locales && RevisarTrozos(Trozo{}, { { u } }, true) == TextoRedLocal;
            Comprobar(unicoDash.valido && unicoDash.videos.size() == 1 && unicoDash.videos[0].unArchivo
                          && RevisarTrozos(unicoDash.videos[0].inicio, unicoDash.videos[0].trozos, true) == TextoRedLocal
                          && RevisarTrozos(unicoDash.videos[0].inicio, unicoDash.videos[0].trozos, false).empty()
                          && RevisarTrozos({ "http://192.168.0.2/init.mp4" }, { { "https://cdn.ejemplo.org/a.m4s" } }, true) == TextoRedLocal
                          && RevisarTrozos(Trozo{}, { { "https://cdn.ejemplo.org/a.m4s" } }, true).empty() && locales,
                      "videos: un DASH de internet que apunta a esta PC o a la red de la casa (archivo único, inicio o partes) no se baja");

            // Su archivo único va como descarga normal (con varias conexiones), pero con «soloAfuera»: no se conecta ni
            // por el nombre ni por un nombre de afuera que lleva aquí (un DNS que miente)
            int antesUnico = web.Pedidos();
            DatosDescarga d;
            d.url = w + "/unico/peli.mp4";
            d.nombre = L"Unico.mp4";
            d.nombreFijo = true;
            d.soloAfuera = true;
            Foto porNombre = Bajar(d, carpeta);
            std::string afuera = "video.ejemplo-afuera.org:" + std::to_string(web.Puerto());
            pruebas::Resolver(afuera + ":127.0.0.1");
            d.url = "http://" + afuera + "/unico/peli.mp4";
            Foto porDns = Bajar(d, carpeta);
            int despuesDeFrenar = web.Pedidos();
            d.soloAfuera = false;   // la persona confirmó que el video es de su red: entonces sí
            Foto confirmado = Bajar(d, carpeta);
            pruebas::Resolver("");
            Comprobar(porNombre.datos.estado == Estado::Error && porNombre.datos.error == TextoRedLocal && porDns.datos.estado == Estado::Error
                          && porDns.datos.error == TextoRedLocal && despuesDeFrenar == antesUnico && !ExisteArchivo(carpeta + L"\\Unico.mp4.atarraya")
                          && confirmado.datos.estado == Estado::Completada && leer(L"Unico.mp4") == unico,
                      "videos: el archivo único de un DASH de internet no se conecta con esta PC ni con su red (tampoco por un DNS que miente)");
        }

        // 9. HLS: la calidad elegida está bien, pero su audio o sus subtítulos están cifrados: no se baja ni una parte
        {
            std::string maestraSub = "#EXTM3U\n"
                                     "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"es\",DEFAULT=YES,URI=\"audio/es.m3u8\"\n"
                                     "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s\",NAME=\"es\",URI=\"subs/es.m3u8\"\n"
                                     "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s\",NAME=\"en\",URI=\"subs/en.m3u8\"\n"
                                     "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"otro\",NAME=\"fr\",URI=\"subs/fr.m3u8\"\n"
                                     "#EXT-X-STREAM-INF:BANDWIDTH=1000,AUDIO=\"a\",SUBTITLES=\"s\"\nv/index.m3u8\n";
            auto ms = LeerHls(maestraSub, "https://cdn.ejemplo.org/p/master.m3u8");
            auto juntas = ListasJuntas(ms, 0);
            Comprobar(ms.maestra && ms.subtitulos.size() == 3 && juntas.size() == 3 && juntas[0] == "https://cdn.ejemplo.org/p/audio/es.m3u8"
                          && juntas[1] == "https://cdn.ejemplo.org/p/subs/es.m3u8" && juntas[2] == "https://cdn.ejemplo.org/p/subs/en.m3u8"
                          && ListasJuntas(ms, 5).empty(),
                      "videos: HLS: las listas que van con una calidad (su audio y sus subtítulos) se leen todas antes de bajar");
            int antesAudio = web.Pedidos();
            Foto conAudioCifrado = BajarVideo(w + "/hls/lista.m3u8", "hls", "", true, carpeta, L"Audio cifrado", { w + "/cifrada/lista.m3u8" });
            int despuesAudio = web.Pedidos();
            Foto conSubsCifrados = BajarVideo(w + "/hls/lista.m3u8", "hls", "", true, carpeta, L"Subs cifrados",
                                              { w + "/audio/bien.m3u8", w + "/subs/cifrados.m3u8" });
            int despuesSubs = web.Pedidos();
            Foto desdeMaestra = BajarVideo(w + "/maestra/master.m3u8", "hls", "", true, carpeta, L"Maestra");
            int despuesMaestra = web.Pedidos();
            Foto conAudioBien = BajarVideo(w + "/hls/lista.m3u8", "hls", "", true, carpeta, L"Audio bien", { w + "/audio/bien.m3u8" });
            Comprobar(conAudioCifrado.datos.estado == Estado::Error && conAudioCifrado.datos.error == TextoProtegido && despuesAudio == antesAudio + 2
                          && conSubsCifrados.datos.error == TextoProtegido && despuesSubs == despuesAudio + 3
                          && desdeMaestra.datos.error == TextoProtegido && despuesMaestra == despuesSubs + 3
                          && !ExisteArchivo(carpeta + L"\\Audio cifrado.ts") && !ExisteArchivo(carpeta + L"\\Audio cifrado.ts.atarraya")
                          && conAudioBien.datos.estado == Estado::Completada && leer(L"Audio bien.ts") == todo,
                      "videos: HLS con el audio o los subtítulos cifrados (aunque el video no lo esté): se leen las listas y no se baja ni una parte");
        }

        // 10. Listas enormes: pasado el tope no se bajan (nunca se recortan ni quedan a medias como terminadas)
        {
            std::string justa = "#EXTM3U\n";
            for (size_t i = 0; i < MaximoPartes; i++) justa += "#EXTINF:1,\ns.ts\n";
            auto lj = LeerHls(justa, base);
            auto le = LeerHls(enorme, base);
            auto pesada = LeerHls(std::string(MaximoLista + 1, '#'), base);
            auto durMucho = LeerDash("<MPD mediaPresentationDuration=\"PT100000S\"><Period><AdaptationSet mimeType=\"video/mp4\"><SegmentTemplate "
                                     "timescale=\"10\" duration=\"5\" media=\"$Number$.m4s\"/><Representation id=\"v\" bandwidth=\"1\"/>"
                                     "</AdaptationSet></Period></MPD>", "https://h.org/m.mpd");
            std::string dosRep = "<MPD mediaPresentationDuration=\"PT80000S\"><Period>"
                                 "<AdaptationSet mimeType=\"video/mp4\"><SegmentTemplate duration=\"1\" media=\"v$Number$.m4s\"/><Representation id=\"v\" bandwidth=\"1\"/></AdaptationSet>"
                                 "<AdaptationSet mimeType=\"audio/mp4\"><SegmentTemplate duration=\"1\" media=\"a$Number$.m4s\"/><Representation id=\"a\" bandwidth=\"1\"/></AdaptationSet>"
                                 "</Period></MPD>";
            auto suma = LeerDash(dosRep, "https://h.org/m.mpd");
            std::string unaRep = dosRep.substr(0, dosRep.find("<AdaptationSet mimeType=\"audio/mp4\">")) + "</Period></MPD>";
            auto sola = LeerDash(unaRep, "https://h.org/m.mpd");
            auto repetida = LeerDash("<MPD mediaPresentationDuration=\"PT10S\"><Period><AdaptationSet mimeType=\"video/mp4\"><SegmentTemplate "
                                     "media=\"$Time$.m4s\"><SegmentTimeline><S t=\"0\" d=\"1\" r=\"1000000000\"/></SegmentTimeline></SegmentTemplate>"
                                     "<Representation id=\"v\" bandwidth=\"1\"/></AdaptationSet></Period></MPD>", "https://h.org/m.mpd");
            size_t bytesSola = 0;
            bool solaBien = !sola.demasiadas && sola.videos.size() == 1 && sola.videos[0].partes == 80000 && sola.videos[0].trozos.empty()
                            && ArmarTrozos(sola.videos[0], bytesSola) && sola.videos[0].trozos.size() == 80000;
            Comprobar(!lj.demasiadas && lj.trozos.size() == MaximoPartes && le.demasiadas && le.trozos.empty() && pesada.demasiadas
                          && durMucho.demasiadas && durMucho.videos.empty() && suma.demasiadas && suma.videos.empty() && suma.audios.empty()
                          && solaBien && repetida.demasiadas && LeerDash(std::string(MaximoLista + 1, ' '), "").demasiadas,
                      "videos: una lista o un manifiesto de más de 150.000 partes (sumando todas las representaciones) o de más de 16 MB se rechaza");
            sola = {};

            // Direcciones muy largas: 150.000 partes de 8 KB pasarían de 1 GB. El manifiesto se lee sin armarlas (solo la
            // receta) y, al armar las de lo que se baja, se deja de armar apenas pasan de 32 MB: el video no se baja.
            std::string larga = "https://cdn.ejemplo.org/" + std::string(8000, 'a') + "/";
            auto largas = LeerDash("<MPD mediaPresentationDuration=\"PT150000S\"><Period><BaseURL>" + larga + "</BaseURL><AdaptationSet "
                                   "mimeType=\"video/mp4\"><SegmentTemplate duration=\"1\" media=\"$Number$.m4s\"/><Representation id=\"v\" "
                                   "bandwidth=\"1\"/></AdaptationSet></Period></MPD>", "https://h.org/m.mpd");
            size_t bytesLargas = 0;
            bool leidas = largas.valido && !largas.demasiadas && largas.videos.size() == 1 && largas.videos[0].partes == MaximoPartes
                          && largas.videos[0].trozos.empty();
            bool frenadas = leidas && !ArmarTrozos(largas.videos[0], bytesLargas) && largas.videos[0].trozos.empty()
                            && largas.videos[0].trozos.capacity() == 0 && bytesLargas > MaximoBytesDirecciones
                            && bytesLargas <= MaximoBytesDirecciones + larga.size() + 64 + sizeof(Trozo);
            // Lo mismo en HLS (una lista con una dirección base muy larga) y en un manifiesto con muchas representaciones largas
            std::string listaLarga = "#EXTM3U\n";
            for (int i = 0; i < 10000; i++) listaLarga += "#EXTINF:1,\ns" + std::to_string(i) + ".ts\n";
            auto hlsLarga = LeerHls(listaLarga, larga + "index.m3u8");
            // 900 calidades (menos que el tope de 1000), cada una con su base de 40 KB y su inicio: lo guardado pasa de 32 MB
            std::string muchas = "<MPD mediaPresentationDuration=\"PT10S\"><Period><BaseURL>https://cdn.ejemplo.org/" + std::string(40000, 'b')
                                 + "/</BaseURL><AdaptationSet mimeType=\"video/mp4\">"
                                   "<SegmentTemplate duration=\"1\" initialization=\"i-$RepresentationID$.mp4\" media=\"$Number$.m4s\"/>";
            for (int i = 0; i < 900; i++) muchas += "<Representation id=\"r" + std::to_string(i) + "\" bandwidth=\"1\"/>";
            muchas += "</AdaptationSet></Period></MPD>";
            auto dashMuchas = LeerDash(muchas, "https://h.org/m.mpd");
            Comprobar(frenadas && hlsLarga.demasiadas && hlsLarga.trozos.empty() && dashMuchas.demasiadas && dashMuchas.videos.empty(),
                      "videos: un DASH con plantilla de muchas partes y direcciones largas no pasa de 32 MB de direcciones (se rechaza con aviso)");
            int antesLargo = web.Pedidos();
            Foto largoMotor = BajarVideo(w + "/largas/manifiesto.mpd", "dash", "video:v", true, carpeta, L"Largas");
            Comprobar(largoMotor.datos.estado == Estado::Error && largoMotor.datos.error == TextoDemasiadas && web.Pedidos() == antesLargo + 1,
                      "videos: el motor rechaza ese DASH sin pedir ni una parte");
            int antesEnorme = web.Pedidos();
            Foto grande = BajarVideo(w + "/enorme/lista.m3u8", "hls", "", true, carpeta, L"Enorme");
            Comprobar(grande.datos.estado == Estado::Error && grande.datos.error == TextoDemasiadas && web.Pedidos() == antesEnorme + 1
                          && !ExisteArchivo(carpeta + L"\\Enorme.ts") && !ExisteArchivo(carpeta + L"\\Enorme.ts.atarraya"),
                      "videos: el motor rechaza una lista enorme con un aviso claro y no baja ni una parte");
        }

        // 11. Memoria: las partes adelantadas tienen un tope de bytes; la que no cabe se baja otra vez cuando es su turno
        {
            std::string archivo;
            auto escribir = [&](int64_t pos, const char* d, size_t n) {
                if (archivo.size() < (size_t)pos + n) archivo.resize((size_t)pos + n);
                memcpy(&archivo[(size_t)pos], d, n);
                return true;
            };
            Ensamblador e(4, 0, 0, escribir, 10);
            bool enMemoria = e.Pedazo(1, "11111111", 8) == Ensamblador::Paso::Bien && e.EnMemoria() == 8;
            bool noCabe = e.Pedazo(2, "222", 3) == Ensamblador::Paso::Lleno && e.EnMemoria() == 8;
            bool directo = e.Pedazo(0, std::string(16, '0').c_str(), 16) == Ensamblador::Paso::Bien && e.EnMemoria() == 8;
            e.Terminar(0);
            e.Terminar(1);
            bool vacio = e.EnMemoria() == 0 && e.Siguiente() == 2;
            e.Pedazo(2, "222", 3);
            e.Terminar(2);
            e.Pedazo(3, "3", 1);
            e.Terminar(3);
            Comprobar(enMemoria && noCabe && directo && vacio && e.Completo() && archivo == std::string(16, '0') + "11111111" + "2223",
                      "videos: las partes adelantadas tienen un tope de memoria; la que no cabe se descarta y se baja en su turno");
            pruebas::TopeDeMemoria(1200);
            Foto poca = BajarVideo(w + "/hls/lista.m3u8", "hls", "", true, carpeta, L"Poca memoria");
            pruebas::TopeDeMemoria(0);
            Comprobar(poca.datos.estado == Estado::Completada && leer(L"Poca memoria.ts") == todo,
                      "videos: con poca memoria para las partes adelantadas, el motor igual junta el video entero y en orden");
        }

        // 12. El programa se cerró de golpe a mitad de una parte: al reanudar, el archivo se corta en la última parte
        //     entera (lo de la parte a medias no se queda pegado antes de la parte entera)
        {
            std::string dosPartes = parte(0) + parte(1);
            auto estadoDe = [&](const std::wstring& nombre, int hechas, int64_t bytes, int64_t hecho) {
                return "{\"version\":1,\"siguienteId\":2,\"descargas\":[{\"id\":1,\"url\":\"" + w + "/hls/lista.m3u8\",\"carpeta\":\""
                       + Utf8(carpeta) + "\",\"nombre\":\"" + Utf8(nombre) + "\",\"estado\":\"pausada\",\"conexiones\":3,"
                       "\"segmentos\":[[0,-1," + std::to_string(hecho) + "]],\"formato\":\"hls\",\"flujoTotal\":6,\"flujoHechos\":"
                       + std::to_string(hechas) + ",\"flujoBytes\":" + std::to_string(bytes) + ",\"flujoLocal\":true}]}";
            };
            auto reanudar = [&](const std::wstring& nombre, int hechas, int64_t bytes, int64_t hecho, const std::string& parcial) {
                std::string json = estadoDe(nombre, hechas, bytes, hecho);
                // Las barras de la carpeta van escapadas en JSON
                std::string escapado;
                for (char ch : json) escapado += ch == '\\' ? std::string("\\\\") : std::string(1, ch);
                EscribirSeguro(carpeta + L"\\estado-corte.json", escapado);
                EscribirSeguro(carpeta + L"\\" + nombre + L".atarraya", parcial);
                Motor m(carpeta + L"\\estado-corte.json");
                m.Cargar();
                m.Reanudar(1);
                return EsperarDescarga(m, 1);
            };
            // Dos partes enteras y 500 bytes de la tercera; «hecho» (el avance que se muestra) llegó a contar esos 500
            Foto corte = reanudar(L"Corte.ts", 2, (int64_t)dosPartes.size(), (int64_t)dosPartes.size() + 500, dosPartes + std::string(500, 'X'));
            // Lo guardado no cuadra (partes juntadas sin bytes): se empieza de cero, sin pegar nada viejo
            Foto raro = reanudar(L"Raro.ts", 2, 0, 777, std::string(777, 'Y'));
            Comprobar(corte.datos.estado == Estado::Completada && leer(L"Corte.ts") == todo && corte.datos.flujoBytes == (int64_t)todo.size()
                          && raro.datos.estado == Estado::Completada && leer(L"Raro.ts") == todo,
                      "videos: si el programa se cerró a mitad de una parte, al reanudar se corta en la última parte entera y el video queda bien");
        }

        // 13. Con un proxy configurado, los videos de las páginas (sus listas, todas sus partes, su archivo único y sus
        //     redirecciones) y las consultas del recolector nunca van por el proxy: van directo y pasan por el filtro de la
        //     red de la casa del socket, que así ve el destino real. Las descargas normales siguen usando el proxy.
        {
            ServidorDePrueba proxy(ServidorDePrueba::PorRuta{ [&](const std::string& ruta, const std::string& r) -> std::string {
                // Un proxy de prueba: le llegan direcciones completas («GET http://sitio/ruta»)
                if (ruta == "http://normal.ejemplo.org/peli.mp4") return servirUnico(r);
                return Http(404, "text/plain", "no existe");
            } });
            std::string puertoWeb = std::to_string(web.Puerto());
            pruebas::ProxyFijo("127.0.0.1:" + std::to_string(proxy.Puerto()));
            // Un nombre de afuera que lleva a esta PC (un DNS que miente)
            pruebas::Resolver("afuera-aqui.ejemplo.org:" + puertoWeb + ":127.0.0.1");
            int p0 = proxy.Pedidos(), w0 = web.Pedidos();
            // Una lista de la casa que la persona confirmó: la lista y sus 6 partes se piden directo, ni una por el proxy
            Foto directo = BajarVideo(w + "/hls/lista.m3u8", "hls", "", true, carpeta, L"Sin proxy");
            int p1 = proxy.Pedidos(), w1 = web.Pedidos();
            // Con una redirección: también directo (la redirección, la lista y sus partes)
            Foto redir = BajarVideo(w + "/redir/lista.m3u8", "hls", "", true, carpeta, L"Sin proxy redir");
            int p2 = proxy.Pedidos(), w2 = web.Pedidos();
            // Una lista de internet que lleva a esta PC (por su IP, o por un nombre de afuera que resuelve aquí): se rechaza
            Foto ipCasa = BajarVideo(w + "/hls/lista.m3u8", "hls", "", false, carpeta, L"Proxy ip");
            Foto nombreCasa = BajarVideo("http://afuera-aqui.ejemplo.org:" + puertoWeb + "/hls/lista.m3u8", "hls", "", false, carpeta, L"Proxy nombre");
            int p3 = proxy.Pedidos(), w3 = web.Pedidos();
            // La ventana «Bajar video» también lee la lista directo
            std::string texto, urlFinal, errorLista;
            bool local = false;
            bool leida = BajarLista(w + "/hls/lista.m3u8", "", "", "", texto, urlFinal, local, errorLista);
            int p4 = proxy.Pedidos(), w4 = web.Pedidos();
            Comprobar(directo.datos.estado == Estado::Completada && leer(L"Sin proxy.ts") == todo && p1 == p0 && w1 == w0 + 7
                          && redir.datos.estado == Estado::Completada && leer(L"Sin proxy redir.ts") == todo && p2 == p1 && w2 == w1 + 8
                          && ipCasa.datos.error == TextoRedLocal && nombreCasa.datos.error == TextoRedLocal && p3 == p2 && w3 == w2
                          && leida && local && p4 == p3 && w4 == w3 + 1,
                      "videos con proxy configurado: la lista y sus partes se piden sin proxy (directo) y un destino de la casa se rechaza");

            // El archivo único de un DASH (y el archivo de un video de la página): directo; si sale de una lista de
            // internet y lleva a esta PC, se rechaza sin conectar
            DatosDescarga u;
            u.nombre = L"Unico sin proxy.mp4";
            u.nombreFijo = true;
            u.sinProxy = true;
            u.url = w + "/unico/peli.mp4";
            Foto unicoDirecto = Bajar(u, carpeta);
            int p5 = proxy.Pedidos(), w5 = web.Pedidos();
            u.nombre = L"Unico casa.mp4";
            u.soloAfuera = true;
            u.url = "http://afuera-aqui.ejemplo.org:" + puertoWeb + "/unico/peli.mp4";
            Foto unicoCasa = Bajar(u, carpeta);
            int p6 = proxy.Pedidos(), w6 = web.Pedidos();
            Comprobar(unicoDirecto.datos.estado == Estado::Completada && leer(L"Unico sin proxy.mp4") == unico && p5 == p4 && w5 > w4
                          && unicoCasa.datos.error == TextoRedLocal && p6 == p5 && w6 == w5,
                      "videos con proxy configurado: el archivo único de un DASH va directo, y si lleva a la casa se rechaza");

            // Una descarga normal (agregada a mano) sigue usando el proxy, como antes
            DatosDescarga n;
            n.nombre = L"Normal por proxy.mp4";
            n.nombreFijo = true;
            n.url = "http://normal.ejemplo.org/peli.mp4";
            Foto normal = Bajar(n, carpeta);
            int p7 = proxy.Pedidos(), w7 = web.Pedidos();
            Comprobar(normal.datos.estado == Estado::Completada && leer(L"Normal por proxy.mp4") == unico && p7 > p6 && w7 == w6,
                      "descargas normales con proxy configurado: siguen usando el proxy, como antes");

            // El recolector consulta sin proxy, y no consulta direcciones de la casa
            InfoEnlace a, b;
            ComprobarEnlace(w + "/unico/peli.mp4", "", a);
            ComprobarEnlace("http://afuera-aqui.ejemplo.org:" + puertoWeb + "/unico/peli.mp4", "", b);
            Comprobar(a.local && b.local && proxy.Pedidos() == p7 && web.Pedidos() == w7,
                      "recolector con proxy configurado: consulta sin proxy y no consulta direcciones de la casa");
            pruebas::ProxyFijo("");
            pruebas::Resolver("");
        }

        // 14. El MP4 del botón «Descargar este video» va sin proxy y con «soloAfuera», como las listas: aunque su sitio
        //     sea de internet, una redirección hacia esta PC se frena en el socket. Si el video (o su página) es de la
        //     casa, la ventana pregunta antes («Video de tu red») y, con un sí, va sin ese filtro.
        {
            // Un «sitio de internet» en 127.0.0.2 (para la prueba cuenta como de afuera) que redirige a esta PC
            ServidorDePrueba afuera(ServidorDePrueba::PorRuta{ [&](const std::string& ruta, const std::string&) -> std::string {
                if (ruta == "/peli.mp4") return Http(302, "text/plain", "", "Location: " + w + "/unico/peli.mp4\r\n");
                return Http(404, "text/plain", "no existe");
            } }, "127.0.0.2");
            red::SimularAfuera("127.0.0.2");
            std::string sitioAfuera = "video.ejemplo.org:" + std::to_string(afuera.Puerto());
            pruebas::Resolver(sitioAfuera + ":127.0.0.2,nas.ejemplo.org:80:192.168.1.5");
            int w0 = web.Pedidos(), a0 = afuera.Pedidos();
            DatosDescarga d;   // como lo arma la ventana para el botón cuando ni el video ni su página son de la casa
            d.url = "http://" + sitioAfuera + "/peli.mp4";
            d.nombre = L"Boton.mp4";
            d.nombreFijo = true;
            d.sinProxy = true;
            d.soloAfuera = true;
            Foto frenado = Bajar(d, carpeta);
            int w1 = web.Pedidos(), a1 = afuera.Pedidos();
            d.soloAfuera = false;   // la persona confirmó que el video es de su red
            d.nombre = L"Boton confirmado.mp4";
            Foto confirmado = Bajar(d, carpeta);
            // Cuándo se pregunta: el video o su página son de esta PC o de la casa (por el nombre o por a dónde lleva)
            bool casa = EsDeLaCasa("http://127.0.0.1:8080/v.mp4") && EsDeLaCasa("http://nas.ejemplo.org/v.mp4")
                        && EsDeLaCasa("http://192.168.0.10/ver.html") && !EsDeLaCasa("http://" + sitioAfuera + "/peli.mp4")
                        && !EsDeLaCasa("") && !EsDeLaCasa("about:blank");
            red::SimularAfuera(nullptr);
            pruebas::Resolver("");
            Comprobar(frenado.datos.estado == Estado::Error && frenado.datos.error == TextoRedLocal && a1 == a0 + 1 && w1 == w0
                          && !ExisteArchivo(carpeta + L"\\Boton.mp4") && !ExisteArchivo(carpeta + L"\\Boton.mp4.atarraya")
                          && confirmado.datos.estado == Estado::Completada && leer(L"Boton confirmado.mp4") == unico && casa,
                      "videos: el MP4 del botón que redirige a 127.0.0.1 se rechaza; si es de la casa se pregunta y, con un sí, se baja");
        }

        SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", nullptr);
        fs::remove_all(carpeta, ec);
    }

    // Un punto de unión (como «mklink /J»): una carpeta que lleva a otra. No hace falta ser administrador.
    bool CrearUnion(const std::wstring& enlace, const std::wstring& destino)
    {
        if (!CreateDirectoryW(enlace.c_str(), nullptr)) return false;
        HANDLE h = CreateFileW(enlace.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        std::wstring sustituto = L"\\??\\" + destino;
        WORD largoS = (WORD)(sustituto.size() * sizeof(wchar_t)), largoI = (WORD)(destino.size() * sizeof(wchar_t));
        std::vector<BYTE> b(16 + largoS + 2 + largoI + 2, 0);
        auto poner = [&](size_t o, WORD v) { memcpy(b.data() + o, &v, sizeof(v)); };
        DWORD marca = IO_REPARSE_TAG_MOUNT_POINT;
        memcpy(b.data(), &marca, sizeof(marca));
        poner(4, (WORD)(8 + largoS + 2 + largoI + 2));   // largo de los datos
        poner(8, 0);                                      // dónde empieza el nombre de destino
        poner(10, largoS);
        poner(12, (WORD)(largoS + 2));                    // dónde empieza el nombre para mostrar
        poner(14, largoI);
        memcpy(b.data() + 16, sustituto.c_str(), largoS);
        memcpy(b.data() + 16 + largoS + 2, destino.c_str(), largoI);
        DWORD n = 0;
        BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, b.data(), (DWORD)b.size(), nullptr, 0, &n, nullptr);
        CloseHandle(h);
        return ok != FALSE;
    }

    // 0.4.0 (revisión): las páginas de la cola las abre la extensión; con varios navegadores conectados, el predeterminado
    // 0.4.0 (revisión 5): la cola y la lista de descargas se guardan juntas, en un solo archivo y de una vez, y al abrir
    // Atarraya (y cuando la extensión cuenta lo que sabe) se junta todo. Para cada operación se simula un cierre de golpe
    // justo antes y justo después de cada escritura, con lo que hizo el navegador mientras tanto, y al reabrir se revisa
    // que nunca haya dos descargas activas de la misma parte ni partes perdidas.
    struct AtarrayaDePrueba
    {
        std::unique_ptr<Motor> m;
        DeUnaEnUna q{ std::wstring() };
        std::wstring archivo;
        std::vector<std::string> escrituras;   // el archivo después de cada escritura
        explicit AtarrayaDePrueba(std::wstring a) : archivo(std::move(a))
        {
            // Como la ventana: la cola sale del mismo archivo que la lista, y se guardan juntas
            m = std::make_unique<Motor>(archivo);
            m->Cargar();
            std::string anexo = m->AnexoCargado();
            if (!anexo.empty())
            {
                auto j = nlohmann::json::parse(anexo);
                if (j.contains("cola")) q.DesdeJson(j["cola"]);
            }
            m->anexo = [this](nlohmann::json& x) { x = { { "cola", q.ComoJson() } }; };
            q.guardarJunto = [this]() {
                bool ok = m->Guardar();
                std::string t;
                LeerArchivo(archivo, t);
                escrituras.push_back(t);
                return ok;
            };
        }
        ~AtarrayaDePrueba()
        {
            m->anexo = nullptr;
            m.reset();
        }
        std::function<EnLaLista(uint64_t)> EnLaListaDe()
        {
            return [this](uint64_t id) {
                EnLaLista r;
                Foto f;
                if (id && m->Buscar(id, f))
                {
                    r.existe = true;
                    r.estado = f.datos.estado;
                }
                return r;
            };
        }
        void Aplicar(const Conciliacion& c)
        {
            for (auto id : c.quitar) m->Quitar(id);
            if (c.cambio || !c.quitar.empty()) q.Guardar();
        }
        // Como la ventana en cada vuelta: las que terminaron o se quitaron sin que la cola se enterara, y las pausas
        void Vuelta()
        {
            for (size_t i = 0; i < q.Partes().size(); i++)
            {
                const auto& p = q.Partes()[i];
                if (p.estado != EstadoParte::Tomada) continue;
                Foto f;
                if (!m->Buscar(p.descarga, f)) q.Saltar((int)i);
                else if (f.datos.estado == Estado::Completada) q.Terminar(p.descarga, true);
            }
            DeUnaEnUna::ListaParaPausas l;
            l.buscar = [this](uint64_t id, Estado& e, bool& pausando) {
                Foto f;
                if (!m->Buscar(id, f)) return false;
                e = f.datos.estado;
                pausando = f.pausando;
                return true;
            };
            l.pausar = [this](uint64_t id) { m->Pausar(id); };
            l.reanudar = [this](uint64_t id) { m->Reanudar(id); };
            l.guardar = [this]() { return m->Guardar(); };
            q.RevisarPausas(l);
            q.RevisarPausas(l);   // la segunda vuelta borra la marca «reanudar» (después de guardar)
            q.Guardar();
        }
    };

    void ConciliarPruebas()
    {
        namespace fs = std::filesystem;
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        const std::wstring base = std::wstring(temporal) + L"atarraya-prueba-conciliar-" + std::to_wstring(GetCurrentProcessId());
        std::error_code ec;
        fs::remove_all(base, ec);
        fs::create_directories(base, ec);
        const std::string inst = "abcdefgh1234";
        auto parte = [&](int n) {
            Parte p;
            p.url = "https://ddownload.com/c" + std::to_string(n) + "k" + std::to_string(n) + "c" + std::to_string(n) + "k/peli.part"
                + std::to_string(n) + ".rar";
            p.nombre = L"peli.part" + std::to_wstring(n) + L".rar";
            p.carpeta = base;
            p.grupo = "g1";
            return p;
        };
        auto tomaDe = [&](int n) { return "i" + inst + "-n" + std::to_string(n) + "-k1"; };
        auto descarga = [&](int n) {
            DatosDescarga d;
            d.url = "https://s1.ddownload.com/d/c" + std::to_string(n) + "/peli.part" + std::to_string(n) + ".rar";
            d.carpeta = base;
            d.nombre = L"peli.part" + std::to_wstring(n) + L".rar";
            d.conexiones = 1;
            d.grupo = "g1";
            return d;
        };
        int archivos = 0;
        auto nueva = [&]() { return std::make_unique<AtarrayaDePrueba>(base + L"\\descargas-" + std::to_wstring(++archivos) + L".json"); };
        // Reabrir con lo que había en el disco
        auto reabrir = [&](const std::string& disco) {
            std::wstring a = base + L"\\reabrir-" + std::to_wstring(++archivos) + L".json";
            EscribirSeguro(a, disco);
            return std::make_unique<AtarrayaDePrueba>(a);
        };

        // El navegador de la prueba: cada descarga suya («toma») para la extensión: «probando» (decide con Atarraya),
        // «siguiendo» (la baja el navegador), «sinConfirmar», «terminada» o "" (ya no está), y la página de su parte
        struct EnElNavegador
        {
            std::string estado, pagina;
        };
        using Navegador = std::map<std::string, EnElNavegador>;
        auto informe = [&](const Navegador& n) {
            Informe inf;
            inf.instancia = inst;
            for (const auto& [t, e] : n)
            {
                if (!e.estado.empty()) inf.tomas.push_back({ t, e.estado, e.pagina });
            }
            return inf;
        };
        // Nunca dos descargas activas de la misma parte, y ninguna parte perdida (sin terminar, sin descarga y sin abrirse).
        // «soloDobles»: recién abierto, antes de que la extensión cuente lo que sabe (una parte que baja el navegador
        // todavía no se puede juzgar)
        auto bien = [&](AtarrayaDePrueba& a, const Navegador& n, std::string& que, bool soloDobles = false) {
            const auto& partes = a.q.Partes();
            for (size_t i = 0; i < partes.size(); i++)
            {
                const auto& p = partes[i];
                Foto f;
                bool hayA = p.estado == EstadoParte::Tomada && p.descarga && a.m->Buscar(p.descarga, f);
                bool activaA = hayA && !p.sinConfirmar
                    && (f.datos.estado == Estado::EnCola || f.datos.estado == Estado::Iniciando || f.datos.estado == Estado::Descargando);
                int activasB = 0;
                bool conoceB = false;
                for (const auto& [t, e] : n)
                {
                    if (e.pagina != p.url || e.estado.empty()) continue;
                    if (e.estado != "terminada") activasB++;   // decidiendo, bajando o sin confirmar: puede estar bajando
                    if (t == p.toma) conoceB = true;
                }
                if ((activaA ? 1 : 0) + activasB > 1)
                {
                    que = "parte " + std::to_string(i + 1) + ": dos descargas activas";
                    return false;
                }
                if (soloDobles) continue;
                bool terminada = p.estado == EstadoParte::Lista || p.estado == EstadoParte::Saltada || p.estado == EstadoParte::Fallo;
                bool porAbrir = p.estado == EstadoParte::Esperando || p.estado == EstadoParte::Abierta || p.estado == EstadoParte::Espera;
                // Una del navegador de una extensión de antes (sin instancia): solo esa extensión puede avisar; se espera
                bool deAntes = p.estado == EstadoParte::Navegador && InstanciaDeToma(p.toma).empty();
                bool sigue = (p.estado == EstadoParte::Tomada && hayA) || (p.estado == EstadoParte::Navegador && (conoceB || p.finalNavegador)) || deAntes;
                if (!terminada && !porAbrir && !sigue)
                {
                    que = "parte " + std::to_string(i + 1) + ": perdida";
                    return false;
                }
            }
            return true;
        };
        // Reabre con «disco» y revisa al abrir; después, si «cuenta», la extensión cuenta lo que sabe (si no, pasa el minuto
        // sin que ninguna cuente nada) y se revisa otra vez
        auto revisar = [&](const std::string& disco, const Navegador& n, bool cuenta, const std::string& caso) {
            auto a = reabrir(disco);
            std::string que;
            a->Aplicar(a->q.ConciliarAlAbrir(a->EnLaListaDe()));
            a->Vuelta();
            bool ok = bien(*a, n, que, true);
            if (cuenta) a->Aplicar(a->q.ConciliarConExtension(informe(n), a->EnLaListaDe()));
            else a->Aplicar(a->q.SoltarRetenidas());
            a->Vuelta();
            ok = ok && bien(*a, n, que);
            Comprobar(ok, ("conciliar: " + caso + (ok ? std::string() : " (" + que + ")")).c_str());
            return a;
        };
        auto unaSola = [&](AtarrayaDePrueba& a, size_t antes, const char* que) {
            Comprobar(a.escrituras.size() == antes + 1,
                      (std::string("conciliar: ") + que + " se guarda de una sola vez (la cola y la lista juntas, en el mismo archivo)").c_str());
        };

        // 1. Tomar con «toma» (lo que encontró Codex): la descarga y la parte se guardan juntas
        {
            auto a = nueva();
            a->q.Agregar({ parte(1) });
            int i = a->q.AbrirSiguiente();
            a->q.Guardar();
            std::string antesDeTomar = a->escrituras.back();
            size_t n0 = a->escrituras.size();
            uint64_t id = a->m->Agregar(descarga(1), true);
            a->q.Probar(i, id, tomaDe(1), 1000);
            a->q.Guardar();
            unaSola(*a, n0, "tomar");
            std::string despues = a->escrituras.back();
            // Mientras Atarraya estuvo cerrado, la extensión no tuvo respuesta y dejó seguir la del navegador
            Navegador siguio{ { tomaDe(1), { "siguiendo", parte(1).url } } };
            Navegador decidiendo{ { tomaDe(1), { "probando", parte(1).url } } };
            auto r1 = revisar(antesDeTomar, siguio, true, "tomar: cierre antes de guardar; queda la del navegador (la cola la adopta al contarla la extensión)");
            Comprobar(r1->q.Partes()[0].estado == EstadoParte::Navegador && r1->q.Partes()[0].toma == tomaDe(1) && r1->m->Fotos().empty(),
                      "conciliar: tomar: la parte pasa a la descarga del navegador que la cola no conocía");
            revisar(antesDeTomar, decidiendo, true, "tomar: cierre antes de guardar, con la extensión todavía decidiendo");
            auto r2 = revisar(despues, siguio, true, "tomar: cierre después de guardar; queda la del navegador y se quita la de Atarraya");
            Comprobar(r2->q.Partes()[0].estado == EstadoParte::Navegador && r2->m->Fotos().empty(),
                      "conciliar: tomar: al abrir se quita la de Atarraya, que solo era una prueba");
            revisar(despues, decidiendo, true, "tomar: cierre después de guardar, con la extensión todavía decidiendo");
            revisar(despues, siguio, false, "tomar: cierre después de guardar, sin que ninguna extensión cuente nada");
        }

        // 2. Tomar sin «toma» (la del navegador ya se canceló al pasársela a Atarraya)
        {
            auto a = nueva();
            a->q.Agregar({ parte(2) });
            int i = a->q.AbrirSiguiente();
            a->q.Guardar();
            std::string antes = a->escrituras.back();
            size_t n0 = a->escrituras.size();
            uint64_t id = a->m->Agregar(descarga(2), true);
            a->q.Tomar(i, id);
            a->q.Guardar();
            unaSola(*a, n0, "tomar sin toma");
            auto r1 = revisar(antes, {}, true, "tomar sin toma: cierre antes de guardar; la parte sigue abierta (se baja otra vez)");
            Comprobar(r1->q.Partes()[0].estado == EstadoParte::Abierta && r1->m->Fotos().empty(), "conciliar: tomar sin toma: nada a medias");
            auto r2 = revisar(a->escrituras.back(), {}, true, "tomar sin toma: cierre después de guardar; sigue la de Atarraya");
            Comprobar(r2->q.Partes()[0].estado == EstadoParte::Tomada && r2->m->Fotos().size() == 1, "conciliar: tomar sin toma: la parte y su descarga, juntas");
        }

        // 3. Atarraya decide que la toma (la extensión cancela la del navegador cuando lo pregunta)
        {
            auto a = nueva();
            a->q.Agregar({ parte(3) });
            int i = a->q.AbrirSiguiente();
            uint64_t id = a->m->Agregar(descarga(3), true);
            a->q.Probar(i, id, tomaDe(3), 1000);
            a->q.Guardar();
            std::string antes = a->escrituras.back();
            size_t n0 = a->escrituras.size();
            a->q.Decidir(i, true);
            a->q.Guardar();
            unaSola(*a, n0, "decidir");
            std::string despues = a->escrituras.back();
            Navegador siguio{ { tomaDe(3), { "siguiendo", parte(3).url } } };
            Navegador decidiendo{ { tomaDe(3), { "probando", parte(3).url } } };
            Navegador cancelada;   // la extensión ya la había cancelado (y la olvidó)
            revisar(antes, siguio, true, "decidir: cierre antes de guardar; queda la del navegador");
            auto r1 = revisar(despues, siguio, true, "decidir: cierre después, sin que la extensión alcanzara a preguntar; queda la del navegador");
            Comprobar(r1->q.Partes()[0].estado == EstadoParte::Navegador && r1->m->Fotos().empty(),
                      "conciliar: decidir: si el navegador siguió, se quita la de Atarraya");
            auto r2 = revisar(despues, cancelada, true, "decidir: cierre después de que la extensión la canceló; sigue la de Atarraya");
            Foto f;
            Comprobar(r2->q.Partes()[0].estado == EstadoParte::Tomada && !r2->q.Partes()[0].sinConfirmar && r2->m->Buscar(id, f)
                          && f.datos.estado == Estado::EnCola,
                      "conciliar: decidir: si la extensión ya no la conoce, la de Atarraya sigue");
            // La extensión todavía decide (se apagó y despertó): Atarraya retiene la suya; cuando la extensión pregunta, se
            // le contesta «tomada», la cancela y avisa que ya no baja
            auto r3 = revisar(despues, decidiendo, true, "decidir: cierre después, con la extensión todavía decidiendo; la de Atarraya espera");
            auto e = r3->q.EstadoDeToma(tomaDe(3));
            Comprobar(e.estado == "tomada" && e.sinConfirmar && r3->m->Buscar(id, f) && f.datos.estado == Estado::Pausada,
                      "conciliar: decidir: mientras la extensión decide, la de Atarraya queda en pausa y se le contesta «tomada»");
            r3->q.Confirmar(0);   // «confirmada»: la del navegador ya no baja
            r3->Vuelta();
            std::string que;
            Comprobar(bien(*r3, cancelada, que) && r3->m->Buscar(id, f) && f.datos.estado == Estado::EnCola && !r3->q.Partes()[0].reanudar,
                      "conciliar: decidir: cuando la extensión confirma que canceló la suya, sigue la de Atarraya");
            auto r4 = revisar(despues, cancelada, false, "decidir: cierre después y el navegador cerrado; al minuto sigue la de Atarraya");
            Comprobar(r4->m->Buscar(id, f) && f.datos.estado == Estado::EnCola && !r4->q.HayRetenidas(),
                      "conciliar: decidir: si ninguna extensión cuenta nada en un minuto, sigue la de Atarraya");
        }

        // 4. Terminar: la descarga terminó y la cola lo anota
        {
            auto a = nueva();
            a->q.Agregar({ parte(4) });
            int i = a->q.AbrirSiguiente();
            DatosDescarga d = descarga(4);
            d.tamano = 100;
            uint64_t id = a->m->AgregarTerminada(d);
            a->q.Tomar(i, id);
            a->q.Guardar();
            std::string antes = a->escrituras.back();
            size_t n0 = a->escrituras.size();
            a->q.Terminar(id, true);
            a->q.Guardar();
            unaSola(*a, n0, "terminar");
            auto r1 = revisar(antes, {}, true, "terminar: cierre antes de anotarla; al abrir queda lista");
            Comprobar(r1->q.Partes()[0].estado == EstadoParte::Lista, "conciliar: terminar: la que terminó sin que la cola se enterara queda lista");
            revisar(a->escrituras.back(), {}, true, "terminar: cierre después");
        }

        // 5. Saltar (también una que baja el navegador: la del navegador sigue, pero la parte ya no es de la cola)
        {
            auto a = nueva();
            a->q.Agregar({ parte(5), parte(6) });
            int i = a->q.AbrirSiguiente();
            uint64_t id = a->m->Agregar(descarga(5), true);
            a->q.Tomar(i, id);
            int j = a->q.AbrirSiguiente();
            a->q.EnNavegador(j, tomaDe(6));
            a->q.Guardar();
            std::string antes = a->escrituras.back();
            size_t n0 = a->escrituras.size();
            a->q.Saltar(i);
            a->q.Saltar(j);
            a->q.Guardar();
            unaSola(*a, n0, "saltar");
            Navegador n{ { tomaDe(6), { "siguiendo", parte(6).url } } };
            revisar(antes, n, true, "saltar: cierre antes de guardar");
            revisar(a->escrituras.back(), n, true, "saltar: cierre después");
        }

        // 6. Pausar: la extensión no pudo cancelar la del navegador ni saber cómo quedó («sin confirmar»)
        // 7. Reanudar: después supo que la del navegador ya no baja
        {
            auto a = nueva();
            a->q.Agregar({ parte(7) });
            int i = a->q.AbrirSiguiente();
            uint64_t id = a->m->Agregar(descarga(7), true);
            a->q.Probar(i, id, tomaDe(7), 1000);
            a->q.Decidir(i, true);
            a->q.Guardar();
            std::string antes = a->escrituras.back();
            size_t n0 = a->escrituras.size();
            a->m->Pausar(a->q.MarcarSinConfirmar(i));
            a->q.Guardar();
            unaSola(*a, n0, "pausar");
            std::string pausada = a->escrituras.back();
            Navegador sinConfirmar{ { tomaDe(7), { "sinConfirmar", parte(7).url } } };
            revisar(antes, sinConfirmar, true, "pausar: cierre antes de guardar; la de Atarraya espera a saber");
            auto r1 = revisar(pausada, sinConfirmar, true, "pausar: cierre después; sigue en pausa hasta saber");
            Comprobar(r1->q.Partes()[0].sinConfirmar && !r1->q.Partes()[0].alAbrir && !r1->q.HayRetenidas(),
                      "conciliar: pausar: si la extensión lo sigue mirando, no vence el minuto (espera lo que diga)");
            size_t n1 = a->escrituras.size();
            a->q.Confirmar(i);
            a->q.Guardar();
            unaSola(*a, n1, "reanudar");
            Navegador yaNoBaja;
            revisar(pausada, yaNoBaja, true, "reanudar: cierre antes de guardar; la extensión ya no la conoce y sigue la de Atarraya");
            auto r2 = revisar(a->escrituras.back(), yaNoBaja, true, "reanudar: cierre después; sigue la de Atarraya");
            Foto f;
            Comprobar(r2->m->Buscar(id, f) && f.datos.estado == Estado::EnCola, "conciliar: reanudar: al abrir se reanuda de verdad");
        }

        // 8. Seguir en el navegador: Atarraya no pudo (o se le pidió soltarla) y queda la del navegador
        // 9. El navegador siguió con una que Atarraya ya había tomado
        {
            auto a = nueva();
            a->q.Agregar({ parte(8), parte(9) });
            int i = a->q.AbrirSiguiente();
            uint64_t id = a->m->Agregar(descarga(8), true);
            a->q.Probar(i, id, tomaDe(8), 1000);
            int j = a->q.AbrirSiguiente();
            uint64_t id2 = a->m->Agregar(descarga(9), true);
            a->q.Probar(j, id2, tomaDe(9), 1000);
            a->q.Decidir(j, true);
            a->q.Guardar();
            std::string antes = a->escrituras.back();
            size_t n0 = a->escrituras.size();
            a->m->Quitar(a->q.Decidir(i, false));
            a->q.Guardar();
            unaSola(*a, n0, "seguir en el navegador");
            std::string despues = a->escrituras.back();
            size_t n1 = a->escrituras.size();
            a->m->Quitar(a->q.NavegadorSiguio(j));
            a->q.Guardar();
            unaSola(*a, n1, "el navegador siguió");
            Navegador n{ { tomaDe(8), { "siguiendo", parte(8).url } }, { tomaDe(9), { "siguiendo", parte(9).url } } };
            revisar(antes, n, true, "seguir en el navegador: cierre antes de guardar");
            revisar(despues, n, true, "seguir en el navegador: cierre después (y antes de quitar la del navegador siguió)");
            auto r = revisar(a->escrituras.back(), n, true, "el navegador siguió: cierre después");
            Comprobar(r->m->Fotos().empty() && r->q.Partes()[1].estado == EstadoParte::Navegador, "conciliar: seguir en el navegador: queda solo la del navegador");
        }

        // 10. La extensión ya no conoce una descarga del navegador (se borraron sus datos): la parte vuelve a abrirse
        {
            auto a = nueva();
            a->q.Agregar({ parte(10), parte(11) });
            int i = a->q.AbrirSiguiente();
            a->q.EnNavegador(i, tomaDe(10));
            int j = a->q.AbrirSiguiente();
            a->q.EnNavegador(j, "n11-de-antes");   // de una extensión de antes: no se juzga
            a->q.Guardar();
            Navegador nada;
            auto r = revisar(a->escrituras.back(), nada, true, "una descarga del navegador que ya nadie sigue: la parte vuelve a abrirse");
            Comprobar(r->q.Partes()[0].estado == EstadoParte::Esperando && r->q.Partes()[0].toma.empty()
                          && r->q.Partes()[1].estado == EstadoParte::Navegador,
                      "conciliar: vuelve a la cola para abrirse otra vez (y la de una extensión de antes no se toca)");
            Informe otra;
            otra.instancia = "zzzzzzzz9999";
            auto c = r->q.ConciliarConExtension(otra, r->EnLaListaDe());
            Comprobar(!c.cambio && c.abrir.empty(), "conciliar: lo que cuenta una extensión no cambia las descargas de otra");
        }

        // 11. Pasar del recolector: la cola se guarda antes de sacar los enlaces del recolector; si se cierra en el medio y
        //     la persona los confirma otra vez, no entran dos veces
        {
            auto a = nueva();
            size_t nuevas = 0;
            a->q.Agregar({ parte(12), parte(13) }, &nuevas);
            bool primera = nuevas == 2;
            Comprobar(a->q.Agregar({ parte(12), parte(14), parte(14) }, &nuevas) && nuevas == 1 && a->q.Partes().size() == 3 && primera,
                      "conciliar: pasar del recolector: una página que ya está en la cola no entra otra vez");
        }

        // 12. Una retenida al abrir que la extensión después marca «sin confirmar» ya no vence al minuto
        {
            DeUnaEnUna q(base + L"\\sola.json");
            q.Agregar({ parte(15) });
            int i = q.AbrirSiguiente();
            q.Probar(i, 99, tomaDe(15), 1000);
            q.Decidir(i, true);
            auto c = q.ConciliarAlAbrir([](uint64_t) { EnLaLista r; r.existe = true; r.estado = Estado::EnCola; return r; });
            Comprobar(c.cambio && q.Partes()[i].sinConfirmar && q.Partes()[i].alAbrir && q.HayRetenidas() && q.MarcarSinConfirmar(i) == 99
                          && !q.Partes()[i].alAbrir && q.Partes()[i].sinConfirmar && !q.HayRetenidas() && q.SoltarRetenidas().motivos.empty(),
                      "conciliar: si la extensión avisa que no pudo confirmarlo, la de Atarraya sigue esperando (sin plazo)");
            q.Guardar();
            DeUnaEnUna otra(base + L"\\sola.json");
            otra.Cargar();
            Comprobar(otra.Partes()[i].sinConfirmar && !otra.Partes()[i].alAbrir, "conciliar: la marca de retenida se guarda");
        }

        // 13. El archivo de la lista guarda la cola junta; al cerrar, la lista se guarda con la última cola guardada
        {
            std::wstring f = base + L"\\anexo.json";
            {
                Motor m(f);
                DeUnaEnUna q(std::wstring{});
                m.anexo = [&q](nlohmann::json& x) { x = { { "cola", q.ComoJson() } }; };
                q.guardarJunto = [&m]() { return m.Guardar(); };
                q.Agregar({ parte(16) });
                m.anexo = nullptr;   // como al cerrar la ventana
                q.guardarJunto = nullptr;
            }   // al cerrar, Motor guarda otra vez: con la última cola guardada
            Motor m2(f);
            m2.Cargar();
            auto j = nlohmann::json::parse(m2.AnexoCargado());
            DeUnaEnUna q2(std::wstring{});
            q2.DesdeJson(j["cola"]);
            Comprobar(q2.Partes().size() == 1 && q2.Partes()[0].url == parte(16).url && InstanciaDeToma(tomaDe(1)) == inst
                          && InstanciaDeToma("n1-abc").empty() && InstanciaDeToma("iABCDEFGH-n1-x").empty(),
                      "conciliar: la cola va en el archivo de la lista y sigue ahí después de cerrar");
        }

        // 14. Atarraya se cerró después de pasar a la carpeta del paquete lo que bajó el navegador y antes de anotarlo: al
        //     abrir, se encuentra ya movido y se anota igual
        {
            std::wstring nav = base + L"\\nav", paq = base + L"\\paq";
            fs::create_directories(nav, ec);
            fs::create_directories(paq, ec);
            {
                std::ofstream o(nav + L"\\peli.part17.rar", std::ios::binary);
                o << std::string(3000, 'q');
            }
            IdentidadArchivo id;
            std::string motivo;
            std::wstring destino;
            bool leido = LeerArchivoDelNavegador(nav + L"\\peli.part17.rar", id, motivo);
            bool movida = MoverArchivoDelNavegador(nav + L"\\peli.part17.rar", id, paq, destino, motivo) == Movida::Movida;
            // Otra vez, como al abrir después del cierre: ya no está donde lo dejó el navegador, pero sí en el paquete
            std::wstring otraVez;
            Comprobar(leido && movida && MoverArchivoDelNavegador(nav + L"\\peli.part17.rar", id, paq, otraVez, motivo) == Movida::Movida
                          && otraVez == destino && ExisteArchivo(paq + L"\\peli.part17.rar"),
                      "conciliar: lo que el navegador bajó y ya se movió antes del cierre se encuentra en el paquete");
            // Si en el paquete hay otro archivo con ese nombre (no es el mismo), no se da por movido
            DeleteFileW((paq + L"\\peli.part17.rar").c_str());
            {
                std::ofstream o(paq + L"\\peli.part17.rar", std::ios::binary);
                o << std::string(3000, 'r');
            }
            Comprobar(MoverArchivoDelNavegador(nav + L"\\peli.part17.rar", id, paq, otraVez, motivo) != Movida::Movida,
                      "conciliar: otro archivo con el mismo nombre en el paquete no cuenta como movido");
        }

        // 15. Pasar la cola de antes al archivo de la lista: si la de antes no se puede leer, no se toca nada (ni se escribe
        //     la nueva ni se borra la vieja) y se intenta otra vez al abrir. Nunca se pierden partes.
        {
            std::wstring vieja = base + L"\\de-una-en-una.json";
            DeUnaEnUna sinNada(base + L"\\no-esta.json");
            bool nada = sinNada.CargarAlAbrir("") == DeUnaEnUna::Origen::Bien && sinNada.Partes().empty();
            {
                DeUnaEnUna q(vieja);
                q.Agregar({ parte(18), parte(19) });
            }
            std::string buena;
            LeerArchivo(vieja, buena);
            DeUnaEnUna deAntes(vieja);
            bool paso = deAntes.CargarAlAbrir("") == DeUnaEnUna::Origen::DeAntes && deAntes.Partes().size() == 2;
            // Con la cola ya en el archivo de la lista, la de antes no se mira
            DeUnaEnUna conAnexo(vieja);
            nlohmann::json anexo = { { "cola", deAntes.ComoJson() } };
            bool delAnexo = conAnexo.CargarAlAbrir(anexo.dump()) == DeUnaEnUna::Origen::Bien && conAnexo.Partes().size() == 2;
            Comprobar(nada && paso && delAnexo, "conciliar: la cola de antes se pasa una vez (y con la del archivo de la lista, esa manda)");
            // Dañada: no se toca (queda igual, con una copia «.danado»)
            std::string danada = buena.substr(0, buena.size() / 2);
            EscribirSeguro(vieja, danada);
            DeUnaEnUna mala(vieja);
            std::string despues;
            bool noSePudo = mala.CargarAlAbrir("") == DeUnaEnUna::Origen::NoSePudo && mala.Partes().empty() && LeerArchivo(vieja, despues)
                && despues == danada && ExisteArchivo(vieja + L".danado");
            // Abierta por otro programa (no se puede leer): tampoco se toca
            EscribirSeguro(vieja, buena);
            HANDLE h = CreateFileW(vieja.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            DeUnaEnUna bloqueada(vieja);
            bool sinLeer = bloqueada.CargarAlAbrir("") == DeUnaEnUna::Origen::NoSePudo && bloqueada.Partes().empty();
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            // Y al abrir otra vez, ya se puede: está entera
            DeUnaEnUna otraVez(vieja);
            bool despuesSi = otraVez.CargarAlAbrir("") == DeUnaEnUna::Origen::DeAntes && otraVez.Partes().size() == 2;
            // Una cola dañada en el archivo de la lista: tampoco se toca
            DeUnaEnUna anexoMalo(vieja);
            bool malAnexo = anexoMalo.CargarAlAbrir("{\"cola\":{\"partes\":5}}") == DeUnaEnUna::Origen::NoSePudo && anexoMalo.Partes().empty()
                && anexoMalo.CargarAlAbrir("{\"cola\":") == DeUnaEnUna::Origen::NoSePudo;
            Comprobar(noSePudo && h != INVALID_HANDLE_VALUE && sinLeer && despuesSi && malAnexo,
                      "conciliar: si la cola de antes no se puede leer (dañada o abierta por otro), no se toca y se intenta otra vez al abrir");
        }
        fs::remove_all(base, ec);
    }

    void PuertosPruebas()
    {
        Comprobar(ProgramaDeProgId(L"ChromeHTML") == L"chrome.exe" && ProgramaDeProgId(L"MSEdgeHTM") == L"msedge.exe"
                      && ProgramaDeProgId(L"FirefoxURL-308046B0AF4A39CB") == L"firefox.exe" && ProgramaDeProgId(L"BraveHTML") == L"brave.exe"
                      && ProgramaDeProgId(L"OperaStable") == L"opera.exe" && ProgramaDeProgId(L"AppXq0fevzme2pys62n3e0fbqa7peapykr8v") == L""
                      && ProgramaDeProgId(L"") == L"",
                  "puerto: el programa del navegador predeterminado según su ProgId (si no se sabe, ninguno)");
        Comprobar(OrdenDePuertos({ L"chrome.exe", L"msedge.exe", L"firefox.exe" }, L"MSEDGE.EXE") == std::vector<size_t>{ 1, 0, 2 }
                      && OrdenDePuertos({ L"chrome.exe", L"msedge.exe" }, L"") == std::vector<size_t>{ 0, 1 }
                      && OrdenDePuertos({ L"chrome.exe" }, L"firefox.exe") == std::vector<size_t>{ 0 } && OrdenDePuertos({}, L"chrome.exe").empty(),
                  "puerto: con varios navegadores conectados va primero el predeterminado; si no está, cualquiera de los otros");
    }

    // 0.4.0 (revisión): el aviso final de lo que bajó el navegador no se pierde aunque Atarraya esté cerrado
    void AvisoFinalPruebas()
    {
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        std::wstring archivo = std::wstring(temporal) + L"atarraya-prueba-final-" + std::to_wstring(GetCurrentProcessId()) + L".json";
        auto parte = [](const char* url) {
            Parte p;
            p.url = url;
            p.nombre = NombreDeUrl(url);
            p.carpeta = L"C:\\Descargas\\Peli";
            return p;
        };
        DeUnaEnUna q(archivo);
        q.Agregar({ parte("https://ddownload.com/e5e5e5e5/peli.part1.rar"), parte("https://ddownload.com/f6f6f6f6/peli.part2.rar"),
                    parte("https://ddownload.com/g7g7g7g7/peli.part3.rar") });
        int i = q.AbrirSiguiente();
        q.EnNavegador(i, "n5-final");
        auto e = q.EstadoDeToma("n5-final");
        Comprobar(e.estado == "navegador" && e.espera, "aviso final: mientras baja el navegador, Atarraya espera su aviso final");
        q.Guardar();
        // El navegador terminó con Atarraya cerrado (la extensión guarda el aviso): al abrirlo, la parte sigue esperándolo
        DeUnaEnUna alVolver(archivo);
        alVolver.Cargar();
        e = alVolver.EstadoDeToma("n5-final");
        Comprobar(alVolver.BuscarToma("n5-final") == i && e.estado == "navegador" && e.espera && alVolver.PorMover().empty(),
                  "aviso final: con Atarraya cerrado, al volver a abrirlo la parte sigue esperando el aviso (la extensión lo repite)");
        IdentidadArchivo id;
        id.volumen = 7;
        id.indice = 42;
        id.tamano = 1000;
        id.modificado = 133000000000000000;
        alVolver.AnotarArchivoDelNavegador(i, L"C:\\Users\\x\\Downloads\\peli.part1.rar", id);
        e = alVolver.EstadoDeToma("n5-final");
        Comprobar(e.estado == "terminada" && !e.espera && alVolver.PorMover() == std::vector<int>{ i },
                  "aviso final: en cuanto llega queda anotado; la extensión deja de repetirlo y la parte se termina de mover");
        // Uno que no terminó en el navegador: ya no se espera nada
        int j = alVolver.AbrirSiguiente();
        alVolver.EnNavegador(j, "n6-fallo");
        alVolver.TerminarEnNavegador(j, false, 0);
        e = alVolver.EstadoDeToma("n6-fallo");
        Comprobar(alVolver.Partes()[j].estado == EstadoParte::Fallo && e.estado == "navegador" && !e.espera,
                  "aviso final: si no terminó en el navegador, Atarraya ya no espera nada de esa descarga");
        // Una que baja Atarraya: lo que diga el navegador importa hasta que termina
        int k = alVolver.AbrirSiguiente();
        alVolver.Probar(k, 88, "n7-toma", 1000);
        auto probando = alVolver.EstadoDeToma("n7-toma");
        alVolver.Decidir(k, true);
        auto tomada = alVolver.EstadoDeToma("n7-toma");
        alVolver.Terminar(88, true);
        auto lista = alVolver.EstadoDeToma("n7-toma");
        auto ninguna = alVolver.EstadoDeToma("n9-otra");
        Comprobar(probando.estado == "probando" && probando.espera && tomada.estado == "tomada" && tomada.espera && lista.estado == "tomada"
                      && !lista.espera && ninguna.estado == "desconocida" && !ninguna.espera,
                  "aviso final: una que baja Atarraya espera avisos hasta que termina; una toma que no es de la cola, ninguno");
        DeleteFileW(archivo.c_str());
    }

    // 0.4.0 (revisión): una sola descarga activa por parte, también si Atarraya se cierra a la mitad de una toma
    void TomasPruebas()
    {
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        std::wstring archivo = std::wstring(temporal) + L"atarraya-prueba-tomas-" + std::to_wstring(GetCurrentProcessId()) + L".json";
        auto parte = [](const char* url) {
            Parte p;
            p.url = url;
            p.nombre = NombreDeUrl(url);
            p.carpeta = L"C:\\Descargas\\Peli";
            return p;
        };
        {
            DeUnaEnUna q(archivo);
            q.Agregar({ parte("https://ddownload.com/a1a1a1a1/peli.part1.rar"), parte("https://ddownload.com/b2b2b2b2/peli.part2.rar") });
            int i = q.AbrirSiguiente();
            q.Probar(i, 55, "n1-abc", 1000);
            Comprobar(q.Partes()[i].estado == EstadoParte::Tomada && q.Partes()[i].estadoToma == EstadoToma::Probando && q.Partes()[i].descarga == 55
                          && q.ParteDeToma("n1-abc") == i && q.BuscarToma("n1-abc") == -1,
                      "tomas: mientras Atarraya prueba, la parte queda tomada y el navegador sigue con la suya");
            q.Guardar();
            // Atarraya se cierra a la mitad: al abrirlo, queda la del navegador y se quita la de Atarraya
            DeUnaEnUna alVolver(archivo);
            alVolver.Cargar();
            auto quitar = alVolver.ResolverTomasAlAbrir();
            const auto& p = alVolver.Partes()[i];
            Comprobar(quitar == std::vector<uint64_t>{ 55 } && p.estado == EstadoParte::Navegador && p.estadoToma == EstadoToma::Navegador
                          && p.toma == "n1-abc" && p.descarga == 0 && alVolver.BuscarToma("n1-abc") == i && alVolver.ResolverTomasAlAbrir().empty(),
                      "tomas: un cierre a la mitad de una toma deja solo la del navegador (la de Atarraya se quita al abrir)");
        }
        {
            DeUnaEnUna q(archivo);
            q.Cargar();
            q.Vaciar();
            q.Agregar({ parte("https://ddownload.com/c3c3c3c3/peli.part3.rar") });
            int i = q.AbrirSiguiente();
            q.Probar(i, 66, "n2-def", 1000);
            Comprobar(q.Decidir(i, true) == 0 && q.Partes()[i].estadoToma == EstadoToma::Tomada && q.Partes()[i].estado == EstadoParte::Tomada,
                      "tomas: si Atarraya la tomó, sigue la de Atarraya (la extensión cancela la del navegador)");
            q.Guardar();
            DeUnaEnUna alVolver(archivo);
            alVolver.Cargar();
            Comprobar(alVolver.ResolverTomasAlAbrir().empty() && alVolver.Partes()[i].estadoToma == EstadoToma::Tomada,
                      "tomas: una que ya tomó Atarraya sigue siendo de Atarraya al volver a abrirlo");
            // Pero el navegador siguió con la suya (no se pudo cancelar): queda la del navegador y Atarraya quita la suya
            Comprobar(alVolver.NavegadorSiguio(i) == 66 && alVolver.Partes()[i].estado == EstadoParte::Navegador
                          && alVolver.NavegadorSiguio(i) == 0,
                      "tomas: si el navegador siguió con una que tomó Atarraya, Atarraya quita la suya (una sola descarga)");
            DeUnaEnUna otra(archivo);
            otra.Cargar();
            Comprobar(otra.Decidir(i, false) == 0, "tomas: decidir solo vale mientras se prueba");
        }
        {
            DeUnaEnUna q(archivo);
            q.Vaciar();
            q.Agregar({ parte("https://ddownload.com/d4d4d4d4/peli.part4.rar") });
            int i = q.AbrirSiguiente();
            q.Probar(i, 77, "n3-ghi", 1000);
            Comprobar(q.Decidir(i, false) == 77 && q.Partes()[i].estado == EstadoParte::Navegador && q.Partes()[i].estadoToma == EstadoToma::Navegador,
                      "tomas: si Atarraya no puede (o se le pide soltarla), queda la del navegador y se quita la de Atarraya");
        }
        {
            // No se pudo cancelar la del navegador ni saber cómo quedó: la parte queda «sin confirmar» y la de Atarraya en
            // pausa, también después de cerrar y abrir Atarraya, hasta saberlo
            DeUnaEnUna q(archivo);
            q.Vaciar();
            q.Agregar({ parte("https://ddownload.com/h8h8h8h8/peli.part5.rar"), parte("https://ddownload.com/i9i9i9i9/peli.part6.rar") });
            int i = q.AbrirSiguiente();
            q.Probar(i, 91, "n4-sc", 1000);
            Comprobar(q.MarcarSinConfirmar(i) == 0, "sin confirmar: solo una que ya tomó Atarraya");
            q.Decidir(i, true);
            uint64_t pausar = q.MarcarSinConfirmar(i);
            auto e = q.EstadoDeToma("n4-sc");
            Comprobar(pausar == 91 && q.Partes()[i].sinConfirmar && e.sinConfirmar && e.estado == "tomada" && q.MarcarSinConfirmar(i) == 0
                          && q.Partes()[i].estado == EstadoParte::Tomada && !q.HayAbierta(),
                      "sin confirmar: la parte queda «sin confirmar» y se pausa la de Atarraya (la cola no sigue con otra)");
            q.Guardar();
            DeUnaEnUna alVolver(archivo);
            alVolver.Cargar();
            Comprobar(alVolver.Partes()[i].sinConfirmar && alVolver.EstadoDeToma("n4-sc").sinConfirmar && alVolver.ResolverTomasAlAbrir().empty(),
                      "sin confirmar: sigue así después de cerrar y abrir Atarraya");
            uint64_t reanudar = alVolver.Confirmar(i);
            Comprobar(reanudar == 91 && !alVolver.Partes()[i].sinConfirmar && !alVolver.EstadoDeToma("n4-sc").sinConfirmar && alVolver.Confirmar(i) == 0,
                      "sin confirmar: cuando se sabe que la del navegador ya no baja, sigue la de Atarraya");
            alVolver.MarcarSinConfirmar(i);
            Comprobar(alVolver.NavegadorSiguio(i) == 91 && !alVolver.Partes()[i].sinConfirmar && alVolver.Partes()[i].estado == EstadoParte::Navegador,
                      "sin confirmar: si la sigue bajando el navegador, queda esa y se quita la de Atarraya");
        }
        {
            // Un cierre de golpe a la mitad: la pausa se pone otra vez, y la reanudación se intenta hasta que de verdad sigue
            DeUnaEnUna q(archivo);
            q.Vaciar();
            q.Agregar({ parte("https://ddownload.com/j1j1j1j1/peli.part7.rar") });
            int i = q.AbrirSiguiente();
            q.Probar(i, 95, "n5-pausa", 1000);
            q.Decidir(i, true);
            q.MarcarSinConfirmar(i);
            q.Guardar();
            DeUnaEnUna tras(archivo);   // se cerró antes de que la lista guardara la pausa: su descarga volvió a la cola
            tras.Cargar();
            Comprobar(tras.RevisarPausa(i, Estado::EnCola, false) == AccionPausa::Pausar && tras.RevisarPausa(i, Estado::Descargando, false) == AccionPausa::Pausar
                          && tras.RevisarPausa(i, Estado::Descargando, true) == AccionPausa::Nada && tras.RevisarPausa(i, Estado::Pausada, false) == AccionPausa::Nada,
                      "sin confirmar: después de un cierre de golpe, la descarga de Atarraya vuelve a la pausa (si volvió a la cola o seguía)");
            tras.Confirmar(i);
            Comprobar(tras.Partes()[i].reanudar && tras.RevisarPausa(i, Estado::Descargando, true) == AccionPausa::Nada
                          && tras.RevisarPausa(i, Estado::Pausada, false) == AccionPausa::Reanudar && tras.Partes()[i].reanudar,
                      "sin confirmar: al confirmarse se reanuda; si la pausa todavía no terminó, se espera y se intenta otra vez");
            tras.Guardar();
            // Una lista de descargas de mentira: cómo está la descarga ahora y cómo quedó guardada
            struct Falsa
            {
                Estado ahora = Estado::Pausada, guardado = Estado::Pausada;
                bool falla = false;
                std::vector<std::string> pasos;
            } f;
            auto lista = [&f, i](DeUnaEnUna* cola) {
                DeUnaEnUna::ListaParaPausas l;
                l.buscar = [&f](uint64_t, Estado& e, bool& pausando) {
                    e = f.ahora;
                    pausando = false;
                    return true;
                };
                l.pausar = [&f](uint64_t) {
                    f.pasos.push_back("pausar");
                    f.ahora = Estado::Pausada;
                };
                l.reanudar = [&f](uint64_t) {
                    f.pasos.push_back("reanudar");
                    f.ahora = Estado::EnCola;
                };
                l.guardar = [&f, cola, i]() {
                    f.pasos.push_back(std::string("guardar con la marca ") + (cola->Partes()[i].reanudar ? "puesta" : "borrada"));
                    if (f.falla) return false;
                    f.guardado = f.ahora;
                    return true;
                };
                return l;
            };
            DeUnaEnUna otraVez(archivo);   // y se cerró otra vez antes de reanudarla
            otraVez.Cargar();
            bool c1 = otraVez.RevisarPausas(lista(&otraVez));   // la reanuda
            bool c2 = otraVez.RevisarPausas(lista(&otraVez));   // ya sigue: guarda la lista y recién después borra la marca
            Comprobar(otraVez.Partes()[i].reanudar == false && !c1 && c2 && f.guardado == Estado::EnCola
                          && f.pasos == std::vector<std::string>{ "reanudar", "guardar con la marca puesta" },
                      "sin confirmar: al abrir Atarraya la reanuda, y la marca se borra recién después de guardar la descarga reanudada");
            // Se cierra de golpe justo entre los dos pasos: la lista ya quedó guardada con la descarga reanudada, pero la cola
            // todavía tiene la marca. Al abrir, la descarga ya sigue: solo se borra la marca (nada queda pausado).
            DeUnaEnUna entreLosDos(archivo);
            entreLosDos.Cargar();
            f.ahora = f.guardado;
            f.pasos.clear();
            bool c3 = entreLosDos.RevisarPausas(lista(&entreLosDos));
            Comprobar(entreLosDos.Partes()[i].reanudar == false && c3 && f.ahora == Estado::EnCola
                          && f.pasos == std::vector<std::string>{ "guardar con la marca puesta" },
                      "sin confirmar: un cierre de golpe justo entre guardar la descarga y borrar la marca no deja la parte pausada");
            // Si la lista no se pudo guardar, la marca sigue; y si se cerró antes de guardarla, al abrir se reanuda otra vez
            DeUnaEnUna sinGuardar(archivo);
            sinGuardar.Cargar();
            f.falla = true;
            sinGuardar.RevisarPausas(lista(&sinGuardar));
            bool sigueLaMarca = sinGuardar.Partes()[i].reanudar;
            f.falla = false;
            f.ahora = Estado::Pausada;   // así quedó guardada: el cierre fue antes de guardar la reanudada
            f.pasos.clear();
            DeUnaEnUna antesDeGuardar(archivo);
            antesDeGuardar.Cargar();
            antesDeGuardar.RevisarPausas(lista(&antesDeGuardar));
            Comprobar(sigueLaMarca && antesDeGuardar.Partes()[i].reanudar && f.pasos == std::vector<std::string>{ "reanudar" },
                      "sin confirmar: si la lista no se guardó, la marca sigue y al abrir Atarraya la descarga se reanuda otra vez");
        }
        DeleteFileW(archivo.c_str());
    }

    // 0.4.0 (revisión): lo que bajó el navegador se pasa a la carpeta del paquete solo si es el mismo archivo, sin enlaces
    // simbólicos ni puntos de unión en ninguna carpeta y sin pisar nada
    void MoverDelNavegadorPruebas()
    {
        namespace fs = std::filesystem;
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        std::wstring base = std::wstring(temporal) + L"atarraya-prueba-mover-" + std::to_wstring(GetCurrentProcessId());
        std::error_code ec;
        fs::remove_all(base, ec);
        const std::wstring nav = base + L"\\navegador", paquete = base + L"\\paquete", real = base + L"\\real", otra = base + L"\\otra";
        for (const auto& d : { nav, paquete, real, otra }) fs::create_directories(d, ec);
        auto escribir = [](const std::wstring& ruta, const std::string& texto, bool agregar = false) {
            std::ofstream f(ruta, std::ios::binary | (agregar ? std::ios::app : std::ios::trunc));
            f << texto;
        };
        auto leer = [](const std::wstring& ruta) {
            std::string t;
            LeerArchivo(ruta, t);
            return t;
        };
        IdentidadArchivo id;
        std::string motivo;
        std::wstring destino;

        // Lo normal: el archivo pasa a la carpeta del paquete, entero
        escribir(nav + L"\\peli.part1.rar", std::string(5000, 'a'));
        bool leido = LeerArchivoDelNavegador(nav + L"\\peli.part1.rar", id, motivo) && id.tamano == 5000 && id.Valida();
        Comprobar(leido && MoverArchivoDelNavegador(nav + L"\\peli.part1.rar", id, paquete, destino, motivo) == Movida::Movida
                      && ExisteArchivo(paquete + L"\\peli.part1.rar") && !ExisteArchivo(nav + L"\\peli.part1.rar")
                      && leer(paquete + L"\\peli.part1.rar") == std::string(5000, 'a'),
                  "mover: lo que bajó el navegador pasa entero a la carpeta del paquete");

        // Una carpeta de la ruta que es un punto de unión (lleva a otra carpeta): ni se lee ni se mueve
        bool union1 = CrearUnion(base + L"\\union", real);
        escribir(real + L"\\peli.part2.rar", "parte dos");
        IdentidadArchivo idReal;
        LeerArchivoDelNavegador(real + L"\\peli.part2.rar", idReal, motivo);
        Comprobar(union1 && !LeerArchivoDelNavegador(base + L"\\union\\peli.part2.rar", id, motivo)
                      && MoverArchivoDelNavegador(base + L"\\union\\peli.part2.rar", idReal, paquete, destino, motivo) == Movida::ConEnlaces
                      && ExisteArchivo(real + L"\\peli.part2.rar") && !ExisteArchivo(paquete + L"\\peli.part2.rar"),
                  "mover: si una carpeta de la ruta es un enlace (punto de unión), no se lee ni se mueve");

        // La carpeta del paquete es un punto de unión a otra carpeta: no se mueve
        bool union2 = CrearUnion(base + L"\\paquete-falso", otra);
        escribir(nav + L"\\peli.part3.rar", "parte tres");
        Comprobar(union2 && LeerArchivoDelNavegador(nav + L"\\peli.part3.rar", id, motivo)
                      && MoverArchivoDelNavegador(nav + L"\\peli.part3.rar", id, base + L"\\paquete-falso", destino, motivo) == Movida::ConEnlaces
                      && ExisteArchivo(nav + L"\\peli.part3.rar") && !ExisteArchivo(otra + L"\\peli.part3.rar"),
                  "mover: si la carpeta del paquete lleva a otra carpeta, no se mueve");

        // El archivo se cambió por otro (con el mismo nombre y tamaño) después de que el navegador avisó: no se mueve
        escribir(nav + L"\\peli.part4.rar", std::string(100, 'x'));
        LeerArchivoDelNavegador(nav + L"\\peli.part4.rar", id, motivo);
        DeleteFileW((nav + L"\\peli.part4.rar").c_str());
        escribir(nav + L"\\peli.part4.rar", std::string(100, 'y'));
        Comprobar(MoverArchivoDelNavegador(nav + L"\\peli.part4.rar", id, paquete, destino, motivo) == Movida::NoCoincide
                      && ExisteArchivo(nav + L"\\peli.part4.rar") && !ExisteArchivo(paquete + L"\\peli.part4.rar"),
                  "mover: un archivo que se cambió por otro antes de moverlo no se mueve");

        // El mismo archivo, pero cambiado (más largo) después del aviso: tampoco
        escribir(nav + L"\\peli.part5.rar", std::string(100, 'z'));
        LeerArchivoDelNavegador(nav + L"\\peli.part5.rar", id, motivo);
        escribir(nav + L"\\peli.part5.rar", "algo mas", true);
        Comprobar(MoverArchivoDelNavegador(nav + L"\\peli.part5.rar", id, paquete, destino, motivo) == Movida::NoCoincide
                      && !ExisteArchivo(paquete + L"\\peli.part5.rar"),
                  "mover: un archivo cambiado después del aviso no se mueve");

        // Nunca pisa: si en la carpeta del paquete ya hay uno con ese nombre, los dos se quedan como estaban
        escribir(paquete + L"\\peli.part6.rar", "el que ya estaba");
        escribir(nav + L"\\peli.part6.rar", "el del navegador");
        LeerArchivoDelNavegador(nav + L"\\peli.part6.rar", id, motivo);
        Comprobar(MoverArchivoDelNavegador(nav + L"\\peli.part6.rar", id, paquete, destino, motivo) == Movida::YaExiste
                      && leer(paquete + L"\\peli.part6.rar") == "el que ya estaba" && leer(nav + L"\\peli.part6.rar") == "el del navegador",
                  "mover: nunca reemplaza un archivo que ya está en la carpeta del paquete");

        // Rutas que no sirven: relativa, con comodines, con «:» de más, o una carpeta
        Comprobar(!LeerArchivoDelNavegador(L"peli.part1.rar", id, motivo) && !LeerArchivoDelNavegador(nav + L"\\*.rar", id, motivo)
                      && !LeerArchivoDelNavegador(nav + L"\\peli.part6.rar:oculto", id, motivo) && !LeerArchivoDelNavegador(nav, id, motivo),
                  "mover: solo una ruta completa a un archivo común");

        // Si Atarraya se cierra después del aviso, al volver a abrirlo sabe qué archivo era y lo termina de mover
        {
            DeUnaEnUna q(base + L"\\cola.json");
            Parte p;
            p.url = "https://ddownload.com/x7x7x7x7/peli.part7.rar";
            p.nombre = L"peli.part7.rar";
            p.carpeta = paquete;
            q.Agregar({ p });
            int i = q.AbrirSiguiente();
            q.EnNavegador(i, "n1-abc");
            escribir(nav + L"\\peli.part7.rar", "parte siete");
            LeerArchivoDelNavegador(nav + L"\\peli.part7.rar", id, motivo);
            q.AnotarArchivoDelNavegador(i, nav + L"\\peli.part7.rar", id);
            q.Guardar();
            DeUnaEnUna alVolver(base + L"\\cola.json");
            alVolver.Cargar();
            auto por = alVolver.PorMover();
            const auto& pv = alVolver.Partes()[i];
            Comprobar(por == std::vector<int>{ i } && pv.identidadNavegador == id && pv.rutaNavegador == nav + L"\\peli.part7.rar" && pv.finalNavegador
                          && MoverArchivoDelNavegador(pv.rutaNavegador, pv.identidadNavegador, paquete, destino, motivo) == Movida::Movida,
                      "mover: si Atarraya se cerró después del aviso, al abrirlo sabe qué archivo era y lo termina de mover");
        }
        // Justo en el momento de mover, alguien intenta cambiar la carpeta del paquete por un enlace a otra (y cambiar el
        // archivo): no puede, y el archivo queda en la carpeta del paquete de verdad
        {
            const std::wstring paquete2 = base + L"\\paquete2", aparte = base + L"\\aparte", trampa = base + L"\\trampa";
            for (const auto& d : { paquete2, trampa }) fs::create_directories(d, ec);
            escribir(nav + L"\\peli.part8.rar", "parte ocho");
            LeerArchivoDelNavegador(nav + L"\\peli.part8.rar", id, motivo);
            bool carpetaCambiada = true, unionHecha = true, archivoCambiado = true;
            pruebas::AntesDeMover = [&]() {
                carpetaCambiada = MoveFileExW(paquete2.c_str(), aparte.c_str(), 0) != FALSE;
                unionHecha = carpetaCambiada && CrearUnion(paquete2, trampa);
                archivoCambiado = MoveFileExW((nav + L"\\peli.part8.rar").c_str(), (nav + L"\\otro.rar").c_str(), 0) != FALSE;
            };
            Movida r = MoverArchivoDelNavegador(nav + L"\\peli.part8.rar", id, paquete2, destino, motivo);
            pruebas::AntesDeMover = nullptr;
            Comprobar(r == Movida::Movida && !carpetaCambiada && !unionHecha && !archivoCambiado && leer(paquete2 + L"\\peli.part8.rar") == "parte ocho"
                          && !ExisteArchivo(trampa + L"\\peli.part8.rar") && !ExisteArchivo(aparte + L"\\peli.part8.rar"),
                      "mover: mientras se mueve, nadie puede cambiar la carpeta del paquete por un enlace ni cambiar el archivo");
            // Y se mueve adentro de la carpeta que se abrió, no por su ruta: aunque su carpeta de arriba cambie de nombre en
            // ese momento, llega a la misma carpeta
            const std::wstring arriba = base + L"\\arriba", arriba2 = base + L"\\arriba-movida";
            fs::create_directories(arriba + L"\\paquete3", ec);
            escribir(nav + L"\\peli.part9.rar", "parte nueve");
            LeerArchivoDelNavegador(nav + L"\\peli.part9.rar", id, motivo);
            bool arribaMovida = false;
            pruebas::AntesDeMover = [&]() { arribaMovida = MoveFileExW(arriba.c_str(), arriba2.c_str(), 0) != FALSE; };
            r = MoverArchivoDelNavegador(nav + L"\\peli.part9.rar", id, arriba + L"\\paquete3", destino, motivo);
            pruebas::AntesDeMover = nullptr;
            bool enLaMisma = leer((arribaMovida ? arriba2 : arriba) + L"\\paquete3\\peli.part9.rar") == "parte nueve";
            Comprobar(enLaMisma && (r == Movida::Movida || (arribaMovida && r == Movida::NoSePudo)) && !ExisteArchivo(nav + L"\\peli.part9.rar"),
                      "mover: el archivo va adentro de la carpeta abierta, aunque su carpeta de arriba cambie de nombre en ese momento");
            RemoveDirectoryW(paquete2.c_str());   // si fuera un enlace, solo se quita el enlace
        }
        RemoveDirectoryW((base + L"\\union").c_str());
        RemoveDirectoryW((base + L"\\paquete-falso").c_str());
        fs::remove_all(base, ec);
    }

    // 0.4.0: las esperas que piden los sitios, las partes que baja el navegador (y la extensión sigue), los sitios donde
    // solo funciona el navegador y cuándo Atarraya pudo seguir la descarga del navegador
    void EsperasYNavegadorPruebas()
    {
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        std::wstring base = std::wstring(temporal) + L"atarraya-prueba-esperas-" + std::to_wstring(GetCurrentProcessId());
        auto parte = [](const std::string& url, const char* grupo) {
            Parte p;
            p.url = url;
            p.nombre = NombreDeUrl(url);
            p.paquete = L"Peli";
            p.grupo = grupo;
            p.carpeta = L"C:\\Descargas\\Peli";
            return p;
        };
        const std::string p1 = "https://ddownload.com/aaa111bbb/peli.part1.rar", p2 = "https://ddownload.com/ccc222ddd/peli.part2.rar",
                          p3 = "https://ddownload.com/eee333fff/peli.part3.rar";
        const int64_t t0 = 1000000;

        Comprobar(MargenDeEspera(60) == 30 && MargenDeEspera(1200) == 60 && MargenDeEspera(100000) == 300,
                  "esperas: el margen que se suma a lo que pide el sitio (30 s, el 5 % o como mucho 5 minutos)");

        DeUnaEnUna cola(base + L"-cola.json");
        cola.Agregar({ parte(p1, "g1"), parte(p2, "g1"), parte(p3, "g1") });
        Comprobar(cola.ParteDePagina(p1) == -1, "esperas: una parte que todavía no se abrió no es una página de la cola");
        cola.AbrirSiguiente();
        Comprobar(cola.ParteDePagina(p1) == 0 && cola.ParteDePagina("https://ddownload.com/aaa111bbb") == 0
                      && cola.ParteDePagina("https://ddownload.com/") == -1 && cola.ParteDePagina("https://otro.org/aaa111bbb/peli.part1.rar") == -1,
                  "esperas: la página que abrió Atarraya (también sin el nombre al final); otra página u otro sitio, no");

        // La cuenta regresiva de la página: la página sigue abierta; solo se avisa al final, una vez
        Comprobar(cola.Esperar(0, false, 30, t0) && cola.Partes()[0].estado == EstadoParte::Abierta && cola.Partes()[0].hasta == t0 + 30,
                  "esperas: la cuenta de la página deja la parte abierta y se muestra");
        Comprobar(cola.CuentasTerminadas(t0 + 29).empty() && cola.CuentasTerminadas(t0 + 30) == std::vector<int>{ 0 }
                      && cola.CuentasTerminadas(t0 + 31).empty() && cola.Vencidas(t0 + 1000).empty(),
                  "esperas: al terminar la cuenta de la página se avisa una vez y no se abre otra vez la página");

        // Un límite del sitio: espera con margen; al cumplirse, vuelve a abrirse con un intento más
        Comprobar(cola.Esperar(0, true, 600, t0) && cola.Partes()[0].estado == EstadoParte::Espera
                      && cola.Partes()[0].hasta == t0 + 600 + MargenDeEspera(600) && cola.HayEnEspera() && !cola.HayAbierta() && cola.Actual() == 0,
                  "esperas: «Blocked: wait 10 minutes» pone la parte a esperar 10 minutos más el margen");
        Comprobar(cola.ParteDePagina(p1) == 0 && !cola.Terminada() && cola.Sobrantes().size() == 3,
                  "esperas: la que espera sigue en la cola (y si se para, vuelve al recolector)");
        auto e = cola.Emparejar("https://s7.ddownload.com/d/t/descarga", "", p1, L"");
        Comprobar(e.parte == 0 && e.segura, "esperas: si la persona baja igual desde esa página, la descarga es de la que espera");
        Comprobar(cola.Vencidas(t0 + 600).empty() && cola.Vencidas(t0 + 600 + MargenDeEspera(600)) == std::vector<int>{ 0 }
                      && cola.Partes()[0].estado == EstadoParte::Abierta && cola.Partes()[0].intentos == 1 && cola.Partes()[0].hasta == 0,
                  "esperas: se cumple el tiempo más el margen: la parte se abre otra vez (un intento más)");
        Comprobar(cola.Esperar(0, true, -1, t0) && cola.Partes()[0].hasta == t0 + EsperaSinTiempo + MargenDeEspera(EsperaSinTiempo),
                  "esperas: «Blocked» sin decir cuánto: se prueba otra vez en 10 minutos");
        cola.Guardar();
        DeUnaEnUna otra(base + L"-cola.json");
        otra.Cargar();
        Comprobar(otra.Partes().size() == 3 && otra.Partes()[0].estado == EstadoParte::Espera && otra.Partes()[0].hasta == cola.Partes()[0].hasta
                      && otra.Partes()[0].intentos == 1 && otra.Partes()[0].limite,
                  "esperas: al volver a abrir Atarraya, la espera sigue donde iba");
        int64_t t = t0;
        bool siguio = true;
        for (int k = 0; k < 20 && siguio; k++)
        {
            t += 100000;
            cola.Vencidas(t);
            siguio = cola.Esperar(0, true, 600, t);
        }
        Comprobar(!siguio && cola.Partes()[0].estado == EstadoParte::Fallo && cola.Partes()[0].intentos == MaxIntentosEspera
                      && !cola.HayEnEspera(),
                  "esperas: si después de 6 intentos el sitio sigue pidiendo esperar, la parte queda para después (vuelve al recolector)");

        // «Abrir otra vez» abre ya una que espera; «Saltar esta» también la salta
        int j = cola.AbrirSiguiente();
        cola.Esperar(j, true, 600, t0);
        cola.AbrirOtraVez(j);
        Comprobar(j == 1 && cola.Partes()[j].estado == EstadoParte::Abierta && cola.Partes()[j].hasta == 0 && !cola.Partes()[j].limite,
                  "esperas: «Abrir otra vez» abre ya la página de una que espera");

        // La baja el navegador: la extensión la sigue y avisa cómo va; al terminar queda lista con la descarga que la anota
        cola.Tomar(j, 55);
        cola.EnNavegador(j, "n7-abc");
        Comprobar(cola.Partes()[j].estado == EstadoParte::Navegador && cola.BuscarToma("n7-abc") == j && cola.BuscarToma("otra") == -1
                      && cola.BuscarToma("") == -1 && cola.Actual() == j && !cola.Terminada() && cola.Partes()[j].descarga == 0,
                  "navegador: la parte que baja el navegador sigue en la cola, con su toma");
        cola.Progreso(j, 1000, 5000);
        Comprobar(cola.Partes()[j].bajado == 1000 && cola.Partes()[j].total == 5000, "navegador: cómo va la descarga del navegador");
        e = cola.Emparejar("https://s7.ddownload.com/d/t/descarga", "", p2, L"");
        Comprobar(e.parte != j, "navegador: mientras la baja el navegador, otra descarga no se toma como esa parte");
        cola.Guardar();
        DeUnaEnUna alVolver(base + L"-cola.json");
        alVolver.Cargar();
        Comprobar(alVolver.BuscarToma("n7-abc") == j && alVolver.Partes()[j].estado == EstadoParte::Navegador,
                  "navegador: al volver a abrir Atarraya, se sigue esperando lo que baja el navegador");
        cola.TerminarEnNavegador(j, true, 77);
        Comprobar(cola.Partes()[j].estado == EstadoParte::Lista && cola.Partes()[j].descarga == 77 && cola.BuscarToma("n7-abc") == -1,
                  "navegador: al terminar, la parte queda lista con la descarga que la anota en la lista");
        int k3 = cola.AbrirSiguiente();
        cola.EnNavegador(k3, "n8-def");
        cola.TerminarEnNavegador(k3, false, 0);
        Comprobar(cola.Partes()[k3].estado == EstadoParte::Fallo && cola.Terminada() && cola.Sobrantes().size() == 2,
                  "navegador: si el navegador no la termina, la parte vuelve al recolector al final");
        Comprobar(cola.BuscarToma("n8-def") == k3, "navegador: una que se cortó se sigue reconociendo (la persona puede reanudarla)");
        cola.TerminarEnNavegador(k3, true, 88);
        Comprobar(cola.Partes()[k3].estado == EstadoParte::Lista && cola.Partes()[k3].descarga == 88,
                  "navegador: si la persona la reanuda en el navegador y termina, queda lista");

        // Las páginas de un sitio de descargas que Atarraya no conoce también van de una en una
        EnlaceRecolectado pagArchivo;
        pagArchivo.estado = Comprobacion::Pagina;
        pagArchivo.url = "https://hoster-desconocido.org/abc123/peli.part1.rar";
        pagArchivo.origen = "lista";
        EnlaceRecolectado articulo = pagArchivo, articuloHtml = pagArchivo, deCnl = pagArchivo, archivo = pagArchivo;
        articulo.url = "https://noticias.org/2026/10/articulo";
        articuloHtml.url = "https://noticias.org/articulo.html";
        deCnl.url = "https://hoster-desconocido.org/f/abc123";
        deCnl.origen = "clicknload";
        archivo.estado = Comprobacion::Archivo;
        Comprobar(PaginaDeSitioDeDescargas(pagArchivo) && PaginaDeSitioDeDescargas(deCnl) && !PaginaDeSitioDeDescargas(articulo)
                      && !PaginaDeSitioDeDescargas(articuloHtml) && !PaginaDeSitioDeDescargas(archivo),
                  "cola: una página que se llama como un archivo («.../peli.part1.rar») o que llegó por Click'n'Load va de una en una; "
                  "un artículo no");

        // Los sitios donde solo funciona el navegador: por sitio (ddl.to y ddownload.com son uno) y se guardan
        SitiosNavegador s(base + L"-sitios.json");
        Comprobar(!s.Necesita("https://ddownload.com/abc"), "navegador: al principio ningún sitio necesita el navegador");
        s.Anotar("https://ddl.to/abc123", 1000);
        Comprobar(s.Necesita("https://ddownload.com/x") && s.Necesita("https://s12.ddownload.com/d/y") && !s.Necesita("https://rapidgator.net/file/1")
                      && !s.Necesita(""),
                  "navegador: un sitio anotado cuenta con sus servidores y sus otros dominios; otros sitios no");
        SitiosNavegador s2(base + L"-sitios.json");
        s2.Cargar();
        Comprobar(s2.Necesita("https://ddl.to/zzz") && s2.Cantidad() == 1, "navegador: los sitios anotados se recuerdan al volver a abrir Atarraya");

        // ¿Atarraya pudo seguir la descarga del navegador?
        Comprobar(DecidirToma(Estado::Descargando, 4096, 2) == Toma::Tomada && DecidirToma(Estado::Completada, 0, 2) == Toma::Tomada,
                  "toma: si ya llegan bytes del archivo (o terminó), Atarraya la tomó y el navegador cancela la suya");
        Comprobar(DecidirToma(Estado::Error, 0, 2) == Toma::Navegador && DecidirToma(Estado::Pausada, 0, 2) == Toma::Navegador,
                  "toma: una página, un 403 o 410 o un enlace usado (error): sigue el navegador");
        Comprobar(DecidirToma(Estado::Iniciando, 0, 5) == Toma::Probando && DecidirToma(Estado::EnCola, 0, 5) == Toma::Probando
                      && DecidirToma(Estado::Descargando, 0, 5) == Toma::Probando && DecidirToma(Estado::Iniciando, 0, 25) == Toma::Navegador,
                  "toma: mientras no llega nada se espera un poco; si tarda más de 25 s, sigue el navegador");

        // Lo que bajó el navegador entra a la lista como terminado y cuenta en su paquete para Jícaro
        std::wstring archivoMotor = base + L"-descargas.json";
        {
            Motor m(archivoMotor);
            DatosDescarga d;
            d.url = "https://s7.ddownload.com/d/t/peli.part1.rar";
            d.carpeta = L"C:\\Descargas\\Peli";
            d.nombre = L"peli.part1.rar";
            d.tamano = 1000;
            d.grupo = "g9";
            d.paquete = L"Peli";
            d.cookies = "secreta";
            uint64_t id1 = m.AgregarTerminada(d);
            d.nombre = L"peli.part2.rar";
            d.url = "https://s7.ddownload.com/d/t/peli.part2.rar";
            uint64_t id2 = m.AgregarTerminada(d);
            Foto f;
            Comprobar(m.Buscar(id1, f) && f.datos.estado == Estado::Completada && f.hecho == 1000 && f.datos.conexiones == 1
                          && f.datos.grupo == "g9" && f.datos.cookies.empty() && id2 == id1 + 1,
                      "navegador: lo que bajó el navegador entra a la lista como terminado, sin cookies");
            DeUnaEnUna q(base + L"-cola2.json");
            q.Agregar({ parte("https://ddownload.com/x1x1x1x1/peli.part1.rar", "g9"), parte("https://ddownload.com/y2y2y2y2/peli.part2.rar", "g9") });
            q.Tomar(q.AbrirSiguiente(), id1);
            q.Terminar(id1, true);
            int n = q.AbrirSiguiente();
            q.EnNavegador(n, "n9-x");
            Comprobar(jicaro::ArchivosDelPaquete("g9", m.Fotos(), q.Partes()).empty(),
                      "navegador: Jícaro espera mientras el navegador baja una parte del paquete");
            q.TerminarEnNavegador(n, true, id2);
            Comprobar(jicaro::ArchivosDelPaquete("g9", m.Fotos(), q.Partes()).size() == 2,
                      "navegador: cuando el navegador termina, el paquete está completo para Jícaro");
            m.Guardar();
            Motor m2(archivoMotor);
            m2.Cargar();
            Comprobar(m2.Buscar(id2, f) && f.datos.estado == Estado::Completada && f.datos.nombre == L"peli.part2.rar",
                      "navegador: sigue en la lista al volver a abrir Atarraya");
        }
        for (const auto& f : { base + L"-cola.json", base + L"-cola2.json", base + L"-sitios.json", archivoMotor })
        {
            DeleteFileW(f.c_str());
            DeleteFileW((f + L".tmp").c_str());
        }
    }

    // Click'n'Load: las tres formas en que un navegador manda un formulario, los nombres del campo de los enlaces, el GET,
    // los topes y lo que queda en el registro (el motivo de cada rechazo; nunca los enlaces ni lo que trae el formulario)
    void ClickNLoadPruebas()
    {
        const auto npos = std::string::npos;
        using Lista = std::vector<std::string>;
        cnl::Campos c;
        Comprobar(cnl::LeerFormulario("application/x-www-form-urlencoded; charset=UTF-8", "urls=a&URLS=b&urls=c&solo&=x&&", c).empty()
                      && c.size() == 4 && cnl::Valor(c, "urls") == "a" && cnl::Tiene(c, "solo") && cnl::Valor(c, "solo").empty(),
                  "CnL: urlencoded con nombres repetidos (quedan todos; el primero manda) y un campo sin valor");
        std::string muchos;
        for (size_t i = 0; i <= cnl::MaxCampos; i++) muchos += "c" + std::to_string(i) + "=1&";
        Comprobar(cnl::LeerFormulario("", muchos, c).rfind("demasiados campos", 0) == 0, "CnL: más de 64 campos se rechaza");
        Comprobar(cnl::LeerFormulario("", "urls=" + std::string(cnl::MaxCampo + 1, 'a'), c).find("demasiado largo") != npos,
                  "CnL: un campo de más de 512 KB se rechaza");

        // multipart/form-data como lo manda Chrome (con algo antes de la primera marca y una parte que es un archivo)
        const std::string b = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
        auto parte = [&](const std::string& nombre, const std::string& valor, const std::string& mas = "") {
            return "--" + b + "\r\nContent-Disposition: form-data; name=\"" + nombre + "\"" + mas + "\r\n\r\n" + valor + "\r\n";
        };
        const std::string fin = "--" + b + "--\r\n";
        const std::string mp = "esto se ignora\r\n" + parte("urls", "https://ejemplo.org/a.zip\r\nhttps://ejemplo.org/b.zip")
                               + parte("source", "https://ejemplo.org/carpeta")
                               + parte("lista", "https://ejemplo.org/c.zip", "; filename=\"lista.txt\"\r\nContent-Type: text/plain") + fin;
        Comprobar(cnl::LeerFormulario("multipart/form-data; boundary=" + b, mp, c).empty() && c.size() == 3
                      && cnl::Valor(c, "urls") == "https://ejemplo.org/a.zip\r\nhttps://ejemplo.org/b.zip"
                      && cnl::Valor(c, "source") == "https://ejemplo.org/carpeta" && cnl::Valor(c, "lista") == "https://ejemplo.org/c.zip",
                  "CnL: multipart (con preámbulo y una parte que es un archivo)");
        Comprobar(cnl::LeerFormulario("Multipart/Form-Data; charset=utf-8; boundary=\"" + b + "\"", mp, c).empty() && c.size() == 3,
                  "CnL: multipart con el boundary entre comillas");
        Comprobar(cnl::LeerFormulario("multipart/form-data; boundary=" + b, parte("urls", "x") + parte("source", "y") + fin, c).empty()
                      && cnl::Valor(c, "urls") == "x" && cnl::Valor(c, "source") == "y",
                  "CnL: multipart que empieza con la primera marca");
        Comprobar(cnl::LeerFormulario("multipart/form-data", mp, c).find("boundary") != npos, "CnL: multipart sin boundary se rechaza");
        Comprobar(cnl::LeerFormulario("multipart/form-data; boundary=" + b, parte("urls", "https://ejemplo.org/a.zip"), c).find("incompleto") != npos,
                  "CnL: multipart sin la marca final se rechaza");
        std::string mpMuchos;
        for (size_t i = 0; i <= cnl::MaxCampos; i++) mpMuchos += parte("c" + std::to_string(i), "1");
        Comprobar(cnl::LeerFormulario("multipart/form-data; boundary=" + b, mpMuchos + fin, c).rfind("demasiados campos", 0) == 0,
                  "CnL: multipart con más de 64 partes se rechaza");
        Comprobar(cnl::LeerFormulario("multipart/form-data; boundary=" + b, parte("urls", std::string(cnl::MaxCampo + 1, 'a')) + fin, c)
                          .find("demasiado largo") != npos,
                  "CnL: multipart con una parte de más de 512 KB se rechaza");

        // text/plain: una línea sin «nombre=» sigue el campo de antes
        Comprobar(cnl::LeerFormulario("text/plain", "urls=https://ejemplo.org/a.zip\r\nhttps://ejemplo.org/b.zip?x=1\r\nsource=https://ejemplo.org/c\r\n", c)
                          .empty()
                      && cnl::Valor(c, "urls") == "https://ejemplo.org/a.zip\nhttps://ejemplo.org/b.zip?x=1"
                      && cnl::Valor(c, "source") == "https://ejemplo.org/c",
                  "CnL: text/plain");
        Comprobar(!cnl::TipoAdmitido("application/json") && cnl::TipoAdmitido("Multipart/Form-Data; boundary=x") && cnl::TipoAdmitido("")
                      && cnl::TipoAdmitido("application/x-www-form-urlencoded;charset=UTF-8")
                      && !cnl::TipoAdmitido("application/x-www-form-urlencodedx"),
                  "CnL: los tipos de formulario que se leen");

        // Los nombres del campo de los enlaces
        std::string t;
        cnl::LeerFormulario("", "url=https%3A%2F%2Fa.org%2F1&links=https%3A%2F%2Fa.org%2F2&link=https%3A%2F%2Fa.org%2F3"
                                "&URLS[]=https%3A%2F%2Fa.org%2F4&urls%5B0%5D=https%3A%2F%2Fa.org%2F5&enlaces=https%3A%2F%2Fa.org%2F6", c);
        Comprobar(cnl::TextoDeEnlaces(c, t)
                      && cnl::Enlaces(t, 1000).enlaces == Lista{ "https://a.org/1", "https://a.org/2", "https://a.org/3", "https://a.org/4", "https://a.org/5" },
                  "CnL: campos urls, url, links, link, urls[] y urls[0] (otros nombres no)");
        cnl::LeerFormulario("", "crypted=x&jk=y", c);
        Comprobar(!cnl::TextoDeEnlaces(c, t), "CnL: un formulario sin campo de enlaces");

        // Cómo se separan los enlaces y qué se cuenta
        auto l = cnl::Enlaces("https://a.org/1\r\nhttps://a.org/2 https://a.org/3\thttps://a.org/4,https://a.org/5, https://a.org/6 ,https://a.org/7\n", 1000);
        Comprobar(l.enlaces.size() == 7 && l.llegaron == 7 && l.otros == 0, "CnL: enlaces separados por saltos de línea, espacios, tabuladores y comas");
        l = cnl::Enlaces("https://a.org/archivo,1.zip https://a.org/Don't.Look.Up.mkv https://a.org/x_(1).rar HTTPS://A.ORG/MAYUS.ZIP", 1000);
        Comprobar(l.enlaces == Lista{ "https://a.org/archivo,1.zip", "https://a.org/Don't.Look.Up.mkv", "https://a.org/x_(1).rar", "HTTPS://A.ORG/MAYUS.ZIP" },
                  "CnL: una coma, una comilla simple o paréntesis dentro de la dirección se quedan (y las mayúsculas)");
        l = cnl::Enlaces("https://a.org/1 https://a.org/1 https://www.youtube.com/watch?v=x hola www.a.org/2 "
                         "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567",
                         1000);
        Comprobar(l.llegaron == 4 && l.enlaces.size() == 2 && l.repetidos == 1 && l.youtube == 1 && l.otros == 2,
                  "CnL: cuenta los repetidos, los de YouTube y lo que no es un enlace");
        Comprobar(cnl::Resumen(l) == "llegaron 4 enlaces y quedaron 2 (1 repetido, 1 de YouTube, que Atarraya no baja); además, 2 trozos de texto que no son enlaces",
                  "CnL: el resumen para el registro");
        l = cnl::Enlaces("<a href=\"https://a.org/1\">uno</a> \"https://a.org/2\"", 1000);
        Comprobar(l.enlaces == Lista{ "https://a.org/1", "https://a.org/2" }, "CnL: enlaces dentro de HTML o entre comillas");
        l = cnl::Enlaces("https%3A%2F%2Fa.org%2F1%0D%0Ahttps%3A%2F%2Fa.org%2F2", 1000);
        Comprobar(l.enlaces == Lista{ "https://a.org/1", "https://a.org/2" }, "CnL: enlaces codificados dos veces");
        l = cnl::Enlaces("https://a.org/espa\xF1ol.zip", 1000);
        Comprobar(l.enlaces == Lista{ "https://a.org/espa%F1ol.zip" }, "CnL: una letra que no es UTF-8 queda como %XX");
        std::string mil;
        for (int i = 0; i < 1005; i++) mil += "https://a.org/" + std::to_string(i) + "\n";
        l = cnl::Enlaces(mil, 1000);
        Comprobar(l.enlaces.size() == 1000 && l.sobran == 5 && l.llegaron == 1005, "CnL: de 1005 enlaces quedan 1000 y se cuentan los que sobran");
        Comprobar(cnl::Resumen(cnl::Enlaces("", 1000)) == "llegaron 0 enlaces y quedaron 0", "CnL: un campo vacío");

        // El servidor de verdad: en un puerto libre y con un registro aparte
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wchar_t temporal[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temporal);
        std::wstring carpeta = std::wstring(temporal) + L"atarraya-prueba-cnl-" + std::to_wstring(GetCurrentProcessId());
        std::error_code ec;
        std::filesystem::remove_all(carpeta, ec);
        std::filesystem::create_directories(carpeta, ec);
        SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", carpeta.c_str());   // el registro de esta prueba va aquí
        std::mutex m;
        std::vector<Lista> recibidos;
        std::vector<std::wstring> paquetes;
        std::vector<Lista> claves;
        auto alRecibir = [&](Lista enlaces, std::wstring paquete, std::string, Lista contrasenas) {
            std::lock_guard g(m);
            recibidos.push_back(std::move(enlaces));
            paquetes.push_back(std::move(paquete));
            claves.push_back(std::move(contrasenas));
        };
        auto cuantos = [&]() {
            std::lock_guard g(m);
            return recibidos.size();
        };
        auto ultimos = [&]() {
            std::lock_guard g(m);
            return recibidos.empty() ? Lista() : recibidos.back();
        };
        auto pedir = [](uint16_t puerto, const std::string& pedido) {
            std::string r;
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(puerto);
            DWORD espera = 10000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&espera), sizeof(espera));
            if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0)
            {
                for (size_t enviado = 0; enviado < pedido.size();)
                {
                    int n = send(s, pedido.data() + enviado, (int)(pedido.size() - enviado), 0);
                    if (n <= 0) break;
                    enviado += (size_t)n;
                }
                // Como un navegador: con la respuesta completa (su Content-Length), cierra su lado
                char buf[4096];
                for (int n; (n = recv(s, buf, sizeof(buf), 0)) > 0;)
                {
                    r.append(buf, (size_t)n);
                    size_t fin = r.find("\r\n\r\n"), largo = r.find("Content-Length: ");
                    if (fin != std::string::npos && largo < fin && r.size() >= fin + 4 + (size_t)atoll(r.c_str() + largo + 16)) break;
                }
            }
            closesocket(s);
            return r;
        };
        auto codigo = [](const std::string& r) { return r.rfind("HTTP/1.1 ", 0) == 0 ? atoi(r.c_str() + 9) : 0; };
        auto cuerpoDe = [](const std::string& r) {
            size_t p = r.find("\r\n\r\n");
            return p == std::string::npos ? std::string() : r.substr(p + 4);
        };

        ServidorCnL servidor;
        std::string error;
        Comprobar(servidor.Iniciar(alRecibir, error, 0) && servidor.Puerto() != 0, "CnL servidor: escucha en un puerto libre");
        const uint16_t puerto = servidor.Puerto();
        const std::string host = "Host: 127.0.0.1:" + std::to_string(puerto) + "\r\n";
        const std::string origen = "Origin: https://carpetas.ejemplo.org\r\n";
        const std::string urlenc = "application/x-www-form-urlencoded";
        auto post = [&](const std::string& ruta, const std::string& tipo, const std::string& cuerpo, const std::string& mas = "") {
            return pedir(puerto, "POST " + ruta + " HTTP/1.1\r\n" + host + (tipo.empty() ? std::string() : "Content-Type: " + tipo + "\r\n")
                                     + "Content-Length: " + std::to_string(cuerpo.size()) + "\r\n" + origen + mas + "\r\n" + cuerpo);
        };

        auto r = post("/flash/add", urlenc,
                      "urls=https%3A%2F%2Fejemplo.org%2Fsecreto-1.zip%0D%0Ahttps%3A%2F%2Fejemplo.org%2Fsecreto-2.zip&package=Paquete+privado");
        Comprobar(codigo(r) == 200 && cuerpoDe(r) == "success\r\n" && cuantos() == 1 && ultimos().size() == 2, "CnL servidor: urlencoded con «urls»");
        r = post("/flash/add", "multipart/form-data; boundary=" + b,
                 parte("urls", "https://ejemplo.org/secreto-3.zip\r\nhttps://ejemplo.org/secreto-4.zip")
                     + parte("source", "https://carpetas.ejemplo.org/f/privado") + fin);
        Comprobar(codigo(r) == 200 && cuantos() == 2 && ultimos().size() == 2, "CnL servidor: multipart/form-data (antes daba 415)");
        r = post("/flash/add", urlenc, "links=https%3A%2F%2Fejemplo.org%2Fsecreto-5.zip");
        Comprobar(codigo(r) == 200 && cuantos() == 3 && ultimos().size() == 1, "CnL servidor: el campo «links» (antes daba 400)");
        r = pedir(puerto, "GET /flash/add?urls=https%3A%2F%2Fejemplo.org%2Fsecreto-6.zip+https%3A%2F%2Fejemplo.org%2Fsecreto-7.zip HTTP/1.1\r\n" + host + "\r\n");
        Comprobar(codigo(r) == 200 && cuantos() == 4 && ultimos().size() == 2, "CnL servidor: GET /flash/add?urls= (antes daba 404)");
        r = post("/flash/add", "text/plain", "urls=https://ejemplo.org/secreto-8.zip\r\nhttps://ejemplo.org/secreto-9.zip\r\n");
        Comprobar(codigo(r) == 200 && cuantos() == 5 && ultimos().size() == 2, "CnL servidor: text/plain");
        r = post("/flash/add/", urlenc, "urls=https%3A%2F%2Fejemplo.org%2Fsecreto-10.zip", "Sec-Fetch-Dest: document\r\n");
        Comprobar(codigo(r) == 200 && r.find("Content-Type: text/html") != npos && cuerpoDe(r).find("Atarraya recibió 1 enlace") != npos
                      && cuerpoDe(r).find("recolector") != npos && cuantos() == 6,
                  "CnL servidor: en una pestaña responde con una página en español (y «/flash/add/» también vale)");
        bool paquetesBien;
        {
            std::lock_guard g(m);
            paquetesBien = paquetes.size() >= 2 && paquetes[0] == L"Paquete privado" && paquetes[1] == L"Click'n'Load de carpetas.ejemplo.org";
        }
        Comprobar(paquetesBien, "CnL servidor: el nombre del paquete (del campo «package» o del sitio)");
        // La contraseña del paquete (campo «passwords», varias separadas por saltos de línea), en los dos formularios
        r = post("/flash/add", urlenc, "urls=https%3A%2F%2Fejemplo.org%2Fsecreto-20.rar&passwords=clave-cnl-secreta-1%0D%0Aclave-cnl-secreta-2");
        bool clavesBien;
        {
            std::lock_guard g(m);
            clavesBien = codigo(r) == 200 && !claves.empty() && claves.back() == Lista{ "clave-cnl-secreta-1", "clave-cnl-secreta-2" }
                         && claves.front().empty();
        }
        Comprobar(clavesBien, "CnL servidor: las contraseñas del campo «passwords» llegan con los enlaces (y sin ellas, ninguna)");

        // Rechazos: ninguno llega al recolector y cada uno queda en el registro con su motivo
        size_t antes = cuantos();
        r = post("/flash/add", "application/json", "{\"urls\":[\"https://ejemplo.org/secreto-11.zip\"]}");
        Comprobar(codigo(r) == 415 && cuerpoDe(r) == "failed\r\n", "CnL servidor: JSON da 415");
        r = post("/flash/add", urlenc, "enlaces=https%3A%2F%2Fejemplo.org%2Fsecreto-12.zip&source=https%3A%2F%2Fcarpetas.ejemplo.org%2Fprivado");
        Comprobar(codigo(r) == 400, "CnL servidor: sin campo de enlaces da 400");
        r = post("/flash/add", urlenc, "urls=hola+mundo+privado");
        Comprobar(codigo(r) == 400, "CnL servidor: sin enlaces válidos da 400");
        r = pedir(puerto, "POST /flash/add HTTP/1.1\r\nHost: otro.ejemplo:" + std::to_string(puerto) + "\r\nContent-Length: 0\r\n" + origen + "\r\n");
        Comprobar(codigo(r) == 403, "CnL servidor: otro host da 403");
        r = pedir(puerto, "POST /flash/add HTTP/1.1\r\n" + host + "Content-Length: 2000000\r\n" + origen + "\r\n");
        Comprobar(codigo(r) == 413, "CnL servidor: más de 1 MB da 413");
        r = post("/flash/addcrypted", urlenc, "crypted=abc");
        Comprobar(codigo(r) == 400, "CnL servidor: un DLC se rechaza");
        r = post("/flash/addcrypted2", urlenc, "crypted=abc");
        Comprobar(codigo(r) == 400, "CnL servidor: addcrypted2 sin «jk» se rechaza");
        r = post("/flash/add", "multipart/form-data; boundary=" + b, mpMuchos + fin);
        Comprobar(codigo(r) == 400, "CnL servidor: multipart con demasiadas partes se rechaza");
        r = pedir(puerto, "GET /otra HTTP/1.1\r\n" + host + "\r\n");
        Comprobar(codigo(r) == 404, "CnL servidor: una dirección que no existe da 404");
        r = pedir(puerto, "GET /carpeta/secreto-13/archivo.zip HTTP/1.1\r\n" + host + "\r\n");
        Comprobar(codigo(r) == 404, "CnL servidor: otra dirección más larga también da 404");
        r = pedir(puerto, "PUT /flash/add HTTP/1.1\r\n" + host + "\r\n");
        Comprobar(codigo(r) == 405, "CnL servidor: PUT da 405");
        r = post("/flash/add", "application/json", "{}", "Sec-Fetch-Dest: document\r\n");
        Comprobar(codigo(r) == 415 && cuerpoDe(r).find("Atarraya no pudo recibir estos enlaces") != npos
                      && cuerpoDe(r).find("tipo de envío no admitido") != npos,
                  "CnL servidor: en una pestaña, el rechazo se explica en español");
        Comprobar(cuantos() == antes, "CnL servidor: ningún rechazo llega al recolector");
        r = pedir(puerto, "GET /jdcheck.js HTTP/1.1\r\nHost: localhost:" + std::to_string(puerto) + "\r\n\r\n");
        Comprobar(codigo(r) == 200 && cuerpoDe(r).rfind("jdownloader=true;", 0) == 0, "CnL servidor: /jdcheck.js sigue igual");
        servidor.Detener();

        // Topes por minuto (otro servidor, con sus propias cuentas): 30 pedidos con enlaces y 20 rechazos en el registro
        {
            ServidorCnL otro;
            Comprobar(otro.Iniciar(alRecibir, error, 0), "CnL servidor: otro en un puerto libre");
            const uint16_t p2 = otro.Puerto();
            const std::string h2 = "Host: localhost:" + std::to_string(p2) + "\r\n";
            int bien = 0;
            for (int i = 0; i < 30; i++)
                bien += codigo(pedir(p2, "GET /flash/add?urls=https%3A%2F%2Fejemplo.org%2Ftope-" + std::to_string(i) + ".zip HTTP/1.1\r\n" + h2 + "\r\n")) == 200;
            r = pedir(p2, "GET /flash/add?urls=https%3A%2F%2Fejemplo.org%2Ftope-30.zip HTTP/1.1\r\n" + h2 + "\r\n");
            Comprobar(bien == 30 && codigo(r) == 429, "CnL servidor: 30 pedidos por minuto; el siguiente da 429");
            for (int i = 0; i < 25; i++) pedir(p2, "DELETE /jdcheck.js HTTP/1.1\r\n" + h2 + "\r\n");
            otro.Detener();
        }

        std::string registro;
        LeerArchivo(carpeta + L"\\registro.txt", registro);
        auto hay = [&](const std::string& s) { return registro.find(s) != npos; };
        auto veces = [&](const std::string& s) {
            size_t n = 0;
            for (size_t p = registro.find(s); p != npos; p = registro.find(s, p + 1)) n++;
            return n;
        };
        Comprobar(hay("Click'n'Load: llegaron 2 enlaces y quedaron 2. Pedido: POST /flash/add, urlencoded, desde carpetas.ejemplo.org"),
                  "CnL registro: cuántos enlaces llegaron, cuántos quedaron y de qué sitio");
        Comprobar(hay("POST /flash/add, multipart, desde carpetas.ejemplo.org") && hay("GET /flash/add, en la dirección, desde un sitio desconocido")
                      && hay("POST /flash/add, text/plain, desde carpetas.ejemplo.org"),
                  "CnL registro: cómo llegó cada formulario");
        Comprobar(hay("rechazado: tipo de envío no admitido. Pedido: POST /flash/add, application/json, desde carpetas.ejemplo.org"),
                  "CnL registro: tipo de envío no admitido");
        Comprobar(hay("rechazado: falta el campo de los enlaces (urls, url, links o link); llegaron 2 campos: enlaces, source"),
                  "CnL registro: el campo que falta (con los nombres que sí llegaron)");
        Comprobar(hay("rechazado: no trae enlaces válidos: llegaron 0 enlaces y quedaron 0; además, 3 trozos de texto que no son enlaces"),
                  "CnL registro: sin enlaces válidos, con la cuenta");
        Comprobar(hay("rechazado: host no permitido (otro.ejemplo:" + std::to_string(puerto) + ")"), "CnL registro: host no permitido");
        Comprobar(hay("rechazado: demasiado grande (más de 1 MB)"), "CnL registro: demasiado grande");
        Comprobar(hay("rechazado: un contenedor DLC"), "CnL registro: DLC");
        Comprobar(hay("rechazado: falta el campo jk; llegó 1 campo: crypted"), "CnL registro: falta «jk»");
        Comprobar(hay("rechazado: demasiados campos (más de 64)"), "CnL registro: demasiados campos");
        Comprobar(hay("rechazado: dirección que no existe. Pedido: GET (otra dirección), desde") && !hay("GET /otra")
                      && !hay("secreto-13"),
                  "CnL registro: dirección que no existe, sin anotar su ruta (aunque sea corta podría traer una clave)");
        Comprobar(hay("rechazado: método no admitido. Pedido: PUT /flash/add"), "CnL registro: método no admitido");
        Comprobar(hay("rechazado: demasiados pedidos (más de 30 por minuto)"), "CnL registro: demasiados pedidos");
        Comprobar(veces("DELETE /jdcheck.js") == 19 && veces("llegan demasiados rechazos") == 1,
                  "CnL registro: como mucho 20 rechazos por minuto (una página no puede llenarlo)");
        Comprobar(!hay("secreto") && !hay("privado") && !hay("hola") && !hay("https://") && !hay("tope-1") && !hay("Paquete"),
                  "CnL registro: nunca los enlaces ni lo que trae el formulario");
        Comprobar(!hay("clave-cnl") && hay("Trae 2 contraseñas del paquete (no se anotan)"),
                  "CnL registro: nunca la contraseña del paquete (solo que vino)");

        SetEnvironmentVariableW(L"ATARRAYA_PRUEBA", nullptr);
        std::filesystem::remove_all(carpeta, ec);
        WSACleanup();
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
    DeUnaEnUnaPruebas();
    PaginaEnVezDeArchivo();
    PaginaEnUnaParte();
    JicaroPruebas();
    FlujosPruebas();
    ClickNLoadPruebas();
    EsperasYNavegadorPruebas();
    MoverDelNavegadorPruebas();
    TomasPruebas();
    AvisoFinalPruebas();
    PuertosPruebas();
    JuegosPruebas();
    JuegosEnPartesPruebas();
    ContrasenasPruebas();
    ConciliarPruebas();
    printf(fallas ? "\n%d FALLAS\n" : "\nTODO BIEN\n", fallas);
    return fallas ? 1 : 0;
}
