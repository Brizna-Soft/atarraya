#pragma once

// Ayudas de Windows para la ventana: abrir, mostrar en la carpeta, Papelera, portapapeles y la marca
// «descargado de internet» que usa Windows para avisar antes de abrir programas bajados.

#include <functional>
#include <string>
#include <vector>

namespace hnd::sistema
{
    bool Abrir(const std::wstring& ruta);
    void MostrarEnCarpeta(const std::wstring& ruta);
    bool MandarAPapelera(const std::wstring& ruta);
    bool EsEjecutable(const std::wstring& nombre);
    void CopiarTexto(const std::wstring& texto);
    std::wstring TextoDelPortapapeles();           // solo la primera línea
    std::wstring TodoElPortapapeles(size_t maximo = 4 * 1024 * 1024);
    bool EsDireccion(const std::wstring& texto);   // http, https o ftp
    bool EsMagnet(const std::wstring& texto);      // magnet:?xt=...

    // Marca el archivo como bajado de internet (Zone.Identifier) y deja que el antivirus lo revise.
    // Corre en otro hilo; «alTerminar(existe)» se llama en ese hilo cuando termina.
    void MarcarComoDescargado(const std::wstring& ruta, const std::string& url, const std::string& referer,
                              std::function<void(bool existe)> alTerminar);
    // Lo mismo para varios archivos (los de un torrent), uno tras otro en un solo hilo.
    // «alTerminar(faltan)» recibe cuántos ya no estaban (los quitó el antivirus).
    void MarcarVariosComoDescargados(std::vector<std::wstring> rutas, std::function<void(size_t faltan)> alTerminar);

    // Lee un archivo de texto (UTF-8, UTF-16 o la página de códigos de Windows). Falso si es muy grande o no se lee.
    bool LeerTexto(const std::wstring& ruta, std::wstring& texto, size_t maximo = 16 * 1024 * 1024);
}
