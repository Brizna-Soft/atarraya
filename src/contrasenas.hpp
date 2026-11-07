#pragma once

// Las contraseñas de los paquetes (para que Jícaro descomprima lo que bajó). Se escriben una vez al agregar los
// enlaces (al pegarlos, en el recolector o desde Click'n'Load, que las trae en su campo «passwords») y se pueden poner
// o cambiar después desde el menú del paquete.
//
//   - Nunca se guardan en claro: van cifradas con DPAPI (CryptProtectData), que solo descifra la misma cuenta de
//     Windows en la misma PC. Así siguen ahí después de cerrar Atarraya.
//   - Nunca van a la línea de comandos ni al registro: a Jícaro le llegan por su entrada estándar.
//   - Recordarlas para otros paquetes es aparte, solo si la persona lo enciende en Ajustes (viene apagado), y también
//     cifradas. Atarraya nunca prueba contraseñas a ciegas: solo las del paquete, en orden y como mucho 5.
//
// No usa wxWidgets (lo usan también las pruebas).

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace hnd::contrasenas
{
    constexpr size_t Maximo = 5;           // por paquete, en el orden en que llegaron
    constexpr size_t MaximoLargo = 512;    // bytes de cada una
    constexpr size_t MaximoRecordadas = 20;

    // Las de un texto (el campo «passwords» de Click'n'Load, que trae una por línea): separadas por saltos de línea
    // (\r\n, \n o \r), sin espacios a los costados, sin vacías, sin repetidas, en UTF-8 válido, sin caracteres de
    // control, de 512 bytes como mucho cada una, en el orden en que llegaron y como mucho 5
    std::vector<std::string> DeTexto(const std::string& texto);
    // Junta dos listas (primero las de «a»), sin repetir y como mucho 5
    std::vector<std::string> Juntar(const std::vector<std::string>& a, const std::vector<std::string>& b);

    // Cifradas con DPAPI (solo para la cuenta de Windows de la persona), en base64. Vacío si la lista está vacía o no se
    // pudo cifrar. Descifrar devuelve vacío si no se puede (otro usuario, otra PC o un texto cambiado).
    std::string Cifrar(const std::vector<std::string>& lista);
    std::vector<std::string> Descifrar(const std::string& cifrado);
    // ¿Se puede leer? Falso si no se puede descifrar (otra cuenta u otra PC) o tiene otro formato (el de antes, en JSON).
    // Lo descifrado se borra antes de soltarlo.
    bool Legible(const std::string& cifrado);

    // Borra de la memoria (antes de soltarla)
    void Borrar(std::string& s);
    void Borrar(std::vector<std::string>& v);
    void Borrar(std::vector<char>& v);
    // Agrega al final de un texto que puede tener una contraseña (un pedido de Click'n'Load): si hay que mudarlo a un
    // lugar más grande, el de antes se borra primero
    void Agregar(std::string& s, const char* datos, size_t largo);

    // La lista en claro justo antes de cifrarla (y justo después de descifrarla): un «1» y cada contraseña con su largo
    // (4 bytes) y sus bytes. Sin JSON: sus objetos harían copias de cada contraseña que después nadie borra. Se arma en un
    // solo bloque del tamaño justo y se lee directo a cada contraseña, sin copias de más (el que llama borra lo suyo).
    // Desempaquetar devuelve false (y la lista vacía) si el bloque no tiene esa forma.
    std::string Empaquetar(const std::vector<std::string>& lista);
    bool Desempaquetar(const char* datos, size_t largo, size_t maximo, std::vector<std::string>& lista);

    // Las guardadas (contrasenas.json, en la carpeta de datos de Atarraya): las de cada paquete de la lista de descargas
    // (por su grupo) y, aparte, las que se recuerdan para otros paquetes. Todo cifrado.
    class Guardadas
    {
    public:
        explicit Guardadas(std::wstring archivo);
        void Cargar();
        bool Guardar() const;

        // Las de un paquete. Una lista vacía las quita.
        void Fijar(const std::string& grupo, const std::vector<std::string>& lista);
        std::vector<std::string> Leer(const std::string& grupo) const;
        bool Tiene(const std::string& grupo) const;
        // Se quedan solo las de estos paquetes (los que siguen en la lista)
        void QuedarseCon(const std::set<std::string>& grupos);
        // Quita las que no se pueden leer (por ejemplo, las del formato de antes): su paquete pide la contraseña otra
        // vez. Devuelve cuántas quitó (las recordadas cuentan como una). Hay que guardar después.
        size_t QuitarIlegibles();

        // Para otros paquetes (solo si la persona lo encendió): la más nueva primero, sin repetir, como mucho 20
        void Recordar(const std::vector<std::string>& lista);
        std::vector<std::string> Recordadas() const;
        bool HayRecordadas() const { return !m_recordadas.empty(); }
        void OlvidarRecordadas();

    private:
        std::wstring m_archivo;
        std::map<std::string, std::string> m_paquetes;   // grupo → cifrado
        std::string m_recordadas;                        // cifrado
    };
}
