#pragma once

// Recolector de enlaces, como el de JDownloader. Aquí esperan los enlaces que llegan del portapapeles, de una lista
// pegada o de Click'n'Load. Atarraya consulta el nombre y el tamaño de cada uno sin bajarlo (una consulta HEAD) y los
// agrupa en paquetes. Nada se baja hasta que la persona lo confirma.
//
// No usa wxWidgets. Las consultas corren en hilos aparte; la ventana llama a Tick() cada medio segundo.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hnd
{
    enum class Comprobacion
    {
        Pendiente,    // todavía no se consulta
        Comprobando,
        Archivo,      // un archivo que se puede bajar
        Pagina,       // una página web: se abre en el navegador
        Sitio,        // página de un sitio de descargas (Rapidgator, 1fichier, Mega...): se abre en el navegador
        NoExiste,     // el servidor dice que ya no está (404 o 410)
        Desconocido,  // no se pudo saber sin bajarlo
        Torrent,      // enlace magnet
        Local,        // dirección de esta PC o de la red de la casa: no se consulta sola
        NoPermitido,  // lleva a YouTube: no se baja
        SinRevisar,   // Windows no dijo cuáles son las redes de la PC: no se consultó; se sabrá al bajar
    };

    struct EnlaceRecolectado
    {
        uint64_t id = 0;
        std::string url, referer;
        std::wstring nombre;
        int64_t tamano = -1;
        Comprobacion estado = Comprobacion::Pendiente;
        std::string detalle;      // en español
        std::wstring paquete;     // lo da Click'n'Load; vacío = se agrupa por el nombre del archivo
        std::string origen;       // "portapapeles", "lista" o "clicknload"
        bool marcado = true;
        int64_t agregado = 0;
    };

    class Recolector
    {
    public:
        explicit Recolector(std::wstring archivo);
        ~Recolector();
        Recolector(const Recolector&) = delete;
        Recolector& operator=(const Recolector&) = delete;

        void Cargar();
        void Guardar();

        // Agrega sin repetir. Devuelve cuántos entraron (hay un tope para que nadie lo llene sin fin).
        // «comoSitio»: vuelven como páginas de sitios de descargas (las que no bajaron en «de una en una»).
        size_t Agregar(const std::vector<std::string>& urls, const std::wstring& paquete, const std::string& referer,
                       const std::string& origen, bool comoSitio = false);
        std::vector<EnlaceRecolectado> Lista();
        void Marcar(const std::vector<uint64_t>& ids, bool si);
        void Quitar(const std::vector<uint64_t>& ids);
        size_t Cantidad();
        bool Contiene(const std::string& url);
        uint64_t Version() const { return m_version; }   // cambia cuando algo cambia

        // Arranca las consultas pendientes (como mucho 4 a la vez) y guarda si hace falta
        void Tick();
        // Al cerrar: corta las consultas y guarda
        void Detener();

        static constexpr size_t Maximo = 5000;

    private:
        void Consultar(std::shared_ptr<std::atomic<bool>> fin);

        struct Hilo
        {
            std::thread t;
            std::shared_ptr<std::atomic<bool>> fin;
        };

        std::wstring m_archivo;
        std::mutex m_m;
        std::vector<EnlaceRecolectado> m_lista;   // protegida por m_m
        uint64_t m_siguiente = 1;
        std::atomic<uint64_t> m_version{ 1 };
        uint64_t m_guardada = 0;
        std::atomic<bool> m_parar{ false };
        std::vector<Hilo> m_hilos;
    };

    // El paquete de un enlace: el que trae, o uno sacado del nombre («peli.part1.rar» y «peli.part2.rar» → «peli»)
    std::wstring PaqueteDe(const EnlaceRecolectado& e);
    const wchar_t* TextoComprobacion(Comprobacion c);

    // Para el portapapeles: solo se recolectan enlaces a archivos, magnet y páginas de sitios de descargas
    bool EnlaceParaRecolectar(const std::string& url);
    bool EsSitioDeDescargas(const std::string& url);
    // localhost, direcciones privadas (192.168..., 10..., 172.16-31...), nombres sin punto o .local / .lan
    bool EsDireccionLocal(const std::string& url);
}
