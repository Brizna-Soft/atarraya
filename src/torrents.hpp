#pragma once

// Torrents y enlaces magnet con libtorrent (en preparación).

#include <string>

namespace hnd
{
    // Versión de libtorrent, o vacío si esta copia se compiló sin torrents
    std::string VersionLibtorrent(bool crearSesion);
}
