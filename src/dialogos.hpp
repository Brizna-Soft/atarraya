#pragma once

#include <wx/dialog.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "ajustes.hpp"
#include "enlaces.hpp"
#include "flujos.hpp"
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

// «Bajar video»: un video por partes (HLS o DASH) que pasó la extensión. Lee su lista con las cookies y el referer de su
// página, muestra las calidades (la mejor ya elegida) y avisa si está protegido o es en vivo (entonces no se baja). En
// DASH, y en HLS con el audio aparte, el video y el audio quedan en dos archivos sin unir: Atarraya no trae FFmpeg.
class DialogoVideo : public wxDialog
{
public:
    struct Pedido
    {
        std::string formato;   // "hls" o "dash"
        std::string url, referer, cookies, agente;
        std::wstring titulo;
    };
    DialogoVideo(wxWindow* padre, const hnd::Ajustes& ajustes, Pedido pedido);
    ~DialogoVideo() override;

    std::vector<hnd::DatosDescarga> Descargas() const;
    bool EmpezarAhora() const { return m_empezar; }

private:
    void Leer();
    void AlLeer(bool ok, const std::string& texto, const std::string& urlFinal, bool local, const std::string& error);
    void NoSePuede(const wxString& estado, const wxString& nota);
    void AlAceptar(bool empezar);

    Pedido m_pedido;
    int m_conexiones;
    std::shared_ptr<std::atomic<bool>> m_parar;
    wxStaticText* m_estado = nullptr;
    wxTextCtrl* m_nombre = nullptr;
    wxStaticText* m_etiquetaCalidad = nullptr;
    wxChoice* m_calidad = nullptr;   // HLS: las calidades; DASH: el video
    wxStaticText* m_etiquetaAudio = nullptr;
    wxChoice* m_audio = nullptr;     // DASH: el audio
    wxStaticText* m_nota = nullptr;
    wxDirPickerCtrl* m_carpeta = nullptr;
    wxButton* m_descargar = nullptr;
    wxButton* m_luego = nullptr;
    bool m_listo = false, m_local = false, m_empezar = true;
    std::string m_urlLista;          // ya con sus redirecciones
    hnd::flujos::ListaHls m_hls;
    hnd::flujos::ManifiestoDash m_dash;
    std::vector<int> m_ordenCalidad, m_ordenAudio;   // de la mejor a la peor
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
