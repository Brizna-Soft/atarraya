// Atarraya para el navegador: pasa las descargas de cualquier sitio al programa Atarraya de Windows, y también los
// videos de las páginas cuando la persona pulsa «Descargar este video» o «Bajar» en la ventanita.
// No guarda nada fuera del navegador, no manda datos a ningún servidor y solo habla con el programa instalado en la
// misma PC (native messaging).

// En Chrome la extensión corre como «service worker» y carga origen.js y medios.js así; Firefox los carga desde su
// manifiesto
if (typeof importScripts === "function") importScripts("origen.js", "medios.js");

const api = globalThis.browser ?? globalThis.chrome;
const HOST = "com.briznasoft.atarraya";
const AJUSTES = { capturar: true, minimoMB: 0 };
// Pedidos donde pueden venir videos: los del reproductor (media) y los que hacen los reproductores HLS y DASH
const TIPOS_MEDIOS = ["media", "xmlhttprequest", "other", "object"];

let listoHasta = 0;            // hasta cuándo damos por hecho que el programa responde
const devueltas = new Set();   // descargas que devolvimos al navegador: no se capturan otra vez
const pestanas = new PestanasDePedidos();   // en qué pestaña nació cada pedido (ver origen.js)

// Los videos de cada pestaña (ver medios.js). Solo miran: nada sale de aquí hasta que la persona pulsa un botón. Se
// guardan en storage.session (dentro del navegador, se borra al cerrarlo) porque este «service worker» se apaga
// cuando no hace nada.
const medios = new MediosPorPestana();
const listo = (async () => {
  try {
    if (api.storage.session) {
      const g = await api.storage.session.get("medios");
      medios.cargar(g && g.medios);
    }
  } catch (e) {
    // Sin storage.session la lista vive solo mientras el «service worker» está despierto
  }
})();
let guardando = null;
function guardarPronto() {
  if (!api.storage.session) return;
  clearTimeout(guardando);
  guardando = setTimeout(() => {
    api.storage.session.set({ medios: medios.exportar() }).catch(() => {});
  }, 300);
}

// Las descargas que salen de un formulario (POST) no se pueden repetir desde el programa: esas se quedan
// en el navegador. Aquí se anotan las direcciones pedidas con otro método que no sea GET.
const noRepetibles = new Map();   // url -> cuándo
api.webRequest.onBeforeRequest.addListener(
  (d) => {
    pestanas.anotar(d.url, d.tabId);
    // Una página nueva en la pestaña (o en uno de sus marcos): se olvidan sus videos de antes
    if (d.tabId >= 0 && (d.type === "main_frame" || d.type === "sub_frame")) {
      listo.then(() => {
        if (d.type === "main_frame") medios.borrar(d.tabId);
        else medios.borrarMarco(d.tabId, d.frameId);
        guardarPronto();
      });
    }
    if (!d.method || d.method === "GET") return;
    noRepetibles.set(d.url, Date.now());
    if (noRepetibles.size > 300) {
      const viejo = Date.now() - 10 * 60000;
      for (const [u, t] of noRepetibles) if (t < viejo) noRepetibles.delete(u);
    }
  },
  { urls: ["<all_urls>"], types: ["main_frame", "sub_frame", "other"] }
);

// Pedidos cuya cadena de redirecciones pasó por YouTube (googlevideo.com) o por un sitio con DRM: no se anotan, y
// ninguna dirección de esa cadena entra a la lista ni lleva botón (por ejemplo, un <video> de cualquier página cuya
// dirección redirige a googlevideo.com)
const bloqueadas = new Map();      // requestId -> { cuando, motivo }
const cadenas = new Map();         // requestId -> direcciones de su cadena de redirecciones hasta ahora
const urlsBloqueadas = new Map();  // dirección -> { cuando, motivo }
function olvidarViejos(mapa, minutos) {
  if (mapa.size <= 500) return;
  const viejo = Date.now() - minutos * 60000;
  for (const [k, v] of mapa) if ((v.cuando || 0) < viejo) mapa.delete(k);
}
// «youtube» o «protegido» si la dirección es de YouTube o de un sitio con DRM, o si redirige a uno; si no, ""
function motivoDeUrl(url) {
  if (!url) return "";
  const m = motivoBloqueo([url]);
  if (m) return m;
  const b = urlsBloqueadas.get(url);
  return b ? b.motivo : "";
}
api.webRequest.onBeforeRedirect.addListener(
  (d) => {
    const antes = cadenas.get(d.requestId);
    const cadena = [...(antes ? antes.urls : []), d.url];
    const motivo = motivoBloqueo([...cadena, d.redirectUrl]) || cadena.map(motivoDeUrl).find((m) => m) || "";
    if (!motivo) {
      cadenas.set(d.requestId, { cuando: Date.now(), urls: cadena.slice(-10) });
      olvidarViejos(cadenas, 5);
      return;
    }
    cadenas.delete(d.requestId);
    const ahora = Date.now();
    bloqueadas.set(d.requestId, { cuando: ahora, motivo });
    for (const u of [...cadena, d.redirectUrl]) urlsBloqueadas.set(u, { cuando: ahora, motivo });
    olvidarViejos(bloqueadas, 5);
    olvidarViejos(urlsBloqueadas, 30);
    if (d.tabId >= 0) {
      listo.then(() => {
        medios.quitar(d.tabId, [...cadena, d.redirectUrl]);
        guardarPronto();
      });
    }
  },
  { urls: ["<all_urls>"], types: TIPOS_MEDIOS }
);

// Solo mira las cabeceras de la respuesta (sin bloquear ni cambiar nada) para saber si es un video, una lista HLS o
// DASH, o un trozo de una transmisión
api.webRequest.onHeadersReceived.addListener(
  (d) => {
    observar(d).catch(() => {});
  },
  { urls: ["<all_urls>"], types: TIPOS_MEDIOS },
  ["responseHeaders"]
);

async function observar(d) {
  if (d.tabId < 0) return;   // no es de una pestaña (por ejemplo, de la misma extensión)
  // Nada de YouTube ni de sitios con DRM: ni la dirección (ni las de antes de una redirección), ni la página o el marco
  // que la pidió
  const porCadena = bloqueadas.get(d.requestId);
  bloqueadas.delete(d.requestId);
  cadenas.delete(d.requestId);
  const motivo = (porCadena && porCadena.motivo) || motivoBloqueo([d.url, d.documentUrl, d.originUrl, d.initiator]);
  const cab = cabecerasDe(d.responseHeaders);
  const c = clasificar(d.url, cab["content-type"]);
  if (!c) return;
  const frameId = d.frameId >= 0 ? d.frameId : 0;
  if (motivo) {
    // Un marco que reproduce videos de YouTube (o de un sitio con DRM): nada de ese marco va a la lista ni lleva botón
    await listo;
    medios.bloquearMarco(d.tabId, frameId, motivo);
    guardarPronto();
    return;
  }
  if (d.statusCode !== 200 && d.statusCode !== 206) return;
  await listo;
  if (d.documentUrl) medios.marco(d.tabId, frameId, d.documentUrl);   // Firefox dice la página del marco
  medios.muestra(d.tabId, frameId, d.url);
  if (!c.trozo) {
    medios.anotar(d.tabId, {
      url: d.url,
      formato: c.formato,
      clase: c.clase,
      tamano: tamanoDe(d.statusCode, cab),
      tipoMime: cab["content-type"] || "",
      nombre: nombreDeMedio(d.url, cab["content-disposition"]),
      frameId,
    });
  }
  guardarPronto();
}

if (api.tabs.onRemoved) {
  api.tabs.onRemoved.addListener((tabId) => {
    listo.then(() => {
      medios.borrar(tabId);
      guardarPronto();
    });
  });
}

async function ajustes() {
  return Object.assign({}, AJUSTES, await api.storage.local.get(AJUSTES));
}

function hablar(mensaje) {
  return api.runtime.sendNativeMessage(HOST, mensaje);
}

async function programaListo() {
  if (Date.now() < listoHasta) return true;
  try {
    const r = await hablar({ tipo: "ping" });
    if (r && r.ok) {
      listoHasta = Date.now() + 60000;
      return true;
    }
  } catch (e) {
    // Sin el programa o sin conectar: el navegador baja como siempre
  }
  return false;
}

// Una cookie en formato Netscape
function lineaCookie(c) {
  const dominio = (c.httpOnly ? "#HttpOnly_" : "") + c.domain;
  const subdominios = c.hostOnly ? "FALSE" : "TRUE";
  const vence = c.expirationDate ? Math.floor(c.expirationDate) : 0;
  return [dominio, subdominios, c.path, c.secure ? "TRUE" : "FALSE", vence, c.name, c.value].join("\t");
}

// Cookies del sitio en formato Netscape, para que el programa pueda bajar lo que pide sesión iniciada
async function cookiesPara(...urls) {
  const lineas = new Set();
  for (const url of urls) {
    if (!url || !/^https?:/i.test(url)) continue;
    try {
      for (const c of await api.cookies.getAll({ url })) lineas.add(lineaCookie(c));
    } catch (e) {
      // Sin permiso para ese sitio: se manda sin cookies
    }
  }
  return [...lineas].join("\n");
}

// Las cookies de un video que está en un marco: las de su dirección, las del marco y las de los servidores de sus
// trozos. Un marco de otro sitio puede tener cookies «partidas» por el sitio de arriba: se piden también esas (donde el
// navegador no las conoce, la consulta falla y se sigue sin ellas).
async function cookiesDeMarco(urls, pagina) {
  const lineas = new Set();
  const sitios = sitiosDeArriba(pagina);
  for (const url of new Set(urls)) {
    if (!url || !/^https?:/i.test(url)) continue;
    const consultas = [{ url }, ...sitios.map((s) => ({ url, partitionKey: { topLevelSite: s } }))];
    for (const consulta of consultas) {
      try {
        for (const c of await api.cookies.getAll(consulta)) lineas.add(lineaCookie(c));
      } catch (e) {
        // Sin permiso, o el navegador no sabe de cookies partidas
      }
    }
  }
  return [...lineas].join("\n");
}

// Atarraya no baja videos de YouTube (sus condiciones no lo permiten): esas descargas se quedan en el navegador
function sitioNoPermitido(url) {
  return esYouTube(url);
}

function nombreDe(ruta) {
  if (!ruta) return "";
  const partes = ruta.split(/[\\/]/);
  return partes[partes.length - 1] || "";
}

// La página donde nació la descarga: la de la pestaña de su pedido (o la de la pestaña que la abrió), nunca la
// pestaña activa. Atarraya la usa para saber de qué página salió (por ejemplo, en «de una en una»). Como todo lo
// demás, solo va al programa de esta PC. Vacía si no se sabe.
async function paginaDe(item) {
  const tabId = pestanas.buscar([item.url, item.finalUrl]);
  if (tabId < 0) return "";
  try {
    const pestana = await api.tabs.get(tabId);
    const abridora = pestana && pestana.openerTabId !== undefined ? await api.tabs.get(pestana.openerTabId).catch(() => null) : null;
    return paginaDeOrigen(item, pestana, abridora);
  } catch (e) {
    return ""; // la pestaña ya se cerró
  }
}

async function enviar(datos) {
  const r = await hablar(Object.assign({ tipo: "descargar", agente: navigator.userAgent }, datos));
  return !!(r && r.ok);
}

// Un video de una pestaña, cuando la persona pulsó el botón: a Atarraya, con las cookies y el «referer» del marco
// donde está el video
async function enviarVideo(tabId, entrada) {
  const pestana = await api.tabs.get(tabId).catch(() => null);
  const pagina = pestana && /^https?:/i.test(pestana.url || "") ? pestana.url : "";
  const marco = medios.urlDeMarco(tabId, entrada.frameId, pagina);
  const motivo = motivoBloqueo([pagina, marco]) || motivoDeUrl(entrada.url) || medios.marcoBloqueado(tabId, entrada.frameId);
  if (motivo === "youtube") return { ok: false, texto: TEXTO_YOUTUBE };
  if (motivo) return { ok: false, texto: TEXTO_SITIO_PROTEGIDO };
  if (entrada.protegido) return { ok: false, texto: TEXTO_PROTEGIDO };
  if (!(await programaListo()))
    return { ok: false, texto: "No encuentro Atarraya. Ábrelo y pulsa «Navegadores» → «Conectar»." };
  const cookies = await cookiesDeMarco([entrada.url, marco, ...medios.muestrasDeMarco(tabId, entrada.frameId)], pagina);
  const pedido = pedidoDeVideo(entrada, pagina, pestana ? pestana.title : "", marco, cookies, navigator.userAgent);
  try {
    const r = await hablar(pedido);
    if (r && r.ok) return { ok: true, texto: "Enviado a Atarraya" };
    return { ok: false, texto: "Atarraya no lo aceptó." };
  } catch (e) {
    return { ok: false, texto: "No se pudo hablar con Atarraya." };
  }
}

// El botón «Descargar este video» de un marco: el archivo del video o, si se reproduce desde un «blob:», la
// transmisión HLS o DASH que pidió ese marco
async function bajarVideoDeMarco(tabId, frameId, mensaje, urlDelMarco) {
  await listo;
  if (urlDelMarco) medios.marco(tabId, frameId, urlDelMarco);
  const motivo = motivoBloqueo([urlDelMarco]) || motivoDeUrl(mensaje.src) || medios.marcoBloqueado(tabId, frameId);
  if (motivo === "youtube") return { ok: false, texto: TEXTO_YOUTUBE };
  if (motivo) return { ok: false, texto: TEXTO_SITIO_PROTEGIDO };
  if (mensaje.protegido) return { ok: false, texto: TEXTO_PROTEGIDO };
  let entrada = null;
  if (mensaje.src && /^https?:/i.test(mensaje.src)) {
    entrada = medios.buscar(tabId, mensaje.src);
    if (!entrada) {
      const c = clasificar(mensaje.src, "");
      entrada = medios.anotar(tabId, {
        url: mensaje.src,
        formato: c && c.formato ? c.formato : "archivo",   // un <video> que no dice su tipo igual es un video
        clase: "video",
        frameId,
        alto: mensaje.alto,
        nombre: nombreDeMedio(mensaje.src, ""),
      });
    }
  } else {
    entrada = medios.transmisionDe(tabId, frameId);
  }
  guardarPronto();
  if (!entrada) {
    return {
      ok: false,
      texto: "No encontré el archivo de este video. Abre la ventanita de Atarraya (el ícono de la extensión) para ver los videos de esta pestaña.",
    };
  }
  return enviarVideo(tabId, entrada);
}

api.downloads.onCreated.addListener(async (item) => {
  try {
    const url = item.finalUrl || item.url;
    if (devueltas.has(url)) {
      devueltas.delete(url);
      return;
    }
    const a = await ajustes();
    if (!a.capturar) return;
    if (!/^(https?|ftp):/i.test(url)) return;            // blob:, data: y file: se quedan en el navegador
    if (sitioNoPermitido(url)) return;
    if (item.state && item.state !== "in_progress") return;
    if (item.byExtensionId) return;                        // la pidió una extensión (o nosotros mismos)
    if (noRepetibles.has(item.url) || noRepetibles.has(url)) return;   // salió de un formulario
    const tamano = item.totalBytes > 0 ? item.totalBytes : (item.fileSize > 0 ? item.fileSize : -1);
    // De cualquier sitio, como IDM. Si la persona puso un mínimo, lo más pequeño sigue en el navegador
    if (tamano >= 0 && tamano < a.minimoMB * 1024 * 1024) return;
    if (!(await programaListo())) return;

    // Todo lo que hace falta se junta ANTES de cancelar en el navegador
    const datos = {
      url,
      referer: item.referrer || "",
      pagina: await paginaDe(item),
      nombre: nombreDe(item.filename),
      tamano,
      tipoMime: item.mime || "",
      cookies: await cookiesPara(item.url, url),
    };
    await api.downloads.cancel(item.id).catch(() => {});
    await api.downloads.erase({ id: item.id }).catch(() => {});
    let ok = false;
    try {
      ok = await enviar(datos);
    } catch (e) {
      ok = false;
    }
    if (!ok) {
      // Si el programa no la aceptó, que la baje el navegador como siempre
      devueltas.add(url);
      try {
        await api.downloads.download({ url });
      } catch (e) {
        devueltas.delete(url);
        console.warn("Atarraya: no se pudo devolver la descarga al navegador", e);
      }
    }
  } catch (e) {
    console.warn("Atarraya:", e);
  }
});

async function crearMenus() {
  try {
    await api.contextMenus.removeAll();
  } catch (e) {}
  api.contextMenus.create({ id: "atarraya-enlace", title: "Descargar con Atarraya", contexts: ["link"] });
  api.contextMenus.create({ id: "atarraya-medio", title: "Descargar con Atarraya", contexts: ["image", "video", "audio"] });
}

api.runtime.onInstalled.addListener(crearMenus);
if (api.runtime.onStartup) api.runtime.onStartup.addListener(crearMenus);

api.contextMenus.onClicked.addListener(async (info, tab) => {
  const url = info.linkUrl || info.srcUrl;
  if (!url || !/^(https?:|ftp:|magnet:\?)/i.test(url)) return;
  if (sitioNoPermitido(url)) return;
  let ok = false;
  try {
    // Un enlace magnet no lleva cookies ni página de origen: es para torrents
    const magnet = /^magnet:/i.test(url);
    const pagina = info.pageUrl || (tab && tab.url) || "";
    ok = await enviar(magnet ? { url } : { url, referer: pagina, pagina, cookies: await cookiesPara(url) });
  } catch (e) {}
  if (!ok) {
    api.action.setBadgeText({ text: "!" });
    api.action.setBadgeBackgroundColor({ color: "#D64545" });
    setTimeout(() => api.action.setBadgeText({ text: "" }), 5000);
  }
});

// Mensajes de la ventanita (estado, videos de una pestaña, «Bajar») y de las páginas (sus videos y el botón
// «Descargar este video»)
api.runtime.onMessage.addListener((mensaje, remitente, responder) => {
  if (!mensaje || typeof mensaje.tipo !== "string") return false;
  // Las páginas de la extensión (la ventanita, aunque se abra en una pestaña) frente a videos.js en una página web
  const deLaExtension = !!(remitente && typeof remitente.url === "string" && remitente.url.startsWith(api.runtime.getURL("")));
  const deUnaPagina = !deLaExtension && !!(remitente && remitente.tab && typeof remitente.tab.id === "number");
  switch (mensaje.tipo) {
    case "estado":
      listoHasta = 0;
      hablar({ tipo: "ping" })
        .then((r) => responder({ ok: !!(r && r.ok), version: r && r.version }))
        .catch(() => responder({ ok: false }));
      return true;

    case "videosDeMarco": {
      // Lo que manda videos.js: los videos de su marco (sin «blob:») y si alguno está protegido
      if (!deUnaPagina) return false;
      const tabId = remitente.tab.id;
      const frameId = typeof remitente.frameId === "number" ? remitente.frameId : 0;
      listo.then(() => {
        const marco = mensaje.marco || remitente.url || "";
        if (cadenaBloqueada([marco, remitente.url])) return;
        medios.marco(tabId, frameId, marco);
        for (const v of Array.isArray(mensaje.videos) ? mensaje.videos.slice(0, 50) : []) {
          const src = typeof v.src === "string" ? v.src : "";
          if (/^https?:/i.test(src) && !motivoDeUrl(src)) {
            const c = clasificar(src, "");
            if (!(c && c.trozo)) {
              medios.anotar(tabId, {
                url: src,
                formato: c && c.formato ? c.formato : "archivo",
                clase: "video",
                frameId,
                alto: Number(v.alto) || 0,
                protegido: !!v.protegido,
                nombre: nombreDeMedio(src, ""),
              });
            }
          }
          if (v.protegido) medios.protegerMarco(tabId, frameId, src);
        }
        guardarPronto();
      });
      return false;
    }

    case "revisarVideo": {
      // videos.js antes de mostrar el botón: ¿este video viene de YouTube o de un sitio con DRM (aunque sea por una
      // redirección), o su marco reproduce algo de ellos? Entonces no lleva botón.
      if (!deUnaPagina) return false;
      const frameId = typeof remitente.frameId === "number" ? remitente.frameId : 0;
      const src = typeof mensaje.src === "string" ? mensaje.src : "";
      listo
        .then(() => ({ bloqueado: !!(motivoDeUrl(src) || medios.marcoBloqueado(remitente.tab.id, frameId)) }))
        .then(responder)
        .catch(() => responder({ bloqueado: false }));
      return true;
    }

    case "bajarVideo": {
      // El botón «Descargar este video» de una página (solo si la persona lo pulsó: lo revisa videos.js)
      if (!deUnaPagina) return false;
      const frameId = typeof remitente.frameId === "number" ? remitente.frameId : 0;
      bajarVideoDeMarco(remitente.tab.id, frameId, mensaje, mensaje.marco || remitente.url || "")
        .then(responder)
        .catch(() => responder({ ok: false, texto: "No se pudo hablar con Atarraya." }));
      return true;
    }

    case "mediosDePestana": {
      // La ventanita: la lista de videos de una pestaña (de todos sus marcos)
      if (!deLaExtension) return false;
      const tabId = Number(mensaje.tabId);
      listo
        .then(async () => {
          const pestana = await api.tabs.get(tabId).catch(() => null);
          const url = pestana ? pestana.url || "" : "";
          if (esYouTube(url)) return { estado: "youtube", texto: TEXTO_YOUTUBE, lista: [] };
          if (esSitioProtegido(url)) return { estado: "protegido", texto: TEXTO_SITIO_PROTEGIDO, lista: [] };
          const lista = medios.lista(tabId);
          // Sin nada que bajar porque lo que hay es de YouTube (o de un sitio con DRM), por ejemplo en un marco: se dice
          const motivo = lista.length ? "" : medios.motivoDePestana(tabId);
          if (motivo) return { estado: motivo, texto: motivo === "youtube" ? TEXTO_YOUTUBE : TEXTO_SITIO_PROTEGIDO, lista: [] };
          return { estado: "bien", lista, titulo: pestana ? pestana.title || "" : "", pagina: url };
        })
        .then(responder)
        .catch(() => responder({ estado: "bien", lista: [] }));
      return true;
    }

    case "bajarMedio": {
      // «Bajar» en la ventanita
      if (!deLaExtension) return false;
      const tabId = Number(mensaje.tabId);
      listo
        .then(() => {
          const entrada = medios.buscar(tabId, mensaje.url);
          if (!entrada) return { ok: false, texto: "Ese video ya no está en la lista. Vuelve a abrir esta ventanita." };
          return enviarVideo(tabId, entrada);
        })
        .then(responder)
        .catch(() => responder({ ok: false, texto: "No se pudo hablar con Atarraya." }));
      return true;
    }
  }
  return false;
});
