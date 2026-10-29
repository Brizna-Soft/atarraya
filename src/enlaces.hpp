#pragma once

// Enlaces: sacar direcciones de un texto (listas), de una página web y de Internet Archive.
// No usa wxWidgets (lo prueba también pruebas\prueba-enlaces.cpp).

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace hnd::enlaces
{
    enum class Tipo { Video, Audio, Imagen, Documento, Comprimido, Programa, Disco, Torrent, Subtitulos, Otro, Pagina };

    struct Enlace
    {
        std::string url;
        std::wstring nombre;     // para mostrar (en Internet Archive puede llevar carpetas)
        Tipo tipo = Tipo::Otro;
        int64_t tamano = -1;     // -1 = no se sabe
        std::wstring formato;    // el que da Internet Archive (por ejemplo «h.264»)
        bool interno = false;    // archivo interno del sitio (metadatos de Internet Archive)
    };

    struct Pagina
    {
        std::wstring titulo;
        std::vector<Enlace> enlaces;
        bool internetArchive = false;
    };

    struct ResultadoTexto
    {
        std::vector<std::string> enlaces;   // http, https, ftp y magnet, sin repetir, en el orden del texto
        int repetidos = 0;
        int noPermitidos = 0;               // de sitios que no permiten bajar sus videos (YouTube)
    };

    // Busca las direcciones de un texto: una por línea, o mezcladas con otras palabras
    ResultadoTexto DeTexto(const std::wstring& texto);

    // Enlaces de una página: <a>, <video>, <audio>, <source>, <img>, og:video y direcciones de archivos en scripts
    std::vector<Enlace> DeHtml(const std::string& html, const std::string& urlBase, std::wstring* titulo = nullptr);

    // archive.org/details/<id> o archive.org/download/<id> (sin un archivo en particular)
    bool EsInternetArchive(const std::string& url, std::string& id);
    // Lista oficial de archivos (respuesta de https://archive.org/metadata/<id>)
    bool DeInternetArchive(const std::string& json, const std::string& id, Pagina& p, std::string& error);

    // Baja la página (o la lista de Internet Archive) y saca sus enlaces. Corre en un hilo aparte.
    bool Buscar(const std::string& url, Pagina& p, std::string& error, const std::atomic<bool>* parar);

    Tipo TipoDeNombre(const std::wstring& nombre);
    const wchar_t* TextoTipo(Tipo t);
    bool EsMagnet(const std::string& texto);
    std::string Sitio(const std::string& url);    // en minúsculas, sin usuario ni puerto
    // Sitios que no permiten bajar sus videos: Atarraya no los baja (YouTube)
    bool NoPermitido(const std::string& url);
    extern const wchar_t* TextoNoPermitido;
}
