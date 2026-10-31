#pragma once

// Torrents y enlaces magnet con libtorrent.
//
// Todo se llama desde el hilo de la ventana (libtorrent trabaja en sus propios hilos). Mientras ningún torrent
// está activo, la sesión no escucha conexiones, ni usa la DHT: el programa solo se conecta cuando la persona
// baja o comparte algo. No se abren puertos en el router (sin UPnP ni NAT-PMP) y no se anuncia nada en la red
// de la casa (sin LSD).
//
// Sin libtorrent (compilado con -SinTorrents), Disponible() es falso y todo lo demás no hace nada.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "motor.hpp"

namespace hnd
{
    struct ArchivoTorrent
    {
        std::wstring ruta;     // dentro del torrent, con «\»
        int64_t tamano = 0;
        int64_t hecho = 0;
        bool bajar = true;
    };

    // Lo que se sabe de un torrent: para la ventana «Nuevo torrent» y para «Elegir archivos»
    struct InfoTorrent
    {
        std::wstring nombre;
        int64_t tamano = -1;                  // -1 si todavía no se sabe (magnet sin datos)
        std::vector<ArchivoTorrent> archivos; // vacío si todavía no se sabe
        bool tieneDatos = false;
        std::string magnet;
        std::string huella;                   // para saber si ya está en la lista
    };

    struct NuevoTorrent
    {
        std::string datos;       // contenido del archivo .torrent (vacío si es un magnet)
        std::string magnet;      // enlace magnet (vacío si es un archivo .torrent)
        std::wstring carpeta;
        std::vector<bool> bajar; // uno por archivo; vacío = todos
        bool elegirDespues = false; // magnet: preguntar qué bajar cuando lleguen los datos
        bool compartir = false;     // seguir compartiendo al terminar
        bool empezar = true;        // si no, queda en pausa («Más tarde»)
    };

    class MotorTorrent
    {
    public:
        explicit MotorTorrent(std::wstring carpetaDatos);
        ~MotorTorrent();
        MotorTorrent(const MotorTorrent&) = delete;
        MotorTorrent& operator=(const MotorTorrent&) = delete;

        static bool Disponible();
        static std::string Version();   // de libtorrent

        // Leer un .torrent o un magnet antes de agregarlo. Devuelven false y un texto en español si no sirve.
        static bool LeerTorrent(const std::string& datos, InfoTorrent& info, std::string& error);
        static bool LeerMagnet(const std::string& magnet, InfoTorrent& info, std::string& error);

        void Cargar();
        uint64_t Agregar(const NuevoTorrent& n, std::string& error);   // 0 si no se pudo
        uint64_t BuscarPorHuella(const std::string& huella);           // 0 si no está

        void Reanudar(uint64_t id);
        void Pausar(uint64_t id);
        void Quitar(uint64_t id, bool aLaPapelera);   // nunca borra para siempre: a la Papelera o nada
        void PausarTodas();
        void ReanudarTodas();
        void LimpiarTerminadas();

        bool Archivos(uint64_t id, InfoTorrent& info);                  // con lo bajado de cada archivo
        bool ElegirArchivos(uint64_t id, const std::vector<bool>& bajar);
        // Archivos que ya existen en la carpeta con el mismo nombre (si se bajara ahí, se dañarían)
        static std::vector<std::wstring> Choques(const InfoTorrent& info, const std::wstring& carpeta, const std::vector<bool>& bajar);
        std::vector<std::wstring> Choques(uint64_t id, const std::vector<bool>& bajar);
        // Otra carpeta para un torrent que todavía no bajó nada (espera que la persona elija los archivos)
        bool CambiarCarpeta(uint64_t id, const std::wstring& carpeta);
        void Compartir(uint64_t id, bool si);

        std::vector<Foto> Fotos();
        bool Buscar(uint64_t id, Foto& foto);
        std::wstring RutaFinal(uint64_t id);    // el archivo (torrent de uno) o su carpeta
        bool UnSoloArchivo(uint64_t id);
        bool HayActivas();
        // Torrents cuyos datos acaban de llegar y esperan que la persona elija los archivos (cada uno, una vez)
        std::vector<uint64_t> PorElegir();
        // Avisos para la persona (por ejemplo, archivos que no se pudieron mandar a la Papelera)
        std::vector<std::wstring> Avisos();

        // Lee los avisos de libtorrent (para que la cola compartida vea los estados al día)
        void Recoger();
        // Llamar cada medio segundo: avisos de libtorrent, cola, velocidades y guardar
        void Tick();
        // Al cerrar: guarda el estado de cada torrent y cierra la sesión
        void Detener();

        int maxSimultaneas = 3;      // torrents bajando a la vez (los que comparten no cuentan)
        bool conPermisos = false;    // si es verdadero, solo arrancan los de «permitidos» (los reparte la ventana)
        std::vector<uint64_t> permitidos;
        int64_t limiteBajada = 0;    // bytes por segundo; 0 = sin límite
        int64_t limiteSubida = 0;
        // Cuando un torrent termina: su foto y sus archivos (para marcarlos como bajados de internet)
        std::function<void(const Foto&, const std::vector<std::wstring>&)> alTerminar;

        struct Datos;

    private:
        std::unique_ptr<Datos> m;
    };
}
