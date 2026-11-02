// Atarraya para el navegador: pasa las descargas de cualquier sitio al programa Atarraya de Windows, y también los
// videos de las páginas cuando la persona pulsa «Descargar este video» o «Bajar» en la ventanita.
// No guarda nada fuera del navegador, no manda datos a ningún servidor y solo habla con el programa instalado en la
// misma PC (native messaging).

// En Chrome la extensión corre como «service worker» y carga origen.js, medios.js y cola-navegador.js así; Firefox los
// carga desde su manifiesto
if (typeof importScripts === "function") importScripts("origen.js", "medios.js", "cola-navegador.js");

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

// La cola de «de una en una» de Atarraya (ver cola-navegador.js): las pestañas que la extensión abrió para sus partes
// (cuando Atarraya se lo pidió por el puerto) y las descargas que baja el navegador y la extensión sigue hasta el final
// (cuando el sitio no deja que otro programa las baje). Las pestañas se guardan en storage.session (se borra al cerrar
// el navegador); las descargas que se siguen, en storage.local hasta que Atarraya confirma su aviso final (el navegador
// recuerda sus descargas aunque se cierre). Este «service worker» se apaga cuando no hace nada.
const cola = new PestanasDeLaCola();
// id de la descarga del navegador -> { toma, pagina, url, referer, ultimoAviso, ultimoBytes, ultimoEstado, desde, final,
// sinConfirmar, probando } («final»: el aviso final, guardado hasta que Atarraya lo confirma; «probando»: todavía se
// decide con Atarraya quién la baja, ver decidirToma)
const seguidas = new Map();
const entregando = new Set();   // avisos finales que se están mandando ahora
const decidiendo = new Set();   // descargas cuya toma se está decidiendo ahora
// Esta extensión en este perfil del navegador (ver nuevaInstancia en cola-navegador.js): va al principio de cada «toma»
let instancia = "";
const listoCola = (async () => {
  try {
    if (api.storage.session) cola.cargar((await api.storage.session.get("cola")).cola);
  } catch (e) {
    // Sin storage.session se recuerda solo mientras el «service worker» está despierto
  }
  try {
    const g = await api.storage.local.get(["seguidas", "instancia"]);
    for (const [id, s] of seguidasGuardadas(g && g.seguidas, Date.now())) seguidas.set(id, s);
    if (instanciaValida(g && g.instancia)) instancia = g.instancia;
  } catch (e) {}
  if (!instancia) {
    instancia = nuevaInstancia((b) => crypto.getRandomValues(b));
    api.storage.local.set({ instancia }).catch(() => {});
  }
  if (seguidas.size) vigilarSeguidas();
})();
// Cada vez que despierta la extensión: los avisos finales que Atarraya no confirmó (por ejemplo, estaba cerrado) van
// otra vez, y lo que se estaba decidiendo cuando se apagó sigue decidiéndose
listoCola.then(() => {
  for (const [id, s] of [...seguidas]) {
    if (s.probando) decidirToma(id);
    else entregarFinal(id);
  }
});
function guardarCola() {
  if (api.storage.session) api.storage.session.set({ cola: cola.exportar() }).catch(() => {});
  api.storage.local.set({ seguidas: [...seguidas] }).catch(() => {});
}

// --- El puerto: la conexión que la extensión deja abierta mientras Atarraya está abierto ------------------------------
// Por ahí Atarraya le pide que abra la página de cada parte de la cola. La extensión la abre ella misma en una pestaña
// nueva y anota esa pestaña: solo en esas lee los avisos de espera. Si Atarraya está cerrado, el puente contesta
// «adios» y se cierra; se vuelve a probar a lo sumo cada 30 segundos, cuando la extensión está despierta.
let puerto = null;
let intentoPuerto = 0;
const abriendo = new Set();   // pestañas que se están abriendo para Atarraya (se espera a saber su número)

function conectarPuerto() {
  if (puerto || Date.now() - intentoPuerto < 30000) return;
  intentoPuerto = Date.now();
  let p = null;
  try {
    p = api.runtime.connectNative(HOST);
  } catch (e) {
    return;   // sin el puente (Atarraya no está conectado con este navegador)
  }
  puerto = p;
  p.onMessage.addListener((m) => {
    if (!m || typeof m.tipo !== "string") return;
    // Atarraya está abierto: los avisos finales guardados van ya, y se le cuenta lo que se sabe de las descargas
    if (m.tipo === "listo") alVolverAtarraya(true);
    else if (m.tipo === "abrir") abrirParaAtarraya(p, m);
  });
  p.onDisconnect.addListener(() => {
    void api.runtime.lastError;   // Atarraya se cerró (o no estaba abierto)
    if (puerto === p) puerto = null;
  });
  try {
    p.postMessage({ tipo: "hola" });
  } catch (e) {
    if (puerto === p) puerto = null;
  }
}

// Atarraya pidió abrir la página de una parte: en una pestaña nueva que abre la extensión, que es la única donde se
// leen sus avisos de espera. Si no se puede, Atarraya la abre como antes.
function abrirParaAtarraya(p, orden) {
  const url = paginaPorAbrir(orden);
  if (!url) return;
  const tarea = (async () => {
    let tab = null;
    try {
      tab = await api.tabs.create({ url });
    } catch (e) {
      return;
    }
    if (!tab || typeof tab.id !== "number") return;
    await listoCola;
    cola.marcar(tab.id, url, Date.now());
    guardarCola();
    // Como cuando Atarraya la abría desde Windows: el navegador pasa al frente con la página
    try {
      if (typeof tab.windowId === "number" && api.windows) await api.windows.update(tab.windowId, { focused: true });
    } catch (e) {}
    try {
      p.postMessage({ tipo: "abierta", id: orden.id, pestana: tab.id });
    } catch (e) {}
  })();
  abriendo.add(tarea);
  tarea.finally(() => abriendo.delete(tarea));
}

// Se prueba al despertar, cada 30 segundos mientras está despierta y cuando la persona navega o baja algo
conectarPuerto();
setInterval(conectarPuerto, 30000);

// Las descargas que salen de un formulario (POST) no se pueden repetir desde el programa: esas se quedan
// en el navegador (salvo las de la cola, que la extensión sigue). Aquí se anotan las direcciones pedidas con otro
// método que no sea GET.
const noRepetibles = new Map();   // url -> cuándo
api.webRequest.onBeforeRequest.addListener(
  (d) => {
    pestanas.anotar(d.url, d.tabId);
    if (d.type === "main_frame") conectarPuerto();   // la persona navega: si Atarraya se abrió, se conecta
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
    listoCola.then(() => {
      cola.olvidar(tabId);
      guardarCola();
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
      if (r.abierto) alVolverAtarraya();
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
    // Un video «blob:»: solo la transmisión que pidió su propio marco
    entrada = medios.transmisionDe(tabId, frameId);
  }
  guardarPronto();
  if (!entrada) return { ok: false, texto: TEXTO_SIN_TRANSMISION };
  return enviarVideo(tabId, entrada);
}

// --- Descargas de la cola de «de una en una» -------------------------------------------------------------------------
// La descarga de una parte no se cancela en el navegador hasta que Atarraya dice que la pudo seguir: si el sitio no lo
// deja (una página en vez del archivo, un 403 o 410, un enlace de un solo uso), el navegador la termina y la extensión
// la sigue hasta el final. Así nunca se pierde el captcha que resolvió la persona.

const esperar = (ms) => new Promise((r) => setTimeout(r, ms));

function seguirEnNavegador(item, datos, toma, sinConfirmar = false) {
  seguidas.set(item.id, {
    toma,
    pagina: datos.paginaCola || datos.pagina || "",
    url: datos.url,
    referer: datos.referer || "",
    ultimoAviso: 0,
    ultimoBytes: -1,
    ultimoEstado: "",
    desde: Date.now(),
    sinConfirmar: sinConfirmar ? { desde: Date.now(), intento: 0, avisado: false } : undefined,
  });
  guardarCola();
  revisarSeguida(item.id);
  vigilarSeguidas();
}

// No se sabe si la del navegador sigue: se anota (storage.local, sin plazo) y se le avisa a Atarraya, que pausa la suya
function anotarSinConfirmar(item, datos, toma) {
  seguirEnNavegador(item, datos, toma, true);
}

// Se le avisa a Atarraya que la parte quedó «sin confirmar» (a lo sumo cada 30 segundos, hasta que lo anota)
async function avisarSinConfirmar(id) {
  const s = seguidas.get(id);
  if (!s || !s.sinConfirmar || s.sinConfirmar.avisado || entregando.has(id) || !tocaFinal(s.sinConfirmar, Date.now(), false)) return;
  entregando.add(id);
  try {
    s.sinConfirmar.intento = Date.now();
    guardarCola();
    let r = null;
    try {
      const e = await hablar({ tipo: "sinConfirmar", toma: s.toma });
      if (e && e.ok) {
        await esperar(1500);
        r = await hablar({ tipo: "toma", toma: s.toma });
      }
    } catch (x) {}
    // Atarraya ya la anotó (o la parte ya no espera nada: no hay nada que pausar)
    if (r && r.ok && (r.sinConfirmar === true || r.espera === false) && s.sinConfirmar) {
      s.sinConfirmar.avisado = true;
      guardarCola();
    }
  } finally {
    entregando.delete(id);
  }
}

async function revisarSeguida(id) {
  await listoCola;
  const s = seguidas.get(id);
  if (!s) return;
  if (s.probando) return;   // todavía se decide quién la baja (ver decidirToma)
  if (s.final) {
    entregarFinal(id);
    return;
  }
  let item = null, consulto = true;
  try {
    [item] = await api.downloads.search({ id });
  } catch (e) {
    consulto = false;
  }
  if (s.sinConfirmar) {
    const d = resolverSinConfirmar(item, consulto);
    if (d === "esperar") {
      avisarSinConfirmar(id);   // todavía no se puede saber: Atarraya mantiene la suya en pausa
      return;
    }
    if (d === "cancelada") {
      // La del navegador ya no baja: sigue la de Atarraya (se le avisa como un aviso final, hasta que lo confirma)
      if (item) {
        try {
          await api.downloads.erase({ id });
        } catch (e) {}
      }
      anotarFinal(id, s, { tipo: "confirmada", toma: s.toma, estado: "cancelada" });
      return;
    }
    // La sigue bajando el navegador (o ya la terminó): se sigue como las demás y Atarraya quita la suya con el primer aviso
    delete s.sinConfirmar;
    s.ultimoEstado = "";
    guardarCola();
  }
  if (!consulto) return;   // no se pudo mirar ahora: otra vez en un rato
  if (!item) {
    // La persona la borró de la lista del navegador antes de que terminara: para Atarraya, no terminó
    anotarFinal(id, s, { tipo: "navegador", toma: s.toma, estado: "cancelada", url: s.url, referer: s.referer, paginaCola: s.pagina,
                         nombre: "", bajado: Math.max(0, Number(s.ultimoBytes) || 0), total: -1, ruta: "" });
    return;
  }
  const aviso = avisoDeSeguimiento(item, s, Date.now());
  if (!aviso) return;
  s.ultimoAviso = Date.now();
  s.ultimoBytes = aviso.bajado;
  s.ultimoEstado = aviso.estado;
  const url = item.finalUrl || item.url || s.url;
  const datos = {
    tipo: "navegador",
    toma: s.toma,
    estado: aviso.estado,
    url: /^(https?|ftp):/i.test(url) ? url : s.url,
    referer: s.referer,
    paginaCola: s.pagina,
    nombre: nombreDe(item.filename),
    bajado: aviso.bajado,
    total: aviso.total,
    ruta: aviso.ruta || "",
  };
  if (aviso.terminar) {
    anotarFinal(id, s, datos);
    return;
  }
  guardarCola();
  try {
    await hablar(datos);
  } catch (e) {
    // Atarraya está cerrado: lo que importa es el aviso final, que se guarda
  }
}

// El aviso final (terminó, falló o se canceló) se guarda con la descarga en storage.local, y la descarga se sigue
// recordando, hasta que Atarraya confirma que ya no espera nada de ella (ver tocaFinal y finalEntregado)
function anotarFinal(id, s, datos) {
  s.final = Object.assign({}, datos, { desde: Date.now(), intento: 0 });
  guardarCola();
  vigilarSeguidas();
  entregarFinal(id, true);
}

async function entregarFinal(id, ya = false) {
  const s = seguidas.get(id);
  if (!s || !s.final || entregando.has(id) || !tocaFinal(s.final, Date.now(), ya)) return;
  entregando.add(id);
  try {
    s.final.intento = Date.now();
    guardarCola();
    const aviso = Object.assign({}, s.final);
    delete aviso.desde;
    delete aviso.intento;
    let r = null;
    try {
      const e = await hablar(aviso);
      // Atarraya lo recibió; lo atiende enseguida. Después se le pregunta si ya no espera nada de esta descarga.
      if (e && e.ok) {
        await esperar(1500);
        r = await hablar({ tipo: "toma", toma: s.toma });
      }
    } catch (x) {
      // Atarraya está cerrado (o no está el puente): queda guardado
    }
    if (finalEntregado(r, s.final) === "listo" && seguidas.get(id) === s) {
      seguidas.delete(id);
      guardarCola();
    }
  } finally {
    entregando.delete(id);
  }
}

// Atarraya volvió a contestar: los avisos finales guardados (y las partes «sin confirmar» que todavía no sabe) van
// enseguida, y se le cuenta lo que se sabe de las descargas de la cola. «conecto»: se acaba de abrir el puerto (Atarraya
// se abrió o volvió); si no, el informe va a lo sumo cada 30 segundos.
function alVolverAtarraya(conecto = false) {
  listoCola.then(() => {
    for (const [id, s] of [...seguidas]) {
      if (s.probando) decidirToma(id);
      else if (s.final) entregarFinal(id, true);
      else if (s.sinConfirmar && !s.sinConfirmar.avisado) {
        s.sinConfirmar.intento = 0;
        avisarSinConfirmar(id);
      }
    }
    enviarInforme(conecto);
  });
}

// Lo que sabe la extensión de las descargas del navegador de la cola, para que Atarraya lo junte con su cola y su lista
// (ver informeDeSeguidas en cola-navegador.js): nunca quedan dos descargas de la misma parte ni una parte sin dueño
let ultimoInforme = 0;
function enviarInforme(ya) {
  if (!instancia || (!ya && Date.now() - ultimoInforme < 30000)) return;
  ultimoInforme = Date.now();
  hablar(informeDeSeguidas(instancia, [...seguidas]))
    .then((r) => {
      if (!r || !r.ok) ultimoInforme = 0;   // Atarraya no estaba: va la próxima vez que conteste
    })
    .catch(() => {
      ultimoInforme = 0;
    });
}

// Mientras se sigue alguna, se mira cada 3 segundos (el aviso de cómo va sale cada 5)
let relojSeguidas = null;
function vigilarSeguidas() {
  if (relojSeguidas) return;
  relojSeguidas = setInterval(() => {
    if (!seguidas.size) {
      clearInterval(relojSeguidas);
      relojSeguidas = null;
      return;
    }
    for (const id of seguidas.keys()) revisarSeguida(id);
  }, 3000);
}
api.downloads.onChanged.addListener((cambio) => {
  listoCola.then(() => {
    if (seguidas.has(cambio.id)) revisarSeguida(cambio.id);
  });
});

// Una descarga de una parte de la cola (Atarraya dijo que es suya, sin duda)
async function descargaDeLaCola(item, datos, r) {
  await listoCola;
  const toma = tomaNueva(instancia, item.id, Date.now());
  // El sitio no deja que otro programa siga sus descargas (Atarraya lo recuerda), o salió de un formulario y no hubo
  // redirección (esa dirección no se puede pedir otra vez): el navegador la baja y la extensión la sigue
  const sinRedireccion = !item.finalUrl || item.finalUrl === item.url;
  if (r.navegador || (datos.porFormulario && sinRedireccion)) {
    seguirEnNavegador(item, datos, toma);
    return;
  }
  // Se anota antes de pasársela a Atarraya (en storage.local): si este «service worker» se apaga o el navegador se cierra
  // mientras se decide quién la baja, al despertar sigue decidiendo (ver decidirToma) y nunca queda sin dueño
  seguidas.set(item.id, {
    toma,
    pagina: datos.paginaCola || datos.pagina || "",
    url: datos.url,
    referer: datos.referer || "",
    ultimoAviso: 0,
    ultimoBytes: -1,
    ultimoEstado: "",
    desde: Date.now(),
    probando: { desde: Date.now() },
  });
  guardarCola();
  // Atarraya prueba seguirla con una conexión, con las cookies y la página de origen; la del navegador sigue mientras
  let ok = false;
  try {
    const pedido = Object.assign({}, datos, { cookies: await cookiesPara(item.url, datos.url), toma });
    delete pedido.porFormulario;
    ok = await enviar(pedido);
  } catch (e) {}
  if (!ok) {
    seguirEnNavegador(item, datos, toma);
    return;
  }
  await decidirToma(item.id);
}

// Decide con Atarraya quién baja una parte (ver TomaDelNavegador en cola-navegador.js): nunca quedan las dos descargas.
// Lo que se está decidiendo queda anotado («probando», con cuándo empezó): si el «service worker» se apaga, al despertar
// sigue desde donde iba.
async function decidirToma(id) {
  await listoCola;
  const s0 = seguidas.get(id);
  if (!s0 || !s0.probando || decidiendo.has(id)) return;
  decidiendo.add(id);
  const toma = s0.toma;
  const datosDe = (s) => ({ paginaCola: s.pagina, url: s.url, referer: s.referer });
  try {
    const decision = new TomaDelNavegador(Number(s0.probando.desde) || Date.now());
    // Atarraya tiene la suya en pausa hasta saber (por ejemplo, se cerró en el medio y al abrirse la retuvo)
    let retenida = false;
    for (;;) {
      await esperar(1000);
      const s = seguidas.get(id);
      if (!s || s.toma !== toma || !s.probando) return;   // ya se resolvió por otro lado
      let e = null;
      try {
        e = await hablar({ tipo: "toma", toma });
      } catch (x) {}
      if (e && e.ok) retenida = e.sinConfirmar === true;
      const accion = decision.paso(e && e.ok ? e.estado : "", Date.now());
      if (accion === "esperar") continue;
      if (accion === "soltar") {
        // Se acabó el plazo: que Atarraya suelte la suya (si todavía no la tomó); su respuesta dice quién la baja
        try {
          await hablar({ tipo: "soltar", toma });
        } catch (x) {}
        continue;
      }
      if (accion === "cancelar") {
        // Atarraya ya recibe el archivo: la del navegador sobra. Si no se pudo cancelar (por ejemplo, el navegador ya la
        // había terminado), sigue la del navegador y Atarraya quita la suya cuando la extensión le avisa
        let cancelo = true, consulto = true;
        try {
          await api.downloads.cancel(id);
        } catch (x) {
          cancelo = false;
        }
        let ahora = null;
        try {
          [ahora] = await api.downloads.search({ id });
        } catch (x) {
          consulto = false;
        }
        const r = resultadoCancelar(ahora, cancelo, consulto);
        if (r === "cancelada") {
          try {
            await api.downloads.erase({ id });
          } catch (x) {
            // Ya no baja; solo queda en la lista del navegador
          }
          if (retenida) {
            // Atarraya espera saberlo para seguir con la suya: se le avisa como un aviso final, hasta que lo confirma
            delete s.probando;
            anotarFinal(id, s, { tipo: "confirmada", toma, estado: "cancelada" });
          } else {
            seguidas.delete(id);
            guardarCola();
          }
          return;
        }
        if (r === "sinConfirmar") {
          // No se pudo cancelar ni saber cómo quedó: pueden estar bajando las dos. Atarraya pausa la suya y la parte no
          // sigue hasta saberlo; la extensión lo sigue mirando (ver revisarSeguida)
          console.warn("Atarraya: no se pudo cancelar ni revisar la descarga del navegador; queda sin confirmar");
          anotarSinConfirmar({ id }, datosDe(s), toma);
          return;
        }
        console.warn("Atarraya: no se pudo cancelar la descarga del navegador (" + r + (cancelo ? "" : ", con error") + "); sigue esa");
      }
      break;
    }
    // La baja el navegador (Atarraya la soltó, no pudo, o no contesta): la extensión la sigue hasta el final
    const s = seguidas.get(id);
    if (s && s.toma === toma && s.probando) seguirEnNavegador({ id }, datosDe(s), toma);
  } finally {
    decidiendo.delete(id);
  }
}

api.downloads.onCreated.addListener(async (item) => {
  conectarPuerto();
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
    const porFormulario = noRepetibles.has(item.url) || noRepetibles.has(url);
    const tamano = item.totalBytes > 0 ? item.totalBytes : (item.fileSize > 0 ? item.fileSize : -1);

    // ¿Es de una parte de la cola? (de una pestaña que abrió Atarraya, o salió de un formulario)
    await listoCola;
    const pestana = pestanas.buscar([item.url, item.finalUrl]);
    const paginaCola = pestana >= 0 ? cola.parte(pestana) : "";
    if (paginaCola || porFormulario) {
      if (await programaListo()) {
        const datos = { url, referer: item.referrer || "", pagina: await paginaDe(item), paginaCola, nombre: nombreDe(item.filename),
                        tamano, tipoMime: item.mime || "", porFormulario };
        let r = null;
        try {
          r = await hablar({ tipo: "emparejar", url, referer: datos.referer, pagina: datos.pagina, paginaCola, nombre: datos.nombre });
        } catch (e) {}
        if (r && r.ok && r.parte >= 0 && r.segura) {
          await descargaDeLaCola(item, datos, r);
          return;
        }
      }
    }
    if (porFormulario) return;   // salió de un formulario y no es de la cola: se queda en el navegador, como siempre
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
      intentoPuerto = 0;   // la persona abrió la ventanita: se prueba conectar ya
      conectarPuerto();
      hablar({ tipo: "ping" })
        .then((r) => responder({ ok: !!(r && r.ok), version: r && r.version }))
        .catch(() => responder({ ok: false }));
      return true;

    case "soyDeLaCola": {
      // cola.js: ¿esta pestaña es de la cola de Atarraya? Solo si la abrió la misma extensión cuando Atarraya se lo
      // pidió (ver abrirParaAtarraya); una pestaña que abrió la persona nunca lo es. Si una se está abriendo justo
      // ahora, se espera a saber su número.
      if (!deUnaPagina || remitente.frameId !== 0) return false;
      const tabId = remitente.tab.id;
      const url = typeof remitente.url === "string" ? remitente.url : "";
      Promise.all([listoCola, ...abriendo])
        .then(() => {
          const si = cola.paginaVista(tabId, url, Date.now());
          if (cola.parte(tabId)) guardarCola();
          return { cola: si };
        })
        .then(responder)
        .catch(() => responder({ cola: false }));
      return true;
    }

    case "esperaVista": {
      // cola.js leyó un aviso de espera en una página de la cola: a Atarraya, con la página que abrió para esa parte
      if (!deUnaPagina || remitente.frameId !== 0) return false;
      const tabId = remitente.tab.id;
      listoCola.then(() => {
        const paginaCola = cola.parte(tabId);
        if (!paginaCola || !cola.leeAvisos(tabId, remitente.url, Date.now())) return;
        const segundos = Number.isFinite(mensaje.segundos) ? Math.max(-1, Math.round(mensaje.segundos)) : -1;
        const url = typeof remitente.url === "string" && /^https?:/i.test(remitente.url) ? remitente.url : "";
        hablar({ tipo: "espera", paginaCola, url, clase: mensaje.clase === "limite" ? "limite" : "pagina", segundos }).catch(() => {});
      });
      return false;
    }

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
