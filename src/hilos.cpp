#include "hilos.hpp"

#include <wx/app.h>

#include <mutex>

namespace
{
    std::mutex m;
    bool abierta = true;
}

void hnd::hilos::Enviar(std::function<void()> f)
{
    std::lock_guard g(m);
    if (!abierta || !wxTheApp) return;
    wxTheApp->CallAfter(std::move(f));
}

void hnd::hilos::Cerrar()
{
    std::lock_guard g(m);
    abierta = false;
}
