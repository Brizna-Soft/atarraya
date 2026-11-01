#pragma once

// Click'n'Load 2: un servidor web mínimo en 127.0.0.1:9666 (el puerto de JDownloader). Sitios como filecrypt.cc le
// mandan los enlaces de una carpeta después de que la persona resuelve el captcha en su navegador.
//
// Cómo es el protocolo (verificado en el código de pyLoad y en la página de JDownloader sobre CNL2):
//   GET  /jdcheck.js          responde «jdownloader=true;» para que la página sepa que hay alguien escuchando
//   GET  /flash/              responde «JDownloader»
//   POST /flash/add           campo «urls» (también «url», «links» o «link»): enlaces sin cifrar, separados por saltos
//                             de línea, espacios o comas. También llega como «GET /flash/add?urls=...».
//   POST /flash/addcrypted2   campos «crypted» (base64) y «jk» (una función de JavaScript que devuelve la clave en
//                             hexadecimal). Cifrado AES-128-CBC; la misma clave es el vector inicial.
// El formulario puede venir como «application/x-www-form-urlencoded», «multipart/form-data» o «text/plain» (las tres
// formas en que un navegador manda un formulario).
//
// Seguridad:
//   - Solo escucha en 127.0.0.1 y solo atiende pedidos cuyo «Host» es 127.0.0.1:9666 o localhost:9666 (así una página
//     no puede usar el truco del «DNS rebinding»).
//   - No ejecuta JavaScript: de «jk» solo acepta la forma «function f(){ return '…32 cifras hexadecimales…'; }»
//     (también partida en trozos unidos con «+»). Lo demás se rechaza.
//   - Pedidos de 1 MB como mucho (también lo que trae la dirección de un GET), 64 campos de 512 KB como mucho,
//     1000 enlaces por pedido y 30 pedidos por minuto.
//   - Nunca empieza una descarga: los enlaces van al recolector y la persona decide.
//   - No acepta contenedores DLC (/flash/addcrypted): su clave es de JDownloader.
//   - Cada rechazo queda en el registro con su motivo y el nombre del sitio; nunca los enlaces ni lo que trae el
//     formulario.
// No usa wxWidgets.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hnd
{
    class ServidorCnL
    {
    public:
        // Se llama en el hilo del servidor: quien la recibe tiene que pasar los datos a su propio hilo
        using AlRecibir = std::function<void(std::vector<std::string> enlaces, std::wstring paquete, std::string origen)>;

        ServidorCnL() = default;
        ~ServidorCnL();
        ServidorCnL(const ServidorCnL&) = delete;
        ServidorCnL& operator=(const ServidorCnL&) = delete;

        // Devuelve false y un texto en español si no se pudo (por ejemplo, el puerto está ocupado).
        // Con el puerto 0, Windows elige uno libre (para las pruebas); Puerto() dice cuál.
        bool Iniciar(AlRecibir alRecibir, std::string& error, uint16_t puerto = 9666);
        void Detener();
        bool Activo() const { return m_activo; }
        uint16_t Puerto() const { return m_puerto; }

    private:
        void Bucle();
        void Atender(uintptr_t conexion);
        int Recibir(uintptr_t conexion, char* buf, int largo, uint64_t limite);   // -1 si se cortó o venció el plazo
        void Anotar(const std::string& rechazo);   // al registro, con un tope por minuto

        struct Trabajo
        {
            std::thread t;
            std::shared_ptr<std::atomic<bool>> fin;
        };

        uintptr_t m_escucha = ~(uintptr_t)0;
        uint16_t m_puerto = 9666;
        std::thread m_hilo;
        std::atomic<bool> m_parar{ false };
        std::atomic<bool> m_activo{ false };
        AlRecibir m_alRecibir;
        std::vector<Trabajo> m_trabajos;     // una conexión cada uno, como mucho 4 a la vez
        std::vector<int64_t> m_conexiones;   // cuándo llegaron las últimas conexiones (tope por minuto)
        std::mutex m_m;
        std::vector<int64_t> m_recientes;    // cuándo llegaron los últimos envíos de enlaces (tope por minuto)
        std::vector<int64_t> m_rechazos;     // cuándo se anotaron los últimos rechazos (tope por minuto)
        bool m_callado = false;              // ya se anotó que no se anotan más rechazos este minuto
    };

    namespace cnl
    {
        constexpr size_t MaxCuerpo = 1024 * 1024;   // el cuerpo de un pedido, o lo que va después de «?» en un GET
        constexpr size_t MaxCampos = 64;           // campos de un formulario (o partes de un multipart)
        constexpr size_t MaxCampo = 512 * 1024;    // largo de cada campo
        constexpr size_t MaxEnlaces = 1000;        // enlaces por pedido

        // Los campos de un formulario en el orden en que llegaron (si un nombre se repite, quedan todos)
        using Campos = std::vector<std::pair<std::string, std::string>>;

        // La clave de «jk» sin ejecutar JavaScript. Falso si no tiene la forma esperada.
        bool ClaveDeJk(const std::string& jk, std::vector<uint8_t>& clave);
        // Descifra «crypted» (base64) con AES-128-CBC; la clave es también el vector inicial
        bool Descifrar(const std::string& crypted, const std::vector<uint8_t>& clave, std::string& texto);

        // ¿Es un tipo de formulario que se sabe leer? (sin tipo, urlencoded, multipart/form-data o text/plain)
        bool TipoAdmitido(const std::string& tipo);
        // Lee un formulario del tipo que dice su «Content-Type». Devuelve vacío si se pudo; si no, el motivo en español.
        std::string LeerFormulario(const std::string& tipo, const std::string& cuerpo, Campos& campos);
        // El primer valor de un campo, sin importar mayúsculas (vacío si no vino)
        std::string Valor(const Campos& campos, const char* nombre);
        bool Tiene(const Campos& campos, const char* nombre);
        // Lo que traen los campos de enlaces («urls», «url», «links» o «link», también como «urls[]»), todos juntos.
        // Falso si no vino ninguno de esos campos.
        bool TextoDeEnlaces(const Campos& campos, std::string& texto);

        struct Lista
        {
            std::vector<std::string> enlaces;   // los que quedaron: sin repetir y en el orden en que llegaron
            size_t llegaron = 0;                // todo lo que parecía un enlace
            size_t repetidos = 0;
            size_t youtube = 0;                 // de sitios que no permiten bajar sus videos
            size_t sobran = 0;                  // pasaron del tope
            size_t otros = 0;                   // trozos de texto que no son enlaces
        };
        // Los enlaces de un texto, separados por saltos de línea, espacios o comas (una coma dentro de una dirección
        // no la parte)
        Lista Enlaces(const std::string& texto, size_t maximo);
        // Para el registro: «llegaron 5 enlaces y quedaron 3 (1 repetido, 1 de YouTube)»
        std::string Resumen(const Lista& lista);
    }
}
