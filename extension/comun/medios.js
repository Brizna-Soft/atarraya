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
    //            bloqueados: {frameId: "youtube" | "protegido"} }
    this.pestanas = new Map();
  }

  pestana(tabId, crear = true) {
    let p = this.pestanas.get(tabId);
    if (!p && crear) {
      p = { medios: [], marcos: {}, muestras: {}, protegidos: [], bloqueados: {} };
      this.pestanas.set(tabId, p);
    }
    if (p && !p.bloqueados) p.bloqueados = {};
    return p;
  }

  // Un marco que reproduce algo de YouTube (googlevideo.com, también por una redirección) o de un sitio con DRM: nada
  // de ese marco va a la lista ni lleva botón, ni lo que ya estaba ni lo que llegue después
  bloquearMarco(tabId, frameId, motivo) {
    const p = this.pestana(tabId);
    p.bloqueados[frameId] = motivo || "youtube";
    p.medios = p.medios.filter((e) => e.frameId !== frameId);
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
    if (p) p.medios = p.medios.filter((e) => !urls.includes(e.url));
  }

  borrar(tabId) {
    this.pestanas.delete(tabId);
  }

  // Un marco que cambió de página: se olvida lo suyo
  borrarMarco(tabId, frameId) {
    const p = this.pestana(tabId, false);
    if (!p) return;
    p.medios = p.medios.filter((e) => e.frameId !== frameId);
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
  // frameId, alto, protegido }. Una dirección que ya estaba se junta con la anterior.
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
    return e;
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

  // La transmisión (HLS o DASH) de un video que se reproduce desde un «blob:»: la primera que pidió ese marco (suele
  // ser la lista principal); si no hay, la primera de la pestaña
  transmisionDe(tabId, frameId) {
    const p = this.pestana(tabId, false);
    if (!p) return null;
    const flujos = p.medios.filter((e) => e.formato === "hls" || e.formato === "dash");
    return flujos.find((e) => e.frameId === frameId) || flujos[0] || null;
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
    SITIOS_YOUTUBE, SITIOS_PROTEGIDOS, TEXTO_YOUTUBE, TEXTO_PROTEGIDO, TEXTO_SITIO_PROTEGIDO, esYouTube,
    esSitioProtegido, cadenaBloqueada, motivoBloqueo, clasificar, tamanoDe, nombreDeMedio, calidadDe, textoTamano,
    cabecerasDe, MediosPorPestana, pedidoDeVideo, sitiosDeArriba, hostDe,
  };
