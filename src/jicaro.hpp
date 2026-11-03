#pragma once

// Jícaro, el descompresor gratis de Brizna Soft. Cuando termina de bajar cada juego de un paquete (un comprimido
// entero, o todas las partes de uno en partes) o un .rar, .zip o .7z suelto, Atarraya le pide que lo descomprima:
//
//   Jicaro.exe --extraer "<primera parte>" --destino "<carpeta, al lado de las partes>" [--contrasena-stdin]
//
// Solo con archivos que bajó Atarraya y con cada ruta revisada y entre comillas: ningún texto del sitio pasa tal cual a
// la línea de comandos. La contraseña del paquete nunca va en la línea de comandos: con «--contrasena-stdin» (desde
// Jícaro 0.2.0) le llega por la entrada estándar, en UTF-8 y sin salto de línea al final. Las partes nunca se borran
// solas.
//
// No usa wxWidgets (lo usan también las pruebas).

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "deunaenuna.hpp"
#include "motor.hpp"
#include "util.hpp"

namespace hnd::jicaro
{
    // Donde queda Jícaro al instalarlo: %LOCALAPPDATA%\Programs\Jicaro\Jicaro.exe
    std::wstring RutaPredeterminada();
    // El Jicaro.exe que se usa: el que eligió la persona (si existe) o el de la ruta de siempre. Vacío si no está.
    std::wstring Buscar(const std::wstring& elegido);

    // Una ruta completa («C:\...» o «\\servidor\carpeta\...») que se puede pasar entre comillas sin que cambie su
    // sentido: sin comillas, sin caracteres de control ni comodines, sin «.» ni «..», sin «:» de más y sin «\», espacio
    // o punto al final.
    bool RutaSegura(const std::wstring& ruta);
    // Una ruta segura que termina en «\Jicaro.exe»
    bool EsRutaDeJicaro(const std::wstring& ruta);
    // "<exe>" --extraer "<archivo>" --destino "<carpeta>" [--contrasena-stdin]. Vacía si alguna ruta no es segura o el
    // programa no es Jícaro. La contraseña nunca va aquí.
    std::wstring ArmarOrden(const std::wstring& exe, const std::wstring& archivo, const std::wstring& destino,
                            bool conContrasena = false);

    // La versión de Jicaro.exe (la de su archivo). Falso si no se puede leer.
    struct Version
    {
        int mayor = 0, menor = 0, parche = 0;
    };
    bool LeerVersion(const std::wstring& exe, Version& v);
    // La contraseña por la entrada estándar («--contrasena-stdin») llega en Jícaro 0.2.0
    bool AceptaContrasena(const Version& v);

    // Un comprimido entre los archivos bajados, con sus partes en orden: la primera es la que lo abre
    struct Comprimido
    {
        std::vector<std::wstring> partes;   // rutas completas
        std::wstring nombre;                // sin la extensión ni el número de parte: «peli.part1.rar» → «peli»
        std::wstring Primera() const { return partes.empty() ? std::wstring() : partes.front(); }
    };
    // Agrupa los archivos en comprimidos: «peli.part1.rar», «peli.part2.rar»... van juntos, igual que «peli.rar» con
    // «peli.r00», «peli.r01»... o «peli.7z.001» con «peli.7z.002»... Cada .zip, .7z o .rar de un solo archivo es uno.
    // Lo demás (videos, .nfo, .sfv...) queda fuera. Las partes se ordenan por su número, no por el texto.
    std::vector<Comprimido> Comprimidos(const std::vector<std::wstring>& archivos);
    // ¿Es un .rar, .zip o .7z entero, no una parte? Solo esos se descomprimen cuando se bajan sueltos: de unas partes
    // sueltas no se sabe si ya llegaron todas.
    bool EsComprimidoSuelto(const std::wstring& nombre);
    // ¿Este .rar es una parte de un RAR en varias partes? Lo dice su cabecera (RAR 4 y RAR 5). Un «peli.rar» así es la
    // primera parte del estilo viejo (.rar, .r00, .r01...). La segunda forma recibe los primeros bytes del archivo.
    bool EsVolumenRar(const std::wstring& ruta);
    bool EsVolumenRar(const std::string& inicio);
    // ¿Este .zip es el último trozo de un ZIP partido (.z01, .z02... y el .zip)? Lo dice el final de su índice central.
    // La segunda forma recibe los últimos bytes del archivo.
    bool EsZipPartido(const std::wstring& ruta);
    bool EsZipPartido(const std::string& fin);

    // Una parte que bajó Atarraya: su descarga (por id), dónde quedó y qué archivo era al terminar
    struct ParteBajada
    {
        uint64_t descarga = 0;
        std::wstring ruta;
        IdentidadArchivo identidad;
    };
    // Las partes de un paquete que terminó de bajar: solo las descargas de su «grupo» (su propia lista, por id, que se
    // guarda con cada descarga). Vacío si todavía falta algo: una descarga del grupo que no terminó bien, o una parte
    // suya en la cola de «de una en una» que todavía no se baja.
    std::vector<ParteBajada> ArchivosDelPaquete(const std::string& grupo, const std::vector<Foto>& descargas,
                                                const std::vector<Parte>& cola);

    // ¿La última parte de un juego en partes (las que bajaron, en orden y sin huecos) es la última del juego? Lo dice el
    // propio comprimido, leído del disco:
    //   - RAR 4 y RAR 5 («peli.part3.rar», «peli.r02»): el bloque del final de esa parte dice si sigue otra;
    //   - 7z partido («peli.7z.003»): la primera parte dice cuánto mide el comprimido entero, y se suman las partes;
    //   - ZIP partido («peli.zip» con «peli.z01»...): el .zip es el último trozo y su índice dice cuántos hay.
    // «NoSe» si no lo dice (por ejemplo, un «.rar.001» de un programa que corta archivos, o cabeceras cifradas).
    // «tipo» es la letra del juego (ver Juego::clave).
    enum class Ultima { NoSe, Es, NoEs };
    Ultima LaUltima(const std::vector<std::wstring>& partes, wchar_t tipo);

    // Cada juego de un paquete: un comprimido entero o un juego de partes («peli.part1.rar», «peli.part2.rar»...;
    // «peli.rar» con «peli.r00»...; «peli.7z.001»...; «peli.zip» con «peli.z01»...). Se descomprime apenas está
    // completo, sin esperar al resto del paquete, y una sola vez.
    //   - «partes»: las suyas que ya bajaron, en orden (la primera es la que se le pasa a Jícaro).
    //   - «faltan»: las suyas que todavía no bajaron (en la lista de descargas o en la cola de «de una en una»).
    //   - «huecos»: falta un número en el medio (o el .rar o el .zip que lo abre).
    //   - «jicaro»: si ya se le pasó a Jícaro (se guarda con cada descarga).
    // Un juego en partes está completo solo si bajaron todas sus partes, sin huecos, y se sabe que la última es la
    // última (LaUltima). Si eso no se puede saber, espera a que termine todo el paquete. Mientras falte alguna parte del
    // paquete de la que no se sabe de qué juego es («dudosas»: por ejemplo, una página de un sitio de descargas sin el
    // nombre del archivo), ningún juego del paquete está completo: podría ser suya. «incompleto»: ya terminó todo el
    // paquete y a ese juego le falta algo (no se le pasa a Jícaro: se avisa).
    struct Juego
    {
        std::wstring clave;   // la letra del tipo, «|», su carpeta y su nombre
        std::wstring nombre;
        std::wstring carpeta;
        std::vector<ParteBajada> partes;
        size_t faltan = 0;
        bool huecos = false;
        bool enPartes = false;
        Ultima ultima = Ultima::NoSe;
        EstadoJicaro jicaro = EstadoJicaro::Pendiente;
        bool completo = false;
        bool incompleto = false;
        wchar_t Tipo() const { return clave.empty() ? L'?' : clave[0]; }
    };
    struct JuegosDelPaquete
    {
        std::vector<Juego> juegos;
        size_t dudosas = 0;
        bool terminado = false;   // ya no baja nada del paquete (ni en la lista ni en la cola)
        bool Completo(const Juego& j) const { return j.completo; }
    };
    JuegosDelPaquete Juegos(const std::string& grupo, const std::vector<Foto>& descargas, const std::vector<Parte>& cola,
                            const std::function<Ultima(const std::vector<std::wstring>&, wchar_t)>& ultima = LaUltima);
    // Otro archivo en la carpeta de un juego que Jícaro tomaría como parte suya (el mismo nombre de juego) y que no es
    // de este paquete («delPaquete»: las rutas de todas sus descargas). Vacío si no hay: así Jícaro nunca junta partes
    // de dos paquetes que quedaron en la misma carpeta.
    std::wstring ParteAjena(const Juego& j, const std::vector<std::wstring>& delPaquete);
    // Lo mismo para un juego que ya espera a Jícaro, por la parte que lo abre: se revisa otra vez justo antes de abrir
    // Jícaro (y en cada reintento), porque mientras espera pueden aparecer partes ajenas en la carpeta
    std::wstring ParteAjenaDe(const std::wstring& primera, const std::vector<std::wstring>& delPaquete);
    // Al abrir Atarraya: las descargas cuyo juego quedó «en curso» (se le había pasado a Jícaro y no terminó bien), una
    // por paquete (o la suelta), para pasárselo otra vez
    std::vector<uint64_t> PorRetomar(const std::vector<Foto>& descargas);

    // Lo que hay que descomprimir de un paquete: un comprimido por vez, armado solo con las partes del paquete (nunca
    // con archivos de otro, aunque estén en la misma carpeta y se llamen igual)
    struct Plan
    {
        std::wstring nombre;              // para la carpeta: el del paquete o el del comprimido
        std::wstring carpeta;             // donde están sus partes
        std::wstring primera;             // la que se le pasa a Jícaro
        std::vector<ParteBajada> partes;  // las de este comprimido, en orden: lo único que «Borrar las partes» puede tocar
    };
    std::vector<Plan> Planear(const std::vector<ParteBajada>& propias, const std::wstring& paquete);

    // «Borrar las partes»: manda a la Papelera solo las que siguen siendo el mismo archivo que bajó Atarraya (mismo
    // volumen, número de archivo, tamaño y fecha que al terminar). Las que no coinciden, las que no se pueden revisar y
    // las que no tienen guardado cómo eran al terminar (por ejemplo, de una versión anterior) se quedan donde están.
    struct Borrado
    {
        size_t mandadas = 0;       // a la Papelera
        size_t distintas = 0;      // ya no son el archivo que bajó Atarraya (o no se pudo revisar): no se tocaron
        size_t sinIdentidad = 0;   // no se guardó cómo era al terminar: no se sabe si es la misma, así que nunca se toca
        size_t fallaron = 0;       // eran las mismas, pero Windows no las pudo mandar a la Papelera
        size_t faltaban = 0;       // ya no estaban
    };
    Borrado BorrarPartes(const std::vector<ParteBajada>& partes, const std::function<bool(const std::wstring&)>& aPapelera);

    // Los grupos de una tanda de enlaces que llegan juntos (del recolector o de una página). Cada paquete tiene un
    // grupo. Si una descarga del paquete ya estaba en la lista sin terminar (bajando, en pausa o con error), entra al
    // grupo, para que Jícaro espere también a esa; si ya era de otro grupo, los dos quedan en uno, así Jícaro corre una
    // sola vez por paquete. Primero se suman las que ya estaban; después se piden los grupos de las nuevas.
    class Tanda
    {
    public:
        void Sumar(const std::wstring& paquete, uint64_t descarga, const std::string& grupoQueTenia);
        std::string Grupo(const std::wstring& paquete);   // uno nuevo si el paquete todavía no tiene
        // Lo que hay que cambiar en la lista: descargas sueltas que entran a un grupo, y grupos que pasan a otro
        struct Entra { uint64_t descarga; std::string grupo; std::wstring paquete; };
        struct Une { std::string viejo, nuevo; std::wstring paquete; };
        std::vector<Entra> Entran() const;
        std::vector<Une> Uniones() const;

    private:
        std::string Final(std::string grupo) const;   // siguiendo las uniones
        std::map<std::wstring, std::string> m_grupos;
        std::vector<Entra> m_entran;
        std::vector<Une> m_uniones;
    };

    // El nombre de la carpeta: el del paquete, salvo que sea uno de los que pone Atarraya cuando no sabe cuál es
    // («Páginas de ...», «Enlaces de ...»): entonces el del comprimido
    std::wstring NombreDeCarpeta(const std::wstring& paquete, const std::wstring& comprimido);
    // Una carpeta que todavía no existe dentro de «carpeta»: «peli»; si ya hay algo con ese nombre, «peli (2)»,
    // «peli (3)»... El nombre se limpia como el de un archivo. Vacía si no hay un nombre libre.
    std::wstring CarpetaDestino(const std::wstring& carpeta, const std::wstring& nombre);

    // Lo que dice Jícaro al terminar, según su código de salida. «Contrasena»: hace falta una (o el cifrado no se
    // puede abrir); «ContrasenaIncorrecta»: la que se le pasó no sirve (Jícaro 0.2.0). «JicaroViejo»: el paquete tiene
    // contraseña y el Jícaro instalado es anterior a la 0.2.0 (no se abrió).
    // «PartesAjenas»: en su carpeta hay partes con el mismo nombre que no son del paquete (no se abrió). «Incompleto»: ya
    // terminó todo el paquete y a ese juego le falta una parte (no se abrió).
    enum class Resultado { Listo, Error, Cancelado, FaltanPartes, Contrasena, YaExtrayendo, Parcial, NoAbrio, ContrasenaIncorrecta, JicaroViejo,
                           PartesAjenas, Incompleto };
    Resultado LeerCodigo(unsigned long codigo);
    // ¿Hay que pedir (otra) contraseña?
    bool PideContrasena(Resultado r);
    // Cómo queda el juego después: «Hecho» solo si Jícaro terminó (bien, a medias o la persona lo canceló: no se vuelve a
    // abrir solo); si no, «Pendiente» (por ejemplo, para intentarlo otra vez con la contraseña)
    EstadoJicaro EstadoDespues(Resultado r);

    // De los avisos que esperan algo (una contraseña o un Jícaro nuevo), saca los de «clave» y los devuelve. «clave» va
    // por valor a propósito: puede ser la de uno de esos avisos, que se mueve y se borra al sacarlo.
    template <class Lista, class ClaveDe, class Espera>
    std::vector<typename Lista::value_type> SacarLosQueEsperan(Lista& lista, const std::string clave, ClaveDe claveDe, Espera espera)
    {
        std::vector<typename Lista::value_type> r;
        for (auto it = lista.begin(); it != lista.end();)
        {
            if (claveDe(*it) == clave && espera(*it))
            {
                r.push_back(std::move(*it));
                it = lista.erase(it);
            }
            else ++it;
        }
        return r;
    }
    // Para la persona, en español. «nombre» es el del paquete o el del comprimido; «destino», la carpeta.
    std::wstring Titulo(Resultado r);
    std::wstring Texto(Resultado r, const std::wstring& nombre, const std::wstring& destino);

    // Abre Jícaro con esa orden, sin quitarle el foco a lo que la persona está haciendo y sin heredarle nada (solo su
    // entrada estándar, si hay contraseña). Con «contrasena», se la escribe por la entrada estándar (UTF-8, sin salto
    // de línea al final) y la cierra: nunca va en la línea de comandos. Devuelve el proceso (un HANDLE, para esperar su
    // código y cerrarlo después) o nullptr si no se pudo abrir.
    void* Abrir(const std::wstring& exe, const std::wstring& orden, const std::string& contrasena = {});
}
