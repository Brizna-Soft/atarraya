// Prueba del motor sin ventana (no se publica).
//
//   prueba-motor <url> <carpeta> [conexiones] [cerrarA] [limiteKBs]
//
// Baja el archivo mostrando el progreso. Si se da «cerrarA» (segundos), a esa altura detiene el motor como si
// se cerrara el programa, crea uno nuevo, carga el estado guardado y sigue desde donde quedó.
// Al final escribe el estado, el error (si hubo) y la huella SHA-256 del archivo.

#include <windows.h>

#include <cstdio>
#include <string>

#include "motor.hpp"
#include "util.hpp"

using namespace hnd;

namespace
{
    bool Seguir(Motor& m, uint64_t id, double hasta, ULONGLONG inicio)
    {
        double ultimo = -1;
        for (;;)
        {
            m.Tick();
            Foto f;
            if (!m.Buscar(id, f)) { printf("la descarga desaparecio\n"); return false; }
            double t = (GetTickCount64() - inicio) / 1000.0;
            if (t - ultimo >= 1.0)
            {
                ultimo = t;
                printf("%6.1f s  %-12s %s / %s  %s  conexiones %d  partes %zu\n", t, Utf8(TextoEstado(f.datos.estado)).c_str(),
                       Utf8(TextoTamano(f.hecho)).c_str(), Utf8(TextoTamano(f.datos.tamano)).c_str(),
                       Utf8(TextoVelocidad(f.velocidad)).c_str(), f.activas, f.datos.segmentos.size());
                fflush(stdout);
            }
            if (f.datos.estado == Estado::Completada || f.datos.estado == Estado::Error) return true;
            if (hasta > 0 && t >= hasta) return false;
            Sleep(250);
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3)
    {
        printf("uso: prueba-motor <url> <carpeta> [conexiones] [cerrarA] [limiteKBs]\n");
        return 2;
    }
    std::string url = Utf8(argv[1]);
    std::wstring carpeta = argv[2];
    int conexiones = argc > 3 ? _wtoi(argv[3]) : 8;
    double cerrarA = argc > 4 ? _wtof(argv[4]) : 0;
    int64_t limite = argc > 5 ? _wtoi64(argv[5]) * 1024 : 0;
    std::wstring estado = carpeta + L"\\prueba-estado.json";
    DeleteFileW(estado.c_str());
    CreateDirectoryW(carpeta.c_str(), nullptr);

    ULONGLONG inicio = GetTickCount64();
    uint64_t id;
    bool termino;
    {
        Motor m(estado);
        m.limiteBytesSeg = limite;
        DatosDescarga d;
        d.url = url;
        d.carpeta = carpeta;
        d.conexiones = conexiones;
        id = m.Agregar(d, true);
        termino = Seguir(m, id, cerrarA, inicio);
        if (!termino) printf("--- se cierra el motor a los %.1f s (como si se cerrara el programa) ---\n", cerrarA);
    }

    Motor m(estado);
    m.limiteBytesSeg = limite;
    m.Cargar();
    if (!termino)
    {
        Foto f;
        m.Buscar(id, f);
        printf("--- motor nuevo: cargado %s de %s, estado %s; sigue ---\n", Utf8(TextoTamano(f.hecho)).c_str(),
               Utf8(TextoTamano(f.datos.tamano)).c_str(), Utf8(TextoEstado(f.datos.estado)).c_str());
        Seguir(m, id, 0, inicio);
    }

    Foto f;
    m.Buscar(id, f);
    std::wstring ruta = m.RutaFinal(id);
    printf("RESULTADO: %s\n", Utf8(TextoEstado(f.datos.estado)).c_str());
    if (!f.datos.error.empty()) printf("ERROR: %s\n", f.datos.error.c_str());
    printf("ARCHIVO: %s (%lld bytes)\n", Utf8(ruta).c_str(), (long long)TamanoArchivo(ruta));
    printf("PARTES USADAS: %zu\n", f.datos.segmentos.size());
    printf("TIEMPO: %.1f s\n", (GetTickCount64() - inicio) / 1000.0);
    if (f.datos.estado == Estado::Completada) printf("SHA256: %s\n", Sha256Archivo(ruta).c_str());
    return f.datos.estado == Estado::Completada ? 0 : 1;
}
