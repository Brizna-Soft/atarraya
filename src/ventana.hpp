#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <memory>
#include <string>
#include <vector>

#include "ajustes.hpp"
#include "motor.hpp"

class wxDataViewCtrl;
class wxDataViewEvent;
class wxSplitterWindow;
class wxStaticText;
class ModeloLista;
class PanelDetalles;
class Bandeja;

class VentanaPrincipal : public wxFrame
{
public:
    VentanaPrincipal(hnd::Motor& motor, hnd::Ajustes& ajustes);
    ~VentanaPrincipal() override;

    void Mostrar();                          // la trae al frente (también desde el ícono junto al reloj)
    void Pedido(const std::string& json);   // del navegador o de otra copia del programa
    void NuevaDescarga(const hnd::DatosDescarga& previa, bool delNavegador, int64_t tamanoAnunciado = -1);
    void Salir();
    hnd::Motor& MotorDescargas() { return m_motor; }

private:
    void CrearMenu();
    void CrearBarra();
    void Refrescar();
    std::vector<uint64_t> Seleccion() const;
    uint64_t PrimeraSeleccion() const;
    void AlCerrar(wxCloseEvent& e);
    void AlTerminar(const hnd::Foto& f);
    void Avisar(const wxString& titulo, const wxString& texto, bool error = false);

    void Reanudar();
    void Pausar();
    void Quitar();
    void AbrirArchivo();
    void AbrirCarpeta();
    void CopiarEnlace();
    void CambiarEnlace();
    void CalcularHuella();
    void MenuContextual(wxDataViewEvent& e);
    void GuardarGeometria();

    hnd::Motor& m_motor;
    hnd::Ajustes& m_ajustes;
    wxDataViewCtrl* m_lista = nullptr;
    ModeloLista* m_modelo = nullptr;
    PanelDetalles* m_detalles = nullptr;
    wxSplitterWindow* m_division = nullptr;
    wxStaticText* m_resumen = nullptr;
    std::unique_ptr<Bandeja> m_bandeja;
    wxTimer m_reloj;
    bool m_saliendo = false;
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
};
