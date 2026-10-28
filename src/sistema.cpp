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
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
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

bool hnd::sistema::EsDireccion(const std::wstring& texto)
{
    std::wstring bajo = Minusculas(texto.substr(0, 8));
    return (bajo.rfind(L"http://", 0) == 0 || bajo.rfind(L"https://", 0) == 0 || bajo.rfind(L"ftp://", 0) == 0)
        && texto.size() > 9 && texto.find_first_of(L" \t\"<>") == std::wstring::npos;
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
