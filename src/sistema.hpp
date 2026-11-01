#pragma once

// Ayudas de Windows para la ventana: abrir, mostrar en la carpeta, Papelera, portapapeles y la marca
// «descargado de internet» que usa Windows para avisar antes de abrir programas bajados.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hnd::sistema
{
    bool Abrir(const std::wstring& ruta);
    // Abre una página web (solo http o https) en el navegador. En la copia de prueba solo la anota en
    // «paginas-abiertas.txt», sin abrir ningún navegador.
    void AbrirPaginaWeb(const std::string& url);
    // Le pide a la extensión del navegador, por el «puerto» que deja abierto mientras Atarraya está abierto, que abra
    // la página en una pestaña nueva (solo en esas lee los avisos de espera de «de una en una»). Si hay varios
    // navegadores conectados, va primero el predeterminado. La extensión contesta «abierta» con el mismo «id». Falso si
    // ninguna está conectada o no recibió la orden. En la copia de prueba, solo los puertos del puente de prueba.
    bool PedirALaExtension(int64_t id, const std::string& url);
    void MostrarEnCarpeta(const std::wstring& ruta);
    // El sonido de aviso de Windows (por ejemplo, «Toca resolver un captcha»). La copia de prueba no suena: lo anota.
    void Sonar();
    // En la copia de prueba no usa la Papelera de la persona: mueve el archivo a «papelera» dentro de su carpeta de datos
    bool MandarAPapelera(const std::wstring& ruta);
    bool EsEjecutable(const std::wstring& nombre);
    void CopiarTexto(const std::wstring& texto);
    std::wstring TextoDelPortapapeles();           // solo la primera línea
    std::wstring TodoElPortapapeles(size_t maximo = 4 * 1024 * 1024);
    // El programa que copió pidió que nadie lo lea (gestores de contraseñas, por ejemplo)
    bool PortapapelesPrivado();
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
