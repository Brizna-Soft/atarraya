#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ajustes.hpp"
#include "clicknload.hpp"
#include "contrasenas.hpp"
#include "deunaenuna.hpp"
#include "hilos.hpp"
#include "jicaro.hpp"
#include "motor.hpp"
#include "recolector.hpp"
#include "torrents.hpp"

class wxButton;
class wxDataViewCtrl;
class wxPanel;
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
    // Una consulta de la extensión («cola», «emparejar», «toma»): se contesta enseguida, en JSON, sin cambiar nada
    std::string Consulta(const std::string& json);
    // Un video que pasó la extensión: un archivo (MP4, WebM...) va como cualquier descarga; HLS o DASH, a «Bajar video»
    void PedidoVideo(const std::string& formato, const std::string& url, const std::wstring& titulo, const std::string& referer,
                     const std::string& pagina, const std::string& cookies, const std::string& agente, const std::string& tipoMime,
                     int64_t tamano);
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
    void AlRecibirCnL(std::vector<std::string> enlaces, std::wstring paquete, std::string origen, std::vector<std::string> contrasenas);
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
                           bool& yaEstaba, const std::wstring& paquete = L"", const std::string& grupo = "");

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

    // «De una en una»: páginas de sitios de descargas, de a una en el navegador
    // Pone las páginas en la cola (y abre la primera si «abrir»). false si la persona prefirió dejarlas en el recolector.
    // «tanda»: los grupos de los paquetes de esta tanda (los comparten con las descargas directas del mismo paquete).
    bool EmpezarDeUnaEnUna(const std::vector<const hnd::EnlaceRecolectado*>& sitios, const std::wstring& carpeta, bool porPaquete,
                           bool abrir, wxWindow* padre, hnd::jicaro::Tanda* tanda = nullptr);
    void CrearBarraSitios(wxWindow* padre);
    void ActualizarBarraSitios();
    // La abre en el navegador: por la extensión, si está conectada (solo en esas pestañas lee los avisos de espera);
    // si no, o si no contesta enseguida, como antes (en la copia de prueba, solo la anota)
    void AbrirParte(int i, bool avisar);
    void RevisarPorAbrir();                // las que la extensión no abrió a tiempo: como antes
    void RevisarPausas();                  // «sin confirmar» en pausa; las confirmadas, reanudadas de verdad
    void Avanzar();                        // terminó o se saltó una: abre la siguiente, o cierra la cola si no queda nada
    // La baja Atarraya, con una conexión. Con «toma», el navegador sigue bajando la suya mientras Atarraya prueba: si
    // Atarraya no puede (una página, un 403 o 410, un enlace de un solo uso), la deja en el navegador.
    void TomarParte(int i, hnd::DatosDescarga d, const std::string& toma = "");
    void TomaANavegador(int i, bool recordarSitio);
    void RevisarTomas();                   // ¿Atarraya pudo seguir las descargas que prueba?
    void RevisarEsperas();                 // esperas que terminaron: se abre la página otra vez y se avisa
    void PedidoNavegador(const std::string& toma, const std::string& estado, const std::string& url, const std::string& referer,
                         const std::string& pagina, const std::wstring& nombre, int64_t bajado, int64_t total,
                         const std::string& ruta);
    void PedidoEspera(const std::string& paginaCola, const std::string& clase, int64_t segundos);
    // Lo que bajó el navegador entra al paquete: se revisa y se anota su archivo (ruta e identidad) y después se pasa a
    // la carpeta de las otras partes y a la lista, como terminado. Si algo no coincide, no se mueve y se avisa.
    void TerminarParteDelNavegador(int i, const std::string& ruta, int64_t tamano);
    void MoverParteDelNavegador(int i);
    void NoSeMovio(int i, const std::string& motivo);
    bool PreguntarSiEsParte(int i, const std::string& url, const std::wstring& nombre);
    // Las que no bajaron vuelven al recolector sin perder ninguna (primero se guarda el recolector, después la cola).
    // false si el recolector no se pudo guardar: entonces la cola queda como estaba.
    bool DevolverYVaciar(size_t& devueltas);
    void GuardarCola();                    // con la lista de descargas, de una vez; y avisa una vez si no se pudo
    void RevisarPartes();                  // descargas de partes que la persona quitó de la lista
    // Conciliar (ver deunaenuna.hpp): lo que dice la lista de la descarga de cada parte, y aplicar lo que resultó
    // (quitar las que sobran, guardar, anotarlo y, con «avanzar», abrir las que volvieron a la cola)
    std::function<hnd::EnLaLista(uint64_t)> EnLaListaDe();
    void AplicarConciliacion(const hnd::Conciliacion& c, bool avanzar);
    void PedidoSeguidas(const nlohmann::json& j);   // lo que cuenta una extensión al conectarse
    // Enlaces de sitios de descargas agregados a mano o desde una página: de una en una. Si la persona no empieza
    // (por ejemplo, para conectar antes la extensión), quedan en el recolector.
    void MandarDeUnaEnUna(const std::vector<hnd::EnlaceRecolectado>& sitios, const std::wstring& carpeta, bool abrir);

    // Descargas cuyo sitio respondió con una página en vez del archivo: se ofrece abrirlas en el navegador
    void OfrecerAbrirPaginas();
    void AbrirElegidasEnNavegador();

    // Jícaro: cuando termina todo un paquete (o un .rar, .zip o .7z suelto), lo descomprime. De a un comprimido a la
    // vez; las partes nunca se borran solas.
    struct TrabajoJicaro
    {
        std::wstring nombre;                             // el del paquete o el del comprimido, para mostrar
        std::wstring exe, primera, destino;
        std::vector<hnd::jicaro::ParteBajada> partes;    // solo las de este juego (por id), con el archivo que eran
        // De qué paquete es (sus contraseñas están guardadas con esa clave, cifradas): nunca se guarda aquí la contraseña
        std::string clave;
        size_t intento = 0;                              // qué contraseña del paquete se prueba (en orden, como mucho 5)
        bool conContrasena = false;                      // a esta vez se le pasó una
    };
    struct ResultadoJicaro
    {
        TrabajoJicaro t;
        hnd::jicaro::Resultado r = hnd::jicaro::Resultado::Error;
        wxString texto;
        bool borradas = false;
    };
    // Si con esta descarga terminó todo su paquete, lo que hay que descomprimir (vacío si falta algo o ya se hizo)
    std::vector<TrabajoJicaro> PrepararJicaro(const hnd::DatosDescarga& d);
    // Guarda la lista ya, con la marca «descomprimido» de las partes como quedó
    void AnotarJicaro();
    // Al abrir: los juegos que quedaron «en curso» se le pasan otra vez a Jícaro
    void RetomarJicaro();
    // Las que ya estaban en la lista y entran a un paquete de la tanda (y los grupos que se juntan)
    void AplicarTanda(const hnd::jicaro::Tanda& tanda);
    // Al terminar una descarga (ya marcada como bajada de internet): qué archivo quedó, para «Borrar las partes»
    void GuardarIdentidad(uint64_t descarga, const std::wstring& ruta);
    void EncolarJicaro(std::vector<TrabajoJicaro> trabajos);
    void SiguienteJicaro();
    void AlTerminarJicaro(unsigned long codigo);
    void MostrarResultadoJicaro(TrabajoJicaro t, hnd::jicaro::Resultado r);
    void CrearBarraJicaro(wxWindow* padre);
    void ActualizarBarraJicaro();
    void BorrarPartes();   // solo cuando la persona lo pulsa: a la Papelera
    // Contraseñas de los paquetes (cifradas). «clave»: el grupo del paquete, o «descarga:<id>» para una suelta.
    std::string ClaveDeContrasena(const hnd::DatosDescarga& d) const;
    // Cuando ya se descomprimieron todos los juegos del paquete, su contraseña se borra (ver ventana.cpp)
    void OlvidarContrasenaSiTermino(const std::string& clave);
    bool PasarContrasena(const std::string& de, const std::string& a);
    void CambiarClaveDeJicaro(const std::string& de, const std::string& a);
    void RevisarLosQueEsperanAlRecolector();
    std::set<uint64_t> m_marcadas;   // descargas con la marca de internet ya puesta (Jícaro solo abre esas)
    std::set<std::string> m_esperanRecolector;   // grupos que esperan a que salgan sus enlaces del recolector
    uint64_t m_versionRecolectorVista = 0;
    // La pide (solo cuando la persona pulsa algo) y la guarda para ese paquete; si estaba esperando, Jícaro sigue
    // «clave» y «nombre» van por valor: pueden ser los de un aviso que se mueve y se borra al seguir con su juego
    bool EscribirContrasena(const std::string clave, const std::wstring nombre, bool incorrecta);
    void ContrasenaDelPaquete();           // desde el menú de la lista
    void RecordarContrasenas(const std::vector<std::string>& lista);   // solo si la persona lo encendió en Ajustes
    void GuardarContrasenas();

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
    hnd::hilos::Vivo m_vivo = hnd::hilos::NuevoVivo();   // falso cuando la ventana se destruye
    bool m_ocupada = false;                 // hay una ventana de elegir archivos abierta
    std::vector<uint64_t> m_porElegir;      // torrents que esperan que la persona elija archivos
    std::vector<uint64_t> m_avisadosElegir; // ya se avisó con un globo

    hnd::DeUnaEnUna m_sitios;
    hnd::SitiosNavegador m_sitiosNavegador; // sitios donde la descarga final solo funciona en el navegador
    // Descargas del navegador que no son de una parte (la extensión las sigue igual): ahí sigue el navegador
    std::set<std::string> m_tomasSinParte;
    // Páginas que se le pidió abrir a la extensión y todavía no contestó que las abrió (por número de orden)
    struct PaginaPorAbrir
    {
        int parte = -1;
        std::string url;
        uint64_t desde = 0;   // GetTickCount64
    };
    std::map<int64_t, PaginaPorAbrir> m_porAbrir;
    int64_t m_ultimaOrden = 0;
    int64_t m_abiertoDesde = 0;             // cuándo se abrió Atarraya (para soltar las retenidas al abrir)
    bool m_colaSinGuardar = false;          // ya se avisó que la cola no se pudo guardar
    wxString m_resumenSitios;               // cómo terminó la última cola (hasta que la persona lo cierra)
    wxPanel* m_barraSitios = nullptr;
    wxStaticText* m_textoSitios = nullptr;
    wxButton* m_bOtraVez = nullptr;
    wxButton* m_bSiguiente = nullptr;
    wxButton* m_bSaltar = nullptr;
    wxButton* m_bParar = nullptr;
    wxButton* m_bCerrarSitios = nullptr;

    std::vector<uint64_t> m_paginasPorAbrir;   // descargas que recibieron una página: falta ofrecer abrirlas
    bool m_ofreciendoPaginas = false;

    std::deque<TrabajoJicaro> m_colaJicaro;
    bool m_jicaroOcupado = false;
    TrabajoJicaro m_jicaroActual;
    std::deque<ResultadoJicaro> m_resultadosJicaro;   // la barra muestra el primero
    std::set<std::string> m_jicaroVistos;             // juegos (o descargas sueltas) que ya pasaron a Jícaro
    std::set<std::wstring> m_destinosJicaro;          // carpetas ya elegidas (en minúsculas), aunque aún no existan
    std::set<std::wstring> m_marcando;                // archivos que Windows todavía marca como bajados de internet
    hnd::contrasenas::Guardadas m_contrasenas;        // las de cada paquete, cifradas (contrasenas.json)
    wxPanel* m_barraJicaro = nullptr;
    wxStaticText* m_textoJicaro = nullptr;
    wxButton* m_bCarpetaJicaro = nullptr;
    wxButton* m_bBorrarPartes = nullptr;
    wxButton* m_bContrasenaJicaro = nullptr;
    wxButton* m_bPaginaJicaro = nullptr;
    wxButton* m_bCerrarJicaro = nullptr;
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
    ID_ABRIR_EN_NAVEGADOR,
    ID_CONTRASENA,
};
