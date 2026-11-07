#include "contrasenas.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

#pragma comment(lib, "crypt32.lib")

#include <nlohmann/json.hpp>

#include "util.hpp"

using json = nlohmann::json;

namespace
{
    // Con esta «entropía», solo Atarraya descifra lo que cifró (otro programa de la misma cuenta necesitaría saberla)
    const char Entropia[] = "Atarraya.contrasenas.1";

    // Sobre el mismo texto, sin copiarlo (cada copia sería otra contraseña en la memoria que alguien tendría que borrar)
    bool Utf8Valido(std::string_view s)
    {
        size_t i = 0;
        while (i < s.size())
        {
            unsigned char c = (unsigned char)s[i];
            size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
            if (n == 0 || i + n > s.size()) return false;
            for (size_t k = 1; k < n; k++)
            {
                if (((unsigned char)s[i + k] >> 6) != 0x2) return false;
            }
            i += n;
        }
        return true;
    }

    std::string_view Recortar(std::string_view s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
        return s.substr(a, b - a);
    }

    std::string Base64(const std::vector<BYTE>& datos)
    {
        DWORD n = 0;
        if (datos.empty() || !CryptBinaryToStringA(datos.data(), (DWORD)datos.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n))
            return {};
        std::string r(n, '\0');
        if (!CryptBinaryToStringA(datos.data(), (DWORD)datos.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, r.data(), &n)) return {};
        r.resize(n);
        while (!r.empty() && r.back() == '\0') r.pop_back();
        return r;
    }

    std::vector<BYTE> DeBase64(const std::string& texto)
    {
        DWORD n = 0;
        if (texto.empty() || texto.size() > 1024 * 1024
            || !CryptStringToBinaryA(texto.c_str(), (DWORD)texto.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr))
            return {};
        std::vector<BYTE> r(n);
        if (!CryptStringToBinaryA(texto.c_str(), (DWORD)texto.size(), CRYPT_STRING_BASE64, r.data(), &n, nullptr, nullptr)) return {};
        r.resize(n);
        return r;
    }

    // Una lista (ver Empaquetar), cifrada con DPAPI y en base64 (vacío si no se pudo). Borra el texto en claro.
    std::string CifrarLista(const std::vector<std::string>& lista)
    {
        std::string claro = hnd::contrasenas::Empaquetar(lista);
        DATA_BLOB entrada = { (DWORD)claro.size(), reinterpret_cast<BYTE*>(claro.data()) };
        DATA_BLOB extra = { (DWORD)(sizeof(Entropia) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(Entropia)) };
        DATA_BLOB salida = {};
        BOOL ok = CryptProtectData(&entrada, L"Atarraya", &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &salida);
        hnd::contrasenas::Borrar(claro);
        if (!ok) return {};
        std::vector<BYTE> cifrado(salida.pbData, salida.pbData + salida.cbData);
        LocalFree(salida.pbData);
        return Base64(cifrado);
    }

    // «legible»: se pudo descifrar y tiene la forma de ahora. Si no (otra cuenta u otra PC, o el formato de antes, en
    // JSON), la lista queda vacía y el bloque descifrado se borra igual antes de soltarlo: nada queda a medias.
    std::vector<std::string> DescifrarLista(const std::string& cifrado, size_t maximo, bool* legible = nullptr)
    {
        std::vector<std::string> r;
        if (legible) *legible = false;
        std::vector<BYTE> datos = DeBase64(cifrado);
        if (datos.empty()) return r;
        DATA_BLOB entrada = { (DWORD)datos.size(), datos.data() };
        DATA_BLOB extra = { (DWORD)(sizeof(Entropia) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(Entropia)) };
        DATA_BLOB salida = {};
        if (!CryptUnprotectData(&entrada, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &salida)) return r;
        // Directo del bloque descifrado a cada contraseña (sin otra copia), y el bloque se borra antes de soltarlo
        bool bien = hnd::contrasenas::Desempaquetar(reinterpret_cast<const char*>(salida.pbData), salida.cbData, maximo, r);
        SecureZeroMemory(salida.pbData, salida.cbData);
        LocalFree(salida.pbData);
        if (legible) *legible = bien;
        return r;
    }

    uint32_t Le32(const char* p)
    {
        return (uint32_t)(unsigned char)p[0] | ((uint32_t)(unsigned char)p[1] << 8) | ((uint32_t)(unsigned char)p[2] << 16)
             | ((uint32_t)(unsigned char)p[3] << 24);
    }
}

std::string hnd::contrasenas::Empaquetar(const std::vector<std::string>& lista)
{
    size_t total = 1;
    for (const auto& c : lista) total += 4 + c.size();
    std::string r;
    r.reserve(total);   // de una vez: nunca se agranda (al agrandarse dejaría copias sin borrar)
    r.push_back('\x01');
    for (const auto& c : lista)
    {
        uint32_t n = (uint32_t)c.size();
        for (int i = 0; i < 4; i++) r.push_back((char)((n >> (8 * i)) & 0xFF));
        r.append(c);
    }
    return r;
}

bool hnd::contrasenas::Desempaquetar(const char* datos, size_t largo, size_t maximo, std::vector<std::string>& lista)
{
    Borrar(lista);
    if (!datos || largo == 0 || datos[0] != '\x01') return false;
    // Primero se revisan los largos y se cuentan, para reservar el lugar justo: la lista nunca se agranda
    size_t cuantas = 0;
    for (size_t p = 1; p < largo;)
    {
        if (largo - p < 4) return false;
        uint32_t n = Le32(datos + p);
        p += 4;
        if (n > largo - p || n > MaximoLargo) return false;
        p += n;
        if (n > 0) cuantas++;
    }
    lista.reserve(std::min(cuantas, maximo));
    for (size_t p = 1; p < largo && lista.size() < maximo;)
    {
        uint32_t n = Le32(datos + p);
        p += 4;
        if (n > 0) lista.emplace_back(datos + p, n);   // directo a su lugar
        p += n;
    }
    return true;
}

std::vector<std::string> hnd::contrasenas::DeTexto(const std::string& texto)
{
    std::vector<std::string> r;
    r.reserve(Maximo);   // las listas de contraseñas nunca se agrandan: al agrandarse dejarían copias sin borrar
    // Cada una se revisa sobre el mismo texto (sin copias temporales) y solo la que sirve se copia, una vez, a su lugar
    std::string_view todo(texto);
    size_t i = 0;
    while (i <= todo.size() && r.size() < Maximo)
    {
        size_t fin = todo.find_first_of("\r\n", i);
        if (fin == std::string_view::npos) fin = todo.size();
        std::string_view c = Recortar(todo.substr(i, fin - i));
        bool control = std::any_of(c.begin(), c.end(), [](char ch) { return (unsigned char)ch < 0x20 || ch == 0x7F; });
        if (!c.empty() && c.size() <= MaximoLargo && !control && Utf8Valido(c)
            && std::none_of(r.begin(), r.end(), [&](const std::string& x) { return std::string_view(x) == c; }))
            r.emplace_back(c);
        if (fin >= todo.size()) break;
        i = fin + 1;
    }
    return r;
}

std::vector<std::string> hnd::contrasenas::Juntar(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    std::vector<std::string> r;
    r.reserve(std::min(Maximo, a.size() + b.size()));
    for (const auto* l : { &a, &b })
    {
        for (const auto& c : *l)
        {
            if (r.size() >= Maximo) return r;
            if (!c.empty() && std::find(r.begin(), r.end(), c) == r.end()) r.push_back(c);
        }
    }
    return r;
}

std::string hnd::contrasenas::Cifrar(const std::vector<std::string>& lista)
{
    if (lista.empty()) return {};
    std::vector<std::string> recortada(lista.begin(), lista.begin() + std::min(lista.size(), Maximo));
    std::string r = CifrarLista(recortada);
    Borrar(recortada);
    return r;
}

std::vector<std::string> hnd::contrasenas::Descifrar(const std::string& cifrado)
{
    return DescifrarLista(cifrado, Maximo);
}

bool hnd::contrasenas::Legible(const std::string& cifrado)
{
    bool legible = false;
    auto lista = DescifrarLista(cifrado, MaximoRecordadas, &legible);
    Borrar(lista);
    return legible;
}

void hnd::contrasenas::Borrar(std::string& s)
{
    // Todo el lugar que tiene reservado, no solo lo que mide ahora (un texto que se achicó guarda el resto). Agrandarlo
    // hasta su capacidad no lo muda de lugar.
    s.resize(s.capacity());
    if (!s.empty()) SecureZeroMemory(s.data(), s.size());
    s.clear();
    s.shrink_to_fit();
}

void hnd::contrasenas::Borrar(std::vector<char>& v)
{
    v.resize(v.capacity());
    if (!v.empty()) SecureZeroMemory(v.data(), v.size());
    v.clear();
    v.shrink_to_fit();
}

void hnd::contrasenas::Agregar(std::string& s, const char* datos, size_t largo)
{
    if (largo == 0) return;
    if (s.size() + largo <= s.capacity())
    {
        s.append(datos, largo);
        return;
    }
    // No cabe: se copia a un lugar nuevo y el de antes se borra antes de soltarlo (append lo soltaría sin borrar)
    std::string nuevo;
    nuevo.reserve(std::max(s.capacity() * 2, s.size() + largo));
    nuevo.append(s);
    nuevo.append(datos, largo);
    Borrar(s);
    s.swap(nuevo);
}

void hnd::contrasenas::Borrar(std::vector<std::string>& v)
{
    for (auto& s : v) Borrar(s);
    v.clear();
}

hnd::contrasenas::Guardadas::Guardadas(std::wstring archivo) : m_archivo(std::move(archivo)) {}

void hnd::contrasenas::Guardadas::Cargar()
{
    m_paquetes.clear();
    m_recordadas.clear();
    std::string texto;
    if (!LeerArchivo(m_archivo, texto)) return;
    try
    {
        json j = json::parse(texto);
        if (auto p = j.find("paquetes"); p != j.end() && p->is_object())
        {
            for (auto& [grupo, cifrado] : p->items())
            {
                if (grupo.empty() || grupo.size() > 64 || !cifrado.is_string()) continue;
                m_paquetes[grupo] = cifrado.get<std::string>();
                if (m_paquetes.size() >= 2000) break;
            }
        }
        if (auto r = j.find("recordadas"); r != j.end() && r->is_string()) m_recordadas = r->get<std::string>();
    }
    catch (...)
    {
        m_paquetes.clear();
        m_recordadas.clear();
    }
}

bool hnd::contrasenas::Guardadas::Guardar() const
{
    json p = json::object();
    for (const auto& [grupo, cifrado] : m_paquetes) p[grupo] = cifrado;
    json j = { { "version", 1 }, { "paquetes", p } };
    if (!m_recordadas.empty()) j["recordadas"] = m_recordadas;
    return EscribirSeguro(m_archivo, j.dump(1));
}

void hnd::contrasenas::Guardadas::Fijar(const std::string& grupo, const std::vector<std::string>& lista)
{
    if (grupo.empty()) return;
    std::string cifrado = lista.empty() ? std::string() : Cifrar(lista);
    if (cifrado.empty()) m_paquetes.erase(grupo);
    else m_paquetes[grupo] = cifrado;
}

std::vector<std::string> hnd::contrasenas::Guardadas::Leer(const std::string& grupo) const
{
    auto it = m_paquetes.find(grupo);
    return it == m_paquetes.end() ? std::vector<std::string>() : Descifrar(it->second);
}

bool hnd::contrasenas::Guardadas::Tiene(const std::string& grupo) const
{
    return m_paquetes.count(grupo) > 0;
}

size_t hnd::contrasenas::Guardadas::QuitarIlegibles()
{
    // Las que no se pueden leer (el formato de antes, en JSON, de las versiones de prueba de la 0.4.1; o de otra cuenta u
    // otra PC) se quitan: su paquete pide la contraseña otra vez. Lo descifrado ya se borró al leerlas.
    size_t quitadas = 0;
    for (auto it = m_paquetes.begin(); it != m_paquetes.end();)
    {
        if (Legible(it->second))
        {
            ++it;
            continue;
        }
        Borrar(it->second);
        it = m_paquetes.erase(it);
        quitadas++;
    }
    if (!m_recordadas.empty() && !Legible(m_recordadas))
    {
        Borrar(m_recordadas);
        quitadas++;
    }
    return quitadas;
}

void hnd::contrasenas::Guardadas::QuedarseCon(const std::set<std::string>& grupos)
{
    for (auto it = m_paquetes.begin(); it != m_paquetes.end();)
    {
        if (grupos.count(it->first)) ++it;
        else it = m_paquetes.erase(it);
    }
}

void hnd::contrasenas::Guardadas::Recordar(const std::vector<std::string>& lista)
{
    // La más nueva primero; sin repetir; como mucho 20
    std::vector<std::string> todas = Recordadas();
    std::vector<std::string> nuevas;
    nuevas.reserve(lista.size() + todas.size());
    for (const auto& c : lista)
    {
        if (!c.empty() && std::find(nuevas.begin(), nuevas.end(), c) == nuevas.end()) nuevas.push_back(c);
    }
    for (const auto& c : todas)
    {
        if (std::find(nuevas.begin(), nuevas.end(), c) == nuevas.end()) nuevas.push_back(c);
    }
    // Las que sobran se borran antes de soltarlas (soltarlas sin borrar dejaría copias en la memoria)
    for (size_t i = MaximoRecordadas; i < nuevas.size(); i++) Borrar(nuevas[i]);
    if (nuevas.size() > MaximoRecordadas) nuevas.resize(MaximoRecordadas);
    std::string cifrado = CifrarLista(nuevas);
    if (!cifrado.empty()) m_recordadas = cifrado;
    Borrar(todas);
    Borrar(nuevas);
}

std::vector<std::string> hnd::contrasenas::Guardadas::Recordadas() const
{
    return DescifrarLista(m_recordadas, MaximoRecordadas);
}

void hnd::contrasenas::Guardadas::OlvidarRecordadas()
{
    m_recordadas.clear();
}
