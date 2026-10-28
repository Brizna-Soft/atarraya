#pragma once

#include <wx/dialog.h>

#include "ajustes.hpp"
#include "motor.hpp"

class wxTextCtrl;
class wxDirPickerCtrl;
class wxSpinCtrl;
class wxCheckBox;
class wxStaticText;

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
