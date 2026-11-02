#pragma once

// «De una en una»: las páginas de los sitios de descargas (ddownload, Rapidgator, 1fichier...) se abren en el
// navegador de a una. La persona resuelve el captcha y pulsa descargar; la extensión le pasa la descarga a Atarraya,
// que la toma, y cuando termina se abre la página siguiente.
//
// Si el sitio no deja que otro programa siga la descarga (responde con una página, con 403 o 410, o el enlace es de un
// solo uso), la baja el navegador y la extensión la sigue hasta el final: así no se pierde el captcha que resolvió la
// persona. Si la página pide esperar («You have to wait 10 minutes», «Blocked»...), Atarraya muestra la cuenta regresiva
// y, cuando se cumple, abre la página otra vez y avisa.
//
// Atarraya no toca la página: no hace clics, no resuelve captchas, no se salta ni acorta esperas, no cambia de IP ni
// usa proxys para eso, ni usa cuentas premium. Solo abre la página y recibe lo que el navegador le pasa.
//
// No usa wxWidgets: la ventana principal la usa y la muestra en una barra.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "util.hpp"

namespace hnd
{
    class Recolector;
    enum class Estado;

    enum class EstadoParte
    {
        Esperando,   // todavía no se abrió
        Abierta,     // está en el navegador: falta que la persona pulse descargar
        Tomada,      // llegó su descarga y la está bajando Atarraya
        Lista,       // terminó de bajar
        Fallo,       // su descarga se detuvo con un error
        Saltada,     // la persona la saltó o quitó su descarga de la lista
        Espera,      // el sitio pidió esperar (límite entre descargas): se abre otra vez sola cuando se cumple
        Navegador,   // la está bajando el navegador y la extensión la sigue
    };

    // Las esperas: cuántas veces se abre otra vez una página que sigue pidiendo esperar, el margen que se suma a lo que
    // pide el sitio y cuánto se espera cuando el sitio no dice cuánto («Blocked»)
    constexpr int MaxIntentosEspera = 6;
    constexpr int64_t EsperaSinTiempo = 10 * 60;
    int64_t MargenDeEspera(int64_t segundos);   // 30 s, o el 5 % si es más; como mucho 5 minutos

    // Las páginas de la cola las abre la extensión, a pedido de Atarraya, por el «puerto» que deja abierto mientras
    // Atarraya está abierto (solo en esas pestañas lee los avisos de espera). Si hay varios navegadores conectados, va
    // primero el predeterminado. «navegadores»: el programa de cada puerto («chrome.exe»...); devuelve el orden.
    std::vector<size_t> OrdenDePuertos(const std::vector<std::wstring>& navegadores, const std::wstring& predeterminado);
    // El programa del navegador predeterminado según su «ProgId» de Windows («ChromeHTML» → «chrome.exe»); "" si no se sabe
    std::wstring ProgramaDeProgId(const std::wstring& progId);

    // La descarga que empezó el navegador y Atarraya prueba seguir: se está probando, la tomó Atarraya (la del navegador
    // se cancela) o sigue el navegador (la de Atarraya se quita). Nunca bajan las dos.
    enum class EstadoToma { Ninguna, Probando, Tomada, Navegador };

    struct Parte
    {
        std::string url;          // la página del sitio de descargas
        std::string referer;      // de dónde vino el enlace (por ejemplo, la página de Click'n'Load)
        std::wstring nombre;      // el nombre que se espera, sacado del enlace (puede faltar)
        std::wstring paquete;
        std::string grupo;        // el de las demás partes de su paquete que llegaron juntas (para Jícaro)
        std::wstring carpeta;     // dónde se guarda lo que baje
        EstadoParte estado = EstadoParte::Esperando;
        uint64_t descarga = 0;    // la descarga de Atarraya, cuando se tomó (o la que anota lo que bajó el navegador)
        // Esperas. En «Espera», cuándo se abre otra vez la página. En una «Abierta», el fin de la cuenta regresiva de la
        // página (solo se avisa: la página ya está abierta). 0 = ninguna.
        int64_t hasta = 0;
        bool limite = false;      // la espera es un límite del sitio (se abre otra vez), no la cuenta de la página
        int intentos = 0;         // veces que se abrió otra vez por una espera
        bool avisada = false;     // ya se avisó que terminó la cuenta de la página
        // La descarga del navegador que Atarraya prueba seguir o que sigue la extensión («toma»), quién la baja y desde
        // cuándo se prueba. Se guarda: así un cierre a la mitad no deja dos descargas del mismo archivo.
        std::string toma;
        EstadoToma estadoToma = EstadoToma::Ninguna;
        int64_t tomaDesde = 0;
        // Cómo va lo que baja el navegador (no se guarda)
        int64_t bajado = 0, total = -1;
        // Lo que bajó el navegador, anotado cuando avisó que terminó (ruta e identidad del archivo: volumen, número de
        // archivo de NTFS, tamaño y fecha). Se guarda: si Atarraya se cierra antes de pasarlo a la carpeta del paquete,
        // lo termina al volver a abrirse. «finalNavegador»: ya llegó el aviso final del navegador (terminó o falló).
        std::wstring rutaNavegador;
        IdentidadArchivo identidadNavegador;
        bool finalNavegador = false;
        // La tomó Atarraya, pero la extensión no pudo cancelar la del navegador ni saber cómo quedó: pueden estar bajando
        // las dos. La de Atarraya queda en pausa hasta que se sepa. Se guarda.
        bool sinConfirmar = false;
        // Ya se supo que la del navegador no sigue: Atarraya tiene que reanudar la suya, que había pausado. Se guarda y se
        // intenta hasta que de verdad sigue (si la pausa todavía no había terminado, reanudar no hace nada).
        bool reanudar = false;
    };

    // Lo que hay que hacer con la descarga de Atarraya de una parte «sin confirmar» (o que hay que reanudar), según cómo
    // está en la lista: pausarla (otra vez, por ejemplo después de un cierre de golpe), reanudarla, nada todavía, o ya
    // está («Listo»: no hay que seguir revisando)
    enum class AccionPausa { Nada, Pausar, Reanudar, Listo };

    // Pasar a la carpeta del paquete lo que bajó el navegador, sin mover nunca un archivo ajeno:
    //   - LeerArchivoDelNavegador: cuando el navegador avisa que terminó. Una ruta completa de esta PC, sin enlaces
    //     simbólicos ni puntos de unión en ninguna carpeta ni en el archivo, y un archivo común. Lee su identidad.
    //   - MoverArchivoDelNavegador: lo abre (mientras está abierto nadie más lo puede cambiar ni borrar), comprueba
    //     otra vez que no hay enlaces, que su ruta real es esa y que sigue siendo el mismo archivo (identidad). Abre
    //     también «carpeta» (tiene que ser la de verdad, sin enlaces) y la deja abierta sin que nadie la pueda renombrar
    //     ni cambiar por un enlace. Lo pasa adentro de esa carpeta abierta, con su nombre, por ese mismo archivo
    //     abierto y sin reemplazar nada. Si es otro disco, no lo mueve (sería copiar gigas): devuelve OtroDisco.
    enum class Movida { Movida, OtroDisco, NoCoincide, ConEnlaces, YaExiste, NoSePudo };
    bool LeerArchivoDelNavegador(const std::wstring& ruta, IdentidadArchivo& identidad, std::string& motivo);
    Movida MoverArchivoDelNavegador(const std::wstring& ruta, const IdentidadArchivo& identidad, const std::wstring& carpeta,
                                    std::wstring& destino, std::string& motivo);
#ifdef ATARRAYA_PRUEBAS
    namespace pruebas
    {
        // Solo en las pruebas: se llama justo antes de mover, con la carpeta y el archivo abiertos (para intentar
        // cambiarlos en ese momento)
        extern std::function<void()> AntesDeMover;
    }
#endif

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

        // La cola se guarda en cada cambio: si se cierra Atarraya, sigue donde iba. Al cargar, las saltadas o con
        // error que no llegaron al recolector siguen ahí hasta que lleguen.
        void Cargar();
        bool Guardar() const;

        // Al final de la cola, y la guarda. Si no se pudo guardar, la cola queda como estaba y devuelve false (así los
        // enlaces no salen del recolector).
        bool Agregar(std::vector<Parte> partes);
        // Pasa a «abierta» la siguiente que espera y devuelve su posición (-1 si no queda ninguna)
        int AbrirSiguiente();
        // ¿Esta descarga que pasó el navegador es de una parte? «referer» y «pagina» (la página de la pestaña donde
        // nació la descarga) dicen de dónde salió. Se toma sin preguntar solo si:
        //   - tiene el nombre de una parte abierta, viene de su sitio (su dominio, sus otros dominios o un servidor
        //     suyo) y no de otra página de ese sitio, o
        //   - viene de la página que Atarraya abrió para una parte y no tiene el nombre de otra parte de la cola.
        // Si viene del sitio de una parte pero no es seguro, se pregunta. Si no, no es de la cola.
        Emparejamiento Emparejar(const std::string& url, const std::string& referer, const std::string& pagina,
                                 const std::wstring& nombre) const;
        void Tomar(int i, uint64_t descarga);
        // Terminó (o se detuvo) una descarga: si era de una parte, la marca y devuelve su posición; si no, -1
        int Terminar(uint64_t descarga, bool ok);
        // La persona la saltó, o quitó su descarga de la lista
        void Saltar(int i);

        // La parte cuya página es esta (la que abrió Atarraya), si está abierta o esperando; -1 si no hay
        int ParteDePagina(const std::string& url) const;
        // La página de la parte pide esperar. Con «limite» (el sitio no deja bajar hasta entonces) la parte pasa a
        // «Espera» y se abre otra vez sola cuando se cumple el tiempo más un margen; si no (la cuenta regresiva de la
        // página), sigue abierta y solo se avisa al final. «segundos» < 0: el sitio no dijo cuánto. Devuelve false si
        // ya se abrió otra vez demasiadas veces: entonces la parte queda con error (vuelve al recolector al final).
        bool Esperar(int i, bool limite, int64_t segundos, int64_t ahora);
        // Las que terminaron de esperar: vuelven a «Abierta» (un intento más) para abrir su página otra vez
        std::vector<int> Vencidas(int64_t ahora);
        // La persona la abre ya («Abrir otra vez»), aunque falte para que termine la espera
        void AbrirOtraVez(int i);
        // Las cuentas regresivas de páginas abiertas que terminaron y todavía no se avisaron (quedan avisadas)
        std::vector<int> CuentasTerminadas(int64_t ahora);
        // Bajando en el navegador, que la extensión sigue con «toma»
        void EnNavegador(int i, const std::string& toma);
        int BuscarToma(const std::string& toma) const;   // una que baja el navegador (o que se cortó ahí)
        int ParteDeToma(const std::string& toma) const;  // en cualquier estado
        // Atarraya prueba seguir con «descarga» la que empezó el navegador («toma»): mientras se decide, la parte queda
        // tomada y el navegador sigue con la suya
        void Probar(int i, uint64_t descarga, const std::string& toma, int64_t ahora);
        // Se decidió quién la baja. Con «atarraya», Atarraya (la extensión cancela la del navegador); si no, el navegador:
        // la parte pasa a «Navegador» y devuelve la descarga de Atarraya que hay que quitar
        uint64_t Decidir(int i, bool atarraya);
        // Al abrir Atarraya: las que estaban probando quedan para el navegador, que siguió bajando la suya. Devuelve las
        // descargas de Atarraya que hay que quitar.
        std::vector<uint64_t> ResolverTomasAlAbrir();
        // El navegador siguió bajando una que Atarraya ya había tomado (por ejemplo, no se pudo cancelar, o la extensión no
        // supo de Atarraya a tiempo): queda la del navegador. Devuelve la descarga de Atarraya que hay que quitar (0: ninguna).
        uint64_t NavegadorSiguio(int i);
        // La extensión no pudo cancelar la del navegador ni saber cómo quedó: la parte queda «sin confirmar» y devuelve la
        // descarga de Atarraya que hay que pausar hasta saberlo (0: no aplica). «Confirmar»: ya se sabe que la del
        // navegador no sigue; devuelve la de Atarraya que hay que reanudar (0: no aplica).
        uint64_t MarcarSinConfirmar(int i);
        uint64_t Confirmar(int i);
        // Cada vez que se revisa: «estado» y «pausando» son los de su descarga en la lista. «Listo»: ya no hay nada que
        // hacer (no cambia nada: la marca «reanudar» la borra RevisarPausas, después de guardar la descarga reanudada).
        AccionPausa RevisarPausa(int i, Estado estado, bool pausando) const;
        // Una vuelta de las pausas (la ventana la hace en cada vuelta y al abrir Atarraya), con lo justo de la lista de
        // descargas. El orden importa: la marca «reanudar» se borra recién después de guardar la lista de descargas con
        // la descarga ya reanudada. Si Atarraya se cierra de golpe entre los dos pasos (o no se pudo guardar), la marca
        // sigue y al abrirlo se revisa otra vez: la parte nunca queda pausada. Devuelve si hay que guardar la cola.
        struct ListaParaPausas
        {
            std::function<bool(uint64_t descarga, Estado& estado, bool& pausando)> buscar;
            std::function<void(uint64_t descarga)> pausar, reanudar;
            std::function<bool()> guardar;   // la lista de descargas, ya; falso si no se pudo
        };
        bool RevisarPausas(const ListaParaPausas& lista, std::vector<int>* repausadas = nullptr);
        // Lo que se le contesta a la extensión sobre una «toma»: quién la baja («probando», «tomada», «navegador»;
        // «terminada»: ya llegó y quedó anotado lo que bajó el navegador; «desconocida»: no es de ninguna parte), si
        // Atarraya todavía espera un aviso del navegador sobre ella (la extensión guarda su aviso final y lo repite
        // hasta que ya no) y si la parte quedó «sin confirmar»
        struct RespuestaToma
        {
            std::string estado = "desconocida";
            bool espera = false;
            bool sinConfirmar = false;
        };
        RespuestaToma EstadoDeToma(const std::string& toma) const;
        void Progreso(int i, int64_t bajado, int64_t total);
        // Lo que bajó el navegador terminó: lista (con la descarga que la anota en la lista de Atarraya) o con error
        void TerminarEnNavegador(int i, bool ok, uint64_t descarga);
        // El navegador avisó que terminó: se anota su archivo (ruta e identidad) antes de pasarlo a la carpeta del paquete
        void AnotarArchivoDelNavegador(int i, const std::wstring& ruta, const IdentidadArchivo& identidad);
        // Las que el navegador terminó y todavía no se pasaron a la carpeta del paquete (por ejemplo, si Atarraya se
        // cerró en el medio): se terminan al volver a abrirlo
        std::vector<int> PorMover() const;
        // Las partes de un grupo pasan a otro (dos paquetes que resultaron ser uno). true si cambió alguna.
        bool CambiarGrupo(const std::string& viejo, const std::string& nuevo);

        // Para devolver al recolector las que no bajaron sin perder ninguna, en este orden:
        //   1. GuardarEnRecolector(Sobrantes(), recolector): las pone en el recolector y lo guarda.
        //   2. Solo si eso salió bien, Vaciar(): saca todo de la cola y la guarda.
        // Cada archivo se escribe aparte y después reemplaza al anterior. Si Atarraya se cierra de golpe entre los
        // dos pasos, las páginas quedan en los dos lados (repetidas, pero ninguna se pierde).
        std::vector<Parte> Sobrantes() const;   // esperando, abiertas, saltadas o con error
        bool Vaciar();   // las que se están bajando siguen en la lista de descargas como cualquier otra

        // La que se muestra: la primera abierta; si no hay, la primera que baja el navegador, después la que baja
        // Atarraya y por último la que espera. -1 si no hay ninguna.
        int Actual() const;
        bool HayAbierta() const;
        bool HayEnEspera() const;
        bool QuedanPorAbrir() const;
        bool Terminada() const;      // tenía partes y ya no queda nada por abrir, por esperar ni por bajar
        size_t Cuantas(EstadoParte e) const;
        bool TieneDescarga(uint64_t descarga) const;   // ¿esa descarga es de una parte de la cola?
        const std::vector<Parte>& Partes() const { return m_partes; }

    private:
        std::wstring m_archivo;
        std::vector<Parte> m_partes;
    };

    // Paso 1 de devolver: pone las partes en el recolector (como páginas de sitios de descargas, con su paquete y su
    // página de origen) y lo guarda. false si alguna no entró (por ejemplo, recolector lleno) o no se pudo guardar.
    bool GuardarEnRecolector(const std::vector<Parte>& partes, Recolector& recolector);

    // Sitios donde la descarga final solo funciona en el navegador (Atarraya no pudo seguirla: el sitio respondió con
    // una página, con 403 o 410, o el enlace era de un solo uso). La próxima vez la deja en el navegador desde el
    // principio y la extensión la sigue: así no se pierde un captcha. Se guardan en un archivo, por sitio (ddl.to y
    // ddownload.com cuentan como uno).
    class SitiosNavegador
    {
    public:
        explicit SitiosNavegador(std::wstring archivo);
        void Cargar();
        bool Guardar() const;
        bool Necesita(const std::string& url) const;
        void Anotar(const std::string& url, int64_t ahora);   // y guarda
        size_t Cantidad() const { return m_sitios.size(); }

    private:
        std::wstring m_archivo;
        std::map<std::string, int64_t> m_sitios;   // sitio → cuándo se anotó
    };

    // ¿Atarraya pudo seguir la descarga que empezó el navegador? Se mira la suya: si ya llegan bytes del archivo, la
    // tomó; si el sitio respondió con una página, con un error (403, 410...) o el enlace ya no sirve, no; si en «plazo»
    // segundos todavía no llegó nada, tampoco. Mientras tanto el navegador sigue con la suya, así que no se pierde nada.
    enum class Toma { Probando, Tomada, Navegador };
    Toma DecidirToma(Estado estado, int64_t hecho, int64_t segundos, int64_t plazo = 25);

    // Para emparejar: el nombre en minúsculas, sin «.html» al final (como en Rapidgator) y solo con letras y números
    std::wstring NombreComparable(const std::wstring& nombre);
    // ¿Las dos direcciones son del mismo sitio? Cuentan sus servidores (s12.ddownload.com) y sus otros dominios (ddl.to)
    bool MismoSitio(const std::string& a, const std::string& b);
    // ¿Es la misma página? La misma dirección (sin «#...», sin «www.» y sin la «/» del final), o una que sigue a la
    // otra (ddownload.com/abc123 y ddownload.com/abc123/peli.rar) si lo que tienen en común lleva un código de 6 o
    // más letras o números. La portada del sitio sola no cuenta.
    bool MismaPagina(const std::string& a, const std::string& b);
}
