#pragma once

// Nombres de archivo: del servidor (Content-Disposition), de la dirección, limpios y sin repetirse.

#include <set>
#include <string>

namespace hnd
{
    // filename*=UTF-8''... o filename="..." (vacío si no trae nombre)
    std::wstring NombreDeContentDisposition(const std::string& valor);
    // Último trozo de la ruta de la dirección, decodificado
    std::wstring NombreDeUrl(const std::string& url);
    // Quita caracteres no permitidos en Windows, nombres reservados y largos excesivos
    std::wstring LimpiarNombre(const std::wstring& nombre);
    // Si el nombre no tiene extensión y el tipo MIME es conocido, le agrega una
    std::wstring AgregarExtension(const std::wstring& nombre, const std::string& tipoMime);
    // «archivo.zip» -> «archivo (1).zip» si ya existe en la carpeta o está ocupado por otra descarga
    std::wstring NombreUnico(const std::wstring& carpeta, const std::wstring& nombre,
                             const std::set<std::wstring>& ocupados);
    std::wstring Minusculas(std::wstring s);

    // ¿Este nombre puede ser el de una página web? Sin extensión, o .html, .htm, .php, .asp... (también una
    // «extensión» rara, larga o con símbolos, que no es de un archivo)
    bool EsNombreDePagina(const std::wstring& nombre);
    // Se pidió un archivo y el servidor respondió con una página web: llega como página (text/html o
    // application/xhtml+xml), no como adjunto, y el nombre que se esperaba no es de página (.rar, .zip, .7z, .exe,
    // .iso, un video...). Pasa, por ejemplo, con la página de un sitio de descargas que pide un captcha.
    bool RespondioPagina(const std::string& tipoMime, bool adjunto, const std::wstring& nombreEsperado);
}
