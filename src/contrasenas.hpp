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

    // Borra de la memoria (antes de soltarla)
    void Borrar(std::string& s);
    void Borrar(std::vector<std::string>& v);

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
