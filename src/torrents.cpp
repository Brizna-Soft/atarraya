#include "torrents.hpp"

#ifdef CON_TORRENTS
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/version.hpp>
#endif

std::string hnd::VersionLibtorrent(bool crearSesion)
{
#ifdef CON_TORRENTS
    if (crearSesion)
    {
        lt::settings_pack p;
        p.set_str(lt::settings_pack::listen_interfaces, "");
        p.set_bool(lt::settings_pack::enable_dht, false);
        lt::session s(lt::session_params(std::move(p)));
        lt::error_code ec;
        auto atp = lt::parse_magnet_uri("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567", ec);
        (void)atp;
    }
    return lt::version();
#else
    (void)crearSesion;
    return {};
#endif
}
