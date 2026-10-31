#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <memory>
#include <string>
#include <vector>

#include "ajustes.hpp"
#include "clicknload.hpp"
#include "motor.hpp"
#include "recolector.hpp"
#include "torrents.hpp"

class wxDataViewCtrl;
class wxDataViewEvent;
class wxSplitterWindow;
class wxStaticText;
class wxUpdateUIEvent;
class ModeloLista;
class PanelDetalles;
class Bandeja;
class VentanaRecolector;

class VentanaPrincipal : public wxFrame
{
public:
    VentanaPrincipal(hnd::Motor& motor, hnd::MotorTorrent& torrents, hnd::Recolector& recolector, hnd::Ajustes& ajustes);
    ~VentanaPrincipal() override;

    void Mostrar();                          // la trae al frente (también desde el ícono junto al reloj)
    void Pedido(const std::string& json);   // del navegador o de otra copia del programa
    void NuevaDescarga(const hnd::DatosDescarga& previa, bool delNavegador, int64_t tamanoAnunciado = -1);
    void AbrirRecolector(bool sinFoco);                     // la ventana del recolector de enlaces
    void AbrirPagina(const std::wstring& url, bool buscarYa);
    void AbrirTorrent();                                    // elegir un .torrent
    void AbrirTorrentDe(const std::wstring& ruta);          // un .torrent que ya está en el disco
    void AgregarMagnet(const std::string& magnet, bool delNavegador, const std::wstring& carpeta = L"");
    void Salir();
    hnd::Motor& MotorDescargas() { return m_motor; }
    hnd::MotorTorrent& MotorTorrents() { return m_torrents; }

protected:
    WXLRESULT MSWWindowProc(WXUINT mensaje, WXWPARAM w, WXLPARAM l) override;   // avisos de cambios en el portapapeles

private:
    void AplicarAjustes();                                  // portapapeles y Click'n'Load, según los ajustes
    void PortapapelesCambio();
    void AlRecibirCnL(std::vector<std::string> enlaces, std::wstring paquete, std::string origen);
    std::vector<uint64_t> ConfirmarRecolector(const std::vector<hnd::EnlaceRecolectado>& lista, const std::wstring& carpeta,
                                              bool porPaquete, bool empezar);
    void CrearMenu();
    void CrearBarra();
    void Refrescar();
    std::vector<uint64_t> Seleccion() const;
    uint64_t PrimeraSeleccion() const;
    bool BuscarFoto(uint64_t id, hnd::Foto& f);
    void AlCerrar(wxCloseEvent& e);
    void AlTerminar(const hnd::Foto& f);
    void AlTerminarTorrent(const hnd::Foto& f, const std::vector<std::wstring>& archivos);
    void Avisar(const wxString& titulo, const wxString& texto, bool error = false);
    void AgregarTorrent(const hnd::InfoTorrent& info, const std::string& datos, bool delNavegador, const std::wstring& carpeta);
    uint64_t AgregarEnlace(const std::string& url, const std::wstring& carpeta, const std::string& referer, bool empezar,
                           bool& yaEstaba);

    void Reanudar();
    void Pausar();
    void Quitar();
    void AbrirArchivo();
    void AbrirCarpeta();
    void CopiarEnlace();
    void CambiarEnlace();
    void CalcularHuella();
    void ElegirArchivos(uint64_t id);
    void CambiarCompartir();
    void MenuContextual(wxDataViewEvent& e);
    void GuardarGeometria();

    hnd::Motor& m_motor;
    hnd::MotorTorrent& m_torrents;
    hnd::Recolector& m_recolector;
    hnd::Ajustes& m_ajustes;
    VentanaRecolector* m_ventanaRecolector = nullptr;
    std::unique_ptr<hnd::ServidorCnL> m_cnl;
    wxString m_estadoCnl;
    bool m_oyendoPortapapeles = false;
    size_t m_ultimoPortapapeles = 0;        // huella del último texto visto (no se guarda el texto)
    std::wstring m_copiadoPorAtarraya;      // lo que copió el propio programa: no se recolecta
    size_t m_cuentaRecolector = (size_t)-1;
    wxDataViewCtrl* m_lista = nullptr;
    ModeloLista* m_modelo = nullptr;
    PanelDetalles* m_detalles = nullptr;
    wxSplitterWindow* m_division = nullptr;
    wxStaticText* m_resumen = nullptr;
    std::unique_ptr<Bandeja> m_bandeja;
    wxTimer m_reloj;
    bool m_saliendo = false;
    bool m_ocupada = false;                 // hay una ventana de elegir archivos abierta
    std::vector<uint64_t> m_porElegir;      // torrents que esperan que la persona elija archivos
    std::vector<uint64_t> m_avisadosElegir; // ya se avisó con un globo
};

// Ids de los comandos (los usa también el menú del ícono junto al reloj)
enum
{
    ID_AGREGAR = wxID_HIGHEST + 100,
    ID_REANUDAR,
    ID_PAUSAR,
    ID_QUITAR,
    ID_ABRIR,
    ID_CARPETA,
    ID_COPIAR,
    ID_CAMBIAR,
    ID_HUELLA,
    ID_REANUDAR_TODAS,
    ID_PAUSAR_TODAS,
    ID_LIMPIAR,
    ID_NAVEGADORES,
    ID_AJUSTES,
    ID_MOSTRAR,
    ID_VARIOS,
    ID_PAGINA,
    ID_TORRENT,
    ID_ELEGIR,
    ID_COMPARTIR,
};
