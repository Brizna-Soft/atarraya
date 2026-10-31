#pragma once

// Conexión con los navegadores («native messaging»): registra el puente para Chrome, Edge, Brave y
// Firefox, solo para esta cuenta de Windows (HKEY_CURRENT_USER) y solo cuando la persona lo pide.

#include <string>
#include <vector>

#include "canal.hpp"

namespace hnd::navegadores
{
    inline const wchar_t* NombreHost = ATARRAYA_HOST;   // la copia de prueba usa otro (ver canal.hpp)
    inline const char* IdChrome = "mnppdeogjpkdgkbmknkaddjcakoofgaf";   // sale de la clave pública de manifest.json
    inline const char* IdFirefox = "atarraya@briznasoft";

    struct Navegador
    {
        std::wstring nombre;
        bool instalado = false;
        bool conectado = false;
    };

    std::vector<Navegador> Estado();
    bool Conectar(std::wstring& error);   // solo en los navegadores instalados
    void Desconectar();
    std::wstring CarpetaExtension();      // «extension» junto al programa
}
