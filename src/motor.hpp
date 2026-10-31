#pragma once

// Motor de descargas: varias conexiones por archivo, partes que se reparten solas cuando una conexión
// termina, reanudación después de cerrar el programa y reintentos cuando se cae la conexión.
// No usa wxWidgets: la ventana le pide «fotos» cada medio segundo.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hnd
{
    // «Compartiendo» solo lo usan los torrents: terminó y sigue subiendo a otras personas
    enum class Estado { EnCola, Iniciando, Descargando, Pausada, Completada, Error, Compartiendo };

    // Los torrents usan ids desde aquí; las descargas normales, desde 1
    constexpr uint64_t BaseIdTorrent = 1ULL << 48;
    inline bool EsIdTorrent(uint64_t id) { return id >= BaseIdTorrent; }

    struct Segmento
    {
        int64_t inicio = 0;
        int64_t fin = -1;     // byte final incluido; -1 = hasta que se acabe (tamaño desconocido)
        int64_t hecho = 0;    // bytes ya escritos desde «inicio»
        bool activo = false;  // una conexión lo está bajando (no se guarda)

        int64_t Falta() const { return fin < 0 ? INT64_MAX : (fin - inicio + 1) - hecho; }
    };

    struct DatosDescarga
    {
        uint64_t id = 0;
        std::string url;            // dirección original (UTF-8)
        std::string referer;
        std::string cookies;        // líneas en formato Netscape (vienen del navegador)
        std::string agente;         // User-Agent del navegador
        std::wstring carpeta;
        std::wstring nombre;
        bool nombreFijo = false;    // lo eligió la persona: el servidor no lo cambia
        int64_t tamano = -1;        // -1 = desconocido
        bool rangos = false;        // el servidor deja pedir partes (se puede reanudar)
        std::string etag, modificado, tipoMime;
        Estado estado = Estado::EnCola;
        std::string error;          // texto en español para la persona
        std::vector<Segmento> segmentos;
        int conexiones = 8;
        int64_t agregada = 0, terminada = 0;

        int64_t Hecho() const;
    };

    struct Foto
    {
        DatosDescarga datos;
        int64_t hecho = 0;
        double velocidad = 0;   // bytes por segundo
        int activas = 0;        // conexiones abiertas ahora (en un torrent: pares conectados)

        // Solo torrents
        bool torrent = false;
        double subida = 0;          // bytes por segundo
        int semillas = 0;           // pares que ya tienen todo
        int64_t subido = 0;         // total subido a otras personas
        bool compartir = false;     // seguir compartiendo al terminar
        bool eligeArchivos = false; // llegaron los datos y falta que la persona elija qué bajar
        bool tieneDatos = false;    // ya se conocen los archivos (un magnet empieza sin ellos)
        bool buscandoDatos = false; // magnet pidiendo los datos a otras personas
        bool revisando = false;     // revisando lo que ya está en el disco
        int archivos = 0;
        std::vector<uint8_t> mapa;  // lo bajado de cada tramo del torrent (0 a 255)
    };

    // Baja una página o un JSON pequeño a memoria (solo http y https). Devuelve false y un texto en español si
    // falla. «parar» permite cancelar.
    bool BajarTexto(const std::string& url, size_t maximo, std::string& cuerpo, std::string& urlFinal,
                    std::string& tipo, std::string& error, const std::atomic<bool>* parar = nullptr);

    // Lo que dice el servidor de un enlace sin bajarlo (una consulta HEAD: no llega ni un byte del archivo)
    struct InfoEnlace
    {
        long codigo = 0;          // 0 si no hubo respuesta
        bool pagina = false;      // es una página web, no un archivo
        std::wstring nombre;
        int64_t tamano = -1;
        std::string tipo, urlFinal;
        std::string error;        // en español, si no se pudo consultar
        bool local = false;       // el nombre llevaba a una dirección de esta PC o de la red de la casa: no se consultó
        bool sinRevisarRed = false; // Windows no dijo cuáles son las redes de la PC: no se consultó (se sabrá al bajar)
        bool noPermitido = false; // una redirección llevaba a YouTube
    };
    void ComprobarEnlace(const std::string& url, const std::string& referer, InfoEnlace& r, const std::atomic<bool>* parar = nullptr);

    class Motor
    {
    public:
        explicit Motor(std::wstring archivoEstado);
        ~Motor();
        Motor(const Motor&) = delete;
        Motor& operator=(const Motor&) = delete;

        void Cargar();
        void Guardar();

        uint64_t Agregar(DatosDescarga d, bool empezar);
        void Reanudar(uint64_t id);
        void Pausar(uint64_t id);
        void Quitar(uint64_t id);                 // la saca de la lista; no borra archivos
        bool CambiarEnlace(uint64_t id, const std::string& url);
        void PausarTodas();
        void ReanudarTodas();
        void LimpiarTerminadas();

        std::vector<Foto> Fotos();
        bool Buscar(uint64_t id, Foto& foto);
        std::wstring RutaFinal(uint64_t id);
        std::wstring RutaParcial(uint64_t id);
        bool HayActivas();
        // ¿Ya está en la lista esta dirección sin terminar? Devuelve su id o 0
        uint64_t BuscarPorUrl(const std::string& url);

        // Recoge las conexiones que terminaron (para que la cola compartida vea los estados al día)
        void Recoger();
        // Llamar cada medio segundo: arranca las de la cola, mide velocidades, avisa y guarda
        void Tick();
        // Al cerrar: pausa todo, espera a las conexiones y guarda
        void Detener();

        int maxSimultaneas = 3;
        // Con «conPermisos», solo arrancan las de «permitidas» (la ventana reparte los turnos entre descargas y torrents)
        bool conPermisos = false;
        std::vector<uint64_t> permitidas;
        std::atomic<int64_t> limiteBytesSeg{ 0 };   // 0 = sin límite
        std::function<void(const Foto&)> alTerminar; // se llama desde Tick (hilo de la ventana)

        struct Descarga;   // por dentro

    private:
        void Arrancar(const std::shared_ptr<Descarga>& x);
        void Ejecutar(std::shared_ptr<Descarga> x);
        void Trabajar(std::shared_ptr<Descarga> x);
        std::shared_ptr<Descarga> Encontrar(uint64_t id);

        std::wstring m_archivo;
        std::mutex m_lista;
        std::vector<std::shared_ptr<Descarga>> m_descargas;
        std::vector<std::shared_ptr<Descarga>> m_retiradas;  // quitadas que aún cierran conexiones
        uint64_t m_siguienteId = 1;
        std::atomic<bool> m_sucio{ false };
        int64_t m_ultimoGuardado = 0;

        struct Limitador;
        std::unique_ptr<Limitador> m_limitador;
    };

    // Para la ventana y las pruebas
    const wchar_t* TextoEstado(Estado e);

    // El error de una descarga cuyo sitio respondió con una página en vez del archivo (la ventana ofrece abrirla en
    // el navegador)
    extern const char* const TextoRespondioPagina;
}
