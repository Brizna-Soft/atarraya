const api = globalThis.browser ?? globalThis.chrome;
const AJUSTES = { capturar: true, minimoMB: 0 };

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
    partes.push("video y audio en dos archivos");
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

async function iniciar() {
  const a = Object.assign({}, AJUSTES, await api.storage.local.get(AJUSTES));
  const capturar = document.getElementById("capturar");
  const minimo = document.getElementById("minimo");
  capturar.checked = a.capturar;
  minimo.value = a.minimoMB;
  capturar.addEventListener("change", () => api.storage.local.set({ capturar: capturar.checked }));
  minimo.addEventListener("change", () => {
    const v = Math.max(0, Math.min(100000, parseInt(minimo.value, 10) || 0));
    minimo.value = v;
    api.storage.local.set({ minimoMB: v });
  });

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
