#pragma once

// Ayudas de Windows para la ventana: abrir, mostrar en la carpeta, Papelera, portapapeles y la marca
// «descargado de internet» que usa Windows para avisar antes de abrir programas bajados.

#include <functional>
#include <string>

namespace hnd::sistema
{
    bool Abrir(const std::wstring& ruta);
    void MostrarEnCarpeta(const std::wstring& ruta);
    bool MandarAPapelera(const std::wstring& ruta);
    bool EsEjecutable(const std::wstring& nombre);
    void CopiarTexto(const std::wstring& texto);
    std::wstring TextoDelPortapapeles();
    bool EsDireccion(const std::wstring& texto);   // http, https o ftp

    // Marca el archivo como bajado de internet (Zone.Identifier) y deja que el antivirus lo revise.
    // Corre en otro hilo; «alTerminar(existe)» se llama en ese hilo cuando termina.
    void MarcarComoDescargado(const std::wstring& ruta, const std::string& url, const std::string& referer,
                              std::function<void(bool existe)> alTerminar);
}
