#include "torrents.hpp"

#ifdef CON_TORRENTS
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/write_resume_data.hpp>
#endif

#include <windows.h>
#include <objbase.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <mutex>
#include <set>
#include <thread>

#include "nombres.hpp"
#include "sistema.hpp"
#include "util.hpp"
#include "version.h"

using json = nlohmann::json;

namespace
{
    // Mensajes que dejan los hilos que mandan archivos a la Papelera (viven más que el motor si hace falta)
    struct Buzon
    {
        std::mutex m;
        std::vector<std::wstring> avisos;
    };
}

#ifdef CON_TORRENTS

namespace
{
    const char* NombreEstado(hnd::Estado e)
    {
        switch (e)
        {
        case hnd::Estado::EnCola: return "cola";
        case hnd::Estado::Iniciando: return "iniciando";
        case hnd::Estado::Descargando: return "descargando";
        case hnd::Estado::Pausada: return "pausada";
        case hnd::Estado::Completada: return "completada";
        case hnd::Estado::Error: return "error";
        case hnd::Estado::Compartiendo: return "compartiendo";
        }
        return "pausada";
    }

    hnd::Estado EstadoDeNombre(const std::string& s)
    {
        // Lo que estaba bajando sigue solo al volver a abrir el programa
        if (s == "cola" || s == "iniciando" || s == "descargando") return hnd::Estado::EnCola;
        if (s == "completada") return hnd::Estado::Completada;
        if (s == "compartiendo") return hnd::Estado::Compartiendo;
        if (s == "error") return hnd::Estado::Error;
        return hnd::Estado::Pausada;
    }

    std::string Hex(const char* d, size_t n)
    {
        static const char* cifras = "0123456789abcdef";
        std::string r;
        for (size_t i = 0; i < n; i++)
        {
            auto c = (unsigned char)d[i];
            r += cifras[c >> 4];
            r += cifras[c & 15];
        }
        return r;
    }

    // Texto de un error del sistema: puede venir en la página de códigos de Windows en vez de UTF-8
    std::string TextoDeSistema(const std::string& s)
    {
        if (s.empty()) return s;
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0) > 0) return s;
        int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), w.data(), n);
        return hnd::Utf8(w);
    }

    // Manda archivos a la Papelera en otro hilo, con reintentos: libtorrent puede tardar en soltarlos.
    // Después quita las carpetas que quedaron vacías (RemoveDirectory no borra carpetas con algo dentro).
    void ALaPapelera(std::vector<std::wstring> archivos, std::vector<std::wstring> carpetas, std::wstring nombre,
                     std::shared_ptr<Buzon> buzon)
    {
        std::thread([archivos = std::move(archivos), carpetas = std::move(carpetas), nombre = std::move(nombre), buzon]() mutable {
            HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            Sleep(700);
            for (int intento = 0; intento < 20 && !archivos.empty(); intento++)
            {
                if (intento > 0) Sleep(1000);
                std::vector<std::wstring> quedan;
                for (auto& a : archivos)
                {
                    if (!hnd::sistema::MandarAPapelera(a)) quedan.push_back(a);
                }
                archivos.swap(quedan);
            }
            std::sort(carpetas.begin(), carpetas.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
            for (auto& c : carpetas) RemoveDirectoryW(c.c_str());
            if (!archivos.empty())
            {
                hnd::Registrar("torrent quitado: " + std::to_string(archivos.size()) + " archivos no se pudieron mandar a la Papelera");
                std::lock_guard g(buzon->m);
                buzon->avisos.push_back(L"No se pudieron mandar a la Papelera " + std::to_wstring(archivos.size())
                                        + L" archivos de «" + nombre + L"». Puede que otro programa los tenga abiertos.");
            }
            if (SUCCEEDED(com)) CoUninitialize();
        }).detach();
    }

    using Reloj = std::chrono::steady_clock;

    const char* Escuchar = "0.0.0.0:6881,[::]:6881";
    const char* NodosDht = "dht.libtorrent.org:25401,router.bittorrent.com:6881,dht.transmissionbt.com:6881,router.utorrent.com:6881";

    struct Entrada
    {
        uint64_t id = 0;
        std::wstring nombre, carpeta;
        std::string magnet, v1, v2;
        hnd::Estado estado = hnd::Estado::EnCola;
        bool compartir = false;
        bool eligeArchivos = false;   // magnet: la persona quiere elegir los archivos cuando lleguen los datos
        std::string error;
        int64_t agregada = 0, terminada = 0;
        int64_t tamano = -1, hecho = 0, subido = 0;
        int archivos = 0;
        lt::torrent_handle h;
        lt::torrent_status st;
        bool hayEstado = false;
        std::vector<uint8_t> mapa;
        Reloj::time_point ultimoResumen = Reloj::now();
    };

    void Huellas(const lt::info_hash_t& ih, std::string& v1, std::string& v2)
    {
        v1.clear();
        v2.clear();
        if (ih.has_v1()) v1 = Hex(ih.v1.data(), (size_t)ih.v1.size());
        if (ih.has_v2()) v2 = Hex(ih.v2.data(), (size_t)ih.v2.size());
    }

    std::string TextoErrorTorrent(const lt::error_code& ec)
    {
        namespace errc = boost::system::errc;
        int v = ec.value();
        bool sistema = ec.category() == boost::system::system_category();
        if (ec == errc::no_space_on_device || (sistema && (v == ERROR_DISK_FULL || v == ERROR_HANDLE_DISK_FULL)))
            return "No hay espacio suficiente en el disco.";
        if (ec == errc::permission_denied || (sistema && v == ERROR_ACCESS_DENIED))
            return "No hay permiso para guardar en la carpeta elegida.";
        if (ec == errc::no_such_file_or_directory || (sistema && (v == ERROR_FILE_NOT_FOUND || v == ERROR_PATH_NOT_FOUND)))
            return "No se encuentran los archivos del torrent. ¿Se movieron o se borraron?";
        if (sistema && (v == ERROR_SHARING_VIOLATION || v == ERROR_LOCK_VIOLATION))
            return "Otro programa tiene abierto un archivo del torrent.";
        return "El torrent se detuvo por un problema con los archivos o con el disco (detalle: " + TextoDeSistema(ec.message()) + ").";
    }

    std::vector<uint8_t> Mapa(const lt::typed_bitfield<lt::piece_index_t>& piezas)
    {
        int n = piezas.size();
        if (n <= 0) return {};
        int tramos = std::min(n, 200);
        std::vector<uint8_t> r((size_t)tramos);
        for (int t = 0; t < tramos; t++)
        {
            int a = (int)((int64_t)t * n / tramos), b = (int)((int64_t)(t + 1) * n / tramos);
            int tiene = 0;
            for (int i = a; i < b; i++)
            {
                if (piezas.get_bit(lt::piece_index_t(i))) tiene++;
            }
            r[(size_t)t] = (uint8_t)(b > a ? tiene * 255 / (b - a) : 0);
        }
        return r;
    }

    // Cuántos archivos ve la persona (los de relleno de los torrents v2 no cuentan)
    int ArchivosVisibles(const lt::file_storage& fs)
    {
        int n = 0;
        for (auto i : fs.file_range())
        {
            if (!fs.pad_file_at(i)) n++;
        }
        return n;
    }

    // ¿Ya se conocen los archivos? (un magnet empieza sin ellos)
    bool ConDatos(const Entrada& e) { return e.hayEstado ? e.st.has_metadata : e.archivos > 0; }
    // Llegaron los datos y falta que la persona elija qué bajar: no corre hasta entonces
    bool EsperaEleccion(const Entrada& e) { return e.eligeArchivos && ConDatos(e); }

    std::wstring NombrePorDefecto(const std::string& v1, const std::string& v2)
    {
        std::string h = v1.empty() ? v2 : v1;
        return L"Torrent " + hnd::Ancho(h.substr(0, 8));
    }
}

struct hnd::MotorTorrent::Datos
{
    std::wstring carpetaTorrents;
    std::wstring archivoLista;
    std::unique_ptr<lt::session> ses;
    std::vector<std::unique_ptr<Entrada>> lista;
    uint64_t siguiente = BaseIdTorrent + 1;
    bool sucio = false;
    int64_t ultimoGuardado = 0;
    bool red = false;          // escuchando y con DHT
    bool dhtUsada = false;
    Reloj::time_point sinActividad{};
    bool contando = false;
    int64_t limBajada = -1, limSubida = -1;   // los últimos que se le pasaron a libtorrent
    int pendientes = 0;        // datos de reanudación pedidos y sin llegar
    std::vector<uint64_t> porElegir;
    std::shared_ptr<Buzon> buzon = std::make_shared<Buzon>();

    std::wstring RutaReanudar(uint64_t id) const { return carpetaTorrents + L"\\" + std::to_wstring(id) + L".resume"; }

    Entrada* Encontrar(uint64_t id)
    {
        for (auto& e : lista)
        {
            if (e->id == id) return e.get();
        }
        return nullptr;
    }

    Entrada* PorHandle(const lt::torrent_handle& h)
    {
        for (auto& e : lista)
        {
            if (e->h == h) return e.get();
        }
        return nullptr;
    }

    lt::settings_pack Ajustes(bool conRed, int64_t bajada, int64_t subida) const
    {
        lt::settings_pack p;
        p.set_str(lt::settings_pack::user_agent, APP_NOMBRE "/" APP_VERSION);
        p.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage);
        p.set_bool(lt::settings_pack::enable_upnp, false);     // no se tocan los puertos del router
        p.set_bool(lt::settings_pack::enable_natpmp, false);
        p.set_bool(lt::settings_pack::enable_lsd, false);      // no se anuncia nada en la red de la casa
        p.set_bool(lt::settings_pack::enable_dht, conRed);
        p.set_str(lt::settings_pack::listen_interfaces, conRed ? Escuchar : "");
        p.set_str(lt::settings_pack::dht_bootstrap_nodes, NodosDht);
        p.set_int(lt::settings_pack::stop_tracker_timeout, 2);
        p.set_int(lt::settings_pack::download_rate_limit, (int)std::clamp<int64_t>(bajada, 0, INT_MAX));
        p.set_int(lt::settings_pack::upload_rate_limit, (int)std::clamp<int64_t>(subida, 0, INT_MAX));
        return p;
    }

    void Red(bool conRed)
    {
        if (!ses || red == conRed) return;
        lt::settings_pack p;
        p.set_bool(lt::settings_pack::enable_dht, conRed);
        p.set_str(lt::settings_pack::listen_interfaces, conRed ? Escuchar : "");
        ses->apply_settings(std::move(p));
        red = conRed;
        if (conRed) dhtUsada = true;
        Registrar(conRed ? "torrents: conectado" : "torrents: sin conexiones (nada activo)");
    }

    void PedirResumen(Entrada& e, bool soloSiCambio)
    {
        if (!e.h.is_valid()) return;
        lt::resume_data_flags_t f = lt::torrent_handle::save_info_dict;
        if (soloSiCambio) f |= lt::torrent_handle::only_if_modified;
        e.h.save_resume_data(f);
        e.ultimoResumen = Reloj::now();
        pendientes++;
    }

    bool AgregarASesion(Entrada& e)
    {
        lt::add_torrent_params atp;
        bool ok = false;
        std::string buf;
        if (LeerArchivo(RutaReanudar(e.id), buf) && !buf.empty())
        {
            lt::error_code ec;
            atp = lt::read_resume_data(lt::span<char const>(buf.data(), (std::ptrdiff_t)buf.size()), ec);
            ok = !ec;
        }
        if (!ok && !e.magnet.empty())
        {
            lt::error_code ec;
            atp = lt::parse_magnet_uri(e.magnet, ec);
            ok = !ec;
        }
        if (!ok)
        {
            e.estado = Estado::Error;
            e.error = "Faltan los datos de este torrent. Quítalo de la lista y vuelve a agregarlo.";
            return false;
        }
        atp.save_path = Utf8(e.carpeta);
        atp.flags &= ~lt::torrent_flags::auto_managed;
        atp.flags |= lt::torrent_flags::paused;
        if (e.eligeArchivos && !atp.ti) atp.flags |= lt::torrent_flags::default_dont_download;
        lt::error_code ec;
        e.h = ses->add_torrent(std::move(atp), ec);
        if (ec)
        {
            e.estado = Estado::Error;
            e.error = "No se pudo volver a abrir este torrent (" + TextoDeSistema(ec.message()) + ").";
            return false;
        }
        return true;
    }

    void AsegurarSesion()
    {
        if (ses) return;
        lt::session_params sp;
        std::string estado;
        if (LeerArchivo(carpetaTorrents + L"\\sesion.dat", estado) && !estado.empty())
        {
            try
            {
                sp = lt::read_session_params(lt::span<char const>(estado.data(), (std::ptrdiff_t)estado.size()),
                                             lt::session::save_dht_state);
            }
            catch (...) {}
        }
        sp.settings = Ajustes(false, limBajada < 0 ? 0 : limBajada, limSubida < 0 ? 0 : limSubida);
        ses = std::make_unique<lt::session>(std::move(sp));
        red = false;
        Registrar(std::string("torrents: sesión de libtorrent ") + lt::version());
        for (auto& e : lista)
        {
            if (!e->h.is_valid()) AgregarASesion(*e);
        }
    }

    // Que corra en libtorrent (sin cambiar el estado que ve la persona)
    void Correr(Entrada& e)
    {
        if (!e.h.is_valid()) return;
        Red(true);
        e.h.unset_flags(lt::torrent_flags::auto_managed);
        e.h.clear_error();
        e.h.resume();
    }

    void Arrancar(Entrada& e)
    {
        e.error.clear();
        e.estado = Estado::Iniciando;
        Correr(e);
        sucio = true;
    }

    void Detener(Entrada& e, Estado nuevo)
    {
        if (e.h.is_valid()) e.h.pause();
        e.estado = nuevo;
        PedirResumen(e, true);
        sucio = true;
    }

    std::vector<std::wstring> ArchivosElegidos(Entrada& e)
    {
        std::vector<std::wstring> r;
        if (!e.h.is_valid()) return r;
        auto ti = e.h.torrent_file();
        if (!ti) return r;
        auto prioridades = e.h.get_file_priorities();
        const auto& fs = ti->files();
        std::string base = Utf8(e.carpeta);
        for (auto i : fs.file_range())
        {
            if (fs.pad_file_at(i)) continue;
            if (static_cast<int>(i) < (int)prioridades.size() && prioridades[static_cast<std::size_t>(static_cast<int>(i))] == lt::dont_download) continue;
            r.push_back(Ancho(fs.file_path(i, base)));
        }
        return r;
    }

    void Terminar(Entrada& e, MotorTorrent& motor)
    {
        if (e.compartir)
        {
            e.estado = Estado::Compartiendo;
        }
        else
        {
            if (e.h.is_valid()) e.h.pause();
            e.estado = Estado::Completada;
        }
        bool avisar = e.terminada == 0;
        if (avisar) e.terminada = AhoraUnix();
        PedirResumen(e, false);
        sucio = true;
        if (avisar)
        {
            Registrar("torrent " + std::to_string(e.id - BaseIdTorrent) + ": terminado");
            if (motor.alTerminar)
            {
                Foto f;
                motor.Buscar(e.id, f);
                motor.alTerminar(f, ArchivosElegidos(e));
            }
        }
    }

    void Actualizar(Entrada& e, MotorTorrent& motor)
    {
        const auto& st = e.st;
        if (st.has_metadata)
        {
            e.tamano = st.total_wanted;
            if (!st.name.empty()) e.nombre = Ancho(st.name);
        }
        e.hecho = st.total_wanted_done;
        e.subido = st.all_time_upload;
        e.mapa = Mapa(st.pieces);
        if (st.errc)
        {
            if (e.estado != Estado::Error)
            {
                e.error = TextoErrorTorrent(st.errc);
                Registrar("torrent " + std::to_string(e.id - BaseIdTorrent) + ": error: " + e.error);
                Detener(e, Estado::Error);
            }
            return;
        }
        if (st.flags & lt::torrent_flags::paused) return;   // el estado lo pone el programa
        // Un magnet que espera la elección corre solo para traer los datos; al llegar se pausa (metadata_received_alert)
        if (e.eligeArchivos && st.has_metadata) return;
        if (st.state == lt::torrent_status::checking_files || st.state == lt::torrent_status::checking_resume_data
            || st.state == lt::torrent_status::downloading_metadata)
        {
            if (e.estado != Estado::Compartiendo) e.estado = Estado::Iniciando;
            return;
        }
        if (st.is_finished)
        {
            if (e.estado != Estado::Compartiendo || e.terminada == 0) Terminar(e, motor);
            return;
        }
        e.estado = Estado::Descargando;
    }

    void ProcesarAlertas(MotorTorrent& motor)
    {
        std::vector<lt::alert*> alertas;
        ses->pop_alerts(&alertas);
        for (lt::alert* a : alertas)
        {
            if (auto* su = lt::alert_cast<lt::state_update_alert>(a))
            {
                for (const auto& st : su->status)
                {
                    Entrada* e = PorHandle(st.handle);
                    if (!e) continue;
                    e->st = st;
                    e->hayEstado = true;
                    Actualizar(*e, motor);
                }
            }
            else if (auto* sr = lt::alert_cast<lt::save_resume_data_alert>(a))
            {
                pendientes = std::max(0, pendientes - 1);
                Entrada* e = PorHandle(sr->handle);
                if (!e) continue;
                auto buf = lt::write_resume_data_buf(sr->params);
                CreateDirectoryW(carpetaTorrents.c_str(), nullptr);
                EscribirSeguro(RutaReanudar(e->id), std::string(buf.begin(), buf.end()));
            }
            else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a))
            {
                pendientes = std::max(0, pendientes - 1);
            }
            else if (auto* mr = lt::alert_cast<lt::metadata_received_alert>(a))
            {
                Entrada* e = PorHandle(mr->handle);
                if (!e) continue;
                if (auto ti = e->h.torrent_file())
                {
                    e->nombre = Ancho(ti->name());
                    e->archivos = ArchivosVisibles(ti->files());
                    e->tamano = ti->total_size();
                }
                e->magnet = lt::make_magnet_uri(e->h);
                if (e->eligeArchivos)
                {
                    e->h.pause();
                    e->estado = Estado::Pausada;
                    porElegir.push_back(e->id);
                }
                PedirResumen(*e, false);
                sucio = true;
            }
            else if (auto* fe = lt::alert_cast<lt::file_error_alert>(a))
            {
                Registrar("torrent: error de archivo: " + TextoDeSistema(fe->error.message()));
            }
            else if (auto* lf = lt::alert_cast<lt::listen_failed_alert>(a))
            {
                Registrar("torrents: no se pudo escuchar en " + lf->address.to_string() + ": " + TextoDeSistema(lf->error.message()));
            }
        }
    }

    void GuardarLista()
    {
        json l = json::array();
        for (auto& e : lista)
        {
            l.push_back({
                { "id", e->id }, { "nombre", Utf8(e->nombre) }, { "carpeta", Utf8(e->carpeta) }, { "magnet", e->magnet },
                { "v1", e->v1 }, { "v2", e->v2 }, { "estado", NombreEstado(e->estado) }, { "compartir", e->compartir },
                { "eligeArchivos", e->eligeArchivos }, { "error", e->error }, { "agregada", e->agregada },
                { "terminada", e->terminada }, { "tamano", e->tamano }, { "hecho", e->hecho }, { "subido", e->subido },
                { "archivos", e->archivos },
            });
        }
        json todo = { { "version", 1 }, { "siguienteId", siguiente }, { "torrents", l } };
        if (EscribirSeguro(archivoLista, todo.dump(1)))
        {
            sucio = false;
            ultimoGuardado = AhoraUnix();
        }
    }
};

hnd::MotorTorrent::MotorTorrent(std::wstring carpetaDatos) : m(std::make_unique<Datos>())
{
    m->carpetaTorrents = carpetaDatos + L"\\torrents";
    m->archivoLista = carpetaDatos + L"\\torrents.json";
}

hnd::MotorTorrent::~MotorTorrent()
{
    Detener();
}

bool hnd::MotorTorrent::Disponible() { return true; }

std::string hnd::MotorTorrent::Version() { return lt::version(); }

bool hnd::MotorTorrent::LeerTorrent(const std::string& datos, InfoTorrent& info, std::string& error)
{
    info = {};
    lt::add_torrent_params atp;
    try
    {
        atp = lt::load_torrent_buffer(lt::span<char const>(datos.data(), (std::ptrdiff_t)datos.size()));
    }
    catch (...)
    {
        error = "El archivo no es un .torrent válido.";
        return false;
    }
    if (!atp.ti)
    {
        error = "El archivo no es un .torrent válido.";
        return false;
    }
    const auto& fs = atp.ti->files();
    info.nombre = Ancho(atp.ti->name());
    info.tamano = atp.ti->total_size();
    info.tieneDatos = true;
    for (auto i : fs.file_range())
    {
        if (fs.pad_file_at(i)) continue;
        ArchivoTorrent a;
        a.ruta = Ancho(fs.file_path(i));
        a.tamano = fs.file_size(i);
        info.archivos.push_back(std::move(a));
    }
    std::string v1, v2;
    Huellas(atp.ti->info_hashes(), v1, v2);
    info.huella = v1.empty() ? v2 : v1;
    info.magnet = lt::make_magnet_uri(*atp.ti);
    if (info.nombre.empty()) info.nombre = NombrePorDefecto(v1, v2);
    return true;
}

bool hnd::MotorTorrent::LeerMagnet(const std::string& magnet, InfoTorrent& info, std::string& error)
{
    info = {};
    lt::error_code ec;
    auto atp = lt::parse_magnet_uri(magnet, ec);
    if (ec)
    {
        error = "El enlace magnet no es válido.";
        return false;
    }
    std::string v1, v2;
    Huellas(atp.info_hashes, v1, v2);
    info.huella = v1.empty() ? v2 : v1;
    info.nombre = atp.name.empty() ? NombrePorDefecto(v1, v2) : Ancho(atp.name);
    info.magnet = magnet;
    return true;
}

void hnd::MotorTorrent::Cargar()
{
    std::string texto;
    if (!LeerArchivo(m->archivoLista, texto)) return;
    try
    {
        json todo = json::parse(texto);
        m->siguiente = std::max<uint64_t>(todo.value("siguienteId", BaseIdTorrent + 1), BaseIdTorrent + 1);
        for (auto& j : todo.at("torrents"))
        {
            auto e = std::make_unique<Entrada>();
            e->id = j.value("id", (uint64_t)0);
            if (!EsIdTorrent(e->id)) continue;
            e->nombre = Ancho(j.value("nombre", ""));
            e->carpeta = Ancho(j.value("carpeta", ""));
            e->magnet = j.value("magnet", "");
            e->v1 = j.value("v1", "");
            e->v2 = j.value("v2", "");
            e->estado = EstadoDeNombre(j.value("estado", "pausada"));
            e->compartir = j.value("compartir", false);
            e->eligeArchivos = j.value("eligeArchivos", false);
            e->error = j.value("error", "");
            e->agregada = j.value("agregada", (int64_t)0);
            e->terminada = j.value("terminada", (int64_t)0);
            e->tamano = j.value("tamano", (int64_t)-1);
            e->hecho = j.value("hecho", (int64_t)0);
            e->subido = j.value("subido", (int64_t)0);
            e->archivos = j.value("archivos", 0);
            if (e->carpeta.empty()) continue;
            m->siguiente = std::max(m->siguiente, e->id + 1);
            m->lista.push_back(std::move(e));
        }
    }
    catch (...)
    {
        CopyFileW(m->archivoLista.c_str(), (m->archivoLista + L".danado").c_str(), FALSE);
    }
    if (!m->lista.empty())
    {
        m->AsegurarSesion();
        for (auto& e : m->lista)
        {
            if (e->h.is_valid())
            {
                if (auto ti = e->h.torrent_file()) e->archivos = ArchivosVisibles(ti->files());
            }
        }
    }
}

uint64_t hnd::MotorTorrent::BuscarPorHuella(const std::string& huella)
{
    if (huella.empty()) return 0;
    for (auto& e : m->lista)
    {
        if (e->v1 == huella || e->v2 == huella || (!e->v2.empty() && huella.size() == 40 && e->v2.rfind(huella, 0) == 0)) return e->id;
    }
    return 0;
}

uint64_t hnd::MotorTorrent::Agregar(const NuevoTorrent& n, std::string& error)
{
    lt::add_torrent_params atp;
    if (!n.datos.empty())
    {
        try
        {
            atp = lt::load_torrent_buffer(lt::span<char const>(n.datos.data(), (std::ptrdiff_t)n.datos.size()));
        }
        catch (...)
        {
            error = "El archivo no es un .torrent válido.";
            return 0;
        }
    }
    else
    {
        lt::error_code ec;
        atp = lt::parse_magnet_uri(n.magnet, ec);
        if (ec)
        {
            error = "El enlace magnet no es válido.";
            return 0;
        }
    }
    auto e = std::make_unique<Entrada>();
    Huellas(atp.ti ? atp.ti->info_hashes() : atp.info_hashes, e->v1, e->v2);
    if (BuscarPorHuella(e->v1) || BuscarPorHuella(e->v2))
    {
        error = "Ese torrent ya está en la lista.";
        return 0;
    }
    if (n.carpeta.empty() || !ExisteCarpeta(n.carpeta))
    {
        error = "La carpeta elegida no existe.";
        return 0;
    }
    if (atp.ti)
    {
        const auto& fs = atp.ti->files();
        std::vector<lt::download_priority_t> prioridades;
        size_t visible = 0;
        bool alguno = false;
        for (auto i : fs.file_range())
        {
            bool bajar = true;
            if (fs.pad_file_at(i)) bajar = false;
            else
            {
                if (!n.bajar.empty()) bajar = visible < n.bajar.size() && n.bajar[visible];
                visible++;
            }
            if (bajar) alguno = true;
            prioridades.push_back(bajar ? lt::default_priority : lt::dont_download);
        }
        if (!alguno)
        {
            error = "Elige al menos un archivo.";
            return 0;
        }
        atp.file_priorities = std::move(prioridades);
        e->nombre = Ancho(atp.ti->name());
        e->tamano = atp.ti->total_size();
        e->archivos = ArchivosVisibles(fs);
    }
    else
    {
        e->nombre = Ancho(atp.name);
        e->eligeArchivos = n.elegirDespues;
        if (e->eligeArchivos) atp.flags |= lt::torrent_flags::default_dont_download;
    }
    if (e->nombre.empty()) e->nombre = NombrePorDefecto(e->v1, e->v2);
    atp.save_path = Utf8(n.carpeta);
    atp.flags &= ~lt::torrent_flags::auto_managed;
    atp.flags |= lt::torrent_flags::paused;

    m->AsegurarSesion();
    lt::error_code ec;
    e->h = m->ses->add_torrent(std::move(atp), ec);
    if (ec)
    {
        error = "No se pudo agregar el torrent (" + TextoDeSistema(ec.message()) + ").";
        return 0;
    }
    e->id = m->siguiente++;
    e->carpeta = n.carpeta;
    e->compartir = n.compartir;
    e->estado = n.empezar ? Estado::EnCola : Estado::Pausada;
    e->agregada = AhoraUnix();
    e->magnet = lt::make_magnet_uri(e->h);
    m->PedirResumen(*e, false);
    uint64_t id = e->id;
    Registrar("torrent " + std::to_string(id - BaseIdTorrent) + ": agregado" + (n.datos.empty() ? " (magnet)" : ""));
    m->lista.push_back(std::move(e));
    m->sucio = true;
    return id;
}

void hnd::MotorTorrent::Reanudar(uint64_t id)
{
    Entrada* e = m->Encontrar(id);
    if (!e || EsperaEleccion(*e)) return;   // primero hay que elegir los archivos
    switch (e->estado)
    {
    case Estado::Pausada:
    case Estado::Error:
        if (e->h.is_valid()) e->h.clear_error();
        e->error.clear();
        e->estado = Estado::EnCola;
        m->sucio = true;
        break;
    case Estado::Completada:
        // Reanudar algo terminado es volver a compartirlo
        e->compartir = true;
        e->estado = Estado::Compartiendo;
        m->Correr(*e);
        m->sucio = true;
        break;
    default:
        break;
    }
}

void hnd::MotorTorrent::Pausar(uint64_t id)
{
    Entrada* e = m->Encontrar(id);
    if (!e) return;
    switch (e->estado)
    {
    case Estado::EnCola:
        e->estado = Estado::Pausada;
        m->sucio = true;
        break;
    case Estado::Iniciando:
    case Estado::Descargando:
        m->Detener(*e, Estado::Pausada);
        break;
    case Estado::Compartiendo:
        m->Detener(*e, Estado::Completada);
        break;
    default:
        break;
    }
}

void hnd::MotorTorrent::Quitar(uint64_t id, bool aLaPapelera)
{
    auto it = std::find_if(m->lista.begin(), m->lista.end(), [&](auto& e) { return e->id == id; });
    if (it == m->lista.end()) return;
    Entrada& e = **it;
    std::vector<std::wstring> archivos, carpetas;
    if (aLaPapelera && e.h.is_valid())
    {
        if (auto ti = e.h.torrent_file())
        {
            const auto& fs = ti->files();
            std::wstring base = e.carpeta;
            std::set<std::wstring> dirs;
            for (auto i : fs.file_range())
            {
                if (fs.pad_file_at(i)) continue;
                std::wstring ruta = base + L"\\" + Ancho(fs.file_path(i));
                archivos.push_back(ruta);
                // Carpetas del torrent (para quitarlas si quedan vacías), sin llegar a la carpeta elegida
                for (auto p = ruta.find_last_of(L'\\'); p != std::wstring::npos && p > base.size(); p = ruta.find_last_of(L'\\', p - 1))
                    dirs.insert(ruta.substr(0, p));
            }
            carpetas.assign(dirs.begin(), dirs.end());
        }
        // Lo bajado a medias de archivos que no se eligieron
        for (const auto& h : { e.v1, e.v2.substr(0, std::min<size_t>(e.v2.size(), 40)) })
        {
            if (!h.empty()) archivos.push_back(e.carpeta + L"\\." + Ancho(h) + L".parts");
        }
    }
    if (e.h.is_valid() && m->ses)
    {
        e.h.pause();
        m->ses->remove_torrent(e.h);
    }
    DeleteFileW(m->RutaReanudar(e.id).c_str());
    std::wstring nombre = e.nombre;
    Registrar("torrent " + std::to_string(e.id - BaseIdTorrent) + ": quitado" + (aLaPapelera ? " (a la Papelera)" : ""));
    m->lista.erase(it);
    m->sucio = true;
    if (!archivos.empty()) ALaPapelera(std::move(archivos), std::move(carpetas), nombre, m->buzon);
}

void hnd::MotorTorrent::PausarTodas()
{
    for (auto& e : m->lista) Pausar(e->id);
}

void hnd::MotorTorrent::ReanudarTodas()
{
    for (auto& e : m->lista)
    {
        if (e->estado == Estado::Pausada || e->estado == Estado::Error) Reanudar(e->id);
    }
}

void hnd::MotorTorrent::LimpiarTerminadas()
{
    std::vector<uint64_t> ids;
    for (auto& e : m->lista)
    {
        if (e->estado == Estado::Completada) ids.push_back(e->id);
    }
    for (auto id : ids) Quitar(id, false);
}

bool hnd::MotorTorrent::Archivos(uint64_t id, InfoTorrent& info)
{
    info = {};
    Entrada* e = m->Encontrar(id);
    if (!e) return false;
    info.nombre = e->nombre;
    info.magnet = e->magnet;
    info.huella = e->v1.empty() ? e->v2 : e->v1;
    if (!e->h.is_valid()) return true;
    auto ti = e->h.torrent_file();
    if (!ti) return true;
    const auto& fs = ti->files();
    auto prioridades = e->h.get_file_priorities();
    std::vector<std::int64_t> progreso;
    e->h.file_progress(progreso, lt::torrent_handle::piece_granularity);
    info.tieneDatos = true;
    info.tamano = ti->total_size();
    for (auto i : fs.file_range())
    {
        if (fs.pad_file_at(i)) continue;
        int k = static_cast<int>(i);
        ArchivoTorrent a;
        a.ruta = Ancho(fs.file_path(i));
        a.tamano = fs.file_size(i);
        a.hecho = k < (int)progreso.size() ? progreso[(size_t)k] : 0;
        a.bajar = e->eligeArchivos || k >= (int)prioridades.size() || prioridades[(size_t)k] != lt::dont_download;
        info.archivos.push_back(std::move(a));
    }
    return true;
}

bool hnd::MotorTorrent::ElegirArchivos(uint64_t id, const std::vector<bool>& bajar)
{
    Entrada* e = m->Encontrar(id);
    if (!e || !e->h.is_valid()) return false;
    auto ti = e->h.torrent_file();
    if (!ti) return false;
    const auto& fs = ti->files();
    std::vector<lt::download_priority_t> prioridades;
    size_t visible = 0;
    bool alguno = false;
    int64_t elegido = 0;
    for (auto i : fs.file_range())
    {
        bool si = false;
        if (!fs.pad_file_at(i))
        {
            si = visible < bajar.size() && bajar[visible];
            visible++;
            if (si) elegido += fs.file_size(i);
        }
        if (si) alguno = true;
        prioridades.push_back(si ? lt::default_priority : lt::dont_download);
    }
    if (!alguno) return false;
    e->h.prioritize_files(prioridades);
    bool eraPendiente = e->eligeArchivos;
    e->eligeArchivos = false;
    e->tamano = elegido;
    if (elegido > e->hecho) e->terminada = 0;   // hay algo nuevo que bajar: se avisa otra vez al terminar
    if (eraPendiente || e->estado == Estado::Completada) e->estado = Estado::EnCola;
    m->PedirResumen(*e, false);
    m->sucio = true;
    return true;
}

void hnd::MotorTorrent::Compartir(uint64_t id, bool si)
{
    Entrada* e = m->Encontrar(id);
    if (!e) return;
    e->compartir = si;
    if (si && e->estado == Estado::Completada)
    {
        e->estado = Estado::Compartiendo;
        m->Correr(*e);
    }
    else if (!si && e->estado == Estado::Compartiendo)
    {
        m->Detener(*e, Estado::Completada);
    }
    m->sucio = true;
}

std::vector<hnd::Foto> hnd::MotorTorrent::Fotos()
{
    std::vector<Foto> r;
    r.reserve(m->lista.size());
    for (auto& e : m->lista)
    {
        Foto f;
        Buscar(e->id, f);
        r.push_back(std::move(f));
    }
    return r;
}

bool hnd::MotorTorrent::Buscar(uint64_t id, Foto& f)
{
    Entrada* e = m->Encontrar(id);
    if (!e) return false;
    f = {};
    f.torrent = true;
    auto& d = f.datos;
    d.id = e->id;
    d.url = e->magnet;
    d.carpeta = e->carpeta;
    d.nombre = e->nombre;
    d.nombreFijo = true;
    d.tamano = e->tamano;
    d.rangos = true;
    d.estado = e->estado;
    d.error = e->error;
    d.agregada = e->agregada;
    d.terminada = e->terminada;
    d.conexiones = 0;
    f.hecho = e->hecho;
    f.subido = e->subido;
    f.compartir = e->compartir;
    f.eligeArchivos = EsperaEleccion(*e);
    f.archivos = e->archivos;
    f.mapa = e->mapa;
    bool corre = e->estado == Estado::Iniciando || e->estado == Estado::Descargando || e->estado == Estado::Compartiendo;
    if (e->hayEstado)
    {
        const auto& st = e->st;
        f.tieneDatos = st.has_metadata;
        if (corre && !(st.flags & lt::torrent_flags::paused))
        {
            f.velocidad = st.download_payload_rate;
            f.subida = st.upload_payload_rate;
            f.activas = st.num_peers;
            f.semillas = st.num_seeds;
            f.buscandoDatos = st.state == lt::torrent_status::downloading_metadata;
            f.revisando = st.state == lt::torrent_status::checking_files || st.state == lt::torrent_status::checking_resume_data;
        }
    }
    else
    {
        f.tieneDatos = e->archivos > 0;
    }
    return true;
}

std::wstring hnd::MotorTorrent::RutaFinal(uint64_t id)
{
    Entrada* e = m->Encontrar(id);
    if (!e) return {};
    if (e->h.is_valid())
    {
        if (auto ti = e->h.torrent_file())
        {
            const auto& fs = ti->files();
            if (ArchivosVisibles(fs) == 1)
            {
                for (auto i : fs.file_range())
                {
                    if (!fs.pad_file_at(i)) return e->carpeta + L"\\" + Ancho(fs.file_path(i));
                }
            }
            return e->carpeta + L"\\" + Ancho(ti->name());
        }
    }
    return e->carpeta + L"\\" + e->nombre;
}

bool hnd::MotorTorrent::UnSoloArchivo(uint64_t id)
{
    Entrada* e = m->Encontrar(id);
    if (!e || !e->h.is_valid()) return false;
    auto ti = e->h.torrent_file();
    return ti && ArchivosVisibles(ti->files()) == 1;
}

bool hnd::MotorTorrent::HayActivas()
{
    for (auto& e : m->lista)
    {
        if (e->estado == Estado::EnCola || e->estado == Estado::Iniciando || e->estado == Estado::Descargando
            || e->estado == Estado::Compartiendo)
            return true;
    }
    return false;
}

std::vector<uint64_t> hnd::MotorTorrent::PorElegir()
{
    std::vector<uint64_t> r;
    r.swap(m->porElegir);
    return r;
}

std::vector<std::wstring> hnd::MotorTorrent::Avisos()
{
    std::vector<std::wstring> r;
    std::lock_guard g(m->buzon->m);
    r.swap(m->buzon->avisos);
    return r;
}

void hnd::MotorTorrent::Tick()
{
    if (!m->ses)
    {
        if (m->sucio) m->GuardarLista();
        return;
    }
    m->ProcesarAlertas(*this);
    m->ses->post_torrent_updates(lt::torrent_handle::query_pieces | lt::torrent_handle::query_name);

    // Velocidad: libtorrent ya sabe repartirla; solo se le cambia si el número se movió
    auto distinto = [](int64_t a, int64_t b) { return (a == 0) != (b == 0) || std::llabs(a - b) > std::max<int64_t>(2048, b / 20); };
    if (m->limBajada < 0 || m->limSubida < 0 || distinto(limiteBajada, m->limBajada) || distinto(limiteSubida, m->limSubida))
    {
        lt::settings_pack p;
        p.set_int(lt::settings_pack::download_rate_limit, (int)std::clamp<int64_t>(limiteBajada, 0, INT_MAX));
        p.set_int(lt::settings_pack::upload_rate_limit, (int)std::clamp<int64_t>(limiteSubida, 0, INT_MAX));
        m->ses->apply_settings(std::move(p));
        m->limBajada = limiteBajada;
        m->limSubida = limiteSubida;
    }

    // Cola: los que comparten no ocupan lugar
    int bajando = 0;
    for (auto& e : m->lista)
    {
        if (e->estado == Estado::Iniciando || e->estado == Estado::Descargando) bajando++;
    }
    for (auto& e : m->lista)
    {
        if (e->estado == Estado::EnCola && !EsperaEleccion(*e) && e->h.is_valid() && bajando < maxSimultaneas)
        {
            m->Arrancar(*e);
            bajando++;
        }
        else if (e->estado == Estado::Compartiendo && e->h.is_valid() && e->hayEstado && (e->st.flags & lt::torrent_flags::paused)
                 && !e->st.errc)
        {
            m->Correr(*e);   // al abrir el programa sigue compartiendo lo que la persona eligió
        }
    }

    // Sin nada activo un rato: se deja de escuchar y la DHT se apaga
    bool activo = false;
    for (auto& e : m->lista)
    {
        if (e->estado == Estado::Iniciando || e->estado == Estado::Descargando || e->estado == Estado::Compartiendo) activo = true;
    }
    if (activo)
    {
        m->contando = false;
        m->Red(true);
    }
    else if (m->red)
    {
        if (!m->contando)
        {
            m->contando = true;
            m->sinActividad = Reloj::now();
        }
        else if (Reloj::now() - m->sinActividad > std::chrono::seconds(20))
        {
            m->Red(false);
            m->contando = false;
        }
    }

    // Datos de reanudación de los que corren, cada minuto
    for (auto& e : m->lista)
    {
        bool corre = e->estado == Estado::Iniciando || e->estado == Estado::Descargando || e->estado == Estado::Compartiendo;
        if (corre && Reloj::now() - e->ultimoResumen > std::chrono::seconds(60)) m->PedirResumen(*e, true);
    }

    int64_t ahora = AhoraUnix();
    if (m->sucio && ahora - m->ultimoGuardado >= 2) m->GuardarLista();
}

void hnd::MotorTorrent::Detener()
{
    if (!m) return;
    if (m->ses)
    {
        m->ses->pause();
        m->pendientes = 0;
        for (auto& e : m->lista)
        {
            if (e->h.is_valid()) m->PedirResumen(*e, false);
        }
        auto fin = Reloj::now() + std::chrono::seconds(10);
        while (m->pendientes > 0 && Reloj::now() < fin)
        {
            if (!m->ses->wait_for_alert(std::chrono::milliseconds(500))) continue;
            std::vector<lt::alert*> alertas;
            m->ses->pop_alerts(&alertas);
            for (lt::alert* a : alertas)
            {
                if (auto* sr = lt::alert_cast<lt::save_resume_data_alert>(a))
                {
                    m->pendientes--;
                    Entrada* e = m->PorHandle(sr->handle);
                    if (!e) continue;
                    auto buf = lt::write_resume_data_buf(sr->params);
                    CreateDirectoryW(m->carpetaTorrents.c_str(), nullptr);
                    EscribirSeguro(m->RutaReanudar(e->id), std::string(buf.begin(), buf.end()));
                }
                else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a))
                {
                    m->pendientes--;
                }
            }
        }
        if (m->dhtUsada)
        {
            auto estado = lt::write_session_params_buf(m->ses->session_state(lt::session::save_dht_state), lt::session::save_dht_state);
            CreateDirectoryW(m->carpetaTorrents.c_str(), nullptr);
            EscribirSeguro(m->carpetaTorrents + L"\\sesion.dat", std::string(estado.begin(), estado.end()));
        }
    }
    if (m->sucio || !m->lista.empty()) m->GuardarLista();
    if (m->ses)
    {
        for (auto& e : m->lista) e->h = {};
        m->ses.reset();   // espera como mucho 2 s a los rastreadores (stop_tracker_timeout)
        Registrar("torrents: sesión cerrada");
    }
}

#else   // ---------------------------------------------------------------- Sin libtorrent

struct hnd::MotorTorrent::Datos
{
    std::shared_ptr<Buzon> buzon = std::make_shared<Buzon>();
};

hnd::MotorTorrent::MotorTorrent(std::wstring) : m(std::make_unique<Datos>()) {}
hnd::MotorTorrent::~MotorTorrent() = default;
bool hnd::MotorTorrent::Disponible() { return false; }
std::string hnd::MotorTorrent::Version() { return {}; }

bool hnd::MotorTorrent::LeerTorrent(const std::string&, InfoTorrent&, std::string& error)
{
    error = "Esta copia de Atarraya se compiló sin torrents.";
    return false;
}

bool hnd::MotorTorrent::LeerMagnet(const std::string&, InfoTorrent&, std::string& error)
{
    error = "Esta copia de Atarraya se compiló sin torrents.";
    return false;
}

void hnd::MotorTorrent::Cargar() {}

uint64_t hnd::MotorTorrent::Agregar(const NuevoTorrent&, std::string& error)
{
    error = "Esta copia de Atarraya se compiló sin torrents.";
    return 0;
}

uint64_t hnd::MotorTorrent::BuscarPorHuella(const std::string&) { return 0; }
void hnd::MotorTorrent::Reanudar(uint64_t) {}
void hnd::MotorTorrent::Pausar(uint64_t) {}
void hnd::MotorTorrent::Quitar(uint64_t, bool) {}
void hnd::MotorTorrent::PausarTodas() {}
void hnd::MotorTorrent::ReanudarTodas() {}
void hnd::MotorTorrent::LimpiarTerminadas() {}
bool hnd::MotorTorrent::Archivos(uint64_t, InfoTorrent&) { return false; }
bool hnd::MotorTorrent::ElegirArchivos(uint64_t, const std::vector<bool>&) { return false; }
void hnd::MotorTorrent::Compartir(uint64_t, bool) {}
std::vector<hnd::Foto> hnd::MotorTorrent::Fotos() { return {}; }
bool hnd::MotorTorrent::Buscar(uint64_t, Foto&) { return false; }
std::wstring hnd::MotorTorrent::RutaFinal(uint64_t) { return {}; }
bool hnd::MotorTorrent::UnSoloArchivo(uint64_t) { return false; }
bool hnd::MotorTorrent::HayActivas() { return false; }
std::vector<uint64_t> hnd::MotorTorrent::PorElegir() { return {}; }
std::vector<std::wstring> hnd::MotorTorrent::Avisos() { return {}; }
void hnd::MotorTorrent::Tick() {}
void hnd::MotorTorrent::Detener() {}

#endif
