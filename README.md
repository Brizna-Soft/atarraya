# Atarraya

**Gestor de descargas gratis para Windows.** Baja cada archivo en varias partes a la vez y se puede pausar y
seguir después, aunque apagues la PC. También recibe las descargas de tu navegador. Sin publicidad, sin virus y
sin recoger tus datos.

Hecho por [Brizna Soft](https://brizna-soft.github.io) en Honduras.

![Atarraya bajando una ISO en 8 partes a la vez](docs/captura.png)

## Descargar

**[Baja la última versión](https://github.com/Brizna-Soft/atarraya/releases/latest)**: el archivo
`Atarraya-x.y.z-x64.zip`.

1. Descomprime el .zip en una carpeta, por ejemplo `Documentos\Atarraya`.
2. Abre `Atarraya.exe`. No hace falta instalar nada.

> **¿Windows dice «Windows protegió su PC»?** Sale porque el programa es nuevo y todavía no tiene firma
> digital, que cuesta dinero. Pulsa **Más información** y luego **Ejecutar de todas formas**.
>
> Si quieres comprobar que el archivo es el original, compara su huella SHA-256 con el archivo `.sha256` de la
> misma página. Los .zip los arma GitHub a partir de este mismo código.

## Qué hace

- **Varias conexiones por archivo.** Hasta 16, y 8 de fábrica. Cuando una parte termina, ayuda a la que más le
  falta, así ninguna conexión se queda sin trabajo.
- **Pausar y seguir.** También después de cerrar el programa o de apagar la PC, si el servidor lo permite.
- **Reintenta solo** cuando se cae el internet.
- **Límite de velocidad**, para no dejar sin internet al resto de la casa.
- **Cola de descargas.** Elige cuántas bajan a la vez; las demás esperan su turno.
- **Cambiar el enlace.** Si un enlace vence, pegas uno nuevo del mismo archivo y sigue donde quedó.
- **Huella SHA-256**, para comprobar que el archivo llegó entero.
- **Cuida tu seguridad.** Marca lo bajado como «descargado de internet», igual que los navegadores, para que
  Windows y tu antivirus lo revisen antes de abrirlo.
- **Queda junto al reloj** mientras baja, y te avisa cuando termina.

## Conectar con el navegador

Atarraya toma las descargas de **cualquier sitio**, igual que IDM. Las únicas que se quedan en el navegador son
las que salen de un formulario y las que arma la misma página.

1. En Atarraya pulsa **Navegadores** → **Conectar**. Esto solo le avisa a tu navegador dónde está Atarraya,
   dentro de tu cuenta de Windows, y se puede quitar con **Quitar la conexión**.
2. Instala la extensión. Mientras llega a las tiendas, se carga a mano:
   - **Chrome, Edge o Brave:**
     1. Abre `chrome://extensions` (en Edge, `edge://extensions`).
     2. Activa el **Modo de desarrollador**.
     3. Pulsa **Cargar descomprimida** y elige la carpeta `extension\chrome` que viene dentro del .zip.
   - **Firefox:** la extensión necesita la firma de Mozilla, que está en trámite. Mientras tanto, usa el clic
     derecho → «Descargar con Atarraya» desde Chrome o Edge, o pega los enlaces en Atarraya.
3. Listo. Lo que bajes en el navegador se pasa a Atarraya. También puedes hacer clic derecho en un enlace y
   elegir **Descargar con Atarraya**.

Si Atarraya está cerrado, se abre solo junto al reloj. Si no está conectado, el navegador baja todo como siempre.

## Privacidad y seguridad

- Atarraya no tiene publicidad, no manda estadísticas y no se conecta a ningún servidor de Brizna Soft. Solo
  baja lo que tú le pides.
- Para que funcionen las descargas que piden haber iniciado sesión, la extensión le pasa a Atarraya, solo dentro
  de tu PC, las cookies del sitio de esa descarga. Atarraya no las guarda cuando la descarga termina y nunca las
  manda a otro sitio.
- No pide ser administrador y no cambia la configuración de Windows. «Conectar con los navegadores» solo
  escribe en tu propia cuenta (HKEY_CURRENT_USER), y «Quitar la conexión» lo borra.
- Tus ajustes y tu lista de descargas están en `%LOCALAPPDATA%\Atarraya`.
- El código está aquí completo, para que cualquiera lo revise.

## ¿Algo falla?

[Reporta el problema aquí](https://github.com/Brizna-Soft/atarraya/issues/new/choose): cuéntanos qué pasó y qué
esperabas. Si puedes, adjunta el archivo `%LOCALAPPDATA%\Atarraya\registro.txt`. Solo anota el nombre de los
sitios, no las direcciones completas.

## Apoyar

Atarraya es y seguirá siendo gratis. Si te sirve, puedes apoyar a Brizna Soft con una
[donación por PayPal](https://paypal.me/BriznaSoft) o simplemente recomendarlo.

## Compilar desde el código

Necesitas Visual Studio 2022 (o sus Build Tools) con C++ y Git. Luego:

```
powershell -ExecutionPolicy Bypass -File compilar.ps1
```

La primera vez baja vcpkg y compila wxWidgets y curl, y tarda unos minutos. El programa queda en `programa\`.

## Licencia y créditos

- Código de Atarraya: licencia MIT, © 2026 Brizna Soft. Hecho con ayuda de IA.
- Usa [curl](https://curl.se), [wxWidgets](https://www.wxwidgets.org) y
  [nlohmann/json](https://github.com/nlohmann/json), entre otras bibliotecas. Sus licencias están en
  `AVISOS-DE-TERCEROS.txt`.
