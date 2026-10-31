#pragma once

// «De una en una»: las páginas de los sitios de descargas (ddownload, Rapidgator, 1fichier...) se abren en el
// navegador de a una. La persona resuelve el captcha y pulsa descargar; la extensión le pasa la descarga a Atarraya,
// que la toma, y cuando termina se abre la página siguiente.
//
// Atarraya no toca la página: no hace clics, no resuelve captchas, no se salta esperas ni usa cuentas premium. Solo
// abre la página y recibe lo que el navegador le pasa.
//
// No usa wxWidgets: la ventana principal la usa y la muestra en una barra.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hnd
{
    class Recolector;

    enum class EstadoParte
    {
        Esperando,   // todavía no se abrió
        Abierta,     // está en el navegador: falta que la persona pulse descargar
        Tomada,      // llegó su descarga y la está bajando Atarraya
        Lista,       // terminó de bajar
        Fallo,       // su descarga se detuvo con un error
        Saltada,     // la persona la saltó o quitó su descarga de la lista
    };

    struct Parte
    {
        std::string url;          // la página del sitio de descargas
        std::string referer;      // de dónde vino el enlace (por ejemplo, la página de Click'n'Load)
        std::wstring nombre;      // el nombre que se espera, sacado del enlace (puede faltar)
        std::wstring paquete;
        std::wstring carpeta;     // dónde se guarda lo que baje
        EstadoParte estado = EstadoParte::Esperando;
        uint64_t descarga = 0;    // la descarga de Atarraya, cuando se tomó
    };

    // Qué es una descarga que pasó el navegador
    struct Emparejamiento
    {
        int parte = -1;         // la parte de la cola (-1: no es de la cola y sigue como cualquier descarga)
        bool segura = false;    // se toma sin preguntar; si no, se le pregunta a la persona
    };

    class DeUnaEnUna
    {
    public:
        explicit DeUnaEnUna(std::wstring archivo);

        // La cola se guarda en cada cambio: si se cierra Atarraya, sigue donde iba
        void Cargar();
        bool Guardar() const;

        // Al final de la cola, y la guarda. Si no se pudo guardar, la cola queda como estaba y devuelve false (así los
        // enlaces no salen del recolector).
        bool Agregar(std::vector<Parte> partes);
        // Pasa a «abierta» la siguiente que espera y devuelve su posición (-1 si no queda ninguna)
        int AbrirSiguiente();
        // ¿Esta descarga que pasó el navegador es de una parte? «referer» y «pagina» (la pestaña donde se pulsó
        // descargar) dicen de dónde salió. Se toma sin preguntar solo si:
        //   - tiene el nombre de una parte abierta y viene de su sitio (su dominio, sus otros dominios o un servidor
        //     suyo), o
        //   - viene de la página que Atarraya abrió para una parte y no tiene el nombre de otra parte de la cola.
        // Si viene del sitio de una parte pero no es seguro, se pregunta. Si no, no es de la cola.
        Emparejamiento Emparejar(const std::string& url, const std::string& referer, const std::string& pagina,
                                 const std::wstring& nombre) const;
        void Tomar(int i, uint64_t descarga);
        // Terminó (o se detuvo) una descarga: si era de una parte, la marca y devuelve su posición; si no, -1
        int Terminar(uint64_t descarga, bool ok);
        // La persona la saltó, o quitó su descarga de la lista
        void Saltar(int i);

        // Para devolver al recolector las que no bajaron sin perder ninguna, en este orden:
        //   1. GuardarEnRecolector(Sobrantes(), recolector): las pone en el recolector y lo guarda.
        //   2. Solo si eso salió bien, Vaciar(): saca todo de la cola y la guarda.
        // Cada archivo se escribe aparte y después reemplaza al anterior. Si Atarraya se cierra de golpe entre los
        // dos pasos, las páginas quedan en los dos lados (repetidas, pero ninguna se pierde).
        std::vector<Parte> Sobrantes() const;   // esperando, abiertas, saltadas o con error
        bool Vaciar();   // las que se están bajando siguen en la lista de descargas como cualquier otra

        // La que se muestra: la primera abierta; si no hay, la primera que se está bajando. -1 si no hay ninguna.
        int Actual() const;
        bool HayAbierta() const;
        bool QuedanPorAbrir() const;
        bool Terminada() const;      // tenía partes y ya no queda nada por abrir ni por bajar
        size_t Cuantas(EstadoParte e) const;
        const std::vector<Parte>& Partes() const { return m_partes; }

    private:
        std::wstring m_archivo;
        std::vector<Parte> m_partes;
    };

    // Paso 1 de devolver: pone las partes en el recolector (como páginas de sitios de descargas, con su paquete y su
    // página de origen) y lo guarda. false si alguna no entró (por ejemplo, recolector lleno) o no se pudo guardar.
    bool GuardarEnRecolector(const std::vector<Parte>& partes, Recolector& recolector);

    // Para emparejar: el nombre en minúsculas, sin «.html» al final (como en Rapidgator) y solo con letras y números
    std::wstring NombreComparable(const std::wstring& nombre);
    // ¿Las dos direcciones son del mismo sitio? Cuentan sus servidores (s12.ddownload.com) y sus otros dominios (ddl.to)
    bool MismoSitio(const std::string& a, const std::string& b);
    // ¿Es la misma página? La misma dirección (sin «#...», sin «www.» y sin la «/» del final), o una que sigue a la
    // otra (ddownload.com/abc123 y ddownload.com/abc123/peli.rar) si lo que tienen en común lleva un código de 6 o
    // más letras o números. La portada del sitio sola no cuenta.
    bool MismaPagina(const std::string& a, const std::string& b);
}
