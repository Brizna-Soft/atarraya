#pragma once

// Click'n'Load 2: un servidor web mínimo en 127.0.0.1:9666 (el puerto de JDownloader). Sitios como filecrypt.cc le
// mandan los enlaces de una carpeta después de que la persona resuelve el captcha en su navegador.
//
// Cómo es el protocolo (verificado en el código de pyLoad y en la página de JDownloader sobre CNL2):
//   GET  /jdcheck.js          responde «jdownloader=true;» para que la página sepa que hay alguien escuchando
//   GET  /flash/              responde «JDownloader»
//   POST /flash/add           campo «urls»: enlaces sin cifrar
//   POST /flash/addcrypted2   campos «crypted» (base64) y «jk» (una función de JavaScript que devuelve la clave en
//                             hexadecimal). Cifrado AES-128-CBC; la misma clave es el vector inicial.
//
// Seguridad:
//   - Solo escucha en 127.0.0.1 y solo atiende pedidos cuyo «Host» es 127.0.0.1:9666 o localhost:9666 (así una página
//     no puede usar el truco del «DNS rebinding»).
//   - No ejecuta JavaScript: de «jk» solo acepta la forma «function f(){ return '…32 cifras hexadecimales…'; }»
//     (también partida en trozos unidos con «+»). Lo demás se rechaza.
//   - Pedidos de 1 MB como mucho, 1000 enlaces por pedido y 30 pedidos por minuto.
//   - Nunca empieza una descarga: los enlaces van al recolector y la persona decide.
//   - No acepta contenedores DLC (/flash/addcrypted): su clave es de JDownloader.
// No usa wxWidgets.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

        // Devuelve false y un texto en español si no se pudo (por ejemplo, el puerto está ocupado)
        bool Iniciar(AlRecibir alRecibir, std::string& error, uint16_t puerto = 9666);
        void Detener();
        bool Activo() const { return m_activo; }

    private:
        void Bucle();
        void Atender(uintptr_t conexion);
        int Recibir(uintptr_t conexion, char* buf, int largo, uint64_t limite);   // -1 si se cortó o venció el plazo

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
    };

    namespace cnl
    {
        // La clave de «jk» sin ejecutar JavaScript. Falso si no tiene la forma esperada.
        bool ClaveDeJk(const std::string& jk, std::vector<uint8_t>& clave);
        // Descifra «crypted» (base64) con AES-128-CBC; la clave es también el vector inicial
        bool Descifrar(const std::string& crypted, const std::vector<uint8_t>& clave, std::string& texto);
        // Campos de un formulario «application/x-www-form-urlencoded»
        std::map<std::string, std::string> LeerFormulario(const std::string& cuerpo);
        // Los enlaces que trae un texto (uno por línea), sin repetir y con un tope
        std::vector<std::string> Enlaces(const std::string& texto, size_t maximo);
    }
}
