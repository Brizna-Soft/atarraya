#include "ajustes.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "util.hpp"

namespace
{
    std::wstring Archivo() { return hnd::CarpetaDatos() + L"\\ajustes.json"; }
}

void hnd::Ajustes::Cargar()
{
    std::string texto;
    if (!LeerArchivo(Archivo(), texto)) return;
    try
    {
        auto j = nlohmann::json::parse(texto);
        carpeta = Ancho(j.value("carpeta", ""));
        conexiones = std::clamp(j.value("conexiones", 8), 1, 16);
        simultaneas = std::clamp(j.value("simultaneas", 3), 1, 10);
        limiteKB = std::max(0, j.value("limiteKB", 0));
        limiteSubidaKB = std::max(0, j.value("limiteSubidaKB", 0));
        compartirTorrents = j.value("compartirTorrents", false);
        elegirArchivosMagnet = j.value("elegirArchivosMagnet", true);
        vigilarPortapapeles = j.value("vigilarPortapapeles", true);
        clickNLoad = j.value("clickNLoad", true);
        carpetaPorPaquete = j.value("carpetaPorPaquete", false);
        preguntarNavegador = j.value("preguntarNavegador", true);
        bandejaAlCerrar = j.value("bandejaAlCerrar", true);
        avisarAlTerminar = j.value("avisarAlTerminar", true);
        avisoBandejaMostrado = j.value("avisoBandejaMostrado", false);
        x = j.value("x", -1);
        y = j.value("y", -1);
        ancho = j.value("ancho", 0);
        alto = j.value("alto", 0);
        maximizada = j.value("maximizada", false);
        altoDetalles = j.value("altoDetalles", 0);
    }
    catch (...) {}
}

void hnd::Ajustes::Guardar() const
{
    nlohmann::json j = {
        { "carpeta", Utf8(carpeta) }, { "conexiones", conexiones }, { "simultaneas", simultaneas },
        { "limiteKB", limiteKB }, { "limiteSubidaKB", limiteSubidaKB }, { "compartirTorrents", compartirTorrents },
        { "elegirArchivosMagnet", elegirArchivosMagnet }, { "vigilarPortapapeles", vigilarPortapapeles },
        { "clickNLoad", clickNLoad }, { "carpetaPorPaquete", carpetaPorPaquete },
        { "preguntarNavegador", preguntarNavegador }, { "bandejaAlCerrar", bandejaAlCerrar },
        { "avisarAlTerminar", avisarAlTerminar }, { "avisoBandejaMostrado", avisoBandejaMostrado },
        { "x", x }, { "y", y }, { "ancho", ancho }, { "alto", alto }, { "maximizada", maximizada },
        { "altoDetalles", altoDetalles },
    };
    EscribirSeguro(Archivo(), j.dump(1));
}

std::wstring hnd::Ajustes::CarpetaEfectiva() const
{
    if (!carpeta.empty() && ExisteCarpeta(carpeta)) return carpeta;
    return CarpetaDescargas();
}
