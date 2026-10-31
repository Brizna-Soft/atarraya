#pragma once

// ¿Una dirección es de esta PC o de su red? Las consultas que Atarraya hace solo (las del recolector) nunca deben
// llegar a la red de la casa: ni a la propia PC, ni al módem, ni a otros equipos, aunque un nombre de afuera o una
// redirección apunte ahí. Con IPv6, los equipos de la casa tienen direcciones públicas del mismo /64: por eso no
// basta con los rangos privados y se miran también las redes de las tarjetas de red de la PC.

#include <cstdint>
#include <vector>

struct sockaddr;

namespace hnd::red
{
    // La red de una dirección de la PC: las direcciones que comparten sus primeros «prefijo» bits
    struct Subred
    {
        bool v6 = false;
        uint8_t direccion[16] = {};   // en IPv4, los 4 primeros bytes
        int prefijo = 0;
    };

    // Rangos que nunca son de afuera: privados, de la propia PC, enlace local, documentación, multidifusión y
    // reservados (registros de IANA). Lo que no es IPv4 ni IPv6 también cuenta como local.
    bool EsRangoLocal(const sockaddr* destino);

    // ¿«destino» cae en «red»? Una IPv4 escrita como IPv6 (::ffff:a.b.c.d, NAT64 64:ff9b::/96 o 6to4 2002::/16)
    // se compara también como IPv4.
    bool EnSubred(const sockaddr* destino, const Subred& red);

    // La red de una dirección de una tarjeta de red, según su prefijo en el enlace (OnLinkPrefixLength). Si el
    // prefijo falta o no tiene sentido, queda solo la dirección. En IPv6 pública se toma por lo menos todo el /64:
    // las direcciones temporales y las de DHCPv6 llegan como /128, pero los equipos de la casa comparten el /64.
    bool SubredDeTarjeta(const sockaddr* direccion, int prefijoEnEnlace, Subred& r);

    // Las direcciones de las tarjetas de red de esta PC con sus redes (GetAdaptersAddresses). «ok» queda en false
    // si Windows no pudo dar la lista.
    std::vector<Subred> LeerSubredesDeLaPc(bool* ok = nullptr);

    enum class Destino
    {
        Afuera,       // se puede consultar
        Local,        // esta PC, su red o un rango local: no se consulta
        SinRevisar,   // Windows no dijo cuáles son las redes de la PC: como no se sabe, tampoco se consulta
    };

    // Lo que revisan las consultas justo antes de conectar: rangos locales, direcciones de esta PC y sus redes. Las
    // redes se leen en cada llamada (la red cambia: wifi, VPN), sin nada guardado que compartir entre hilos.
    Destino RevisarDestino(const sockaddr* destino);

#ifdef ATARRAYA_PRUEBAS
    // Solo en las pruebas: hace como si Windows no pudiera dar las redes de la PC
    void SimularFallaAlLeerRedes(bool fallar);
#endif
}
