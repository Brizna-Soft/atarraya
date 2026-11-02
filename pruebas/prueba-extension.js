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
      MediosPorPestana, pedidoDeVideo, sitiosDeArriba, TEXTO_YOUTUBE, TEXTO_SIN_TRANSMISION };
const E = enNode ? require("../extension/comun/esperas.js") : { normalizarTexto, segundosDe, reconocerEspera };
const C = enNode
  ? require("../extension/comun/cola-navegador.js")
  : { PestanasDeLaCola, paginaPorAbrir, sitioBase, avisoDeSeguimiento, TomaDelNavegador, resultadoCancelar, resolverSinConfirmar, tocaFinal,
      finalEntregado, seguidasGuardadas, nuevaInstancia, instanciaValida, tomaNueva, instanciaDeToma, informeDeSeguidas };

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
  comprobar(/if \(!entrada\) return \{ ok: false, texto: TEXTO_SIN_TRANSMISION \}/.test(fondo) && !/flujos\[0\]/.test(leer("../extension/comun/medios.js")),
    "extensión: el botón de un «blob:» sin lista en su marco no manda nada a Atarraya");
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
  // Lo que se decide queda anotado antes de pasarle la descarga a Atarraya, y al despertar se sigue decidiendo
  comprobar(enLaCola.indexOf("probando: { desde: Date.now() }") > 0 && enLaCola.indexOf("probando: { desde: Date.now() }") < enLaCola.indexOf("guardarCola()")
    && enLaCola.indexOf("guardarCola()") < enLaCola.indexOf("ok = await enviar(pedido)") && /await decidirToma\(item\.id\)/.test(enLaCola)
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

decir(fallas ? `\n${fallas} FALLAS` : "\nTODO BIEN");
if (enNode) process.exitCode = fallas ? 1 : 0;
