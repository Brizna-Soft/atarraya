#pragma once

#include <string>

namespace hnd
{
    struct Ajustes
    {
        std::wstring carpeta;           // dónde se guardan las descargas (vacío = «Descargas»)
        int conexiones = 8;             // conexiones por archivo
        int simultaneas = 3;            // descargas a la vez
        int limiteKB = 0;               // KB/s en total; 0 = sin límite
        int limiteSubidaKB = 0;         // KB/s que los torrents suben a otras personas; 0 = sin límite
        bool compartirTorrents = false; // la última elección de «Seguir compartiendo cuando termine»
        bool elegirArchivosMagnet = true; // la última elección de «Elegir los archivos cuando lleguen los datos»
        bool vigilarPortapapeles = true; // los enlaces de archivos que se copian pasan al recolector
        bool clickNLoad = true;         // recibir enlaces de Click'n'Load en 127.0.0.1:9666 (van al recolector)
        bool carpetaPorPaquete = false; // en el recolector: una carpeta por paquete
        bool preguntarNavegador = true; // mostrar la ventana «Nueva descarga» para lo que llega del navegador
        bool bandejaAlCerrar = true;    // la X deja el programa junto al reloj
        bool avisarAlTerminar = true;
        bool avisoBandejaMostrado = false;

        // Ventana
        int x = -1, y = -1, ancho = 0, alto = 0;
        bool maximizada = false;
        int altoDetalles = 0;

        void Cargar();
        void Guardar() const;
        std::wstring CarpetaEfectiva() const;
    };
}
