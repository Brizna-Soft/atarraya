#pragma once

// Pasar trabajo de un hilo cualquiera al hilo de la ventana, sin riesgos al cerrar el programa.
//
// Los hilos de trabajo no deben copiar wxWeakRef (su lista interna no está protegida entre hilos) ni llamar a
// wxTheApp->CallAfter mientras la aplicación se destruye. En su lugar:
//   - Enviar(f) pone f en la cola de la ventana solo si la aplicación sigue viva;
//   - quien crea el trabajo guarda un Vivo (un indicador compartido) y lo revisa dentro de f, ya en el hilo de la
//     ventana, antes de tocar la ventana o el diálogo.

#include <atomic>
#include <functional>
#include <memory>

namespace hnd::hilos
{
    using Vivo = std::shared_ptr<std::atomic<bool>>;
    inline Vivo NuevoVivo() { return std::make_shared<std::atomic<bool>>(true); }

    void Enviar(std::function<void()> f);
    // Al cerrar: desde aquí no se manda nada más al hilo de la ventana
    void Cerrar();
}
