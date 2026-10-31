#pragma once

#include <wx/dialog.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "ajustes.hpp"
#include "enlaces.hpp"
#include "motor.hpp"
#include "torrents.hpp"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxDataViewListCtrl;
class wxDirPickerCtrl;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

// «Nueva descarga»: dirección, carpeta, nombre y conexiones
class DialogoAgregar : public wxDialog
{
public:
    // Si «delNavegador» es verdadero, los datos ya vienen llenos y hay un botón «Más tarde»
    DialogoAgregar(wxWindow* padre, const hnd::Ajustes& ajustes, const hnd::DatosDescarga& previa, bool delNavegador,
                   int64_t tamanoAnunciado = -1);

    hnd::DatosDescarga Datos() const;
    bool EmpezarAhora() const { return m_empezar; }

private:
    void AlAceptar(bool empezar);

    hnd::DatosDescarga m_previa;
    wxTextCtrl* m_url = nullptr;
    wxTextCtrl* m_nombre = nullptr;
    wxDirPickerCtrl* m_carpeta = nullptr;
    wxSpinCtrl* m_conexiones = nullptr;
    bool m_empezar = true;
};

// «Nuevo torrent» (un archivo .torrent o un enlace magnet) y «Elegir archivos» de uno que ya está en la lista
class DialogoTorrent : public wxDialog
{
public:
    enum class Modo { Nuevo, Elegir };
    DialogoTorrent(wxWindow* padre, const hnd::Ajustes& ajustes, const hnd::InfoTorrent& info, Modo modo, bool delNavegador = false,
                   const std::wstring& carpeta = L"");

    std::vector<bool> Elegidos() const;
    std::wstring Carpeta() const;
    bool Compartir() const;
    bool ElegirDespues() const;
    bool EmpezarAhora() const { return m_empezar; }

private:
    void Resumen();
    void Marcar(bool si);
    void AlAceptar(bool empezar);

    hnd::InfoTorrent m_info;
    Modo m_modo;
    wxDataViewListCtrl* m_lista = nullptr;
    wxStaticText* m_resumen = nullptr;
    wxDirPickerCtrl* m_carpeta = nullptr;
    wxCheckBox* m_compartir = nullptr;
    wxCheckBox* m_despues = nullptr;
    bool m_empezar = true;
};

// «Pegar enlaces en el recolector»: una lista pegada o un .txt (no empieza nada: los enlaces van al recolector)
class DialogoVarios : public wxDialog
{
public:
    DialogoVarios(wxWindow* padre, const hnd::Ajustes& ajustes);

    const std::vector<std::string>& Enlaces() const { return m_enlaces; }

private:
    void Contar();
    void AbrirTxt();
    void AlAceptar();

    wxTextCtrl* m_texto = nullptr;
    wxStaticText* m_cuenta = nullptr;
    std::vector<std::string> m_enlaces;
};

// «Enlaces de una página»: lee la página (o la lista de Internet Archive) y deja marcar qué bajar
class DialogoPagina : public wxDialog
{
public:
    DialogoPagina(wxWindow* padre, const hnd::Ajustes& ajustes, const std::wstring& url, bool buscarYa);
    ~DialogoPagina() override;

    std::vector<hnd::enlaces::Enlace> Elegidos() const;
    std::string Pagina() const { return m_urlPagina; }   // para mandarla como «Referer»
    std::wstring Carpeta() const;
    bool EmpezarAhora() const { return m_empezar; }

private:
    void Buscar();
    void AlTerminarBusqueda(bool ok, hnd::enlaces::Pagina p, const std::string& error, const std::string& url);
    void Llenar();
    void Resumen();
    void Marcar(bool si);
    bool Visible(const hnd::enlaces::Enlace& e) const;
    void AlAceptar(bool empezar);

    wxTextCtrl* m_url = nullptr;
    wxButton* m_buscar = nullptr;
    wxStaticText* m_estado = nullptr;
    wxChoice* m_filtro = nullptr;
    wxTextCtrl* m_contiene = nullptr;
    wxDataViewListCtrl* m_lista = nullptr;
    wxStaticText* m_resumen = nullptr;
    wxDirPickerCtrl* m_carpeta = nullptr;
    wxButton* m_descargar = nullptr;
    wxButton* m_luego = nullptr;
    std::vector<hnd::enlaces::Enlace> m_todos;
    std::vector<char> m_marcado;
    std::vector<size_t> m_visibles;
    std::shared_ptr<std::atomic<bool>> m_parar;
    std::string m_urlPagina;
    bool m_internetArchive = false;
    bool m_empezar = true;
};

class DialogoAjustes : public wxDialog
{
public:
    DialogoAjustes(wxWindow* padre, hnd::Ajustes& ajustes);

private:
    hnd::Ajustes& m_ajustes;
};

class DialogoNavegadores : public wxDialog
{
public:
    explicit DialogoNavegadores(wxWindow* padre);

private:
    void Actualizar();
    wxStaticText* m_estado = nullptr;
};
