#include "sistema.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <thread>
#include <vector>

#include "nombres.hpp"
#include "util.hpp"

namespace
{
    // Identifica a Atarraya ante los servicios de adjuntos de Windows
    const GUID GuidAtarraya = { 0x6b1f3c2e, 0x8d4a, 0x4f61, { 0x9e, 0x2b, 0x5a, 0x77, 0x31, 0xc0, 0x4d, 0x18 } };
}

bool hnd::sistema::Abrir(const std::wstring& ruta)
{
    return (INT_PTR)ShellExecuteW(nullptr, L"open", ruta.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

void hnd::sistema::AbrirPaginaWeb(const std::string& url)
{
    std::string inicio = url.substr(0, 8);
    for (auto& c : inicio) c = (char)tolower((unsigned char)c);
    if (inicio.rfind("http://", 0) != 0 && inicio.rfind("https://", 0) != 0) return;   // solo páginas web
#ifdef ATARRAYA_PRUEBAS
    // Copia de prueba: no se abre ningún navegador, solo se anota la página
    std::wstring archivo = CarpetaDePrueba() + L"\\paginas-abiertas.txt";
    std::string antes;
    LeerArchivo(archivo, antes);
    EscribirSeguro(archivo, antes + url + "\n");
#else
    ShellExecuteW(nullptr, L"open", Ancho(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void hnd::sistema::MostrarEnCarpeta(const std::wstring& ruta)
{
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(ruta.c_str());
    if (pidl)
    {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        return;
    }
    // Si el archivo ya no está, se abre la carpeta
    auto p = ruta.find_last_of(L'\\');
    if (p != std::wstring::npos) ShellExecuteW(nullptr, L"open", ruta.substr(0, p).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool hnd::sistema::MandarAPapelera(const std::wstring& ruta)
{
    if (!ExisteArchivo(ruta)) return true;
    std::wstring doble = ruta;
    doble.push_back(L'\0');   // SHFileOperation pide la lista terminada en dos ceros
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = doble.c_str();
    // FOF_WANTNUKEWARNING: si el disco no tiene Papelera (por ejemplo, algunas memorias USB), Windows pregunta antes
    // de borrar para siempre, en vez de borrar sin avisar
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_WANTNUKEWARNING;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

bool hnd::sistema::EsEjecutable(const std::wstring& nombre)
{
    static const wchar_t* extensiones[] = { L".exe", L".msi", L".bat", L".cmd", L".ps1", L".vbs", L".js", L".jse",
        L".scr", L".com", L".pif", L".lnk", L".msix", L".appx", L".hta", L".wsf", L".reg" };
    std::wstring bajo = Minusculas(nombre);
    for (auto* e : extensiones)
    {
        size_t n = wcslen(e);
        if (bajo.size() >= n && bajo.compare(bajo.size() - n, n, e) == 0) return true;
    }
    return false;
}

void hnd::sistema::CopiarTexto(const std::wstring& texto)
{
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (texto.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h)
    {
        memcpy(GlobalLock(h), texto.c_str(), bytes);
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    }
    CloseClipboard();
}

std::wstring hnd::sistema::TextoDelPortapapeles()
{
    std::wstring r;
    if (!OpenClipboard(nullptr)) return r;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h)
    {
        auto* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p) r = p;
        GlobalUnlock(h);
    }
    CloseClipboard();
    // Solo la primera línea y sin espacios alrededor
    auto fin = r.find_first_of(L"\r\n");
    if (fin != std::wstring::npos) r = r.substr(0, fin);
    while (!r.empty() && iswspace(r.back())) r.pop_back();
    size_t i = 0;
    while (i < r.size() && iswspace(r[i])) i++;
    return r.substr(i);
}

std::wstring hnd::sistema::TodoElPortapapeles(size_t maximo)
{
    std::wstring r;
    // El programa que acaba de copiar puede tenerlo abierto un momento: se reintenta un poco antes de rendirse
    bool abierto = false;
    for (int i = 0; i < 6 && !(abierto = OpenClipboard(nullptr) != FALSE); i++) Sleep(25);
    if (!abierto) return r;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h)
    {
        auto* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p)
        {
            size_t tope = GlobalSize(h) / sizeof(wchar_t);
            size_t n = 0;
            while (n < tope && n < maximo && p[n]) n++;
            r.assign(p, n);
        }
        GlobalUnlock(h);
    }
    CloseClipboard();
    return r;
}

bool hnd::sistema::PortapapelesPrivado()
{
    // Los formatos que usan los programas para pedir que el portapapeles no se lea ni se guarde en el historial
    static const UINT excluir = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    static const UINT ignorar = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
    static const UINT historial = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    if ((excluir && IsClipboardFormatAvailable(excluir)) || (ignorar && IsClipboardFormatAvailable(ignorar))) return true;
    if (historial && IsClipboardFormatAvailable(historial) && OpenClipboard(nullptr))
    {
        bool privado = false;
        if (HANDLE h = GetClipboardData(historial))
        {
            if (auto* v = static_cast<const DWORD*>(GlobalLock(h)))
            {
                privado = GlobalSize(h) >= sizeof(DWORD) && *v == 0;
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return privado;
    }
    return false;
}

bool hnd::sistema::EsDireccion(const std::wstring& texto)
{
    std::wstring bajo = Minusculas(texto.substr(0, 8));
    return (bajo.rfind(L"http://", 0) == 0 || bajo.rfind(L"https://", 0) == 0 || bajo.rfind(L"ftp://", 0) == 0)
        && texto.size() > 9 && texto.size() <= 8192 && texto.find_first_of(L" \t\r\n\"<>") == std::wstring::npos;
}

bool hnd::sistema::EsMagnet(const std::wstring& texto)
{
    return Minusculas(texto.substr(0, 8)) == L"magnet:?" && texto.size() > 20 && texto.size() <= 8192
        && texto.find_first_of(L" \t\r\n\"<>") == std::wstring::npos;
}

bool hnd::sistema::LeerTexto(const std::wstring& ruta, std::wstring& texto, size_t maximo)
{
    texto.clear();
    int64_t tamano = TamanoArchivo(ruta);
    if (tamano < 0 || (uint64_t)tamano > maximo) return false;
    std::string b;
    if (!LeerArchivo(ruta, b)) return false;
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE)
    {
        texto.assign(reinterpret_cast<const wchar_t*>(b.data() + 2), (b.size() - 2) / sizeof(wchar_t));
        return true;
    }
    if (b.size() >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) b.erase(0, 3);
    if (b.empty()) return true;
    UINT pagina = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, b.data(), (int)b.size(), nullptr, 0) > 0 ? CP_UTF8 : CP_ACP;
    int n = MultiByteToWideChar(pagina, 0, b.data(), (int)b.size(), nullptr, 0);
    texto.assign(n, L'\0');
    MultiByteToWideChar(pagina, 0, b.data(), (int)b.size(), texto.data(), n);
    return true;
}

void hnd::sistema::MarcarComoDescargado(const std::wstring& ruta, const std::string& url, const std::string& referer,
                                        std::function<void(bool)> alTerminar)
{
    std::thread([ruta, url, referer, alTerminar]() {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IAttachmentExecute* ae = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_AttachmentServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ae))))
        {
            ae->SetClientGuid(GuidAtarraya);
            ae->SetLocalPath(ruta.c_str());
            ae->SetSource(Ancho(url).c_str());
            if (!referer.empty()) ae->SetReferrer(Ancho(referer).c_str());
            ae->Save();   // escribe la marca y avisa al antivirus
            ae->Release();
        }
        if (SUCCEEDED(com)) CoUninitialize();
        if (alTerminar) alTerminar(ExisteArchivo(ruta));
    }).detach();
}

void hnd::sistema::MarcarVariosComoDescargados(std::vector<std::wstring> rutas, std::function<void(size_t)> alTerminar)
{
    std::thread([rutas = std::move(rutas), alTerminar]() {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        size_t faltan = 0;
        for (auto& ruta : rutas)
        {
            if (!ExisteArchivo(ruta)) continue;   // no se eligió o ya no está
            IAttachmentExecute* ae = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_AttachmentServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ae))))
            {
                ae->SetClientGuid(GuidAtarraya);
                ae->SetLocalPath(ruta.c_str());
                ae->SetSource(L"about:internet");   // un torrent no tiene una dirección web: se marca como de internet
                ae->Save();
                ae->Release();
            }
            if (!ExisteArchivo(ruta)) faltan++;
        }
        if (SUCCEEDED(com)) CoUninitialize();
        if (alTerminar) alTerminar(faltan);
    }).detach();
}
