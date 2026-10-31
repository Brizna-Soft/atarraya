#pragma once

// Utilidades sin wxWidgets (las usa también la prueba del motor).

#include <cstdint>
#include <string>

namespace hnd
{
    std::wstring Ancho(const std::string& utf8);
    std::string Utf8(const std::wstring& texto);

    // Textos para la persona: «1.5 GB», «2.3 MB/s», «3 min 20 s»
    std::wstring TextoTamano(int64_t bytes);
    std::wstring TextoVelocidad(double bytesPorSegundo);
    std::wstring TextoDuracion(int64_t segundos);

    std::wstring CarpetaDescargas();     // la carpeta «Descargas» de la persona
    std::wstring CarpetaDatos();         // %LOCALAPPDATA%\Atarraya (la crea si no existe)
    std::wstring CarpetaDelPrograma();   // donde está el .exe

#ifdef ATARRAYA_PRUEBAS
    // Solo en la copia de prueba (no se publica): sus datos nunca van a %LOCALAPPDATA%\Atarraya, sino a la carpeta
    // de la variable ATARRAYA_PRUEBA (ruta completa) o, si no está, a %TEMP%\Atarraya-prueba. Las páginas que se
    // abrirían en el navegador solo se anotan en «paginas-abiertas.txt» dentro de esa carpeta.
    std::wstring CarpetaDePrueba();
#endif

    bool ExisteArchivo(const std::wstring& ruta);
    bool ExisteCarpeta(const std::wstring& ruta);
    int64_t TamanoArchivo(const std::wstring& ruta); // -1 si no existe

    // Lo que identifica a un archivo en el disco: su volumen, su número de archivo (en NTFS no se repite, aunque otro
    // archivo tome su nombre), su tamaño y su fecha de modificación
    struct IdentidadArchivo
    {
        uint32_t volumen = 0;
        uint64_t indice = 0;
        int64_t tamano = -1;
        int64_t modificado = 0;   // FILETIME
        bool Valida() const { return tamano >= 0; }
        bool operator==(const IdentidadArchivo&) const = default;
    };
    // Con GetFileInformationByHandle. No sigue enlaces: si en esa ruta ahora hay un enlace, lee el del enlace. Falso si no
    // existe, si es una carpeta o si no se puede abrir.
    bool LeerIdentidad(const std::wstring& ruta, IdentidadArchivo& identidad);
    int64_t EspacioLibre(const std::wstring& carpeta); // -1 si no se sabe

    // Escribe a un archivo temporal y lo cambia por el bueno: nunca deja el archivo a medias
    bool EscribirSeguro(const std::wstring& ruta, const std::string& contenido);
    bool LeerArchivo(const std::wstring& ruta, std::string& contenido);

    // Huella SHA-256 en hexadecimal (vacía si falla)
    std::string Sha256Archivo(const std::wstring& ruta);

    int64_t AhoraUnix();
    // 16 letras y números al azar: el grupo de las descargas de un mismo paquete que llegaron juntas
    std::string NuevoId();

    // Registro corto para ayudar a encontrar fallas (%LOCALAPPDATA%\Atarraya\registro.txt).
    // No guarda direcciones completas: solo el sitio.
    void Registrar(const std::string& texto);
    std::string SoloSitio(const std::string& url);
}
