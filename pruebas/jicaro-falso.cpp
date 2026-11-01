// Un Jícaro falso para las pruebas (no se publica). Hace como Jícaro 0.2.0 con «--contrasena-stdin»: lee la
// contraseña de su entrada estándar y no descomprime nada. Deja en «informe.txt», al lado de él, su línea de comandos y
// lo que le llegó por la entrada (en hexadecimal), para que las pruebas revisen que la contraseña nunca va en la línea
// de comandos y que llega entera por la entrada.
//
// Códigos de salida, como Jícaro: 0 bien (la contraseña es «buena»), 4 hace falta contraseña, 7 contraseña incorrecta.

#include <windows.h>

#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    std::wstring linea = GetCommandLineW();
    bool conContrasena = linea.find(L"--contrasena-stdin") != std::wstring::npos;
    std::string entrada;
    if (conContrasena)
    {
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        char buf[4096];
        DWORD n = 0;
        while (h && h != INVALID_HANDLE_VALUE && ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0 && entrada.size() < 65536)
            entrada.append(buf, n);
    }

    wchar_t ruta[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, ruta, MAX_PATH);
    std::wstring informe = ruta;
    informe = informe.substr(0, informe.find_last_of(L'\\') + 1) + L"informe.txt";
    int n8 = WideCharToMultiByte(CP_UTF8, 0, linea.c_str(), (int)linea.size(), nullptr, 0, nullptr, nullptr);
    std::string linea8(n8, '\0');
    WideCharToMultiByte(CP_UTF8, 0, linea.c_str(), (int)linea.size(), linea8.data(), n8, nullptr, nullptr);
    std::string hex;
    const char* cifras = "0123456789abcdef";
    for (unsigned char c : entrada)
    {
        hex += cifras[c >> 4];
        hex += cifras[c & 15];
    }
    std::string texto = "linea=" + linea8 + "\nentrada=" + hex + "\n";
    HANDLE f = CreateFileW(informe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD escritos = 0;
        WriteFile(f, texto.data(), (DWORD)texto.size(), &escritos, nullptr);
        CloseHandle(f);
    }
    if (!conContrasena) return linea.find(L"cifrado") != std::wstring::npos ? 4 : 0;
    if (entrada.empty()) return 4;
    return entrada == "buena" ? 0 : 7;
}
