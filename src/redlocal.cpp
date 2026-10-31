#include "redlocal.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace
{
    // Registro de IANA de direcciones IPv4 de uso especial
    bool EsIpv4Local(uint32_t ip)
    {
        uint32_t a = ip >> 24, b = (ip >> 16) & 0xFF, c = (ip >> 8) & 0xFF;
        return a == 0 || a == 10 || a == 127 || (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168)
            || (a == 100 && b >= 64 && b <= 127)          // CGNAT
            || (a == 192 && b == 0 && (c == 0 || c == 2))  // 192.0.0.0/24 y documentación 192.0.2.0/24
            || (a == 192 && b == 88 && c == 99)            // 6to4 relé
            || (a == 198 && (b == 18 || b == 19))          // pruebas de rendimiento
            || (a == 198 && b == 51 && c == 100)           // documentación
            || (a == 203 && b == 0 && c == 113)            // documentación
            || a >= 224;                                   // multidifusión y reservadas
    }

    // Una forma de mirar un destino: la dirección tal cual o la IPv4 que lleva dentro
    struct Forma
    {
        bool v6 = false;
        uint8_t b[16] = {};
    };

    bool Ceros(const uint8_t* desde, const uint8_t* hasta)
    {
        return std::all_of(desde, hasta, [](uint8_t x) { return x == 0; });
    }

    // La dirección y, si es una IPv6 que lleva una IPv4 (::ffff:a.b.c.d, NAT64 o 6to4), también esa IPv4
    int Formas(const sockaddr* s, Forma (&f)[2])
    {
        if (s->sa_family == AF_INET)
        {
            memcpy(f[0].b, &reinterpret_cast<const sockaddr_in*>(s)->sin_addr, 4);
            return 1;
        }
        if (s->sa_family != AF_INET6) return 0;
        const uint8_t* b = reinterpret_cast<const sockaddr_in6*>(s)->sin6_addr.u.Byte;
        f[0].v6 = true;
        memcpy(f[0].b, b, 16);
        const uint8_t* v4 = nullptr;
        if (Ceros(b, b + 10) && b[10] == 0xFF && b[11] == 0xFF) v4 = b + 12;                               // ::ffff:a.b.c.d
        else if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xFF && b[3] == 0x9B && Ceros(b + 4, b + 12)) v4 = b + 12;   // NAT64
        else if (b[0] == 0x20 && b[1] == 0x02) v4 = b + 2;                                                  // 6to4
        if (!v4) return 1;
        memcpy(f[1].b, v4, 4);
        return 2;
    }

    bool MismosBits(const uint8_t* a, const uint8_t* b, int bits)
    {
        int enteros = bits / 8, resto = bits % 8;
        if (memcmp(a, b, enteros) != 0) return false;
        if (resto == 0) return true;
        uint8_t mascara = (uint8_t)(0xFF << (8 - resto));
        return (a[enteros] & mascara) == (b[enteros] & mascara);
    }

#ifdef ATARRAYA_PRUEBAS
    std::atomic<bool> g_simularFalla{ false };
#endif

    ULONG LeerTarjetas(ULONG opciones, IP_ADAPTER_ADDRESSES* lista, ULONG* largo)
    {
#ifdef ATARRAYA_PRUEBAS
        if (g_simularFalla) return ERROR_NOT_ENOUGH_MEMORY;   // como si Windows fallara
#endif
        return GetAdaptersAddresses(AF_UNSPEC, opciones, nullptr, lista, largo);
    }
}

#ifdef ATARRAYA_PRUEBAS
void hnd::red::SimularFallaAlLeerRedes(bool fallar)
{
    g_simularFalla = fallar;
}
#endif

bool hnd::red::EsRangoLocal(const sockaddr* s)
{
    if (s->sa_family == AF_INET)
        return EsIpv4Local(ntohl(reinterpret_cast<const sockaddr_in*>(s)->sin_addr.s_addr));
    if (s->sa_family != AF_INET6) return true;
    const uint8_t* b = reinterpret_cast<const sockaddr_in6*>(s)->sin6_addr.u.Byte;
    auto v4En = [&](int i) { return EsIpv4Local((uint32_t)b[i] << 24 | (uint32_t)b[i + 1] << 16 | (uint32_t)b[i + 2] << 8 | b[i + 3]); };
    if (Ceros(b, b + 10) && b[10] == 0xFF && b[11] == 0xFF) return v4En(12);                             // ::ffff:a.b.c.d
    if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xFF && b[3] == 0x9B && Ceros(b + 4, b + 12)) return v4En(12);   // 64:ff9b::/96 (NAT64)
    // Fuera de 2000::/3 no hay direcciones públicas: ::1, fe80::/10, fec0::/10, fc00::/7, multidifusión, 64:ff9b:1::/48...
    if ((b[0] & 0xE0) != 0x20) return true;
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] < 0x02) return true;                   // 2001::/23 (Teredo y otros especiales)
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0D && b[3] == 0xB8) return true;  // 2001:db8::/32 (documentación)
    if (b[0] == 0x3F && b[1] == 0xFF && (b[2] & 0xF0) == 0) return true;            // 3fff::/20 (documentación)
    if (b[0] == 0x20 && b[1] == 0x02) return v4En(2);                                // 2002::/16 (6to4 lleva una IPv4)
    return false;
}

bool hnd::red::EnSubred(const sockaddr* destino, const Subred& red)
{
    Forma f[2];
    int n = Formas(destino, f);
    int bits = std::clamp(red.prefijo, 0, red.v6 ? 128 : 32);
    for (int i = 0; i < n; i++)
    {
        if (f[i].v6 == red.v6 && MismosBits(f[i].b, red.direccion, bits)) return true;
    }
    return false;
}

bool hnd::red::SubredDeTarjeta(const sockaddr* s, int prefijo, Subred& r)
{
    r = {};
    if (!s) return false;
    if (s->sa_family == AF_INET)
    {
        memcpy(r.direccion, &reinterpret_cast<const sockaddr_in*>(s)->sin_addr, 4);
        r.prefijo = prefijo >= 1 && prefijo <= 32 ? prefijo : 32;
        return true;
    }
    if (s->sa_family != AF_INET6) return false;
    r.v6 = true;
    memcpy(r.direccion, &reinterpret_cast<const sockaddr_in6*>(s)->sin6_addr, 16);
    r.prefijo = prefijo >= 1 && prefijo <= 128 ? prefijo : 128;
    if (r.prefijo > 64 && (r.direccion[0] & 0xE0) == 0x20) r.prefijo = 64;   // pública (2000::/3): todo su /64
    return true;
}

std::vector<hnd::red::Subred> hnd::red::LeerSubredesDeLaPc(bool* ok)
{
    std::vector<Subred> r;
    if (ok) *ok = false;
    const ULONG opciones = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME;
    std::vector<uint64_t> memoria;   // de 8 en 8 bytes: la lista de Windows necesita memoria alineada
    ULONG largo = 16 * 1024;
    ULONG res = ERROR_BUFFER_OVERFLOW;
    for (int intento = 0; intento < 4 && res == ERROR_BUFFER_OVERFLOW; intento++)
    {
        memoria.assign(largo / sizeof(uint64_t) + 1, 0);
        largo = (ULONG)(memoria.size() * sizeof(uint64_t));
        res = LeerTarjetas(opciones, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(memoria.data()), &largo);
    }
    // Cualquier otra respuesta cuenta como falla, también «ninguna dirección»: la PC siempre tiene por lo menos las
    // suyas propias (127.0.0.1 y ::1)
    if (res != NO_ERROR) return r;
    for (auto* t = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(memoria.data()); t; t = t->Next)
    {
        for (auto* u = t->FirstUnicastAddress; u; u = u->Next)
        {
            const sockaddr* s = u->Address.lpSockaddr;
            if (!s) continue;
            int minimo = s->sa_family == AF_INET6 ? (int)sizeof(sockaddr_in6) : (int)sizeof(sockaddr_in);
            if (u->Address.iSockaddrLength < minimo) continue;
            Subred x;
            if (SubredDeTarjeta(s, u->OnLinkPrefixLength, x)) r.push_back(x);
        }
    }
    if (ok) *ok = true;
    return r;
}

hnd::red::Destino hnd::red::RevisarDestino(const sockaddr* destino)
{
    if (EsRangoLocal(destino)) return Destino::Local;
    bool ok = false;
    auto redes = LeerSubredesDeLaPc(&ok);
    if (!ok) return Destino::SinRevisar;   // no se sabe cuáles son las redes de la PC: no se consulta
    bool local = std::any_of(redes.begin(), redes.end(), [&](const Subred& r) { return EnSubred(destino, r); });
    return local ? Destino::Local : Destino::Afuera;
}
