// Prueba de la extensión sin navegador (no se publica):   node pruebas/prueba-extension.js
// De qué página salió una descarga: la pestaña real donde nació su pedido, nunca la pestaña activa. Los videos de
// cada pestaña y de sus marcos (medios.js): qué es un video, una lista HLS o DASH o un trozo, nada de YouTube ni de
// sitios con DRM, y lo que se le pasa a Atarraya.

const enNode = typeof require === "function" && typeof module !== "undefined";
const { PestanasDePedidos: Pestanas, paginaDeOrigen: origen } = enNode
  ? require("../extension/comun/origen.js")
  : { PestanasDePedidos, paginaDeOrigen }; // en un navegador, origen.js y medios.js se cargan antes que este archivo
const M = enNode
  ? require("../extension/comun/medios.js")
  : { esYouTube, esSitioProtegido, cadenaBloqueada, motivoBloqueo, clasificar, tamanoDe, nombreDeMedio, cabecerasDe,
      MediosPorPestana, pedidoDeVideo, sitiosDeArriba, TEXTO_YOUTUBE, TEXTO_SIN_TRANSMISION, TEXTO_SIN_ARCHIVO, TEXTO_SIN_HABLAR,
      TEXTO_FALLO_EXTENSION, TEXTO_EXTENSION_REINICIADA, TEXTO_EXTENSION_CALLADA, puedeSerLista, formatoPorContenido,
      puedeSerTextoDeLista, leerHls, duracionDash, elegirLista, revisarLista, listaDelVideo, textoSinRespuesta, MAXIMO_MIRADAS };
const E = enNode ? require("../extension/comun/esperas.js") : { normalizarTexto, segundosDe, reconocerEspera };
const C = enNode
  ? require("../extension/comun/cola-navegador.js")
  : { PestanasDeLaCola, paginaPorAbrir, sitioBase, avisoDeSeguimiento, TomaDelNavegador, resultadoCancelar, resolverSinConfirmar, tocaFinal,
      finalEntregado, seguidasGuardadas, nuevaInstancia, instanciaValida, tomaNueva, instanciaDeToma, informeDeSeguidas, despuesDeGuardar };

let fallas = 0;
function decir(texto) {
  console.log(texto);
  if (typeof document !== "undefined") document.body.append(texto + "\n");
}
function comprobar(ok, que) {
  decir((ok ? "bien " : "FALLA") + "  " + que);
  if (!ok) fallas++;
}

// En qué pestaña nació cada pedido
const p = new Pestanas(120000, 3);
const archivo = "https://s1.ddownload.com/d/abc/peli.part1.rar";
p.anotar(archivo, 7, 1000);
p.anotar("https://otro.org/a", -1, 1000); // un pedido que no es de una pestaña (de la propia extensión, por ejemplo)
comprobar(p.buscar([archivo], 2000) === 7, "extensión: la pestaña donde nació el pedido de la descarga");
comprobar(p.buscar(["https://otro.org/a"], 2000) === -1 && p.buscar(["https://no-se-vio.org/"], 2000) === -1,
  "extensión: un pedido sin pestaña o que no se vio no da ninguna");
comprobar(p.buscar(["", archivo], 2000) === 7, "extensión: sirve la dirección antes o después de las redirecciones");
comprobar(p.buscar([archivo], 1000 + 121000) === -1, "extensión: un pedido de hace más de dos minutos ya no cuenta");
p.anotar("https://x.org/1", 1, 5000);
p.anotar("https://x.org/2", 2, 5000);
p.anotar("https://x.org/3", 3, 5000);
comprobar(p.mapa.size <= 3 && p.buscar([archivo], 5000) === -1 && p.buscar(["https://x.org/3"], 5000) === 3,
  "extensión: recuerda pocos pedidos y olvida los más viejos");

// La página de origen: la de esa pestaña o, si la descarga abrió una pestaña nueva, la de la que la abrió
const item = { url: archivo, finalUrl: "https://cdn.ddownload.com/d/abc/peli.part1.rar" };
const pagina = { url: "https://ddownload.com/abc123def/peli.part1.rar" };
comprobar(origen(item, pagina, null) === pagina.url, "extensión: la página de la pestaña donde se pulsó descargar");
comprobar(origen(item, { url: "about:blank" }, pagina) === pagina.url && origen(item, { url: archivo }, pagina) === pagina.url,
  "extensión: si la descarga abrió una pestaña nueva, la página de la que la abrió");
comprobar(origen(item, null, null) === "" && origen(item, { url: "chrome://newtab/" }, null) === "",
  "extensión: si no se sabe, nada (no se adivina con la pestaña activa)");

// --- Videos (medios.js) ------------------------------------------------------------------------------------------------
const c = (url, tipo) => M.clasificar(url, tipo) || {};
comprobar(c("https://cdn.ej.org/v/peli.mp4", "video/mp4").formato === "archivo" && c("https://cdn.ej.org/v/peli.mp4", "video/mp4").clase === "video"
  && c("https://cdn.ej.org/v/peli.webm", "").formato === "archivo" && c("https://cdn.ej.org/v/peli.MKV?t=1", "application/octet-stream").formato === "archivo"
  && c("https://cdn.ej.org/a/cancion.mp3", "audio/mpeg").clase === "audio" && c("https://cdn.ej.org/a/x.m4a", "").clase === "audio",
  "videos: un archivo de video o de audio (por su tipo o por su extensión)");
comprobar(c("https://cdn.ej.org/hls/master.m3u8", "application/vnd.apple.mpegurl").formato === "hls"
  && c("https://cdn.ej.org/hls/lista?id=3", "application/x-mpegURL").formato === "hls"
  && c("https://cdn.ej.org/hls/lista.m3u8", "text/plain").formato === "hls" && c("https://cdn.ej.org/hls/x", "audio/mpegurl").formato === "hls",
  "videos: una lista HLS (.m3u8 o tipo mpegurl)");
comprobar(c("https://cdn.ej.org/d/manifiesto.mpd", "").formato === "dash" && c("https://cdn.ej.org/d/m?x=1", "application/dash+xml").formato === "dash",
  "videos: una lista DASH (.mpd o application/dash+xml)");
comprobar(c("https://cdn.ej.org/hls/seg-12.ts", "video/mp2t").trozo && c("https://cdn.ej.org/d/v/3.m4s", "video/iso.segment").trozo
  && c("https://cdn.ej.org/d/chunk_3.m4a", "audio/mp4").trozo && c("https://cdn.ej.org/d/init.mp4", "video/mp4").trozo
  && c("https://cdn.ej.org/d/frag5.mp4", "video/mp4").trozo && c("https://cdn.ej.org/x/a.cmfv", "").trozo
  && c("https://cdn.ej.org/x/raro", "video/mp2t").trozo,
  "videos: los trozos de una transmisión (.ts, .m4s, init, seg-12, chunk_3...) no son videos enteros");
comprobar(M.clasificar("https://ej.org/pagina.html", "text/html") === null && M.clasificar("https://ej.org/peli.mp4", "text/html") === null
  && M.clasificar("https://ej.org/foto.jpg", "image/jpeg") === null && M.clasificar("blob:https://ej.org/1234", "video/mp4") === null
  && M.clasificar("ftp://ej.org/peli.mp4", "") === null && M.clasificar("no es una dirección", "video/mp4") === null,
  "videos: una página (aunque se llame .mp4), una imagen, un blob: o un ftp: no son videos");
comprobar(M.tamanoDe(206, { "content-range": "bytes 0-1023/5000000" }) === 5000000 && M.tamanoDe(200, { "content-length": "1234" }) === 1234
  && M.tamanoDe(206, { "content-range": "bytes 0-1023/*" }) === -1 && M.tamanoDe(206, { "content-length": "1024" }) === -1
  && M.tamanoDe(200, {}) === -1,
  "videos: el tamaño entero sale del total de Content-Range (206) o de Content-Length (200)");
comprobar(M.nombreDeMedio("https://cdn/x/a.mp4", 'attachment; filename="Mi video.mp4"') === "Mi video.mp4"
  && M.nombreDeMedio("https://cdn/x/a.mp4", "attachment; filename*=UTF-8''Pel%C3%ADcula.mp4") === "Película.mp4"
  && M.nombreDeMedio("https://cdn/x/Mi%20video.webm?t=1", "") === "Mi video.webm",
  "videos: el nombre del servidor o el del final de la dirección");
comprobar(JSON.stringify(M.cabecerasDe([{ name: "Content-Type", value: "video/mp4" }, { name: "Content-Length", value: "9" }]))
  === JSON.stringify({ "content-type": "video/mp4", "content-length": "9" }),
  "videos: las cabeceras de webRequest con claves en minúsculas");

// Nada de YouTube (tampoco googlevideo.com ni después de una redirección) y nada de sitios con DRM
comprobar(["https://www.youtube.com/watch?v=x", "https://m.youtube.com/", "https://youtu.be/x", "https://www.youtube-nocookie.com/embed/x",
  "https://rr3---sn-abc.googlevideo.com/videoplayback?x=1", "https://%79outube.com/", "https://www.youtubekids.com/", "https://youtube.com./"]
  .every(M.esYouTube) && !M.esYouTube("https://notyoutube.com/") && !M.esYouTube("https://youtube.com.ejemplo.org/") && !M.esYouTube("nada"),
  "videos: YouTube, youtu.be, youtube-nocookie.com, m.youtube.com y googlevideo.com quedan fuera");
comprobar(M.cadenaBloqueada(["https://cdn.ejemplo.org/v.mp4", "https://r1---sn.googlevideo.com/videoplayback"])
  && !M.cadenaBloqueada(["https://cdn.ejemplo.org/v.mp4", "https://cdn2.ejemplo.org/v.mp4", undefined]),
  "videos: una redirección que pasa por googlevideo.com también queda fuera");
comprobar(M.esSitioProtegido("https://www.netflix.com/watch/1") && M.esSitioProtegido("https://tv.apple.com/x")
  && M.esSitioProtegido("https://www.disneyplus.com/") && M.esSitioProtegido("https://ipv4-c001.1.nflxvideo.net/range/0-1")
  && M.esSitioProtegido("https://www.amazon.com/gp/video/detail/x") && M.esSitioProtegido("https://www.clarovideo.com/")
  && !M.esSitioProtegido("https://www.apple.com/") && !M.esSitioProtegido("https://www.amazon.com/dp/B0")
  && !M.esSitioProtegido("https://3dsmax.com/") && !M.esSitioProtegido("https://archive.org/details/x"),
  "videos: Netflix, Disney+, Prime Video, Apple TV+, Claro video y sus servidores quedan fuera (sitios con DRM)");

// Los videos de una pestaña con todos sus marcos: cada uno recuerda el suyo
{
  const m = new M.MediosPorPestana(5);
  m.marco(7, 0, "https://pagina.org/ver/1");
  m.marco(7, 3, "https://reproductor.net/embed/9");
  m.marco(7, 4, "about:blank");   // no es una dirección web: no cuenta
  m.anotar(7, { url: "https://cdn.pagina.org/a.mp4", formato: "archivo", clase: "video", tamano: 5000000, tipoMime: "video/mp4", frameId: 0 }, 1000);
  m.anotar(7, { url: "https://cdn.reproductor.net/hls/master.m3u8", formato: "hls", frameId: 3 }, 2000);
  m.anotar(8, { url: "https://otra.org/b.mp4", formato: "archivo", tamano: 9000000, frameId: 0 }, 1500);
  m.muestra(7, 3, "https://seg.cdn2.net/s/1.ts");
  m.muestra(7, 3, "https://seg.cdn2.net/s/2.ts");
  m.muestra(7, 3, "https://r1.googlevideo.com/videoplayback");
  const l = m.lista(7);
  comprobar(l.length === 2 && l[0].url === "https://cdn.pagina.org/a.mp4" && l[0].marco === "https://pagina.org/ver/1"
    && l[1].formato === "hls" && l[1].marco === "https://reproductor.net/embed/9" && m.lista(8).length === 1
    && m.urlDeMarco(7, 4, "respaldo") === "respaldo",
    "videos: la lista junta los videos de la pestaña y de sus marcos; cada uno con la dirección de su marco");
  comprobar(m.muestrasDeMarco(7, 3).length === 1 && m.muestrasDeMarco(7, 3)[0] === "https://seg.cdn2.net/s/1.ts" && m.muestrasDeMarco(7, 0).length === 0,
    "videos: se recuerdan los servidores de los trozos de cada marco (para sus cookies), sin googlevideo.com");
  comprobar(m.transmisionDe(7, 3).url === "https://cdn.reproductor.net/hls/master.m3u8" && m.transmisionDe(7, 0) === null
    && m.transmisionDe(7, 5) === null && m.transmisionDe(8, 0) === null && m.transmisionDe(99, 0) === null,
    "videos: un video «blob:» usa solo la lista HLS o DASH de su propio marco (nunca la de otro marco de la pestaña)");
  comprobar(M.TEXTO_SIN_TRANSMISION === "No encontré este video todavía: dale play y vuelve a intentar.",
    "videos: un «blob:» sin lista en su marco no baja nada y lo dice («dale play y vuelve a intentar»)");

  // La misma dirección otra vez (un video que se reproduce por partes, 206): una sola entrada, con el tamaño entero
  m.anotar(7, { url: "https://cdn.pagina.org/a.mp4", formato: "archivo", tamano: 7000000, frameId: 0 }, 3000);
  m.anotar(7, { url: "https://cdn.pagina.org/a.mp4", formato: "archivo", tamano: -1, frameId: 0 }, 3500);
  comprobar(m.lista(7).filter((e) => e.url === "https://cdn.pagina.org/a.mp4").length === 1 && m.buscar(7, "https://cdn.pagina.org/a.mp4").tamano === 7000000,
    "videos: la misma dirección no se repite y se queda con el tamaño entero");
  comprobar(m.anotar(7, { url: "https://r4---sn.googlevideo.com/videoplayback?id=1", formato: "archivo", frameId: 0 }) === null
    && m.anotar(7, { url: "https://www.netflix.com/v.mp4", formato: "archivo", frameId: 0 }) === null
    && !m.lista(7).some((e) => M.esYouTube(e.url)),
    "videos: nada de YouTube ni de sitios con DRM entra a la lista");

  // Un video con DRM en el marco 3: sus listas quedan protegidas (también las que lleguen después) y van al final
  m.protegerMarco(7, 3, "");
  m.anotar(7, { url: "https://cdn.reproductor.net/dash/m.mpd", formato: "dash", frameId: 3 }, 4000);
  const p = m.lista(7);
  comprobar(p.length === 3 && !p[0].protegido && p[1].protegido && p[2].protegido && p[2].formato === "dash",
    "videos: un video protegido (DRM) protege las listas de su marco y va al final de la lista");

  // Lo guardado en storage.session vuelve igual, sin pisar lo que ya había en memoria
  const guardado = JSON.parse(JSON.stringify(m.exportar()));
  const otra = new M.MediosPorPestana(5);
  otra.anotar(7, { url: "https://cdn.pagina.org/nuevo.webm", formato: "archivo", tamano: 3000000, frameId: 0 }, 5000);
  otra.cargar(guardado);
  otra.cargar(null);
  otra.cargar("roto");
  const urls = otra.lista(7).map((e) => e.url);
  comprobar(urls.length === 4 && urls.includes("https://cdn.pagina.org/nuevo.webm") && urls.includes("https://cdn.reproductor.net/hls/master.m3u8")
    && otra.urlDeMarco(7, 3, "") === "https://reproductor.net/embed/9" && otra.lista(7).find((e) => e.formato === "hls").protegido
    && otra.muestrasDeMarco(7, 3).length === 1,
    "videos: la lista se guarda y vuelve (con sus marcos) aunque el «service worker» se apague");

  // Cuando un marco cambia de página se olvida lo suyo; cuando la pestaña cambia, todo
  m.borrarMarco(7, 3);
  comprobar(m.lista(7).length === 1 && m.urlDeMarco(7, 3, "") === "" && m.transmisionDe(7, 0) === null,
    "videos: un marco que cambia de página se olvida");
  m.borrar(7);
  comprobar(m.lista(7).length === 0 && m.lista(8).length === 1, "videos: una pestaña que cambia de página empieza de cero");

  // Tope: se van las más viejas
  const t = new M.MediosPorPestana(5);
  for (let i = 1; i <= 7; i++) t.anotar(1, { url: "https://ej.org/v" + i + ".mp4", formato: "archivo", tamano: 5000000, frameId: 0 }, i);
  const lt = t.lista(1).map((e) => e.url);
  comprobar(lt.length === 5 && lt[0] === "https://ej.org/v3.mp4" && lt[4] === "https://ej.org/v7.mp4",
    "videos: hay un tope por pestaña y se van los más viejos");
}

// YouTube que llega por una redirección o por un marco que reproduce desde googlevideo.com: nada de ese marco
{
  const m = new M.MediosPorPestana();
  m.marco(3, 0, "https://noticias.org/nota");
  m.marco(3, 2, "https://reproductor.net/embed/1");
  m.anotar(3, { url: "https://cdn.noticias.org/a.mp4", formato: "archivo", tamano: 5000000, frameId: 0 }, 1);
  m.anotar(3, { url: "https://reproductor.net/hls/master.m3u8", formato: "hls", frameId: 2 }, 2);
  m.bloquearMarco(3, 2, M.motivoBloqueo(["https://rr1---sn.googlevideo.com/videoplayback"]));
  m.anotar(3, { url: "https://reproductor.net/hls/otra.m3u8", formato: "hls", frameId: 2 }, 3);
  const l = m.lista(3);
  comprobar(l.length === 1 && l[0].frameId === 0 && m.marcoBloqueado(3, 2) === "youtube" && m.marcoBloqueado(3, 0) === ""
    && m.transmisionDe(3, 2) === null,
    "videos: un marco que reproduce desde googlevideo.com no sale en la lista (ni lo de antes ni lo de después)");
  m.quitar(3, ["https://cdn.noticias.org/a.mp4"]);
  comprobar(m.lista(3).length === 0 && m.motivoDePestana(3) === "youtube" && m.motivoDePestana(99) === "",
    "videos: una dirección que redirige a YouTube se quita, y la ventanita sabe por qué no hay nada");
  const otra = new M.MediosPorPestana();
  otra.cargar(JSON.parse(JSON.stringify(m.exportar())));
  comprobar(otra.marcoBloqueado(3, 2) === "youtube" && otra.anotar(3, { url: "https://reproductor.net/x.mp4", formato: "archivo", frameId: 2 }) === null,
    "videos: el marco bloqueado se recuerda aunque el «service worker» se apague");
  m.borrarMarco(3, 2);
  comprobar(m.marcoBloqueado(3, 2) === "", "videos: si ese marco cambia de página, empieza de cero");
  comprobar(M.motivoBloqueo(["https://ej.org/a", "https://www.netflix.com/x"]) === "protegido" && M.motivoBloqueo(["https://ej.org/a", undefined]) === ""
    && M.motivoBloqueo(["https://www.netflix.com/", "https://youtu.be/x"]) === "youtube",
    "videos: el motivo (YouTube o sitio con DRM) de una cadena de direcciones");
}

// Las listas de cada calidad (o del audio) que pide un reproductor HLS justo después de la principal no salen aparte
{
  const m = new M.MediosPorPestana();
  m.anotar(4, { url: "https://ej.org/hls/master.m3u8", formato: "hls", frameId: 0 }, 1000);
  m.anotar(4, { url: "https://ej.org/hls/720p/index.m3u8", formato: "hls", frameId: 0 }, 1200);
  m.anotar(4, { url: "https://ej.org/hls/audio/index.m3u8", formato: "hls", frameId: 0 }, 1300);
  m.anotar(4, { url: "https://otro.net/hls/master.m3u8", formato: "hls", frameId: 6 }, 1400);
  m.anotar(4, { url: "https://ej.org/hls2/master.m3u8", formato: "hls", frameId: 0 }, 61000);
  const urls = m.lista(4).map((e) => e.url);
  comprobar(JSON.stringify(urls) === JSON.stringify(["https://ej.org/hls/master.m3u8", "https://otro.net/hls/master.m3u8", "https://ej.org/hls2/master.m3u8"])
    && m.buscar(4, "https://ej.org/hls/720p/index.m3u8") !== null,
    "videos: las listas que llegan justo después de la principal no salen aparte (las de otro marco o de otro momento, sí)");
}

// El orden de la ventanita: video antes que solo audio, lo protegido al final; sin los muy chicos ni los trozos chicos
{
  const m = new M.MediosPorPestana();
  m.anotar(1, { url: "https://ej.org/cancion.mp3", formato: "archivo", clase: "audio", tamano: 4000000, frameId: 0 }, 1);
  m.anotar(1, { url: "https://ej.org/ding.mp3", formato: "archivo", clase: "audio", tamano: 9000, frameId: 0 }, 2);
  m.anotar(1, { url: "https://ej.org/peli.mp4", formato: "archivo", clase: "video", tamano: 90000000, frameId: 0 }, 3);
  m.anotar(1, { url: "https://ej.org/sin-tamano.webm", formato: "archivo", clase: "video", tamano: -1, frameId: 0 }, 4);
  m.anotar(1, { url: "https://ej.org/protegido.mp4", formato: "archivo", clase: "video", tamano: 9000000, frameId: 0, protegido: true }, 5);
  const sinLista = m.lista(1).map((e) => e.url);
  m.anotar(1, { url: "https://ej.org/hls/master.m3u8", formato: "hls", frameId: 0 }, 6);
  m.anotar(1, { url: "https://ej.org/hls/v720.mp4", formato: "archivo", clase: "video", tamano: 900000, frameId: 0 }, 7);
  m.anotar(1, { url: "https://otro-sitio.net/corto.mp4", formato: "archivo", clase: "video", tamano: 900000, frameId: 5 }, 8);
  const conLista = m.lista(1).map((e) => e.url);
  comprobar(JSON.stringify(sinLista) === JSON.stringify(["https://ej.org/peli.mp4", "https://ej.org/sin-tamano.webm", "https://ej.org/cancion.mp3",
    "https://ej.org/protegido.mp4"]),
    "videos: en la ventanita va primero el video, después el audio y al final lo protegido; lo muy chico no se muestra");
  comprobar(conLista.includes("https://ej.org/hls/master.m3u8") && !conLista.includes("https://ej.org/hls/v720.mp4")
    && conLista.includes("https://ej.org/peli.mp4") && conLista.includes("https://otro-sitio.net/corto.mp4"),
    "videos: un «archivo» chico en el marco de una lista HLS o DASH es un trozo y no se muestra (en otro marco, sí)");
}

// Lo que se le pasa a Atarraya: exactamente estos campos; el «referer» es el del marco del video
{
  const e = { url: "https://cdn.reproductor.net/hls/master.m3u8", formato: "hls", tamano: -1, tipoMime: "application/vnd.apple.mpegurl" };
  const p = M.pedidoDeVideo(e, "https://pagina.org/ver/1", "  " + "T".repeat(400) + "  ", "https://reproductor.net/embed/9", "linea", "Agente/1");
  comprobar(JSON.stringify(Object.keys(p).sort()) === JSON.stringify(["agente", "cookies", "formato", "pagina", "referer", "tamano", "tipo", "tipoMime", "titulo", "url"])
    && p.tipo === "video" && p.formato === "hls" && p.url === e.url && p.referer === "https://reproductor.net/embed/9"
    && p.pagina === "https://pagina.org/ver/1" && p.titulo.length === 300 && p.titulo[0] === "T" && p.tamano === -1
    && p.cookies === "linea" && p.agente === "Agente/1",
    "videos: el pedido a Atarraya lleva la página de arriba y, como «referer», la del marco donde está el video");
  const sinMarco = M.pedidoDeVideo({ url: "https://ej.org/a.mp4", formato: "archivo", tamano: 5 }, "https://ej.org/", "", "", "", "");
  comprobar(sinMarco.referer === "https://ej.org/" && sinMarco.tamano === 5 && sinMarco.titulo === "" && sinMarco.tipoMime === "",
    "videos: sin marco conocido, el «referer» es la página");
  comprobar(M.pedidoDeVideo({ url: "https://ej.org/l.mpd", formato: "dash", tamano: 67 }, "https://ej.org/", "", "", "", "").tamano === -1,
    "videos: el tamaño de una lista HLS o DASH no se manda como si fuera el del video");
  comprobar(JSON.stringify(M.sitiosDeArriba("https://www.ejemplo.org/x")) === JSON.stringify(["https://www.ejemplo.org", "https://ejemplo.org"])
    && JSON.stringify(M.sitiosDeArriba("https://ejemplo.org/")) === JSON.stringify(["https://ejemplo.org"])
    && M.sitiosDeArriba("about:blank").length === 0 && M.sitiosDeArriba("").length === 0,
    "videos: los sitios de arriba para las cookies partidas de un marco de otro sitio");
}

// --- Listas que no dicen que lo son (como las de LinkedIn) ------------------------------------------------------------
// Las direcciones de LinkedIn: sin extensión, con partes numeradas
const LI = (n) => "https://dms.licdn.com/playlist/vid/v2/D5605AQprueba/hls-2Mbps-premium-quality-analysis/9f8e7d6c5b/" + n
  + "/1730000000000?e=1730003600&v=beta&t=abc";
comprobar(c(LI(1), "application/vnd.apple.mpegurl").formato === "hls" && c(LI(1), "application/x-mpegURL").formato === "hls"
  && c(LI(1), "application/dash+xml").formato === "dash",
  "videos: una lista sin extensión en la dirección se reconoce por su tipo (mpegurl, x-mpegURL, dash+xml)");
comprobar(M.puedeSerLista(LI(1), "application/octet-stream", 1382, "GET") && M.puedeSerLista(LI(1), "binary/octet-stream", -1, "GET")
  && M.puedeSerLista(LI(1), "", 1382, "GET") && M.puedeSerLista(LI(1), "text/plain; charset=utf-8", 1382, undefined)
  && M.puedeSerLista("https://cdn.ej.org/x/abc", "application/octet-stream", 900000, "GET"),
  "videos: una respuesta sin extensión y con un tipo que no dice nada puede ser una lista (se anota para mirarla si se pulsa el botón)");
comprobar(!M.puedeSerLista(LI(1), "application/octet-stream", 1382, "POST") && !M.puedeSerLista(LI(1), "application/json", 1382, "GET")
  && !M.puedeSerLista(LI(1), "image/gif", 43, "GET") && !M.puedeSerLista("https://ej.org/app.js", "application/octet-stream", 900, "GET")
  && !M.puedeSerLista("https://ej.org/x/seg-12.ts", "application/octet-stream", 900, "GET")
  && !M.puedeSerLista("https://ej.org/api/beacon", "text/plain", 20, "GET") && !M.puedeSerLista(LI(1), "application/octet-stream", 50 * 1024 * 1024, "GET")
  && !M.puedeSerLista("blob:https://ej.org/1", "", 10, "GET") && !M.puedeSerLista("nada", "", 10, "GET"),
  "videos: nada de lo que no puede ser una lista (otro método, JSON, imágenes, scripts, trozos, texto sin pista de video, muy grande)");
const listaHls = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.000,\n../2/1730000000000?e=1\n#EXTINF:4.0,\n../3/x\n"
  + "#EXTINF:3.5,\n../4/x\n#EXT-X-ENDLIST\n";
const maestra = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720\nhls-720/lista?x=1\n#EXT-X-STREAM-INF:BANDWIDTH=800000\nhttps://otro.cdn/480.m3u8\n";
const mpd = '<?xml version="1.0" encoding="UTF-8"?>\n<!-- hecho a mano -->\n<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" '
  + 'mediaPresentationDuration="PT1M3.5S" minBufferTime="PT2S"><Period/></MPD>';
comprobar(M.formatoPorContenido(listaHls) === "hls" && M.formatoPorContenido("\uFEFF  \r\n#EXTM3U\n") === "hls" && M.formatoPorContenido(mpd) === "dash"
  && M.formatoPorContenido('<mpd:MPD xmlns:mpd="urn:mpeg:dash:schema:mpd:2011">') === "dash" && M.formatoPorContenido("<html><body>") === ""
  && M.formatoPorContenido("\u0000\u0000\u0000\u0018ftypiso5") === "" && M.formatoPorContenido("G@\u0011\u0010") === "" && M.formatoPorContenido(null) === ""
  && M.formatoPorContenido("<?xml version='1.0'?><MPDX/>") === "",
  "videos: por dentro, una lista HLS empieza con #EXTM3U y un manifiesto DASH es un <MPD> (lo demás no)");
comprobar(M.puedeSerTextoDeLista("#EXT") && M.puedeSerTextoDeLista("#EXTM3U\n#EXT-X") && M.puedeSerTextoDeLista("  <?xml")
  && !M.puedeSerTextoDeLista("\u0000\u0000\u0000\u0018ftypiso5") && !M.puedeSerTextoDeLista("{\"a\":1, \"b\": 2}"),
  "videos: al mirar por dentro, se deja de bajar en cuanto el principio no es de una lista");
const h1 = M.leerHls(listaHls, LI(1));
const h2 = M.leerHls(maestra, "https://cdn.ej.org/v/master?id=7");
const h3 = M.leerHls("#EXTM3U\n#EXTINF:4,\na\n#EXTINF:4,\nb\n", "https://ej.org/vivo");
comprobar(!h1.maestra && h1.duracion === 11.5 && h2.maestra && h2.primera === "https://cdn.ej.org/v/hls-720/lista?x=1" && h2.duracion === -1
  && h3.duracion === -1,
  "videos: una lista HLS dice cuánto dura (la suma de sus partes); la principal, cuál es su primera calidad; en vivo, no se sabe");
comprobar(M.duracionDash(mpd) === 63.5 && M.duracionDash('<MPD mediaPresentationDuration="PT16S">') === 16
  && M.duracionDash('<MPD mediaPresentationDuration="P0DT1H0M0S">') === 3600 && M.duracionDash('<MPD type="dynamic">') === -1
  && M.duracionDash('<MPD mediaPresentationDuration="mucho">') === -1,
  "videos: un manifiesto DASH dice cuánto dura");
{
  const a = { url: "a" }, b = { url: "b" }, v = { url: "v" }, x = { url: "x" };
  const el = (listas, d) => M.elegirLista(listas, d);
  const sin = (r, motivo) => r.entrada === null && r.motivo === motivo;
  comprobar(el([{ entrada: a, duracion: -1, orden: 1000 }], 0).entrada === a && sin(el([], 16), "ninguna") && sin(el(null, 16), "ninguna"),
    "videos: sin la duración del video y con una sola lista posible, esa");
  comprobar(sin(el([{ entrada: b, duracion: -1, orden: 1200 }, { entrada: a, duracion: -1, orden: 1000 }], 0), "cual")
    && sin(el([{ entrada: a, duracion: -1, orden: 1000 }, { entrada: b, duracion: -1, orden: 60000 }], 0), "cual"),
    "videos: sin la duración del video y con listas de dos videos (aunque lleguen juntas), ninguna: no se sabe cuál es");
  comprobar(el([{ entrada: a, duracion: -1, maestra: true, referencias: ["v", "x"], orden: 1 }, { entrada: v, duracion: -1, orden: 2 },
    { entrada: x, duracion: -1, orden: 3 }], 0).entrada === a
    && sin(el([{ entrada: a, duracion: -1, maestra: true, referencias: ["v"], orden: 1 }, { entrada: v, duracion: -1, orden: 2 },
      { entrada: b, duracion: -1, orden: 3 }], 0), "cual"),
    "videos: sin la duración, la principal si nombra a todas las demás (son sus calidades); si sobra una, ninguna");
  comprobar(el([{ entrada: a, duracion: 16, orden: 1 }, { entrada: b, duracion: 24, orden: 2 }], 24.02).entrada === b
    && el([{ entrada: a, duracion: 16, orden: 1 }, { entrada: b, duracion: 24, orden: 2 }], 16.4).entrada === a,
    "videos: con varios videos en la página (como el inicio de LinkedIn), la lista que dura lo mismo que el video pulsado");
  comprobar(el([{ entrada: v, duracion: 16, maestra: false, orden: 2 }, { entrada: a, duracion: 16, maestra: true, referencias: ["v"], orden: 1 }], 16).entrada === a,
    "videos: entre la principal y una de sus calidades, la principal (Atarraya deja elegir la calidad)");
  comprobar(sin(el([{ entrada: a, duracion: 16, orden: 1 }, { entrada: b, duracion: 16, orden: 2 }], 16), "cual")
    && sin(el([{ entrada: v, duracion: 16, maestra: false, orden: 2 }, { entrada: a, duracion: 16, maestra: true, orden: 1 }], 16), "cual"),
    "videos: dos listas que duran lo mismo y no son del mismo video (o no se sabe), ninguna");
  comprobar(sin(el([{ entrada: a, duracion: 16, orden: 1 }, { entrada: x, duracion: -1, orden: 3 }], 50), "cual")
    && sin(el([{ entrada: x, duracion: -1, orden: 3 }], 50), "cual"),
    "videos: si no se pudo saber cuánto dura una lista, no se elige ninguna sin comprobar (dice que no sabe cuál es)");
  comprobar(sin(el([{ entrada: a, duracion: 16, orden: 1 }, { entrada: b, duracion: 24, orden: 2 }], 50), "otros"),
    "videos: si todas dicen otra duración, ninguna (son de otros videos)");
}
comprobar(M.TEXTO_CUAL_VIDEO === "No encontré cuál de los videos de la página es este.",
  "videos: el motivo cuando no se sabe cuál de las listas es la del video");
// Lo que va al registro de Atarraya desde la extensión: solo el sitio y el tipo de error
{
  const e = new TypeError("falló https://ana:se(cre)to@x.org/?clave=se(cre)to");
  e.stack = "TypeError: falló https://ana:se(cre)to@x.org/?clave=se(cre)to\n"
    + "    at bajarVideoDeMarco (chrome-extension://mnppdeogjpkdgkbmknkaddjcakoofgaf/background.js:474:57)\n"
    + "    at chrome-extension://mnppdeogjpkdgkbmknkaddjcakoofgaf/background.js:1062:7\n"
    + "    at chrome-extension://mnppdeogjpkdgkbmknkaddjcakoofgaf/background.js:474:57\n"
    + "    at otra (https://x.org/de-la-pagina.js:1:2)";
  const r1 = M.resumenDeFallo("botón «Descargar este video»", e, "www.linkedin.com");
  comprobar(r1 === "botón «Descargar este video»: TypeError en background.js:474:57, background.js:1062:7 (sitio: www.linkedin.com)",
    "registro: de un error de la extensión va solo dónde (su archivo y línea), su tipo y el sitio; nunca su mensaje");
  const f = new Error("x");
  f.name = "Nombre con espacios y ana:se(cre)to@x.org";
  f.stack = "bajar@moz-extension://1234-abcd/medios.js:10:5\nfuera@moz-extension://1234-abcd/no-es-nuestro.js:3:3";
  comprobar(M.resumenDeFallo("«Bajar» de la ventanita", f, "ana:x@sitio.org") === "«Bajar» de la ventanita: Error en medios.js:10:5"
    && M.resumenDeFallo("algo", null, "") === "algo: Error" && M.resumenDeFallo("algo", "texto con ana:clave@x.org", "") === "algo: Error",
    "registro: un tipo de error raro, un sitio que no es un sitio y lo que no es un error no pasan");
  const L = M.limpiarParaRegistro;
  const t1 = L("Error: falló https://ana:clave123@cdn.ejemplo.org:8443/v/1/lista.m3u8?token=abc123&e=99#x en algo");
  const t2 = L("visita https://cdn.ejemplo.org:8443/v/1/lista.m3u8?token=abc123 y (https://otro.org/x?y=1) también");
  const t3 = L("Cookie: li_at=AQEDAR1aBc2dEf3gHi4jKl5; JSESSIONID=\"ajax:5551234\" de persona.real@correo-ejemplo.org con ftp usuario:secreto@archivos.org/x");
  const t4 = L("clave suelta eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0 y un nombreMuyLargoDeFuncionSinNumeros");
  const t5 = L("con paréntesis usuario:cla(ve)@sitio.org https://u:p(a)ss@sitio.org/x pagina?clave=se(cre)to y a=b(c)d");
  comprobar(t1 === "Error: falló (…) en algo" && t2 === "visita https://cdn.ejemplo.org y https://otro.org también",
    "registro: de una dirección queda solo su sitio; si trae usuario o clave, nada");
  comprobar(t3 === "Cookie: li_at=… JSESSIONID=… de (…) con ftp (…)",
    "registro: sin valores de cookies ni de parámetros, sin correos y sin nada con «@»");
  comprobar(t5 === "con paréntesis (…) (…) pagina y a=…" && !/cla|se\(|cre|p\(a/.test(t5),
    "registro: tampoco pasan claves con paréntesis (en «usuario:clave@», en direcciones o en parámetros)");
  comprobar(t4 === "clave suelta … y un nombreMuyLargoDeFuncionSinNumeros" && L(null) === "" && L("  a\n\tb ") === "a b"
    && L(r1) === r1,
    "registro: sin cadenas largas que parecen claves (los nombres sin números quedan); una sola línea; lo de resumenDeFallo pasa igual");
}
comprobar(M.textoSinRespuesta(new Error("Extension context invalidated."), true) === M.TEXTO_EXTENSION_REINICIADA
  && M.textoSinRespuesta(null, false) === M.TEXTO_EXTENSION_REINICIADA
  && M.textoSinRespuesta(new Error("Could not establish connection. Receiving end does not exist."), true) === M.TEXTO_EXTENSION_CALLADA
  && M.textoSinRespuesta(null, true) === M.TEXTO_EXTENSION_CALLADA,
  "videos: si la extensión no contesta, el botón dice por qué (se reinició: recargar la página), no que falló Atarraya");
comprobar([M.TEXTO_SIN_ARCHIVO, M.TEXTO_FALLO_EXTENSION, M.TEXTO_EXTENSION_REINICIADA, M.TEXTO_EXTENSION_CALLADA, M.TEXTO_SIN_TRANSMISION]
    .every((t) => typeof t === "string" && t.length > 10 && !/hablar con Atarraya/.test(t))
  && M.TEXTO_SIN_ARCHIVO === "No encontré el archivo completo de este video." && /^No se pudo hablar con Atarraya/.test(M.TEXTO_SIN_HABLAR),
  "videos: «No se pudo hablar con Atarraya» es solo para cuando falló hablar con el programa");
{
  // Las respuestas dudosas: por marco, sin repetir, con tope, y se olvidan con su marco o su pestaña
  const m = new M.MediosPorPestana(4);
  m.dudosa(5, { url: LI(1), frameId: 0, tamano: 1382, tipoMime: "application/octet-stream" }, 10);
  m.dudosa(5, { url: LI(1), frameId: 0, tamano: 1382 }, 11);
  m.dudosa(5, { url: "https://otro.net/p/1", frameId: 2, tamano: -1 }, 12);
  m.dudosa(5, { url: "https://r1---sn.googlevideo.com/videoplayback", frameId: 0 }, 13);
  comprobar(m.dudosasDe(5, 0).length === 1 && m.dudosasDe(5, 0)[0].cuando === 11 && m.dudosasDe(5, 2).length === 1 && m.dudosasDe(9, 0).length === 0
    && m.lista(5).length === 0,
    "videos: una respuesta dudosa se anota por marco (una vez, nada de YouTube) y no sale en la ventanita");
  for (let i = 2; i <= 6; i++) m.dudosa(5, { url: LI(i), frameId: 0, tamano: 300000 }, 20 + i);
  comprobar(m.dudosasDe(5, 0).length + m.dudosasDe(5, 2).length === 4 && !m.dudosasDe(5, 0).some((d) => d.url === LI(1)),
    "videos: hay un tope de respuestas dudosas por pestaña (se van las más viejas)");
  const otra = new M.MediosPorPestana(4);
  otra.cargar(JSON.parse(JSON.stringify(m.exportar())));
  comprobar(otra.dudosasDe(5, 0).length === m.dudosasDe(5, 0).length,
    "videos: las respuestas dudosas se guardan y vuelven aunque el «service worker» se apague");
  m.sinDudas(5, LI(6));
  m.borrarMarco(5, 2);
  comprobar(!m.dudosasDe(5, 0).some((d) => d.url === LI(6)) && m.dudosasDe(5, 2).length === 0,
    "videos: una respuesta que ya se miró no se vuelve a mirar; un marco que cambia de página olvida las suyas");
  m.bloquearMarco(5, 0, "youtube");
  m.dudosa(5, { url: LI(9), frameId: 0 }, 99);
  comprobar(m.dudosasDe(5, 0).length === 0, "videos: un marco bloqueado (YouTube o DRM) no guarda respuestas dudosas");
}

// background.js nunca pregunta por la pestaña activa ni por la última ventana, y solo mira (no bloquea nada).
// Los dos manifiestos: el botón sobre los videos va en todos los marcos, pero nunca en YouTube.
if (enNode) {
  const fs = require("fs");
  const path = require("path");
  const leer = (r) => fs.readFileSync(path.join(__dirname, r), "utf8");
  const fondo = leer("../extension/comun/background.js");
  comprobar(!/tabs\.query|active\s*:\s*true|lastFocusedWindow|currentWindow/.test(fondo),
    "extensión: background.js no usa la pestaña activa");
  comprobar(!/blocking/.test(fondo) && /onHeadersReceived/.test(fondo) && /importScripts\("origen\.js", "medios\.js", "cola-navegador\.js"\)/.test(fondo),
    "extensión: background.js solo mira los pedidos (no bloquea ni cambia nada) y carga medios.js");
  const videos = leer("../extension/comun/videos.js");
  comprobar(/isTrusted/.test(videos) && /cadenaBloqueada/.test(videos) && !/innerHTML/.test(videos) && /attachShadow\(\{ mode: "closed" \}\)/.test(videos),
    "extensión: el botón solo responde a un clic de la persona, no aparece en YouTube y no usa innerHTML");
  comprobar(/tipo: "revisarVideo"/.test(videos) && /case "revisarVideo"/.test(fondo) && /onBeforeRedirect/.test(fondo) && /bloquearMarco/.test(fondo),
    "extensión: antes de mostrar el botón pregunta si el video viene de YouTube (también por una redirección o por su marco)");
  comprobar(/return \{ ok: false, texto: algo \? TEXTO_SIN_ARCHIVO : TEXTO_SIN_TRANSMISION \}/.test(fondo) && !/flujos\[0\]/.test(leer("../extension/comun/medios.js")),
    "extensión: el botón de un «blob:» sin lista en su marco no manda nada a Atarraya (y dice si el video está pero su lista no)");
  // «No se pudo hablar con Atarraya» solo cuando falló hablar con el programa: ni en un error de la extensión ni cuando
  // la extensión no contesta
  const popupJs = leer("../extension/comun/popup.js");
  comprobar(!/No se pudo hablar con Atarraya/.test(fondo + videos + popupJs) && (fondo.match(/TEXTO_SIN_HABLAR/g) || []).length === 1
    && /catch \(e\) \{\s*\/\/[^\n]*\n\s*console\.warn\("Atarraya: no se pudo pasar el video al programa", e\);\s*return \{ ok: false, texto: TEXTO_SIN_HABLAR \};/.test(fondo)
    && (fondo.match(/responder\(\{ ok: false, texto: TEXTO_FALLO_EXTENSION \}\)/g) || []).length === 2 && /textoSinRespuesta\(fallo, extensionViva\(\)\)/.test(videos),
    "extensión: «No se pudo hablar con Atarraya» es solo para cuando falló el pedido al programa; un error de la extensión lo dice así");
  comprobar(/fetch\(url, \{ credentials: "include", redirect: "error", signal: control\.signal \}\)/.test(fondo)
    && /if \(!\/\^https\?:\/i\.test\(url\) \|\| motivoDeUrl\(url\)\) return falla/.test(fondo)
    && /if \(r\.redirected \|\| hostDe\(r\.url \|\| url\) !== hostDe\(url\) \|\| motivoBloqueo\(\[r\.url\]\)\) return falla/.test(fondo)
    && (fondo.match(/fetch\(/g) || []).length === 1,
    "extensión: al mirar una lista no se sigue ninguna redirección, y nunca se pide nada de YouTube ni de un sitio con DRM");
  comprobar(/const limpio = limpiarParaRegistro\(texto\)/.test(fondo) && /if \(r\.motivo === "cual"\) return \{ ok: false, texto: TEXTO_CUAL_VIDEO \}/.test(fondo),
    "extensión: lo que va al registro pasa por limpiarParaRegistro; si no se sabe cuál es la lista, lo dice");
  comprobar(/anotarEnRegistro\(resumenDeFallo\(donde, e, sitio\)\)/.test(fondo) && !/anotarEnRegistro\([^)]*\.(stack|message)/.test(fondo)
    && (fondo.match(/anotarEnRegistro\(/g) || []).length === 3,
    "extensión: de un error al registro va solo su resumen (sin su mensaje ni su pila)");
  comprobar(/anotarFallo\("botón «Descargar este video»", e, hostDe\(mensaje\.marco \|\| remitente\.url \|\| ""\)\)/.test(fondo)
    && /hablar\(\{ tipo: "registro", texto: limpio \}\)/.test(fondo)
    && /duracion: Number\.isFinite\(v\.duration\)/.test(videos) && /puedeSerLista\(d\.url, cab\["content-type"\], tamano, d\.method\)/.test(fondo)
    && /listaDelVideo\(medios, tabId, frameId,/.test(fondo),
    "extensión: un error del botón queda en el registro de Atarraya; el botón manda la duración del video; las listas dudosas se anotan y se miran al pulsar");
  const popup = leer("../extension/comun/popup.js");
  comprobar(!/innerHTML/.test(popup), "extensión: la ventanita muestra los nombres como texto (sin innerHTML)");
  for (const nav of ["chrome", "firefox"]) {
    const man = JSON.parse(leer("../extension/" + nav + "/manifest.json"));
    const cs = (man.content_scripts || [])[0] || {};
    const fuera = cs.exclude_matches || [];
    comprobar(cs.all_frames === true && cs.match_about_blank === true && JSON.stringify(cs.js) === JSON.stringify(["medios.js", "videos.js"])
      && ["*://*.youtube.com/*", "*://youtu.be/*", "*://*.youtube-nocookie.com/*", "*://*.googlevideo.com/*", "*://*.netflix.com/*"].every((x) => fuera.includes(x)),
      "extensión (" + nav + "): el botón va en todos los marcos, pero no en YouTube ni en sitios con DRM");
    comprobar(man.version === "0.4.0" && JSON.stringify(man.permissions) === JSON.stringify(["downloads", "nativeMessaging", "contextMenus", "cookies", "storage", "webRequest"])
      && JSON.stringify(man.host_permissions) === JSON.stringify(["<all_urls>"]),
      "extensión (" + nav + "): versión 0.4.0 y ningún permiso nuevo");
  }
  const ff = JSON.parse(leer("../extension/firefox/manifest.json"));
  comprobar(JSON.stringify(ff.background.scripts) === JSON.stringify(["origen.js", "medios.js", "cola-navegador.js", "background.js"]),
    "extensión (firefox): carga origen.js, medios.js y cola-navegador.js antes que background.js");
  comprobar(/importScripts\("origen\.js", "medios\.js", "cola-navegador\.js"\)/.test(fondo),
    "extensión (chrome): background.js carga cola-navegador.js");

  // La cola: los avisos de espera solo se leen en la página de arriba de las pestañas de la cola, sin tocar la página
  for (const nav of ["chrome", "firefox"]) {
    const man = JSON.parse(leer("../extension/" + nav + "/manifest.json"));
    const cs = (man.content_scripts || [])[1] || {};
    const fuera = cs.exclude_matches || [];
    comprobar(JSON.stringify(cs.js) === JSON.stringify(["esperas.js", "cola.js"]) && cs.all_frames === false
      && ["*://*.youtube.com/*", "*://*.netflix.com/*"].every((x) => fuera.includes(x)),
      "extensión (" + nav + "): los avisos de espera se leen solo en la página de arriba (y nunca en YouTube)");
  }
  const colaJs = leer("../extension/comun/cola.js");
  const esperasJs = leer("../extension/comun/esperas.js");
  comprobar(/tipo: "soyDeLaCola"/.test(colaJs) && /if \(r && r\.cola\) empezar\(\)/.test(colaJs),
    "extensión: cola.js no hace nada en una página que no es de la cola");
  // Una pestaña es de la cola solo si la abrió la misma extensión, cuando Atarraya se lo pidió por el puerto
  const abrir = fondo.slice(fondo.indexOf("function abrirParaAtarraya"), fondo.indexOf("// Se prueba al despertar"));
  comprobar(!/tabs\.onCreated/.test(fondo) && !/tipo: "cola"/.test(fondo) && !/openerTabId === undefined/.test(fondo)
    && (fondo.match(/cola\.marcar\(/g) || []).length === 1 && (fondo.match(/tabs\.create\(/g) || []).length === 1
    && abrir.indexOf("tabs.create(") > 0 && abrir.indexOf("tabs.create(") < abrir.indexOf("cola.marcar(") && /paginaPorAbrir\(orden\)/.test(abrir),
    "extensión: una pestaña es de la cola solo si la abrió la misma extensión (tabs.create), a pedido de Atarraya");
  comprobar(/connectNative\(HOST\)/.test(fondo) && /postMessage\(\{ tipo: "hola" \}\)/.test(fondo) && /m\.tipo === "listo"\) alVolverAtarraya\(true\)/.test(fondo)
    && /cola\.paginaVista\(tabId, url/.test(fondo) && /cola\.leeAvisos\(tabId, remitente\.url/.test(fondo),
    "extensión: el puerto con Atarraya; al conectar van los avisos finales guardados, y los avisos de espera solo de pestañas de la cola");
  const puenteCpp = leer("../puente/puente.cpp");
  comprobar(/tipo == "hola"\) return Puerto\(\)/.test(puenteCpp) && /tipo", "adios"/.test(puenteCpp) && !/tipo == "cola"/.test(puenteCpp)
    && /DireccionWeb\(url\)\) return FALSE/.test(puenteCpp),
    "puente: el puerto solo con Atarraya abierto (si no, «adios»), solo abre páginas http o https y ya no hay consulta «cola»");
  comprobar(!/\.click\(|dispatchEvent|\.submit\(|innerHTML|\.value\s*=|focus\(/.test(colaJs + esperasJs) && !/document\./.test(esperasJs),
    "extensión: cola.js y esperas.js solo leen el texto: no hacen clics, no escriben ni mandan formularios");
  const enLaCola = fondo.slice(fondo.indexOf("async function descargaDeLaCola"), fondo.indexOf("api.downloads.onCreated.addListener"));
  const decidir = fondo.slice(fondo.indexOf("async function decidirToma"), fondo.indexOf("api.downloads.onCreated.addListener"));
  comprobar(/downloads\.cancel\(id\)/.test(decidir) && decidir.indexOf('accion === "cancelar"') > 0
    && decidir.indexOf('accion === "cancelar"') < decidir.indexOf("downloads.cancel(id)")
    && /new TomaDelNavegador\(/.test(decidir) && !/\.catch\(\(\) => \{\}\)/.test(enLaCola),
    "extensión: la descarga de una parte se cancela en el navegador solo cuando Atarraya ya la tomó (y sin tragarse los errores)");
  comprobar(/resultadoCancelar\(ahora, cancelo, consulto\)/.test(decidir) && /tipo: "soltar"/.test(decidir)
    && decidir.indexOf("resultadoCancelar(") < decidir.indexOf("downloads.erase({ id })")
    && /if \(r === "sinConfirmar"\) \{[\s\S]*?anotarSinConfirmar\(\{ id \}, datosDe\(s\), toma\);\s*return;\s*\}/.test(decidir),
    "extensión: al vencer el plazo le pide a Atarraya que suelte la suya, y después de cancelar mira si de verdad se canceló");
  // Lo que se decide queda anotado antes de pasarle la descarga a Atarraya (se espera la confirmación de storage.local), y
  // al despertar se sigue decidiendo
  comprobar(enLaCola.indexOf("probando: { desde: Date.now() }") > 0 && enLaCola.indexOf("probando: { desde: Date.now() }") < enLaCola.indexOf("despuesDeGuardar(")
    && /despuesDeGuardar\(\s*guardarCola,\s*async \(\) => \{[\s\S]*?return await enviar\(pedido\);/.test(enLaCola)
    && (enLaCola.match(/enviar\(/g) || []).length === 1 && /await decidirToma\(item\.id\)/.test(enLaCola)
    && /return api\.storage\.local\.set\(\{ seguidas: \[\.\.\.seguidas\] \}\)\.then\(/.test(fondo)
    && /if \(!\(await guardarCola\(\)\)\) return;\s*\/\/ sin anotarlo no se avisa/.test(fondo)
    && /\/\/ Primero queda guardado el aviso final[^\n]*\n\s*if \(!\(await guardarCola\(\)\)\) return;/.test(fondo)
    && /if \(s\.probando\) decidirToma\(id\);\s*else entregarFinal\(id\);/.test(fondo) && /if \(s\.probando\) return;/.test(fondo)
    && /tomaNueva\(instancia, item\.id, Date\.now\(\)\)/.test(enLaCola),
    "extensión: quién baja una parte se anota antes de pasársela a Atarraya, y si el «service worker» se apaga, al despertar se sigue decidiendo");
  // Si Atarraya la tenía retenida (al abrirse, no sabía si el navegador siguió), después de cancelar se le avisa hasta que lo confirma
  comprobar(/if \(e && e\.ok\) retenida = e\.sinConfirmar === true;/.test(decidir)
    && /if \(retenida\) \{[\s\S]*?anotarFinal\(id, s, \{ tipo: "confirmada", toma, estado: "cancelada" \}\);[\s\S]*?\} else \{\s*seguidas\.delete\(id\);/.test(decidir),
    "extensión: si Atarraya retuvo la suya hasta saber, el aviso de que la del navegador ya no baja se repite hasta que lo confirma");
  // El aviso final no se borra al mandarlo: solo cuando Atarraya confirma que ya no espera nada de esa descarga
  const revisar = fondo.slice(fondo.indexOf("function anotarSinConfirmar"), fondo.indexOf("function alVolverAtarraya"));
  comprobar((fondo.match(/seguidas\.delete\(/g) || []).length === 2 && (revisar.match(/seguidas\.delete\(/g) || []).length === 1
    && revisar.indexOf("finalEntregado(r, s.final)") > 0
    && revisar.indexOf("finalEntregado(r, s.final)") < revisar.indexOf("seguidas.delete(id)") && /if \(aviso\.terminar\) \{\s*anotarFinal\(/.test(revisar),
    "extensión: el aviso final se guarda y la descarga se olvida solo cuando Atarraya lo confirma");
  comprobar(/resolverSinConfirmar\(item, consulto\)/.test(revisar) && /anotarFinal\(id, s, \{ tipo: "confirmada"/.test(revisar)
    && /hablar\(\{ tipo: "sinConfirmar", toma: s\.toma \}\)/.test(revisar) && /if \(!consulto\) return;/.test(revisar),
    "extensión: una parte «sin confirmar» se sigue mirando hasta saber si la del navegador sigue (y no se da por borrada si no se pudo mirar)");
  comprobar(/seguidasGuardadas\(/.test(fondo) && /if \(r\.abierto\) alVolverAtarraya\(\)/.test(fondo)
    && /listoCola\.then\(\(\) => \{\s*for \(const \[id, s\] of \[\.\.\.seguidas\]\) \{\s*if \(s\.probando\) decidirToma\(id\);\s*else entregarFinal\(id\);/.test(fondo),
    "extensión: los avisos finales guardados se mandan otra vez al despertar y cuando Atarraya vuelve a contestar");
  // Al conectarse, cuenta lo que sabe de las descargas de la cola (Atarraya lo concilia con su cola y su lista)
  const volver = fondo.slice(fondo.indexOf("function alVolverAtarraya"), fondo.indexOf("function vigilarSeguidas"));
  comprobar(/enviarInforme\(conecto\)/.test(volver) && /hablar\(informeDeSeguidas\(instancia, \[\.\.\.seguidas\]\)\)/.test(volver)
    && /if \(!r \|\| !r\.ok\) ultimoInforme = 0;/.test(volver) && /instanciaValida\(g && g\.instancia\)/.test(fondo)
    && /nuevaInstancia\(\(b\) => crypto\.getRandomValues\(b\)\)/.test(fondo),
    "extensión: al conectarse con Atarraya le cuenta las descargas de la cola que sigue (con su instancia, al azar y guardada)");
  comprobar(/tipo == "seguidas"/.test(puenteCpp) && /SoloLasSeguidas\(j\)\.dump\(\)/.test(puenteCpp) && /tomas\.size\(\) >= 500/.test(puenteCpp),
    "puente: lo que cuenta la extensión pasa solo con sus campos de texto, con un largo máximo y como mucho 500");
}

// --- Avisos de espera de los sitios de descargas (esperas.js) ----------------------------------------------------------
comprobar(E.segundosDe("You have to wait 9 minutes, 41 seconds till next download") === 581
  && E.segundosDe("1 hour 5 min") === 3900 && E.segundosDe("2h 03m") === 7380 && E.segundosDe("120 sec") === 120
  && E.segundosDe("Next download in 09:41") === 581 && E.segundosDe("espera 1:02:03") === 3723 && E.segundosDe("30s") === 30
  && E.segundosDe("1 hora y 5 minutos") === 3900 && E.segundosDe("15 minutes") === 900 && E.segundosDe("sin tiempo") === -1,
  "esperas: duraciones escritas de muchas formas (9 minutes, 41 seconds; 2h 03m; 09:41; 1:02:03; 1 hora y 5 minutos)");
comprobar(E.segundosDe("Archivo de 100 MB en 5 partes, espera 30 segundos") === 30,
  "esperas: «100 MB» o «5 partes» no son una duración");
const ve = (t) => E.reconocerEspera(t) || { clase: "ninguna", segundos: null };
const casos = [
  // ddownload y katfile (XFileSharing)
  ["Free download\nYou have to wait 9 minutes, 41 seconds till next download", "limite", 581],
  ["Create Download Link\nWait 30 seconds", "pagina", 30],
  // aviso «Blocked» entre partes
  ["Blocked: wait 10 minutes", "limite", 600],
  ["Blocked", "limite", -1],
  // Rapidgator
  ["You have reached your hourly downloads limit. Please wait 01:23:45 or buy premium", "limite", 5025],
  ["Please wait 30 seconds", "pagina", 30],
  // nitroflare
  ["You have to wait 120 minutes to download your next file", "limite", 7200],
  ["Your free download limit has been reached", "limite", -1],
  // 1fichier
  ["Warning! You must wait 15 minutes to download again or subscribe to a premium offer", "limite", 900],
  ["Vous devez attendre 15 minutes pour télécharger à nouveau", "limite", 900],
  // turbobit / genéricos
  ["You have reached the limit of downloads from your IP address, please wait 59:59", "limite", 3599],
  ["Download limit exceeded. Try again in 1 hour", "limite", 3600],
  ["Next download available in\n04:30", "limite", 270],
  ["Your download link will be available in 25 seconds", "pagina", 25],
  ["25 seconds left", "pagina", 25],
  // español
  ["Has alcanzado el límite de descargas. Espera 10 minutos", "limite", 600],
  ["Debes esperar 45 segundos", "pagina", 45],
  ["Bloqueado: vuelve a intentarlo en 20 minutos", "limite", 1200],
  ["La descarga estará disponible en 15 segundos", "pagina", 15],
];
for (const [texto, clase, segundos] of casos) {
  const r = ve(texto);
  comprobar(r.clase === clase && r.segundos === segundos,
    "esperas: «" + texto.replace(/\n/g, " / ") + "» → " + clase + " " + segundos + (r.clase === clase && r.segundos === segundos ? "" : " (salió " + r.clase + " " + r.segundos + ")"));
}
const normales = [
  "Premium: no waiting time, no limits, parallel downloads.",
  "Waiting time between downloads | 60 minutes | none",
  "Download peli.part1.rar (2.2 GB)\nSlow download | High speed download",
  "Términos y condiciones: el límite de descargas para usuarios gratis es de 1 GB por día",
  "Updated 14:30 · 120 comments",
  "Please wait while we check your browser...",
];
comprobar(normales.every((t) => E.reconocerEspera(t) === null),
  "esperas: una página normal (tablas de premium, condiciones, «please wait» sin tiempo) no es un aviso de espera");
comprobar(ve("Free users have to wait 30 seconds.\nBlocked: wait 10 minutes").clase === "limite" && ve("Free users have to wait 30 seconds.\nBlocked: wait 10 minutes").segundos === 600,
  "esperas: si hay un límite y una cuenta, gana el límite");

// --- Las pestañas de la cola y lo que se sigue en el navegador (cola-navegador.js) --------------------------------------
const pc = new C.PestanasDeLaCola(3600000, 60000);
// Una pestaña abierta a mano (por la persona, o por Atarraya sin la extensión conectada): nunca es de la cola
const parte2 = "https://ddownload.com/abc123def/peli.part2.rar";
comprobar(!pc.paginaVista(5, parte2, 2000) && pc.parte(5, 2000) === "" && !pc.leeAvisos(5, parte2, 2000),
  "cola: una pestaña abierta a mano no es de la cola, aunque tenga la página de una parte (ahí no se leen avisos)");
pc.marcar(8, parte2, 2000);
comprobar(pc.paginaVista(8, parte2, 2500) && pc.parte(8, 3000) === parte2 && pc.parte(9, 3000) === ""
  && pc.paginaVista(8, "https://s3.ddownload.com/captcha", 4000) && pc.leeAvisos(8, "https://ddownload.com/x", 4000),
  "cola: en la pestaña que abrió la extensión para una parte se leen los avisos de su sitio");
comprobar(!pc.paginaVista(8, "https://otro-sitio.com/", 5000) && !pc.leeAvisos(8, "https://otro-sitio.com/", 5000) && pc.parte(8, 5000) === parte2,
  "cola: si en esa pestaña se va a otro sitio, ahí no se leen avisos (sus descargas siguen siendo de la parte)");
pc.marcar(9, "https://katfile.com/xyz/peli.part3.rar", 10000);
comprobar(pc.paginaVista(9, "https://katfile.cloud/xyz", 11000) && pc.leeAvisos(9, "https://katfile.cloud/otra", 12000),
  "cola: si el sitio la manda enseguida a otro al abrirla (una redirección), también se leen ahí");
pc.marcar(10, "https://katfile.com/xyz/peli.part4.rar", 10000);
pc.marcar(11, "https://rapidgator.net/file/1", 10000);
pc.paginaVista(11, "https://rapidgator.net/file/1", 10500);
comprobar(!pc.paginaVista(10, "https://katfile.cloud/xyz", 10000 + 61000) && !pc.paginaVista(11, "https://rg.to/file/1", 11000),
  "cola: la redirección cuenta solo para la primera página y en el primer minuto");
const otra = new C.PestanasDeLaCola();
otra.cargar(JSON.parse(JSON.stringify(pc.exportar())));
comprobar(otra.parte(8, 3000) === parte2 && otra.leeAvisos(9, "https://katfile.cloud/z", 13000) && !otra.leeAvisos(5, parte2, 3000),
  "cola: se recuerda aunque el «service worker» se apague (storage.session)");
comprobar(pc.parte(8, 2000 + 3600001) === "", "cola: una pestaña de la cola se olvida después de unas horas");
pc.marcar(12, "javascript:alert(1)", 1000);
comprobar(pc.parte(12, 1000) === "", "cola: solo páginas http o https");
const raro = new C.PestanasDeLaCola();
raro.cargar({ cola: [[13, { parte: "javascript:alert(1)", desde: 1000 }], ["x", { parte: "https://a.com/", desde: 1 }], [14, null], "z"] });
comprobar(raro.cola.size === 0, "cola: lo guardado se revisa al cargarlo");
comprobar(C.paginaPorAbrir({ tipo: "abrir", id: 3, url: "https://ddownload.com/abc/peli.part1.rar" }) === "https://ddownload.com/abc/peli.part1.rar"
  && C.paginaPorAbrir({ tipo: "abrir", id: 3, url: "javascript:alert(1)" }) === "" && C.paginaPorAbrir({ tipo: "abrir", id: 3, url: "file:///C:/x" }) === ""
  && C.paginaPorAbrir({ tipo: "abrir", url: "https://a.com/" }) === "" && C.paginaPorAbrir({ tipo: "otra", id: 1, url: "https://a.com/" }) === ""
  && C.paginaPorAbrir({ tipo: "abrir", id: 1, url: "https://a.com/\"><x" }) === "" && C.paginaPorAbrir(null) === "",
  "cola: por el puerto, Atarraya solo puede pedir que se abran páginas http o https");
comprobar(C.sitioBase("https://s12.ddownload.com/d/x") === "ddownload.com" && C.sitioBase("https://www.ejemplo.co.uk/a") === "ejemplo.co.uk"
  && C.sitioBase("http://127.0.0.1:47613/p") === "127.0.0.1" && C.sitioBase("no es una dirección") === "",
  "cola: el sitio de una dirección (sin los servidores de adelante)");

const sg = { ultimoAviso: 0, ultimoBytes: -1, ultimoEstado: "" };
const enCurso = { state: "in_progress", bytesReceived: 1000, totalBytes: 5000, filename: "" };
const a1 = C.avisoDeSeguimiento(enCurso, sg, 10000);
comprobar(a1 && a1.estado === "bajando" && a1.bajado === 1000 && a1.total === 5000 && !a1.terminar,
  "seguir: la primera vez dice cómo va");
Object.assign(sg, { ultimoAviso: 10000, ultimoBytes: 1000, ultimoEstado: "bajando" });
comprobar(C.avisoDeSeguimiento({ ...enCurso, bytesReceived: 2000 }, sg, 12000) === null
  && C.avisoDeSeguimiento(enCurso, sg, 20000) === null && C.avisoDeSeguimiento({ ...enCurso, bytesReceived: 3000, paused: true }, sg, 20000) === null,
  "seguir: no repite el aviso antes de 5 segundos, ni si no avanzó, ni en pausa");
const a2 = C.avisoDeSeguimiento({ ...enCurso, bytesReceived: 3000 }, sg, 16000);
comprobar(a2 && a2.estado === "bajando" && a2.bajado === 3000, "seguir: cada 5 segundos, cómo va");
const a3 = C.avisoDeSeguimiento({ state: "complete", bytesReceived: 5000, totalBytes: 5000, filename: "C:\\Users\\x\\Downloads\\peli.part2.rar" }, sg, 17000);
comprobar(a3 && a3.estado === "completa" && a3.ruta === "C:\\Users\\x\\Downloads\\peli.part2.rar" && a3.terminar,
  "seguir: al terminar, dónde quedó el archivo");
const a4 = C.avisoDeSeguimiento({ state: "interrupted", error: "NETWORK_FAILED", canResume: true, bytesReceived: 3000, totalBytes: 5000 }, sg, 18000);
const a5 = C.avisoDeSeguimiento({ state: "interrupted", error: "USER_CANCELED", canResume: false, bytesReceived: 3000, totalBytes: 5000 }, sg, 18000);
comprobar(a4 && a4.estado === "fallo" && !a4.terminar && a5 && a5.estado === "cancelada" && a5.terminar,
  "seguir: si se corta y se puede reanudar, se sigue mirando; si la persona la cancela, no");
comprobar(C.avisoDeSeguimiento({ state: "interrupted", error: "NETWORK_FAILED", canResume: true }, { ...sg, ultimoEstado: "fallo" }, 19000) === null,
  "seguir: el corte se avisa una sola vez");

// --- Una sola descarga por parte (TomaDelNavegador) ---------------------------------------------------------------------
const t1 = new C.TomaDelNavegador(1000, 45000, 15000);
comprobar(t1.paso("probando", 2000) === "esperar" && t1.paso("", 3000) === "esperar" && t1.paso("desconocida", 4000) === "esperar"
  && t1.paso("tomada", 5000) === "cancelar",
  "toma: mientras Atarraya prueba, la del navegador espera; si Atarraya la tomó, se cancela la del navegador");
comprobar(new C.TomaDelNavegador(1000).paso("navegador", 2000) === "seguir" && new C.TomaDelNavegador(1000).paso("terminada", 2000) === "seguir",
  "toma: si Atarraya no pudo (o la soltó), sigue la del navegador");
const t2 = new C.TomaDelNavegador(1000, 45000, 15000);
comprobar(t2.paso("probando", 45999) === "esperar" && t2.paso("probando", 46000) === "soltar" && t2.paso("probando", 50000) === "esperar"
  && t2.paso("navegador", 51000) === "seguir",
  "toma: al vencer el plazo, primero se le pide a Atarraya que suelte la suya y se espera que lo confirme");
const t3 = new C.TomaDelNavegador(1000, 45000, 15000);
comprobar(t3.paso("probando", 46000) === "soltar" && t3.paso("tomada", 47000) === "cancelar",
  "toma: si Atarraya ya la tenía cuando se le pidió soltarla, se cancela la del navegador (nunca quedan las dos)");
const t4 = new C.TomaDelNavegador(1000, 45000, 15000);
comprobar(t4.paso("", 46000) === "soltar" && t4.paso("", 60999) === "esperar" && t4.paso("", 61000) === "seguir",
  "toma: si Atarraya no contesta (está cerrado, no baja nada), sigue la del navegador; al abrirse, Atarraya quita la suya");
comprobar(C.resultadoCancelar(null) === "cancelada" && C.resultadoCancelar({ state: "interrupted", error: "USER_CANCELED" }) === "cancelada"
  && C.resultadoCancelar({ state: "complete" }) === "terminada" && C.resultadoCancelar({ state: "in_progress" }) === "sigue"
  && C.resultadoCancelar({ state: "in_progress" }, false, true) === "sigue" && C.resultadoCancelar(null, true, false) === "cancelada",
  "toma: después de cancelar se mira cómo quedó; si no se canceló, sigue la del navegador y Atarraya quita la suya");
comprobar(new C.TomaDelNavegador(1000).paso("ajena", 2000) === "seguir", "toma: una descarga que no es de una parte sigue en el navegador");

// --- Un aviso final con Atarraya cerrado ---------------------------------------------------------------------------------
const DIA = 24 * 3600000;
const sf = { toma: "n7-final", desde: 1000, ultimoEstado: "bajando", final: { tipo: "navegador", toma: "n7-final", estado: "completa", ruta: "C:\\Users\\x\\Downloads\\peli.part1.rar", desde: 1000, intento: 0 } };
comprobar(C.tocaFinal(sf.final, 1000, true) && C.finalEntregado(null) === "repetir" && C.finalEntregado({ ok: false, abierto: false }) === "repetir",
  "aviso final: con Atarraya cerrado no se confirma y queda guardado");
sf.final.intento = 1000;
comprobar(!C.tocaFinal(sf.final, 30999, false) && C.tocaFinal(sf.final, 31000, false) && C.tocaFinal(sf.final, 2000, true) && !C.tocaFinal(undefined, 50000, true),
  "aviso final: se manda otra vez cada 30 segundos, y enseguida al volver a conectar con Atarraya");
const vuelven = C.seguidasGuardadas(JSON.parse(JSON.stringify([[7, sf], [8, { toma: "n8-vieja", desde: 1000 }], ["x", {}], [9, null]])), 1000 + 5 * DIA);
comprobar(vuelven.length === 1 && vuelven[0][0] === 7 && vuelven[0][1].final.estado === "completa" && vuelven[0][1].final.ruta === sf.final.ruta,
  "aviso final: sigue guardado aunque pasen días (storage.local); una descarga sin aviso final se olvida a los 3 días");
comprobar(C.seguidasGuardadas([[7, sf]], 1000 + 400 * DIA).length === 1 && C.seguidasGuardadas([[8, { toma: "n8", desde: 1000 }]], 1000 + DIA).length === 1
  && C.seguidasGuardadas([[8, { toma: "n8", desde: 1000 }]], 1000 + 4 * DIA).length === 0,
  "aviso final: se guarda hasta que Atarraya lo confirma, sin plazo (aunque pase más de un año)");
comprobar(C.finalEntregado({ ok: true, estado: "navegador", espera: true }) === "repetir" && C.finalEntregado({ ok: true, estado: "terminada", espera: false }) === "listo"
  && C.finalEntregado({ ok: true, estado: "desconocida", espera: false }) === "listo" && C.finalEntregado({ ok: true, estado: "navegador" }) === "repetir",
  "aviso final: se borra solo cuando Atarraya contesta que ya no espera nada de esa descarga");

// --- Cancelar sin poder confirmarlo: nunca se da por cancelada ------------------------------------------------------
comprobar(C.resultadoCancelar(null, false, false) === "sinConfirmar" && C.resultadoCancelar({ state: "in_progress" }, false, false) === "sinConfirmar",
  "sin confirmar: si falla cancelar y también falla mirar cómo quedó, la parte queda «sin confirmar» (no se da por cancelada)");
comprobar(C.resolverSinConfirmar(null, false) === "esperar" && C.resolverSinConfirmar({ state: "in_progress" }, false) === "esperar"
  && C.resolverSinConfirmar(null, true) === "cancelada" && C.resolverSinConfirmar({ state: "interrupted", error: "USER_CANCELED" }, true) === "cancelada"
  && C.resolverSinConfirmar({ state: "in_progress" }, true) === "sigue" && C.resolverSinConfirmar({ state: "complete" }, true) === "sigue",
  "sin confirmar: no sigue hasta saberlo; si ya no baja, sigue la de Atarraya; si la baja el navegador, Atarraya quita la suya");
const sc = { toma: "n9-sc", desde: 1000, sinConfirmar: { desde: 1000, intento: 0, avisado: false } };
comprobar(C.seguidasGuardadas(JSON.parse(JSON.stringify([[9, sc]])), 1000 + 400 * DIA).length === 1,
  "sin confirmar: se guarda (sin plazo) hasta saberlo, aunque el navegador o Atarraya se cierren");
const confirmada = { tipo: "confirmada", toma: "n9-sc", estado: "cancelada" };
comprobar(C.finalEntregado({ ok: true, espera: true, sinConfirmar: true }, confirmada) === "repetir"
  && C.finalEntregado({ ok: true, espera: true, sinConfirmar: false }, confirmada) === "listo" && C.finalEntregado(null, confirmada) === "repetir"
  && C.finalEntregado({ ok: true, espera: false }, confirmada) === "repetir",
  "sin confirmar: el aviso de que ya no baja se repite hasta que Atarraya reanuda la suya");

// --- Lo que la extensión cuenta al conectarse (conciliar) --------------------------------------------------------------
let semilla = 7;
const inst = C.nuevaInstancia((b) => { for (let i = 0; i < b.length; i++) b[i] = (semilla = (semilla * 97 + 13) % 256); });
comprobar(C.instanciaValida(inst) && inst.length === 12 && !C.instanciaValida("ABC") && !C.instanciaValida("abc") && !C.instanciaValida(7)
  && C.instanciaValida("abcdefgh") && !C.instanciaValida("a".repeat(33)),
  "conciliar: cada extensión tiene su instancia (12 letras o números al azar)");
const tn = C.tomaNueva(inst, 77, 1700000000000);
comprobar(tn === "i" + inst + "-n77-" + (1700000000000).toString(36) && C.instanciaDeToma(tn) === inst && C.instanciaDeToma("n7-final") === ""
  && C.instanciaDeToma("iABC-n1-x") === "" && C.instanciaDeToma(null) === "" && tn.length <= 128,
  "conciliar: cada «toma» empieza con la instancia de su extensión (las de antes no tienen)");
const inf = C.informeDeSeguidas(inst, [
  [1, { toma: "t1", pagina: "https://ddownload.com/a1/p1.rar", probando: { desde: 1 } }],
  [2, { toma: "t2", pagina: "https://ddownload.com/a2/p2.rar" }],
  [3, { toma: "t3", final: { tipo: "navegador", estado: "completa" } }],
  [4, { toma: "t4", final: { tipo: "navegador", estado: "cancelada" } }],
  [5, { toma: "t5", sinConfirmar: { desde: 1, intento: 0, avisado: false } }],
  [6, { toma: "t6", final: { tipo: "confirmada", toma: "t6", estado: "cancelada" } }],
  [7, null], ["x", {}], 8,
]);
comprobar(inf.tipo === "seguidas" && inf.instancia === inst && JSON.stringify(inf.tomas.map((t) => t.estado))
    === JSON.stringify(["probando", "siguiendo", "completa", "terminada", "sinConfirmar", "terminada"])
  && inf.tomas[0].pagina === "https://ddownload.com/a1/p1.rar" && inf.tomas[2].pagina === "" && inf.tomas.every((t) => Object.keys(t).length === 3),
  "conciliar: cuenta cada descarga de la cola que sigue (decidiendo, bajando, sin confirmar, terminada) con su página, y nada más");
comprobar(C.informeDeSeguidas(inst, Array.from({ length: 600 }, (_, i) => [i, { toma: "t" + i }])).tomas.length === 500
  && C.informeDeSeguidas(inst, null).tomas.length === 0,
  "conciliar: como mucho 500");
const pr = { toma: tn, desde: 1000, probando: { desde: 1000 } };
comprobar(C.seguidasGuardadas([[3, pr]], 1000 + DIA).length === 1 && C.seguidasGuardadas([[3, pr]], 1000 + 4 * DIA).length === 0,
  "conciliar: lo que se estaba decidiendo se recuerda (storage.local) para seguir decidiendo al despertar");

// --- Una descarga de la cola se pasa recién cuando quedó guardado quién la baja --------------------------------------
(async () => {
  const pasos = [];
  const tarde = (v) => new Promise((r) => setTimeout(() => r(v), 20));
  const r1 = await C.despuesDeGuardar(async () => { pasos.push("guardar"); const ok = await tarde(true); pasos.push("guardado"); return ok; },
    () => { pasos.push("pasar"); return "pasada"; }, () => { pasos.push("no"); return "no"; });
  const bienOrden = r1 === "pasada" && JSON.stringify(pasos) === JSON.stringify(["guardar", "guardado", "pasar"]);
  const pasos2 = [];
  const r2 = await C.despuesDeGuardar(() => tarde(false), () => { pasos2.push("pasar"); return "pasada"; }, () => { pasos2.push("no"); return "no"; });
  const r3 = await C.despuesDeGuardar(() => { throw new Error("sin lugar"); }, () => "pasada", () => "no");
  const r4 = await C.despuesDeGuardar(() => tarde("sí"), () => "pasada", () => "no");   // solo «true» cuenta
  comprobar(bienOrden && r2 === "no" && JSON.stringify(pasos2) === JSON.stringify(["no"]) && r3 === "no" && r4 === "no",
    "duplicados: la descarga se le pasa a Atarraya recién cuando storage.local confirmó que guardó la decisión; si no, no se pasa");

  // --- El botón de un video «blob:»: su lista, también si no dice que lo es (LinkedIn) --------------------------------
  {
    // Como LinkedIn: la parte 1 es la lista (1382 bytes, «application/octet-stream»), las partes 2 a 6 son el video
    const m = new M.MediosPorPestana();
    m.dudosa(4, { url: LI(1), frameId: 0, tamano: 1382, tipoMime: "application/octet-stream" }, 100);
    for (let i = 2; i <= 6; i++) m.dudosa(4, { url: LI(i), frameId: 0, tamano: 300000 + i * 230000, tipoMime: "application/octet-stream" }, 100 + i);
    const leidas = [];
    const leer = async (u) => {
      leidas.push(u);
      return u === LI(1) ? listaHls : "";
    };
    const e = (await M.listaDelVideo(m, 4, 0, 11.5, leer)).entrada;
    comprobar(e && e.url === LI(1) && e.formato === "hls" && leidas[0] === LI(1) && m.transmisionDe(4, 0) === e && m.dudosasDe(4, 0).length === 0
      && m.lista(4).some((x) => x.url === LI(1)) && m.vista(4, 0, LI(1)),
      "LinkedIn: al pulsar el botón, la lista sin extensión ni tipo se reconoce por dentro y pasa a la lista de videos");
    const otraVez = [];
    const e2 = (await M.listaDelVideo(m, 4, 0, 0, async (u) => { otraVez.push(u); return null; })).entrada;
    comprobar(e2 === e && otraVez.length === 0, "LinkedIn: después ya se sabe cuál es (sin la duración, no se pide nada otra vez)");
  }
  {
    // Solo se vuelve a pedir lo que el reproductor de ESA pestaña y ESE marco pidió: nunca lo que dice una lista
    const m = new M.MediosPorPestana();
    const principal = "https://cdn.ej.org/v/principal";
    const maestraAfuera = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttps://rr1---sn-abc.googlevideo.com/videoplayback?id=1\n"
      + "#EXT-X-STREAM-INF:BANDWIDTH=2\nhttps://otro-servidor.net/720.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=3\nhttps://cdn.ej.org/v/no-pedida\n";
    m.dudosa(4, { url: principal, frameId: 0, tamano: 300 }, 10);
    m.dudosa(5, { url: "https://otro-servidor.net/720.m3u8", frameId: 0, tamano: 300 }, 11);   // en otra pestaña
    m.dudosa(4, { url: "https://cdn.ej.org/v/no-pedida", frameId: 2, tamano: 300 }, 12);         // en otro marco
    m.anotar(4, { url: "https://cdn.ej.org/la-pagina-dice.m3u8", formato: "hls", frameId: 0 }, 13);   // la contó la página
    const leidas = [];
    const r = await M.listaDelVideo(m, 4, 0, 11.5, async (u) => { leidas.push(u); return u === principal ? maestraAfuera : "#EXTM3U\n"; });
    comprobar(JSON.stringify(leidas) === JSON.stringify([principal]) && r.entrada === null && r.motivo === "cual"
      && !m.vista(4, 0, "https://cdn.ej.org/la-pagina-dice.m3u8") && !m.vista(4, 0, "https://cdn.ej.org/v/no-pedida")
      && !m.vista(4, 0, "https://otro-servidor.net/720.m3u8") && m.vista(4, 2, "https://cdn.ej.org/v/no-pedida"),
      "videos: solo se vuelve a pedir lo que el reproductor de esa pestaña y ese marco pidió (nada de lo que dice una lista, ni googlevideo.com)");
    // Una calidad que el reproductor sí pidió en ese marco se puede leer para saber cuánto dura
    m.dudosa(4, { url: "https://cdn.ej.org/v/no-pedida", frameId: 0, tamano: 900 }, 14);
    const leidas2 = [];
    const r2 = await M.listaDelVideo(m, 4, 0, 11.5, async (u) => {
      leidas2.push(u);
      return u === principal ? maestraAfuera : u === "https://cdn.ej.org/v/no-pedida" ? listaHls : "";
    });
    comprobar(r2.entrada && r2.entrada.url === principal && !leidas2.some((u) => /googlevideo|otro-servidor|la-pagina-dice/.test(u)),
      "videos: de una lista principal se lee solo una calidad que el reproductor pidió, y así se sabe cuánto dura");
  }
  {
    // Si no se pudo leer cuánto dura (el servidor no contestó), no se elige ninguna: puede ser de otro video
    const m = new M.MediosPorPestana();
    m.anotar(4, { url: "https://cdn.ej.org/hls/master.m3u8", formato: "hls", frameId: 0, vista: true }, 10);
    const r = await M.listaDelVideo(m, 4, 0, 30, async () => null);
    comprobar(r.entrada === null && r.motivo === "cual",
      "videos: si no se pudo leer la duración de la lista, no se manda (dice que no sabe cuál de los videos es este)");
    // Sin la duración del video y con listas de dos videos (pedidas con más de 15 s de diferencia): tampoco
    m.anotar(4, { url: "https://cdn.ej.org/hls2/master.m3u8", formato: "hls", frameId: 0, vista: true }, 60000);
    const r2 = await M.listaDelVideo(m, 4, 0, 0, async () => null);
    comprobar(r2.entrada === null && r2.motivo === "cual", "videos: sin la duración del video y con listas de varios videos, ninguna");
  }
  {
    // Sin la duración del video: dos videos cuyas listas llegaron con menos de 15 s de diferencia. Se pulsa el segundo:
    // no se puede saber cuál es, así que ninguna (antes se elegía la primera)
    const m = new M.MediosPorPestana();
    const A = "https://cdn.ej.org/a/lista", B = "https://cdn.ej.org/b/lista";
    m.anotar(4, { url: A, formato: "hls", frameId: 0, vista: true }, 1000);
    m.anotar(4, { url: B, formato: "hls", frameId: 0, vista: true }, 3000);
    const leer = async (u) => (u === A ? listaHls : u === B ? "#EXTM3U\n#EXTINF:6,\nb\n#EXT-X-ENDLIST\n" : "");
    const r = await M.listaDelVideo(m, 4, 0, 0, leer);
    comprobar(r.entrada === null && r.motivo === "cual",
      "videos: sin la duración y con listas de dos videos que llegaron juntas, ninguna (no se elige la primera)");
    // Sin la duración, la principal y su calidad (la principal la nombra): es un solo video, la principal
    const p = new M.MediosPorPestana();
    const principal = "https://cdn.ej.org/v/principal", calidad = "https://cdn.ej.org/v/hls-720/lista?x=1";
    p.anotar(4, { url: principal, formato: "hls", frameId: 0, vista: true }, 1000);
    p.anotar(4, { url: calidad, formato: "hls", frameId: 0, vista: true }, 1200);
    const leerP = async (u) => (u === principal ? maestra.replace("hls-720/lista?x=1", calidad) : u === calidad ? listaHls : "");
    const rp = await M.listaDelVideo(p, 4, 0, 0, leerP);
    comprobar(rp.entrada && rp.entrada.url === principal, "videos: sin la duración, la principal con su calidad es un solo video");
    // Con la duración: dos videos distintos que duran lo mismo, ninguna
    const d = new M.MediosPorPestana();
    d.anotar(4, { url: A, formato: "hls", frameId: 0, vista: true }, 1000);
    d.anotar(4, { url: B, formato: "hls", frameId: 0, vista: true }, 90000);
    const rd = await M.listaDelVideo(d, 4, 0, 11.5, async (u) => (u === A || u === B ? listaHls : ""));
    comprobar(rd.entrada === null && rd.motivo === "cual", "videos: dos videos que duran lo mismo: no se sabe cuál es, ninguna");
  }
  {
    // El inicio de LinkedIn: dos videos en el mismo marco, cada uno con su lista; se pulsa el segundo (24 s)
    const m = new M.MediosPorPestana();
    const A = (n) => LI(n), B = (n) => LI(n).replace("D5605AQprueba", "D5605AQotro");
    const lista24 = "#EXTM3U\n#EXTINF:6,\nb2\n#EXTINF:6,\nb3\n#EXTINF:6,\nb4\n#EXTINF:6,\nb5\n#EXT-X-ENDLIST\n";
    m.dudosa(4, { url: A(1), frameId: 0, tamano: 1382 }, 100);
    m.dudosa(4, { url: A(2), frameId: 0, tamano: 400000 }, 101);
    m.dudosa(4, { url: B(1), frameId: 0, tamano: 1500 }, 200);
    m.dudosa(4, { url: B(2), frameId: 0, tamano: 500000 }, 201);
    const leer = async (u) => (u === A(1) ? listaHls : u === B(1) ? lista24 : "");
    const e = (await M.listaDelVideo(m, 4, 0, 24, leer)).entrada;
    comprobar(e && e.url === B(1), "LinkedIn (inicio con varios videos): el botón de un video baja ese video, no el primero de la página");
    // Pulsar ahora el primero: ya están las dos en la lista; se elige por su duración
    const e2 = (await M.listaDelVideo(m, 4, 0, 11.5, leer)).entrada;
    comprobar(e2 && e2.url === A(1), "LinkedIn (inicio con varios videos): y el otro video, su propia lista");
    // Un video que no es ninguno de los dos: ninguna
    const r3 = await M.listaDelVideo(m, 4, 0, 90, leer);
    comprobar(r3.entrada === null && r3.motivo === "otros", "videos: si ninguna lista es la del video, no se manda una de otro video");
  }
  {
    // Una lista que se reconoció por su tipo, pero es de un video de antes (la página cambió sin recargarse); la de
    // este video llegó como dudosa
    const m = new M.MediosPorPestana();
    m.anotar(4, { url: "https://cdn.ej.org/viejo/master.m3u8", formato: "hls", frameId: 0, vista: true }, 50);
    m.dudosa(4, { url: LI(1), frameId: 0, tamano: 1382 }, 100);
    const leer = async (u) => (u === LI(1) ? listaHls : u.includes("viejo") ? "#EXTM3U\n#EXTINF:30,\na\n#EXT-X-ENDLIST\n" : "");
    const e = (await M.listaDelVideo(m, 4, 0, 11.5, leer)).entrada;
    comprobar(e && e.url === LI(1), "videos: una lista vieja de otro video no gana a la de este video");
    // La principal con sus calidades: la principal, aunque la calidad también esté
    const p = new M.MediosPorPestana();
    const principal = "https://cdn.ej.org/v/principal";
    p.dudosa(4, { url: principal, frameId: 0, tamano: 300 }, 10);
    p.dudosa(4, { url: "https://cdn.ej.org/v/hls-720/lista?x=1", frameId: 0, tamano: 900 }, 11);
    const leerP = async (u) => (u === principal ? maestra.replace("hls-720/lista?x=1", "https://cdn.ej.org/v/hls-720/lista?x=1")
      : u === "https://cdn.ej.org/v/hls-720/lista?x=1" ? listaHls : "");
    const ep = (await M.listaDelVideo(p, 4, 0, 11.5, leerP)).entrada;
    comprobar(ep && ep.url === principal, "videos: la lista principal (con las calidades) antes que una de sus calidades");
    // DASH que no dice que lo es
    const d = new M.MediosPorPestana();
    d.dudosa(4, { url: "https://cdn.ej.org/d/1/x", frameId: 3, tamano: 800 }, 10);
    const ed = (await M.listaDelVideo(d, 4, 3, 63.5, async () => mpd)).entrada;
    comprobar(ed && ed.formato === "dash" && ed.frameId === 3, "videos: un manifiesto DASH sin extensión ni tipo, por dentro");
    // Una que no se pudo leer (sin conexión, por ejemplo) se vuelve a mirar en el próximo clic; una que no es lista, no
    const n = new M.MediosPorPestana();
    n.dudosa(4, { url: LI(1), frameId: 0, tamano: 1382 }, 10);
    n.dudosa(4, { url: LI(2), frameId: 0, tamano: 400000 }, 11);
    const en = await M.listaDelVideo(n, 4, 0, 11.5, async (u) => (u === LI(1) ? null : ""));
    comprobar(en.entrada === null && n.dudosasDe(4, 0).length === 1 && n.dudosasDe(4, 0)[0].url === LI(1),
      "videos: lo que no se pudo leer se vuelve a probar; lo que no es una lista se olvida");
    // Como mucho MAXIMO_MIRADAS por clic, las más chicas primero
    const t = new M.MediosPorPestana(100);
    for (let i = 0; i < 40; i++) t.dudosa(4, { url: "https://cdn.ej.org/p/" + i, frameId: 0, tamano: i === 33 ? 1000 : 600000 }, i);
    const vistas = [];
    await M.listaDelVideo(t, 4, 0, 11.5, async (u) => { vistas.push(u); return ""; });
    comprobar(vistas.length === M.MAXIMO_MIRADAS && vistas[0] === "https://cdn.ej.org/p/33",
      "videos: en cada clic se miran pocas respuestas dudosas, las más chicas primero (una lista es chica)");
  }

  decir(fallas ? `\n${fallas} FALLAS` : "\nTODO BIEN");
  if (enNode) process.exitCode = fallas ? 1 : 0;
})();
