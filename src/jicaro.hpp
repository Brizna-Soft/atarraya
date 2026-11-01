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

    // Cada juego de un paquete: un comprimido entero o un juego de partes («peli.part1.rar», «peli.part2.rar»...;
    // «peli.rar» con «peli.r00»...; «peli.7z.001»...; «peli.zip» con «peli.z01»...). Se descomprime apenas está
    // completo, sin esperar al resto del paquete, y una sola vez.
    //   - «partes»: las suyas que ya bajaron, en orden (la primera es la que se le pasa a Jícaro).
    //   - «faltan»: las suyas que todavía no bajaron (en la lista de descargas o en la cola de «de una en una»).
    //   - «descomprimido»: ya se le pasó a Jícaro (se guarda con cada descarga).
    // Mientras falte alguna parte del paquete de la que no se sabe de qué juego es («dudosas»: por ejemplo, una página
    // de un sitio de descargas sin el nombre del archivo), ningún juego del paquete está completo: podría ser suya.
    struct Juego
    {
        std::wstring clave;
        std::wstring nombre;
        std::wstring carpeta;
        std::vector<ParteBajada> partes;
        size_t faltan = 0;
        bool descomprimido = false;
    };
    struct JuegosDelPaquete
    {
        std::vector<Juego> juegos;
        size_t dudosas = 0;
        bool Completo(const Juego& j) const { return dudosas == 0 && j.faltan == 0 && !j.partes.empty(); }
    };
    JuegosDelPaquete Juegos(const std::string& grupo, const std::vector<Foto>& descargas, const std::vector<Parte>& cola);

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
    enum class Resultado { Listo, Error, Cancelado, FaltanPartes, Contrasena, YaExtrayendo, Parcial, NoAbrio, ContrasenaIncorrecta, JicaroViejo };
    Resultado LeerCodigo(unsigned long codigo);
    // ¿Hay que pedir (otra) contraseña?
    bool PideContrasena(Resultado r);
    // Para la persona, en español. «nombre» es el del paquete o el del comprimido; «destino», la carpeta.
    std::wstring Titulo(Resultado r);
    std::wstring Texto(Resultado r, const std::wstring& nombre, const std::wstring& destino);

    // Abre Jícaro con esa orden, sin quitarle el foco a lo que la persona está haciendo y sin heredarle nada (solo su
    // entrada estándar, si hay contraseña). Con «contrasena», se la escribe por la entrada estándar (UTF-8, sin salto
    // de línea al final) y la cierra: nunca va en la línea de comandos. Devuelve el proceso (un HANDLE, para esperar su
    // código y cerrarlo después) o nullptr si no se pudo abrir.
    void* Abrir(const std::wstring& exe, const std::wstring& orden, const std::string& contrasena = {});
}
