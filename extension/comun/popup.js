const api = globalThis.browser ?? globalThis.chrome;
const AJUSTES = { capturar: true, minimoMB: 0, botonVideos: true };

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

// La pestaña de la que se muestran los videos: la de esta ventana o, para las pruebas, «popup.html?pestana=<id>»
async function pestanaElegida() {
  const pedida = parseInt(new URLSearchParams(location.search).get("pestana"), 10);
  if (Number.isInteger(pedida) && pedida >= 0) return pedida;
  try {
    const [t] = await api.tabs.query({ active: true, currentWindow: true });
    return t && typeof t.id === "number" ? t.id : -1;
  } catch (e) {
    return -1;
  }
}

function hostDe(url) {
  try {
    return new URL(url).hostname;
  } catch (e) {
    return "";
  }
}

function archivoDe(url) {
  try {
    return decodeURIComponent(new URL(url).pathname.split("/").pop() || "");
  } catch (e) {
    return "";
  }
}

// El nombre y lo que se sabe de cada video, sin HTML: todo va como texto
function detalle(e, hostPestana) {
  const partes = [];
  if (e.formato === "hls") {
    partes.push("HLS" + (archivoDe(e.url) ? " (" + archivoDe(e.url) + ")" : ""));
    partes.push("la calidad se elige en Atarraya");
  } else if (e.formato === "dash") {
    partes.push("DASH" + (archivoDe(e.url) ? " (" + archivoDe(e.url) + ")" : ""));
    partes.push("imagen y sonido en dos archivos: no se juntan");
  } else {
    const tipo = (e.tipoMime || "").split(";")[0].trim().toLowerCase();
    const base = (e.nombre || e.url || "").split("?")[0];
    const punto = base.lastIndexOf(".");
    const ext = punto > base.lastIndexOf("/") ? base.slice(punto + 1).toUpperCase() : "";
    partes.push(tipo.startsWith("audio/") || e.clase === "audio" ? "Audio" : "Video");
    if (ext && ext.length <= 5) partes.push(ext);
  }
  if (e.alto > 0) partes.push(e.alto + "p");
  if (e.formato === "archivo" && e.tamano >= 0) partes.push(textoTamano(e.tamano));   // el de una lista no es el del video
  // Un video que está en un marco de otro sitio (el reproductor de otra página, por ejemplo)
  const hostMarco = hostDe(e.marco);
  if (hostMarco && hostPestana && hostMarco !== hostPestana) partes.push("en un marco de " + hostMarco);
  return partes.join(" · ");
}

// Una transmisión se llama como la página (el nombre de su lista no dice nada); un archivo, como el archivo
function nombreVisible(e, titulo) {
  if ((e.formato === "hls" || e.formato === "dash") && titulo) return titulo;
  if (e.nombre) return e.nombre;
  try {
    const u = new URL(e.url);
    return u.pathname.split("/").pop() || u.hostname;
  } catch (err) {
    return e.url;
  }
}

async function mostrarVideos() {
  const estado = document.getElementById("videosEstado");
  const lista = document.getElementById("videos");
  const resultado = document.getElementById("videosResultado");
  const tabId = await pestanaElegida();
  let r = null;
  try {
    r = tabId >= 0 ? await api.runtime.sendMessage({ tipo: "mediosDePestana", tabId }) : null;
  } catch (e) {
    r = null;
  }
  lista.textContent = "";
  if (!r) {
    estado.textContent = "No se pudo leer esta pestaña.";
    return;
  }
  if (r.estado !== "bien") {
    estado.textContent = r.texto || "";
    return;
  }
  if (!r.lista || !r.lista.length) {
    estado.textContent = "Todavía no hay videos en esta pestaña. Dale play al video y vuelve a abrir esta ventanita.";
    return;
  }
  estado.textContent = r.lista.length === 1 ? "1 video en esta pestaña:" : r.lista.length + " videos en esta pestaña:";
  const titulo = String(r.titulo || "").trim();
  const hostPestana = hostDe(r.pagina || "");
  for (const e of r.lista) {
    const fila = document.createElement("li");
    fila.className = "video";
    const textos = document.createElement("div");
    textos.className = "textos";
    const nombre = document.createElement("div");
    nombre.className = "nombre";
    nombre.textContent = nombreVisible(e, titulo);
    nombre.title = e.url;
    const linea = document.createElement("div");
    linea.className = "detalle";
    linea.textContent = e.protegido ? "Protegido (DRM o cifrado): Atarraya no lo baja" : detalle(e, hostPestana);
    textos.append(nombre, linea);
    const boton = document.createElement("button");
    boton.type = "button";
    boton.textContent = e.protegido ? "Protegido" : "Bajar";
    boton.disabled = !!e.protegido;
    boton.addEventListener("click", async () => {
      boton.disabled = true;
      resultado.hidden = false;
      resultado.className = "resultado";
      resultado.textContent = "Enviando a Atarraya...";
      let respuesta = null;
      try {
        respuesta = await api.runtime.sendMessage({ tipo: "bajarMedio", tabId, url: e.url });
      } catch (err) {
        respuesta = null;
      }
      if (respuesta && respuesta.ok) {
        resultado.textContent = "Enviado a Atarraya: ahí eliges la calidad y la carpeta.";
        resultado.className = "resultado bien";
      } else {
        // Sin respuesta de la extensión (no de Atarraya)
        resultado.textContent = (respuesta && respuesta.texto) || "La extensión de Atarraya no respondió. Vuelve a intentar.";
        resultado.className = "resultado mal";
        boton.disabled = false;
      }
    });
    fila.append(textos, boton);
    lista.append(fila);
  }
}

// Si el navegador siguió corriendo la parte de fondo vieja después de actualizar la extensión (pasa en Chrome con las
// extensiones cargadas desde una carpeta), la extensión se recarga sola, una vez. Si ya se recargó hace poco y sigue
// igual, se dice qué hacer y queda un botón para volver a probar. Recargar no baja nada ni toca las descargas del
// navegador.
const ESPERA_RECARGA = 10 * 60 * 1000;
// Si no contesta (y no es que contestó otra versión), se prueba 3 veces antes de decidir: una falla pasajera no recarga
async function fondoAlDiaAqui() {
  const esperada = api.runtime.getManifest().version;
  for (let intento = 0; intento < 3; intento++) {
    let r = null;
    try {
      r = await api.runtime.sendMessage({ tipo: "fondo" });
    } catch (e) {
      r = null;
    }
    if (r && typeof r.version === "string") return r.version === esperada;
    await new Promise((listo) => setTimeout(listo, 400));
  }
  return false;
}

// Al recargar, el navegador vacía storage.session, donde están las pestañas que la extensión abrió para la cola de
// «de una en una». Antes de recargar se deja una copia en storage.local; la parte de fondo nueva la toma una sola vez,
// si es de hace poco (ver «colaDeRecarga» en background.js). true si quedó guardada (o no había nada que guardar).
async function guardarColaParaRecargar() {
  try {
    const g = api.storage.session ? await api.storage.session.get("cola") : {};
    if (g && g.cola !== undefined) await api.storage.local.set({ colaDeRecarga: { cola: g.cola, cuando: Date.now() } });
    return true;
  } catch (e) {
    return false;
  }
}

// ¿El navegador está bajando partes de un sitio de descargas para Atarraya? Quitar la extensión borraría su
// seguimiento (storage.local), así que entonces no se aconseja quitarla
async function hayDescargasDeSitios() {
  try {
    const l = await api.storage.local.get("seguidas");
    if (Array.isArray(l && l.seguidas) && l.seguidas.length) return true;
    const s = api.storage.session ? await api.storage.session.get("cola") : {};
    const c = s && s.cola;   // como lo guarda PestanasDeLaCola.exportar(): { cola: [[pestaña, parte], ...] }
    return !!(c && Array.isArray(c.cola) && c.cola.length);
  } catch (e) {
    return true;   // si no se sabe, no se aconseja quitarla
  }
}

function recargarExtension() {
  try {
    api.runtime.reload();
    return true;
  } catch (e) {
    return false;   // si el navegador no deja, queda el texto con los pasos
  }
}

// La copia de la cola se hace justo antes de recargar (nunca antes de esperar), así es la de este momento. Si la
// recarga no se pudo, la copia se borra: no queda ninguna esperando a una recarga que no pasó.
// «soloConCopia»: la recarga sola no se hace si la copia no se pudo guardar; la del botón sí (la persona la pidió).
async function copiarYRecargar(soloConCopia) {
  const copiada = await guardarColaParaRecargar();
  if (!copiada && soloConCopia) return false;
  if (recargarExtension()) return true;
  try {
    await api.storage.local.remove("colaDeRecarga");
  } catch (e) {
    // la parte de fondo la descarta sola al minuto
  }
  return false;
}

async function revisarFondo() {
  if (await fondoAlDiaAqui()) return true;
  const estado = document.getElementById("estado");
  let antes = 0;
  let sePuedeAnotar = true;
  try {
    const g = await api.storage.local.get("fondoRecargado");
    antes = Number(g && g.fondoRecargado) || 0;
  } catch (e) {
    sePuedeAnotar = false;   // sin poder anotar, no se recarga sola (para no recargar una y otra vez)
  }
  const ahora = Date.now();
  if (sePuedeAnotar && !(antes > ahora - ESPERA_RECARGA && antes <= ahora)) {
    let anotado = false;
    try {
      await api.storage.local.set({ fondoRecargado: ahora });
      anotado = true;
    } catch (e) {
      // No se pudo anotar: se sigue como si ya se hubiera probado
    }
    if (anotado) {
      estado.textContent = "La extensión quedó a medias al actualizarse. La estoy recargando...";
      estado.className = "estado mal";
      await new Promise((listo) => setTimeout(listo, 1200));
      if (await copiarYRecargar(true)) return false;
    }
  }
  estado.textContent = "La extensión de Atarraya sigue a medias después de actualizarse. Pulsa «Recargar la extensión». " +
    ((await hayDescargasDeSitios())
      ? "Si sigue igual, espera a que el navegador termine las partes que baja para Atarraya: no quites la extensión " +
        "mientras tanto, porque perdería su seguimiento."
      : "Si sigue igual, en chrome://extensions (en Edge, edge://extensions) pulsa «Quitar» en Atarraya y vuelve a " +
        "cargarla con «Cargar descomprimida».");
  estado.className = "estado mal";
  const boton = document.createElement("button");
  boton.type = "button";
  boton.className = "recargar";
  boton.textContent = "Recargar la extensión";
  boton.addEventListener("click", async () => {
    boton.disabled = true;
    try {
      await api.storage.local.set({ fondoRecargado: Date.now() });
    } catch (e) {
      // igual se recarga: la persona lo pidió
    }
    if (!(await copiarYRecargar(false))) boton.disabled = false;
  });
  estado.after(boton);
  return false;
}

async function iniciar() {
  const a = Object.assign({}, AJUSTES, await api.storage.local.get(AJUSTES));
  const capturar = document.getElementById("capturar");
  const minimo = document.getElementById("minimo");
  capturar.checked = a.capturar;
  minimo.value = a.minimoMB;
  capturar.addEventListener("change", () => api.storage.local.set({ capturar: capturar.checked }));
  // El botón sobre los videos (ver videos.js): apagado, la lista de videos de aquí abajo sigue funcionando
  const botonVideos = document.getElementById("botonVideos");
  botonVideos.checked = a.botonVideos !== false;
  botonVideos.addEventListener("change", () => api.storage.local.set({ botonVideos: botonVideos.checked }));
  minimo.addEventListener("change", () => {
    const v = Math.max(0, Math.min(100000, parseInt(minimo.value, 10) || 0));
    minimo.value = v;
    api.storage.local.set({ minimoMB: v });
  });

  if (!(await revisarFondo())) {
    document.getElementById("videosEstado").textContent = "Primero hay que arreglar la extensión.";
    return;
  }

  mostrarVideos();

  const estado = document.getElementById("estado");
  try {
    const r = await api.runtime.sendMessage({ tipo: "estado" });
    if (r && r.ok) {
      estado.textContent = "Conectado con Atarraya " + (r.version || "") + ".";
      estado.className = "estado bien";
    } else {
      throw new Error("sin respuesta");
    }
  } catch (e) {
    estado.textContent = "No encuentro Atarraya. Ábrelo y pulsa «Navegadores» → «Conectar». " +
      "Mientras tanto, el navegador baja todo como siempre.";
    estado.className = "estado mal";
  }
}

iniciar();
