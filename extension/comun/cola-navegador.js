// Lo que la extensión hace por la cola de «de una en una» de Atarraya. Lo usa background.js; está aparte para poder
// probarlo sin navegador (pruebas/prueba-extension.js).
//
// 1. Qué pestañas abrió Atarraya para su cola. Atarraya abre cada página desde Windows, así que llega como una pestaña
//    que no abrió otra pestaña. Esas son candidatas: cuando su página carga, se le pregunta a Atarraya si es de su
//    cola. Solo en esas se leen los avisos de espera, y sus descargas van con la página que abrió Atarraya.
// 2. Cómo va una descarga que baja el navegador y la extensión sigue (cuando el sitio no deja que otro programa la
//    baje): el aviso para Atarraya.

class PestanasDeLaCola {
  constructor(vidaCandidata = 2 * 60000, vidaCola = 6 * 3600000, consultas = 3) {
    this.vidaCandidata = vidaCandidata;   // cuánto se espera que una candidata cargue una página de la cola
    this.vidaCola = vidaCola;             // cuánto se recuerda una pestaña de la cola
    this.consultas = consultas;           // cuántas veces como mucho se pregunta por una candidata
    this.cola = new Map();                // tabId -> { parte (la página que abrió Atarraya), desde }
    this.candidatas = new Map();          // tabId -> { cuando, consultas }
  }

  candidata(tabId, ahora) {
    if (typeof tabId !== "number" || tabId < 0 || this.cola.has(tabId)) return;
    this.candidatas.set(tabId, { cuando: ahora, consultas: 0 });
    if (this.candidatas.size > 200) {
      for (const [id, c] of this.candidatas) if (ahora - c.cuando > this.vidaCandidata || this.candidatas.size > 200) this.candidatas.delete(id);
    }
  }

  // ¿Se le pregunta a Atarraya por esta pestaña? Cada candidata, en sus primeros minutos y pocas veces (la página que
  // abre Atarraya carga enseguida; así navegar en una pestaña nueva no hace preguntas sin fin)
  puedePreguntar(tabId, ahora) {
    const c = this.candidatas.get(tabId);
    if (!c) return false;
    if (ahora - c.cuando > this.vidaCandidata || c.consultas >= this.consultas) {
      this.candidatas.delete(tabId);
      return false;
    }
    c.consultas++;
    return true;
  }

  marcar(tabId, parte, ahora) {
    if (typeof tabId !== "number" || tabId < 0 || !/^https?:/i.test(parte || "")) return;
    this.cola.set(tabId, { parte, desde: ahora });
    this.candidatas.delete(tabId);
  }

  // La página que abrió Atarraya en esa pestaña, o "" si no es de la cola
  parte(tabId, ahora = Date.now()) {
    const c = this.cola.get(tabId);
    if (!c) return "";
    if (ahora - c.desde > this.vidaCola) {
      this.cola.delete(tabId);
      return "";
    }
    return c.parte;
  }

  olvidar(tabId) {
    this.cola.delete(tabId);
    this.candidatas.delete(tabId);
  }

  exportar() {
    return { cola: [...this.cola], candidatas: [...this.candidatas] };
  }

  cargar(datos) {
    if (!datos || typeof datos !== "object") return;
    for (const [id, c] of Array.isArray(datos.cola) ? datos.cola : []) {
      if (typeof id === "number" && c && typeof c.parte === "string" && typeof c.desde === "number") this.cola.set(id, c);
    }
    for (const [id, c] of Array.isArray(datos.candidatas) ? datos.candidatas : []) {
      if (typeof id === "number" && c && typeof c.cuando === "number") this.candidatas.set(id, { cuando: c.cuando, consultas: Number(c.consultas) || 0 });
    }
  }
}

// El sitio de una dirección sin los servidores de adelante (s12.ddownload.com → ddownload.com). Solo para saber si una
// pestaña de la cola sigue en el sitio de su parte; Atarraya revisa lo demás.
function sitioBase(url) {
  let h = "";
  try {
    h = new URL(url).hostname.toLowerCase().replace(/\.$/, "");
  } catch (e) {
    return "";
  }
  if (/^[\d.]+$/.test(h) || h.includes(":")) return h;
  const p = h.split(".");
  const dos = p.length >= 3 && p[p.length - 1].length === 2 && ["co", "com", "net", "org", "gov", "gob", "edu", "ac"].includes(p[p.length - 2]);
  return p.slice(dos ? -3 : -2).join(".");
}

// El aviso para Atarraya de una descarga que sigue la extensión, o null si no hay nada nuevo que contar. «seguida»
// recuerda el último aviso. «terminar»: ya no hay que seguirla.
function avisoDeSeguimiento(item, seguida, ahora) {
  if (!item || !seguida) return null;
  const bajado = Math.max(0, Number(item.bytesReceived) || 0);
  const total = Number(item.totalBytes) > 0 ? Number(item.totalBytes) : -1;
  if (item.state === "complete") {
    return { estado: "completa", bajado, total: total > 0 ? total : bajado, ruta: item.filename || "", terminar: true };
  }
  if (item.state === "interrupted") {
    if (seguida.ultimoEstado === "fallo" || seguida.ultimoEstado === "cancelada") return null;
    const cancelada = item.error === "USER_CANCELED";
    // Si se puede reanudar, se sigue mirando: puede que la persona la reanude en el navegador
    return { estado: cancelada ? "cancelada" : "fallo", bajado, total, terminar: cancelada || !item.canResume };
  }
  if (item.paused) return null;
  if (seguida.ultimoEstado === "bajando" && ahora - (seguida.ultimoAviso || 0) < 5000) return null;
  if (seguida.ultimoEstado === "bajando" && bajado === seguida.ultimoBytes) return null;
  return { estado: "bajando", bajado, total, terminar: false };
}

if (typeof module !== "undefined") module.exports = { PestanasDeLaCola, sitioBase, avisoDeSeguimiento };
