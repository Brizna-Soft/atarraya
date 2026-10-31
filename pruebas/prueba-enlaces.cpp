// Prueba de los enlaces sin ventana (no se publica).
//
//   prueba-enlaces             pruebas fijas (sin internet)
//   prueba-enlaces <url>       baja esa página (o la lista de Internet Archive) y muestra sus enlaces

#include <windows.h>

#include <curl/curl.h>

#include <cstdio>
#include <string>

#include <bcrypt.h>

#include "clicknload.hpp"
#include "enlaces.hpp"
#include "recolector.hpp"
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
    printf(fallas ? "\n%d FALLAS\n" : "\nTODO BIEN\n", fallas);
    return fallas ? 1 : 0;
}
