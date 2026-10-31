// Atarraya para el navegador: pasa las descargas de cualquier sitio al programa Atarraya de Windows.
// No guarda nada fuera del navegador, no manda datos a ningún servidor y solo habla con el programa
// instalado en la misma PC (native messaging).

const api = globalThis.browser ?? globalThis.chrome;
const HOST = "com.briznasoft.atarraya";
const AJUSTES = { capturar: true, minimoMB: 0 };

let listoHasta = 0;            // hasta cuándo damos por hecho que el programa responde
const devueltas = new Set();   // descargas que devolvimos al navegador: no se capturan otra vez

// Las descargas que salen de un formulario (POST) no se pueden repetir desde el programa: esas se quedan
// en el navegador. Aquí se anotan las direcciones pedidas con otro método que no sea GET.
const noRepetibles = new Map();   // url -> cuándo
api.webRequest.onBeforeRequest.addListener(
  (d) => {
    if (!d.method || d.method === "GET") return;
    noRepetibles.set(d.url, Date.now());
    if (noRepetibles.size > 300) {
      const viejo = Date.now() - 10 * 60000;
      for (const [u, t] of noRepetibles) if (t < viejo) noRepetibles.delete(u);
    }
  },
  { urls: ["<all_urls>"], types: ["main_frame", "sub_frame", "other"] }
);

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

// Cookies del sitio en formato Netscape, para que el programa pueda bajar lo que pide sesión iniciada
async function cookiesPara(...urls) {
  const lineas = new Set();
  for (const url of urls) {
    if (!url || !/^https?:/i.test(url)) continue;
    try {
      for (const c of await api.cookies.getAll({ url })) {
        const dominio = (c.httpOnly ? "#HttpOnly_" : "") + c.domain;
        const subdominios = c.hostOnly ? "FALSE" : "TRUE";
        const vence = c.expirationDate ? Math.floor(c.expirationDate) : 0;
        lineas.add([dominio, subdominios, c.path, c.secure ? "TRUE" : "FALSE", vence, c.name, c.value].join("\t"));
      }
    } catch (e) {
      // Sin permiso para ese sitio: se manda sin cookies
    }
  }
  return [...lineas].join("\n");
}

// Atarraya no baja videos de YouTube (sus condiciones no lo permiten): esas descargas se quedan en el navegador
function sitioNoPermitido(url) {
  try {
    const sitio = new URL(url).hostname.toLowerCase();
    return ["youtube.com", "youtu.be", "youtube-nocookie.com", "googlevideo.com", "youtubekids.com"].some(
      (d) => sitio === d || sitio.endsWith("." + d)
    );
  } catch (e) {
    return false;
  }
}

function nombreDe(ruta) {
  if (!ruta) return "";
  const partes = ruta.split(/[\\/]/);
  return partes[partes.length - 1] || "";
}

// La pestaña donde la persona pulsó descargar. Atarraya la usa para saber de qué página salió la descarga (por
// ejemplo, en «de una en una»). Como todo lo demás, solo va al programa de esta PC.
async function paginaActual() {
  try {
    const [tab] = await api.tabs.query({ active: true, lastFocusedWindow: true });
    return tab && /^https?:/i.test(tab.url || "") ? tab.url : "";
  } catch (e) {
    return "";
  }
}

async function enviar(datos) {
  const r = await hablar(Object.assign({ tipo: "descargar", agente: navigator.userAgent }, datos));
  return !!(r && r.ok);
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
      pagina: await paginaActual(),
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

// La ventanita de la extensión pregunta por el estado
api.runtime.onMessage.addListener((mensaje, _remitente, responder) => {
  if (mensaje && mensaje.tipo === "estado") {
    listoHasta = 0;
    hablar({ tipo: "ping" })
      .then((r) => responder({ ok: !!(r && r.ok), version: r && r.version }))
      .catch(() => responder({ ok: false }));
    return true;
  }
  return false;
});
