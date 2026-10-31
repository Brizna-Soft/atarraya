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

    class DeUnaEnUna
    {
    public:
        explicit DeUnaEnUna(std::wstring archivo);

        // La cola se guarda en cada cambio: si se cierra Atarraya, sigue donde iba
        void Cargar();
        bool Guardar() const;

        void Agregar(std::vector<Parte> partes);   // al final de la cola
        // Pasa a «abierta» la siguiente que espera y devuelve su posición (-1 si no queda ninguna)
        int AbrirSiguiente();
        // ¿Esta descarga que pasó el navegador es de una parte abierta? Primero por el nombre del archivo; si no
        // coincide con ninguna, la primera parte abierta del mismo sitio. -1 si no es de ninguna.
        int Emparejar(const std::string& url, const std::string& referer, const std::wstring& nombre) const;
        void Tomar(int i, uint64_t descarga);
        // Terminó (o se detuvo) una descarga: si era de una parte, la marca y devuelve su posición; si no, -1
        int Terminar(uint64_t descarga, bool ok);
        // La persona la saltó, o quitó su descarga de la lista
        void Saltar(int i);
        // Para la cola: devuelve las que no bajaron (esperando, abiertas, saltadas o con error) y la vacía. Las que se
        // están bajando siguen en la lista de descargas como cualquier otra.
        std::vector<Parte> Parar();

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

    // Para emparejar: el nombre en minúsculas, sin «.html» al final (como en Rapidgator) y solo con letras y números
    std::wstring NombreComparable(const std::wstring& nombre);
    // ¿Las dos direcciones son del mismo sitio? Cuentan sus servidores (s12.ddownload.com) y sus otros dominios (ddl.to)
    bool MismoSitio(const std::string& a, const std::string& b);
}
