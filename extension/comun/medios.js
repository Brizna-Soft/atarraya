// Videos de cada pestaña y de todos sus marcos: lo que el navegador pidió (archivos de video o audio, listas HLS y
// DASH) y lo que hay en la página. Lo usan background.js y videos.js; está aparte para poder probarlo sin navegador
// (pruebas/prueba-extension.js).
//
// Nada sale de la extensión hasta que la persona pulsa un botón. Nada de YouTube (tampoco googlevideo.com, ni después
// de una redirección) y nada de sitios que protegen sus videos con DRM.

// Atarraya no baja videos de YouTube: sus condiciones no lo permiten
const SITIOS_YOUTUBE = ["youtube.com", "youtu.be", "youtube-nocookie.com", "googlevideo.com", "youtubekids.com",
  "youtube.googleapis.com"];

// Sitios que protegen sus videos (DRM) y sus servidores de videos: ahí no hay botón ni lista
const SITIOS_PROTEGIDOS = ["netflix.com", "nflxvideo.net", "disneyplus.com", "disney-plus.net", "dssott.com",
  "bamgrid.com", "primevideo.com", "amazonvideo.com", "aiv-cdn.net", "aiv-delivery.net", "max.com", "hbomax.com",
  "hbomaxcdn.com", "hulu.com", "hulustream.com", "tv.apple.com", "music.apple.com", "paramountplus.com",
  "peacocktv.com", "crunchyroll.com", "starplus.com", "vix.com", "clarovideo.com", "spotify.com", "deezer.com",
  "tidal.com", "dazn.com", "mubi.com", "globoplay.globo.com", "blim.com"];

const TEXTO_YOUTUBE = "Atarraya no baja videos de YouTube: sus condiciones no lo permiten.";
const TEXTO_PROTEGIDO = "Este video está protegido: Atarraya no lo baja.";
const TEXTO_SITIO_PROTEGIDO = "Este sitio protege sus videos: Atarraya no los baja.";
// Un video «blob:» cuyo marco todavía no pidió su lista HLS o DASH
const TEXTO_SIN_TRANSMISION = "No encontré este video todavía: dale play y vuelve a intentar.";
// Lo que dice el botón cuando no se puede: cada motivo con su texto. «No se pudo hablar con Atarraya» es solo para cuando
// falló hablar con el programa; nunca para lo demás.
// Un video «blob:» cuyo marco pidió partes del video, pero ninguna lista que sea la de este video
const TEXTO_SIN_ARCHIVO = "No encontré el archivo completo de este video.";
// Hay listas, pero no se pudo comprobar cuál es la de este video (no se manda una que podría ser de otro)
const TEXTO_CUAL_VIDEO = "No encontré cuál de los videos de la página es este.";
const TEXTO_SIN_HABLAR = "No se pudo hablar con Atarraya. Vuelve a intentar.";
// Un error de la misma extensión (queda en el registro de Atarraya, si está abierto)
const TEXTO_FALLO_EXTENSION = "Algo falló en la extensión al buscar este video. Vuelve a intentar.";
// El botón no recibió respuesta de la extensión
const TEXTO_EXTENSION_REINICIADA = "La extensión de Atarraya se reinició. Recarga esta página y vuelve a intentar.";
const TEXTO_EXTENSION_CALLADA = "La extensión de Atarraya no respondió. Recarga esta página y vuelve a intentar.";

// Extensiones de archivos de video y de audio que se bajan enteros
const EXTENSIONES_MEDIO = {
  mp4: "video", m4v: "video", webm: "video", mov: "video", mkv: "video", ogv: "video", "3gp": "video", avi: "video",
  flv: "video", mp3: "audio", m4a: "audio", ogg: "audio", oga: "audio", opus: "audio", flac: "audio", wav: "audio",
  aac: "audio", weba: "audio",
};
// Trozos de una transmisión: no son un video entero
const EXTENSIONES_TROZO = ["ts", "m4s", "cmfv", "cmfa", "cmft", "m4f"];
const MINIMO_ARCHIVO = 64 * 1024;          // menos que esto no se muestra (sonidos de la página, iconos...)
const MINIMO_CON_TRANSMISION = 2 * 1024 * 1024;   // si su marco tiene una lista HLS o DASH, un «archivo» chico es un trozo
// Un reproductor HLS pide la lista principal y enseguida las de cada calidad o del audio: esas no se muestran aparte
// (Atarraya deja elegir la calidad desde la principal)
const VENTANA_LISTAS = 15000;

function hostDe(url) {
  try {
    return new URL(url).hostname.toLowerCase().replace(/\.$/, "");
  } catch (e) {
    return "";
  }
}

function enLista(host, lista) {
  return !!host && lista.some((d) => host === d || host.endsWith("." + d));
}

function esYouTube(url) {
  return enLista(hostDe(url), SITIOS_YOUTUBE);
}

function esSitioProtegido(url) {
  const host = hostDe(url);
  if (enLista(host, SITIOS_PROTEGIDOS)) return true;
  // Prime Video dentro de Amazon
  try {
    return /(^|\.)amazon\.[a-z.]+$/.test(host) && /\/gp\/video\//i.test(new URL(url).pathname);
  } catch (e) {
    return false;
  }
}

// ¿Esta dirección (o alguna de una cadena de redirecciones) es de YouTube o de un sitio con DRM?
function cadenaBloqueada(urls) {
  return urls.some((u) => u && (esYouTube(u) || esSitioProtegido(u)));
}

// Por qué no: «youtube» si alguna es de YouTube, «protegido» si es de un sitio con DRM, "" si ninguna
function motivoBloqueo(urls) {
  if (urls.some((u) => u && esYouTube(u))) return "youtube";
  if (urls.some((u) => u && esSitioProtegido(u))) return "protegido";
  return "";
}

function extensionDe(ruta) {
  const nombre = ruta.split("/").pop() || "";
  const punto = nombre.lastIndexOf(".");
  return punto > 0 ? nombre.slice(punto + 1).toLowerCase() : "";
}

function decodificar(texto) {
  try {
    return decodeURIComponent(texto);
  } catch (e) {
    return texto;
  }
}

// Nombres de trozos: «seg-12.mp4», «chunk_3.m4a», «frag5.mp4», «init.mp4», «segment12.webm»...
function pareceTrozo(ruta) {
  const nombre = (ruta.split("/").pop() || "").toLowerCase();
  return /(^|[^a-z])(seg|segment|chunk|frag|fragment)[-_]?\d+/.test(nombre) || /^init([-_.][^/]*)?\.(mp4|m4s|m4v|m4a|webm)$/.test(nombre);
}

// Qué es una respuesta: { formato: "hls" | "dash" | "archivo", clase: "video" | "audio" }, { trozo: true } si es un trozo
// de una transmisión, o null si no es un video
function clasificar(url, tipoMime) {
  let u;
  try {
    u = new URL(url);
  } catch (e) {
    return null;
  }
  if (u.protocol !== "http:" && u.protocol !== "https:") return null;
  const tipo = String(tipoMime || "").toLowerCase().split(";")[0].trim();
  const ruta = decodificar(u.pathname).toLowerCase();
  const ext = extensionDe(ruta);
  if (tipo === "text/html" || tipo === "application/xhtml+xml") return null;   // una página (de error, por ejemplo)
  if (tipo.includes("mpegurl") || ext === "m3u8") return { formato: "hls", clase: "video" };
  if (tipo === "application/dash+xml" || ext === "mpd") return { formato: "dash", clase: "video" };
  if (EXTENSIONES_TROZO.includes(ext) || tipo === "video/mp2t" || tipo.includes("iso.segment") || pareceTrozo(ruta))
    return { trozo: true };
  if (tipo.startsWith("video/")) return { formato: "archivo", clase: "video" };
  if (tipo.startsWith("audio/")) return { formato: "archivo", clase: "audio" };
  if (EXTENSIONES_MEDIO[ext] && (tipo === "" || tipo === "application/octet-stream" || tipo === "binary/octet-stream"))
    return { formato: "archivo", clase: EXTENSIONES_MEDIO[ext] };
  return null;
}

// --- Listas que no dicen que lo son ------------------------------------------------------------------------------------
// Hay sitios (LinkedIn, por ejemplo) que piden su lista HLS o DASH sin extensión en la dirección y con un tipo que no
// dice nada. Esas respuestas «dudosas» solo se anotan; se miran por dentro (se piden otra vez, solo el principio) cuando
// la persona pulsa el botón de un video «blob:» de su marco, nunca antes.
const TIPOS_BINARIOS = ["application/octet-stream", "binary/octet-stream", "application/binary", "application/x-binary"];
// Con estos tipos (o sin tipo) la dirección tiene que parecer de un video: hay muchos otros pedidos de texto en una página
const TIPOS_DE_TEXTO = ["", "text/plain", "application/xml", "text/xml"];
const PISTAS_DE_LISTA = /m3u|mpd|playlist|manifest|master|hls|dash|video|vid\b|stream|media/;
const EXTENSIONES_OTRAS = ["js", "mjs", "css", "json", "map", "png", "jpg", "jpeg", "gif", "webp", "avif", "svg", "ico", "bmp",
  "woff", "woff2", "ttf", "otf", "eot", "html", "htm", "wasm", "vtt", "srt", "zip", "rar", "7z", "exe", "pdf"];
const MAXIMO_DUDOSA = 4 * 1024 * 1024;   // una lista es chica; lo más grande es un video o un trozo

function puedeSerLista(url, tipoMime, tamano, metodo) {
  if (metodo && metodo !== "GET") return false;   // solo lo que se puede pedir otra vez sin cambiar nada
  let u;
  try {
    u = new URL(url);
  } catch (e) {
    return false;
  }
  if (u.protocol !== "http:" && u.protocol !== "https:") return false;
  if (tamano > MAXIMO_DUDOSA) return false;
  const tipo = String(tipoMime || "").toLowerCase().split(";")[0].trim();
  const ruta = decodificar(u.pathname).toLowerCase();
  const ext = extensionDe(ruta);
  if (EXTENSIONES_OTRAS.includes(ext) || EXTENSIONES_TROZO.includes(ext) || EXTENSIONES_MEDIO[ext] || pareceTrozo(ruta)) return false;
  if (TIPOS_BINARIOS.includes(tipo)) return true;
  return TIPOS_DE_TEXTO.includes(tipo) && PISTAS_DE_LISTA.test(ruta);
}

// Qué es un texto por dentro: «hls» (empieza con #EXTM3U), «dash» (su elemento principal es <MPD>) o "" (otra cosa)
function formatoPorContenido(texto) {
  const t = String(texto || "").replace(/^\uFEFF/, "").replace(/^\s+/, "");
  if (t.startsWith("#EXTM3U")) return "hls";
  const sinPrologo = t.replace(/^<\?xml[\s\S]*?\?>\s*/, "").replace(/^(<!--[\s\S]*?-->\s*)+/, "");
  return /^<([A-Za-z_][\w.-]*:)?MPD[\s>/]/.test(sinPrologo) ? "dash" : "";
}

// Mientras se baja el principio de una respuesta: ¿todavía puede ser una lista? (si no, se deja de bajar)
function puedeSerTextoDeLista(principio) {
  const t = String(principio || "").replace(/^\uFEFF/, "").replace(/^\s+/, "");
  return t.length < 7 || t.startsWith("#EXTM3U") || t.startsWith("<");
}

// Lo que dice una lista HLS: si es la principal (trae calidades), las direcciones completas de sus calidades (y la
// primera), y cuánto dura (la suma de sus #EXTINF; -1 si es la principal, si sigue en vivo, sin #EXT-X-ENDLIST, o si no
// se sabe)
function leerHls(texto, base) {
  let maestra = false;
  const calidades = [];
  let segundos = 0;
  let partes = 0;
  let fin = false;
  let esperaCalidad = false;
  for (const linea of String(texto || "").split(/\r?\n/)) {
    const l = linea.trim();
    if (!l) continue;
    if (l.startsWith("#EXT-X-STREAM-INF")) {
      maestra = true;
      esperaCalidad = true;
    } else if (l.startsWith("#EXTINF:")) {
      const s = parseFloat(l.slice(8));
      if (Number.isFinite(s) && s >= 0) {
        segundos += s;
        partes++;
      }
    } else if (l.startsWith("#EXT-X-ENDLIST")) {
      fin = true;
    } else if (!l.startsWith("#")) {
      if (esperaCalidad && calidades.length < 100) {
        try {
          calidades.push(new URL(l, base).href);
        } catch (e) {
          // una dirección que no se entiende: se sigue sin ella
        }
      }
      esperaCalidad = false;
    }
  }
  return { maestra, calidades, primera: calidades[0] || "", duracion: !maestra && fin && partes ? segundos : -1 };
}

// Cuánto dura un manifiesto DASH (su mediaPresentationDuration, como «PT1M3.5S»); -1 si no lo dice
function duracionDash(texto) {
  const m = /<(?:[A-Za-z_][\w.-]*:)?MPD\b[^>]*?\smediaPresentationDuration\s*=\s*["']([^"']+)["']/.exec(String(texto || ""));
  if (!m) return -1;
  const d = /^P(?:(\d+(?:\.\d+)?)D)?(?:T(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?)?$/.exec(m[1].trim());
  if (!d || !(d[1] || d[2] || d[3] || d[4])) return -1;
  return Number(d[1] || 0) * 86400 + Number(d[2] || 0) * 3600 + Number(d[3] || 0) * 60 + Number(d[4] || 0);
}

// Cuál de las listas de un marco es la del video que pulsó la persona: { entrada, motivo }. «listas»: [{ entrada,
// duracion (-1 si no se sabe), maestra, orden }] («orden»: cuándo la pidió el marco). «duracion»: la del video (0 si
// no se sabe). Nunca una sin comprobar que sea la de este video; si no hay, «motivo» dice por qué: «ninguna» (el marco
// no pidió listas), «otros» (todas son de otros videos) o «cual» (no se pudo saber cuál es la de este video).
//  - Con la duración del video: la que dura lo mismo (la principal antes que una de sus calidades; entre iguales, la más
//    nueva). En una página con varios videos (el inicio de LinkedIn, por ejemplo) cada uno tiene su lista.
//  - Sin ella (un video en vivo, por ejemplo): la primera, solo si todas son del mismo video (la principal y las que el
//    reproductor pidió enseguida: sus calidades y su audio).
function elegirLista(listas, duracion) {
  const porOrden = (listas || []).filter((l) => l && l.entrada).sort((a, b) => a.orden - b.orden);
  if (!porOrden.length) return { entrada: null, motivo: "ninguna" };
  if (!(duracion > 0 && Number.isFinite(duracion))) {
    const unSoloVideo = porOrden.every((l) => l.orden - porOrden[0].orden <= VENTANA_LISTAS);
    return unSoloVideo ? { entrada: porOrden[0].entrada, motivo: "" } : { entrada: null, motivo: "cual" };
  }
  const igual = (d) => d >= 0 && Math.abs(d - duracion) <= Math.max(1.5, duracion * 0.02);
  const iguales = porOrden.filter((l) => igual(l.duracion));
  if (iguales.length) {
    iguales.sort((a, b) => (b.maestra ? 1 : 0) - (a.maestra ? 1 : 0) || b.orden - a.orden);
    return { entrada: iguales[0].entrada, motivo: "" };
  }
  // Ninguna dura lo mismo. Si de alguna no se pudo saber cuánto dura, no se sabe cuál es la de este video.
  return { entrada: null, motivo: porOrden.some((l) => !(l.duracion >= 0)) ? "cual" : "otros" };
}

// Qué lista es una dirección y cuánto dura, según lo que dice por dentro: { formato, duracion, maestra }, null si se leyó
// y no es una lista, undefined si no se pudo leer. «leer(url)» trae el texto de una lista ("" si se leyó y no es una
// lista, null si no se pudo). De una lista principal se lee también una de sus calidades, que dice cuánto dura, pero
// solo una que el reproductor ya pidió («vista»): nunca se sigue lo que dice una lista hacia otras direcciones.
async function revisarLista(url, leer, vista = () => false) {
  const texto = await leer(url);
  if (texto === null || texto === undefined) return undefined;
  const formato = formatoPorContenido(texto);
  if (formato === "dash") return { formato, duracion: duracionDash(texto), maestra: false };
  if (formato !== "hls") return null;
  const h = leerHls(texto, url);
  if (!h.maestra) return { formato, duracion: h.duracion, maestra: false };
  const calidad = h.calidades.find((c) => vista(c));
  if (!calidad) return { formato, duracion: -1, maestra: true };
  const t = await leer(calidad);
  const c = formatoPorContenido(t) === "hls" ? leerHls(t, calidad) : null;
  return { formato, duracion: c && !c.maestra ? c.duracion : -1, maestra: true };
}

const MAXIMO_MIRADAS = 16;   // respuestas dudosas que se miran por dentro en cada clic

// La lista (HLS o DASH) de un video «blob:» de un marco, cuando la persona pulsó su botón: { entrada, motivo } (ver
// elegirLista). Entre las que ese marco pidió con su tipo y sus respuestas dudosas (que se miran por dentro ahora, las
// más chicas primero). Solo se vuelve a pedir lo que el reproductor de esa pestaña y ese marco pidió («leer» nunca recibe
// otra dirección). Las dudosas que resultan ser listas pasan a la lista de videos de la pestaña; las que no, se olvidan.
async function listaDelVideo(medios, tabId, frameId, duracion, leer, enParalelo = 4) {
  const conDuracion = duracion > 0 && Number.isFinite(duracion);
  const conocidas = medios.transmisionesDe(tabId, frameId);
  // Sin la duración del video no hace falta mirar nada: la primera, si todas son del mismo video
  if (conocidas.length && !conDuracion)
    return elegirLista(conocidas.map((e) => ({ entrada: e, duracion: -1, maestra: false, orden: e.cuando })), 0);
  const vista = (u) => medios.vista(tabId, frameId, u);
  const pedidas = new Map();   // cada dirección se pide una sola vez por clic
  const leerVista = (u) => {
    if (!vista(u)) return Promise.resolve(null);
    if (!pedidas.has(u)) pedidas.set(u, Promise.resolve().then(() => leer(u)));
    return pedidas.get(u);
  };
  const grupo = (d) => (d.tamano >= 0 && d.tamano <= 256 * 1024 ? 0 : d.tamano < 0 ? 1 : 2);
  const dudosas = medios
    .dudosasDe(tabId, frameId)
    .sort((a, b) => grupo(a) - grupo(b) || b.cuando - a.cuando)
    .slice(0, MAXIMO_MIRADAS);
  const tareas = [
    ...conocidas.map((e) => async () => {
      const r = await revisarLista(e.url, leerVista, vista);
      // Una conocida que no se pudo leer sigue siendo una lista: no se sabe cuánto dura
      return { entrada: e, duracion: r ? r.duracion : -1, maestra: !!(r && r.maestra), orden: e.cuando };
    }),
    ...dudosas.map((d) => async () => {
      const r = await revisarLista(d.url, leerVista, vista);
      if (!r) {
        if (r === null) medios.sinDudas(tabId, d.url);   // no es una lista: no se vuelve a mirar (si no se pudo leer, sí)
        return null;
      }
      const entrada = medios.anotar(tabId, { url: d.url, formato: r.formato, clase: "video", tipoMime: d.tipoMime || "",
        nombre: nombreDeMedio(d.url, ""), frameId, vista: true }, d.cuando);
      medios.sinDudas(tabId, d.url);
      return entrada ? { entrada, duracion: r.duracion, maestra: r.maestra, orden: d.cuando } : null;
    }),
  ];
  const listas = [];
  let siguiente = 0;
  const trabajar = async () => {
    while (siguiente < tareas.length) {
      const r = await tareas[siguiente++]();
      if (r) listas.push(r);
    }
  };
  await Promise.all(Array.from({ length: Math.min(enParalelo, tareas.length) }, trabajar));
  return elegirLista(listas, conDuracion ? duracion : 0);
}

// Un error de la extensión para el registro de Atarraya, sin datos de la persona: de cada dirección queda solo el sitio
// (sin usuario ni contraseña, sin ruta ni parámetros); sin «usuario:clave@», sin correos, sin el valor de parámetros ni
// de cookies («nombre=…») y sin cadenas largas que parezcan claves. Las direcciones de la misma extensión
// (chrome-extension://, moz-extension://) quedan con su archivo y su línea, sin parámetros: dicen dónde falló.
function limpiarParaRegistro(texto) {
  let t = String(texto === undefined || texto === null ? "" : texto).replace(/\u0000/g, " ");
  // Primero las direcciones (ya limpias, se apartan para que lo demás no las toque)
  const direcciones = [];
  t = t.replace(/\b([a-z][a-z0-9+.-]*):\/\/([^\s/?#'"<>()]*)([^\s'"<>()]*)/gi, (todo, esquema, autoridad, resto) => {
    const e = esquema.toLowerCase();
    let limpia;
    if ((e === "chrome-extension" || e === "moz-extension") && !autoridad.includes("@"))
      limpia = e + "://" + autoridad + resto.replace(/[?#].*$/, "");
    else limpia = e + "://" + autoridad.replace(/^.*@/, "") + (resto ? "/…" : "");
    direcciones.push(limpia);
    return "\u0000" + (direcciones.length - 1) + "\u0000";
  });
  t = t
    .replace(/[^\s/:@'"<>()\u0000]+:[^\s/@'"<>()\u0000]*@/g, "")   // usuario:clave@ suelto
    .replace(/[\w.%+-]+@[\w-]+(?:\.[\w-]+)+/g, "(correo)")         // correos (y usuario@sitio)
    .replace(/=\s*"[^"]*"/g, '="…"')                                // valores entre comillas
    .replace(/=\s*'[^']*'/g, "='…'")
    .replace(/=[^\s;&,'"<>()\u0000]+/g, "=…")                       // valores de parámetros y de cookies
    .replace(/[A-Za-z0-9+/_.%-]{20,}/g, (x) => (/\d/.test(x) && /[A-Za-z]/.test(x) ? "…" : x));   // claves sueltas
  t = t.replace(/\u0000(\d+)\u0000/g, (todo, i) => direcciones[Number(i)] || "");
  return t.replace(/\s+/g, " ").trim();
}

// El botón no recibió respuesta de la extensión. Si este marco quedó hablando con una extensión que ya no está (se
// actualizó, se recargó o se apagó mientras la página seguía abierta), recargar la página lo arregla.
function textoSinRespuesta(error, extensionViva) {
  const mensaje = String((error && error.message) || error || "");
  if (!extensionViva || /context invalidated/i.test(mensaje)) return TEXTO_EXTENSION_REINICIADA;
  return TEXTO_EXTENSION_CALLADA;
}

// El tamaño entero: el total de «Content-Range» (una respuesta 206 de un video que se reproduce por partes) o
// «Content-Length» de una respuesta entera. -1 si no se sabe.
function tamanoDe(codigo, cabeceras) {
  const rango = cabeceras["content-range"];
  if (rango) {
    const m = /\/\s*(\d+)\s*$/.exec(rango);
    return m ? Number(m[1]) : -1;
  }
  if (codigo === 200 && cabeceras["content-length"] !== undefined) {
    const n = Number(cabeceras["content-length"]);
    return Number.isFinite(n) && n >= 0 ? n : -1;
  }
  return -1;
}

function nombreDeDisposicion(valor) {
  if (!valor) return "";
  let m = /filename\*\s*=\s*([^']*)'[^']*'([^;]+)/i.exec(valor);
  if (m) return decodificar(m[2].trim().replace(/^"|"$/g, ""));
  m = /filename\s*=\s*"([^"]*)"/i.exec(valor) || /filename\s*=\s*([^;]+)/i.exec(valor);
  return m ? m[1].trim() : "";
}

// El nombre que se muestra: el del servidor o el final de la dirección
function nombreDeMedio(url, disposicion) {
  const delServidor = nombreDeDisposicion(disposicion);
  if (delServidor) return delServidor;
  try {
    return decodificar(new URL(url).pathname.split("/").pop() || "") || hostDe(url);
  } catch (e) {
    return "";
  }
}

// «720p» a partir del alto del video (0 si no se sabe)
function calidadDe(alto) {
  return alto > 0 ? alto + "p" : "";
}

function textoTamano(bytes) {
  if (!(bytes >= 0)) return "";
  const unidades = ["bytes", "KB", "MB", "GB", "TB"];
  let n = bytes;
  let i = 0;
  while (n >= 1024 && i < unidades.length - 1) {
    n /= 1024;
    i++;
  }
  return (i === 0 ? String(n) : n.toFixed(1)) + " " + unidades[i];
}

// Cabeceras de webRequest («[{name, value}]») como objeto con claves en minúsculas
function cabecerasDe(lista) {
  const r = {};
  for (const h of lista || []) if (h && h.name) r[h.name.toLowerCase()] = h.value || "";
  return r;
}

// Los videos de cada pestaña, de todos sus marcos juntos. Cada uno recuerda en qué marco está (su id y su dirección):
// las cookies y el «referer» que van a Atarraya son los de ese marco. Se guarda en storage.session, porque el
// «service worker» se apaga cuando no hace nada.
class MediosPorPestana {
  constructor(tope = 60) {
    this.tope = tope;
    // tabId -> { medios: [entrada], marcos: {frameId: url}, muestras: {frameId: {host: url}}, protegidos: [frameId],
    //            bloqueados: {frameId: "youtube" | "protegido"}, dudosas: [{ url, frameId, tamano, tipoMime, cuando }] }
    this.pestanas = new Map();
  }

  pestana(tabId, crear = true) {
    let p = this.pestanas.get(tabId);
    if (!p && crear) {
      p = { medios: [], marcos: {}, muestras: {}, protegidos: [], bloqueados: {}, dudosas: [] };
      this.pestanas.set(tabId, p);
    }
    if (p && !p.bloqueados) p.bloqueados = {};
    if (p && !Array.isArray(p.dudosas)) p.dudosas = [];
    return p;
  }

  // Un marco que reproduce algo de YouTube (googlevideo.com, también por una redirección) o de un sitio con DRM: nada
  // de ese marco va a la lista ni lleva botón, ni lo que ya estaba ni lo que llegue después
  bloquearMarco(tabId, frameId, motivo) {
    const p = this.pestana(tabId);
    p.bloqueados[frameId] = motivo || "youtube";
    p.medios = p.medios.filter((e) => e.frameId !== frameId);
    p.dudosas = p.dudosas.filter((d) => d.frameId !== frameId);
  }

  marcoBloqueado(tabId, frameId) {
    const p = this.pestana(tabId, false);
    return (p && p.bloqueados[frameId]) || "";
  }

  // Por qué no hay nada en esta pestaña («youtube», «protegido» o "")
  motivoDePestana(tabId) {
    const p = this.pestana(tabId, false);
    const motivos = p ? Object.values(p.bloqueados) : [];
    return motivos.includes("youtube") ? "youtube" : motivos[0] || "";
  }

  // Direcciones que resultaron ser de YouTube o de un sitio con DRM (por una redirección): se quitan de la lista
  quitar(tabId, urls) {
    const p = this.pestana(tabId, false);
    if (!p) return;
    p.medios = p.medios.filter((e) => !urls.includes(e.url));
    p.dudosas = p.dudosas.filter((d) => !urls.includes(d.url));
  }

  borrar(tabId) {
    this.pestanas.delete(tabId);
  }

  // Un marco que cambió de página: se olvida lo suyo
  borrarMarco(tabId, frameId) {
    const p = this.pestana(tabId, false);
    if (!p) return;
    p.medios = p.medios.filter((e) => e.frameId !== frameId);
    p.dudosas = p.dudosas.filter((d) => d.frameId !== frameId);
    delete p.marcos[frameId];
    delete p.muestras[frameId];
    delete p.bloqueados[frameId];
    p.protegidos = p.protegidos.filter((f) => f !== frameId);
  }

  marco(tabId, frameId, url) {
    if (!url || !/^https?:/i.test(url)) return;
    this.pestana(tabId).marcos[frameId] = url;
  }

  // Un pedido de medios (también un trozo) de un marco: para mandar después las cookies de esos servidores
  muestra(tabId, frameId, url) {
    const host = hostDe(url);
    if (!host || esYouTube(url)) return;
    const p = this.pestana(tabId);
    const m = (p.muestras[frameId] = p.muestras[frameId] || {});
    if (!m[host] && Object.keys(m).length < 20) m[host] = url;
  }

  // Un video, una lista o un archivo de audio de un marco. «entrada»: { url, formato, clase, tamano, tipoMime, nombre,
  // frameId, alto, protegido, vista }. «vista»: la extensión vio al navegador pedirla para ese marco (no la contó la
  // página): solo esas se pueden volver a pedir. Una dirección que ya estaba se junta con la anterior.
  anotar(tabId, entrada, ahora = Date.now()) {
    if (!entrada || !entrada.url || !entrada.formato || cadenaBloqueada([entrada.url])) return null;
    const p = this.pestana(tabId);
    const frameId = typeof entrada.frameId === "number" && entrada.frameId >= 0 ? entrada.frameId : 0;
    if (p.bloqueados[frameId]) return null;
    let e = p.medios.find((x) => x.url === entrada.url);
    if (!e) {
      e = { url: entrada.url, formato: entrada.formato, clase: entrada.clase || "video", tamano: -1, tipoMime: "",
        nombre: "", frameId, alto: 0, protegido: false, cuando: ahora };
      p.medios.push(e);
      // Tope: se van las más viejas
      while (p.medios.length > this.tope) p.medios.shift();
    }
    if (entrada.tamano >= 0 && entrada.tamano > e.tamano) e.tamano = entrada.tamano;
    if (entrada.tipoMime && !e.tipoMime) e.tipoMime = entrada.tipoMime;
    if (entrada.nombre && !e.nombre) e.nombre = entrada.nombre;
    if (entrada.alto > 0) e.alto = entrada.alto;
    if (entrada.protegido || p.protegidos.includes(frameId)) e.protegido = true;
    if (entrada.vista && e.frameId === frameId) e.vista = true;
    return e;
  }

  // ¿El navegador pidió esta dirección para el reproductor de esa pestaña y ese marco? (la vio la extensión: una lista o
  // una respuesta dudosa). Solo esas se pueden volver a pedir con cookies.
  vista(tabId, frameId, url) {
    const p = this.pestana(tabId, false);
    if (!p || !url) return false;
    return p.medios.some((e) => e.frameId === frameId && e.vista === true && e.url === url)
      || p.dudosas.some((d) => d.frameId === frameId && d.url === url);
  }

  // El marco tiene un video protegido (DRM): sus listas HLS y DASH también lo están
  protegerMarco(tabId, frameId, src) {
    const p = this.pestana(tabId);
    if (!p.protegidos.includes(frameId)) p.protegidos.push(frameId);
    for (const e of p.medios) {
      if (e.frameId === frameId && (e.formato !== "archivo" || !src || e.url === src)) e.protegido = true;
    }
  }

  buscar(tabId, url) {
    const p = this.pestana(tabId, false);
    return (p && p.medios.find((e) => e.url === url)) || null;
  }

  // La dirección del marco donde está un video (o la de la pestaña, si no se sabe)
  urlDeMarco(tabId, frameId, respaldo = "") {
    const p = this.pestana(tabId, false);
    return (p && p.marcos[frameId]) || respaldo;
  }

  // Direcciones de muestra de los servidores de medios de un marco (para sus cookies)
  muestrasDeMarco(tabId, frameId) {
    const p = this.pestana(tabId, false);
    return p && p.muestras[frameId] ? Object.values(p.muestras[frameId]) : [];
  }

  // La transmisión (HLS o DASH) de un video que se reproduce desde un «blob:»: la primera que pidió ESE marco (suele
  // ser la lista principal). Nunca la de otro marco: sería otro video, con las cookies y el «referer» de otra página.
  transmisionDe(tabId, frameId) {
    return this.transmisionesDe(tabId, frameId)[0] || null;
  }

  // Todas las listas HLS y DASH que pidió un marco, en el orden en que las pidió
  transmisionesDe(tabId, frameId) {
    const p = this.pestana(tabId, false);
    if (!p) return [];
    return p.medios
      .filter((e) => e.frameId === frameId && (e.formato === "hls" || e.formato === "dash"))
      .sort((a, b) => a.cuando - b.cuando);
  }

  // Una respuesta que puede ser una lista aunque no lo diga (ver puedeSerLista). Se recuerda por marco hasta que se mira
  // por dentro; con un tope por pestaña (se van las más viejas).
  dudosa(tabId, entrada, ahora = Date.now()) {
    if (!entrada || !entrada.url || cadenaBloqueada([entrada.url])) return;
    const frameId = typeof entrada.frameId === "number" && entrada.frameId >= 0 ? entrada.frameId : 0;
    const p = this.pestana(tabId);
    if (p.bloqueados[frameId] || p.medios.some((e) => e.url === entrada.url)) return;
    p.dudosas = p.dudosas.filter((d) => d.url !== entrada.url);
    p.dudosas.push({ url: entrada.url, frameId, tamano: entrada.tamano >= 0 ? entrada.tamano : -1,
      tipoMime: String(entrada.tipoMime || "").slice(0, 100), cuando: ahora });
    while (p.dudosas.length > this.tope) p.dudosas.shift();
  }

  dudosasDe(tabId, frameId) {
    const p = this.pestana(tabId, false);
    return p ? p.dudosas.filter((d) => d.frameId === frameId) : [];
  }

  // Ya se miró por dentro: si era una lista, ya está con los videos; si no, no se vuelve a mirar
  sinDudas(tabId, url) {
    const p = this.pestana(tabId, false);
    if (p) p.dudosas = p.dudosas.filter((d) => d.url !== url);
  }

  // Lo que muestra la ventanita: sin los trozos y sin lo muy chico. Primero lo que se puede bajar (video antes que
  // solo audio), lo protegido al final; dentro de cada grupo, en el orden en que apareció.
  lista(tabId) {
    const p = this.pestana(tabId, false);
    if (!p) return [];
    // Un «archivo» chico en un marco que reproduce una lista HLS o DASH suele ser un trozo de esa lista (en otro
    // marco, no)
    const conTransmision = new Set(p.medios.filter((e) => e.formato !== "archivo").map((e) => e.frameId));
    // La primera lista HLS de cada marco (la principal); las que pidió poco después son sus calidades o su audio
    const principales = {};
    for (const e of p.medios) {
      if (e.formato === "hls" && !(e.frameId in principales)) principales[e.frameId] = e;
    }
    const visibles = p.medios.filter((e) => {
      if (p.bloqueados[e.frameId]) return false;
      if (e.formato === "hls") {
        const principal = principales[e.frameId];
        return principal === e || e.cuando - principal.cuando > VENTANA_LISTAS;
      }
      if (e.formato !== "archivo") return true;
      if (e.tamano >= 0 && e.tamano < MINIMO_ARCHIVO) return false;
      if (conTransmision.has(e.frameId) && e.tamano >= 0 && e.tamano < MINIMO_CON_TRANSMISION) return false;
      return true;
    });
    const grupo = (e) => (e.protegido ? 2 : e.clase === "audio" ? 1 : 0);
    return visibles
      .map((e, i) => ({ e, i }))
      .sort((a, b) => grupo(a.e) - grupo(b.e) || a.e.cuando - b.e.cuando || a.i - b.i)
      .map(({ e }) => Object.assign({}, e, { marco: this.urlDeMarco(tabId, e.frameId, "") }));
  }

  exportar() {
    const r = {};
    for (const [tabId, p] of this.pestanas) r[tabId] = p;
    return r;
  }

  // Junta lo guardado con lo que ya está en memoria (lo de memoria es más nuevo)
  cargar(guardado) {
    if (!guardado || typeof guardado !== "object") return;
    for (const [clave, p] of Object.entries(guardado)) {
      const tabId = Number(clave);
      if (!Number.isInteger(tabId) || !p || !Array.isArray(p.medios)) continue;
      const actual = this.pestana(tabId);
      for (const e of p.medios) {
        if (e && e.url && !actual.medios.some((x) => x.url === e.url)) actual.medios.push(Object.assign({}, e));
      }
      actual.medios.sort((a, b) => a.cuando - b.cuando);
      while (actual.medios.length > this.tope) actual.medios.shift();
      actual.marcos = Object.assign({}, p.marcos || {}, actual.marcos);
      actual.muestras = Object.assign({}, p.muestras || {}, actual.muestras);
      actual.protegidos = [...new Set([...(p.protegidos || []), ...actual.protegidos])];
      actual.bloqueados = Object.assign({}, p.bloqueados && typeof p.bloqueados === "object" ? p.bloqueados : {}, actual.bloqueados);
      actual.medios = actual.medios.filter((e) => !actual.bloqueados[e.frameId]);
      const guardadas = Array.isArray(p.dudosas) ? p.dudosas : [];
      for (const d of guardadas) {
        if (d && typeof d.url === "string" && d.url && typeof d.frameId === "number" && !actual.dudosas.some((x) => x.url === d.url)
            && !actual.medios.some((e) => e.url === d.url)) actual.dudosas.push(Object.assign({}, d));
      }
      actual.dudosas = actual.dudosas.filter((d) => !actual.bloqueados[d.frameId]).sort((a, b) => a.cuando - b.cuando);
      while (actual.dudosas.length > this.tope) actual.dudosas.shift();
    }
  }
}

// Lo que se le pasa a Atarraya cuando la persona pulsa «Bajar» (o el botón sobre el video)
function pedidoDeVideo(entrada, pagina, titulo, marco, cookies, agente) {
  return {
    tipo: "video",
    formato: entrada.formato,
    url: entrada.url,
    titulo: String(titulo || "").trim().slice(0, 300),
    referer: marco || pagina || "",
    pagina: pagina || "",
    cookies: cookies || "",
    agente: agente || "",
    // El tamaño de una lista HLS o DASH no es el del video
    tamano: entrada.formato === "archivo" && entrada.tamano >= 0 ? entrada.tamano : -1,
    tipoMime: entrada.tipoMime || "",
  };
}

// Las direcciones de los sitios para las cookies partidas por sitio de arriba («partitioned»): el sitio de la página
// y, si tiene subdominios, también su dominio (no se sabe cuál es el «sitio» sin la lista de sufijos públicos)
function sitiosDeArriba(pagina) {
  try {
    const u = new URL(pagina);
    if (u.protocol !== "http:" && u.protocol !== "https:") return [];
    const partes = u.hostname.split(".");
    const r = [u.protocol + "//" + u.hostname];
    for (let i = 1; i < partes.length - 1; i++) r.push(u.protocol + "//" + partes.slice(i).join("."));
    return r;
  } catch (e) {
    return [];
  }
}

if (typeof module !== "undefined")
  module.exports = {
    SITIOS_YOUTUBE, SITIOS_PROTEGIDOS, TEXTO_YOUTUBE, TEXTO_PROTEGIDO, TEXTO_SITIO_PROTEGIDO, TEXTO_SIN_TRANSMISION, esYouTube,
    esSitioProtegido, cadenaBloqueada, motivoBloqueo, clasificar, tamanoDe, nombreDeMedio, calidadDe, textoTamano,
    cabecerasDe, MediosPorPestana, pedidoDeVideo, sitiosDeArriba, hostDe,
    TEXTO_SIN_ARCHIVO, TEXTO_SIN_HABLAR, TEXTO_FALLO_EXTENSION, TEXTO_EXTENSION_REINICIADA, TEXTO_EXTENSION_CALLADA,
    puedeSerLista, formatoPorContenido, puedeSerTextoDeLista, leerHls, duracionDash, elegirLista, revisarLista, listaDelVideo,
    textoSinRespuesta, MAXIMO_MIRADAS, TEXTO_CUAL_VIDEO, limpiarParaRegistro,
  };
