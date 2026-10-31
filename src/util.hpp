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

    // Solo para probar el programa sin tocar la copia de verdad: si la variable ATARRAYA_PRUEBA trae una carpeta
    // (ruta completa), los datos van ahí, esa copia no choca con la que ya está abierta y las páginas que se abrirían
    // en el navegador solo se anotan en «paginas-abiertas.txt». Vacía en el uso normal.
    std::wstring CarpetaDePrueba();

    bool ExisteArchivo(const std::wstring& ruta);
    bool ExisteCarpeta(const std::wstring& ruta);
    int64_t TamanoArchivo(const std::wstring& ruta); // -1 si no existe
    int64_t EspacioLibre(const std::wstring& carpeta); // -1 si no se sabe

    // Escribe a un archivo temporal y lo cambia por el bueno: nunca deja el archivo a medias
    bool EscribirSeguro(const std::wstring& ruta, const std::string& contenido);
    bool LeerArchivo(const std::wstring& ruta, std::string& contenido);

    // Huella SHA-256 en hexadecimal (vacía si falla)
    std::string Sha256Archivo(const std::wstring& ruta);

    int64_t AhoraUnix();

    // Registro corto para ayudar a encontrar fallas (%LOCALAPPDATA%\Atarraya\registro.txt).
    // No guarda direcciones completas: solo el sitio.
    void Registrar(const std::string& texto);
    std::string SoloSitio(const std::string& url);
}
