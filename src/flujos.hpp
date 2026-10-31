#pragma once

// Videos por partes: listas HLS (.m3u8) y manifiestos DASH (.mpd). Lee la lista, dice qué calidades trae, si está
// protegida (DRM o cifrada: entonces no se baja), si es en vivo, y arma la lista de partes que hay que bajar y juntar
// en orden. Atarraya no trae FFmpeg: lo de HLS se junta en un .ts (o un .mp4 si es CMAF) y en DASH el video y el audio
// quedan en dos archivos sin unir.
//
// No usa wxWidgets ni curl (lo usan el motor, la ventana y las pruebas).

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace hnd::flujos
{
    // Topes, para que una lista mal hecha (o hecha para molestar) no llene la memoria
    constexpr size_t MaximoPartes = 150000;                  // partes de un video, sumando todas sus listas o representaciones
    constexpr size_t MaximoLista = 16u * 1024 * 1024;        // bytes de una lista HLS o de un manifiesto DASH
    constexpr size_t MaximoEnMemoria = 128u * 1024 * 1024;   // bytes de las partes adelantadas que esperan su turno
    constexpr size_t MaximoListasJuntas = 40;                // listas de audio y subtítulos que se revisan con una calidad
    // Bytes que ocupan las direcciones de las partes de un video (todas sus listas, o las representaciones que se bajan).
    // Pocas partes con direcciones muy largas (o con una base muy larga) también llenan la memoria.
    constexpr size_t MaximoBytesDirecciones = 32u * 1024 * 1024;

    // Textos para la persona
    extern const char* const TextoProtegido;     // DRM o cifrado
    extern const char* const TextoEnVivo;
    extern const char* const TextoVariasPartes;  // DASH con más de un período
    extern const char* const TextoRedLocal;      // la lista pide partes de esta PC o de la red de la casa
    extern const char* const TextoDemasiadas;    // más partes o más bytes que los topes: no se baja (nunca a medias)

    // Una dirección de la lista, completa: relativa a la de la lista (ya con sus redirecciones), como un navegador
    std::string Resolver(const std::string& base, const std::string& relativa);

    // Una parte para bajar: su dirección y, si es un trozo de un archivo más grande, desde dónde y cuánto
    struct Trozo
    {
        std::string url;
        int64_t desde = -1;   // -1: el archivo entero
        int64_t largo = -1;
    };

    // ---- HLS ----
    struct Variante
    {
        std::string url;
        int64_t banda = 0;          // bits por segundo (BANDWIDTH)
        int ancho = 0, alto = 0;    // RESOLUTION
        std::string codecs;
        std::string grupoAudio;     // AUDIO="...": el audio va aparte, en ese grupo
        std::string grupoSubtitulos;   // SUBTITLES="...": sus subtítulos (Atarraya no los baja, pero los revisa)
    };
    struct AudioAparte
    {
        std::string url, grupo, nombre, idioma;
        bool porDefecto = false;
    };
    struct Subtitulos
    {
        std::string url, grupo;
    };
    struct ListaHls
    {
        bool valida = false;        // empieza con #EXTM3U
        bool maestra = false;       // trae calidades (EXT-X-STREAM-INF), no partes
        bool cifrada = false;       // EXT-X-KEY o EXT-X-SESSION-KEY, con cualquier método
        bool enVivo = false;        // sin EXT-X-ENDLIST ni EXT-X-PLAYLIST-TYPE:VOD
        bool demasiadas = false;    // pasa de MaximoPartes partes, de MaximoLista bytes o de MaximoBytesDirecciones: no se baja (sin partes)
        size_t bytes = 0;           // lo que ocupan sus direcciones (para el tope de MaximoBytesDirecciones por video)
        std::vector<Variante> variantes;
        std::vector<AudioAparte> audios;
        std::vector<Subtitulos> subtitulos;
        Trozo inicio;               // EXT-X-MAP (CMAF/fMP4); url vacía si no hay
        std::vector<Trozo> trozos;
        double duracion = 0;        // segundos
    };
    ListaHls LeerHls(const std::string& texto, const std::string& url);
    int MejorVariante(const ListaHls& l);                  // la de más bits por segundo (o más resolución); -1 si no hay
    int AudioDeVariante(const ListaHls& l, int variante);  // su audio aparte (el que es por defecto); -1 si va dentro
    // Las otras listas que se usan con una calidad de la maestra: su audio aparte y sus subtítulos. Todas se leen y se
    // revisan antes de bajar una sola parte (si alguna está cifrada, el video no se baja).
    std::vector<std::string> ListasJuntas(const ListaHls& maestra, int variante);

    // ---- DASH ----
    // Las direcciones de las partes NO se arman al leer el manifiesto (con muchas partes y direcciones largas no
    // cabrían en memoria): cada representación guarda cómo armarlas, y se arman solo las de la que se baja (ArmarTrozos)
    struct Tramo
    {
        int64_t t = 0;
        bool conT = false;          // trae su propio «t»
        int64_t d = 0, repetir = 0; // duración de cada parte y cuántas veces más se repite
    };
    struct Receta
    {
        enum class Tipo { Ninguna, Numeros, LineaDeTiempo, Lista } tipo = Tipo::Ninguna;
        std::string base, media;    // la BaseURL de la representación y la plantilla de sus partes
        int64_t numero = 1;         // el primer $Number$
        int64_t duracion = 0;       // Numeros: la duración de cada parte
        std::vector<Tramo> tramos;  // LineaDeTiempo
        std::vector<std::pair<std::string, std::string>> lista;   // Lista (SegmentList): media y mediaRange, sin resolver
    };
    struct Representacion
    {
        std::string id;
        bool video = false, audio = false;
        int64_t banda = 0;
        int ancho = 0, alto = 0;
        std::string mime, codecs;
        Trozo inicio;               // segmento de inicio (url vacía si no hay)
        std::vector<Trozo> trozos;  // vacío hasta ArmarTrozos (un archivo único ya trae el suyo)
        size_t partes = 0;          // cuántas partes tiene, aunque todavía no estén armadas
        bool unArchivo = false;     // SegmentBase o solo BaseURL: todo es un archivo (trozos tiene ese)
        Receta receta;
    };
    struct ManifiestoDash
    {
        bool valido = false;
        bool protegido = false;     // ContentProtection en cualquier parte
        bool enVivo = false;        // type="dynamic"
        bool variasPartes = false;  // más de un Period
        bool demasiadas = false;    // más de MaximoPartes partes (sumando todas las representaciones) o más de MaximoLista bytes
        double duracion = 0;
        std::vector<Representacion> videos, audios;
    };
    ManifiestoDash LeerDash(const std::string& xml, const std::string& url);
    int MejorRepresentacion(const std::vector<Representacion>& r);   // -1 si no hay
    int BuscarRepresentacion(const std::vector<Representacion>& r, const std::string& id);   // -1 si no está
    // Arma las direcciones de las partes de una representación (en r.trozos), de una en una, y suma lo que ocupan a
    // «bytes» (el inicio también cuenta). Falso si «bytes» pasa de MaximoBytesDirecciones: se deja de armar enseguida,
    // r.trozos queda vacío y el video no se baja (TextoDemasiadas).
    bool ArmarTrozos(Representacion& r, size_t& bytes);

    // ¿Las partes se pueden bajar? Solo http y https, nada de YouTube ni de sitios con DRM y, con «soloAfuera» (la
    // lista es de internet), nada de esta PC ni de la red de la casa. El problema en español, o vacío si se pueden.
    std::string RevisarTrozos(const Trozo& inicio, const std::vector<Trozo>& trozos, bool soloAfuera = false);

    // Para la persona: «1080p · 5.0 Mb/s», «Audio · 128 kb/s»
    std::wstring TextoCalidad(int ancho, int alto, int64_t banda, bool soloAudio);
    // La extensión del archivo final: «.ts» en HLS clásico, «.mp4» en CMAF/fMP4; en DASH según su tipo
    std::wstring ExtensionHls(const ListaHls& l);
    std::wstring ExtensionDash(const Representacion& r);

    // Junta las partes en el archivo, en orden aunque lleguen desordenadas. La que toca se escribe apenas llega cada
    // pedazo (así una parte grande no se queda en memoria); las que llegan antes de su turno esperan en memoria, con un
    // tope de bytes: si no caben, se descartan y se vuelven a bajar cuando les toque. No es seguro entre hilos: quien lo
    // usa lo protege.
    class Ensamblador
    {
    public:
        // «escribir(posición, datos, n)» escribe en el archivo; false si falló el disco
        using Escribir = std::function<bool(int64_t, const char*, size_t)>;
        Ensamblador(size_t total, size_t yaEscritas, int64_t bytesYaEscritos, Escribir escribir, size_t maximoEnMemoria = MaximoEnMemoria);

        enum class Paso { Bien, Lleno, Disco };
        // Lleno: una parte adelantada ya no cabe en memoria; se descartó lo suyo y hay que bajarla otra vez cuando sea
        // su turno (Siguiente() == parte). Disco: falló la escritura.
        Paso Pedazo(size_t parte, const char* datos, size_t n);
        bool Terminar(size_t parte);                              // false si falló el disco
        void Descartar(size_t parte);   // falló a mitad: se olvida lo que tenía (lo escrito se pisa al reintentar)

        size_t Siguiente() const { return m_siguiente; }          // la primera que falta escribir entera
        int64_t BytesEscritos() const { return m_bytes; }         // de las partes enteras
        int64_t BytesDeLaActual() const { return m_actual; }      // de la que toca, escritos a medias
        bool Completo() const { return m_siguiente >= m_total; }
        size_t EnMemoria() const;

    private:
        bool Vaciar(size_t parte);   // escribe lo guardado de la que toca

        size_t m_total, m_siguiente;
        int64_t m_bytes, m_actual = 0;
        size_t m_maximo, m_enMemoria = 0;
        std::map<size_t, std::string> m_esperando;
        std::set<size_t> m_terminadas;
        Escribir m_escribir;
    };
}
