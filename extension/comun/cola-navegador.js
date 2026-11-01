// Lo que la extensión hace por la cola de «de una en una» de Atarraya. Lo usa background.js; está aparte para poder
// probarlo sin navegador (pruebas/prueba-extension.js).
//
// 1. Qué pestañas abrió Atarraya para su cola. Atarraya abre cada página desde Windows, así que llega como una pestaña
//    que no abrió otra pestaña. Esas son candidatas: cuando su página carga, se le pregunta a Atarraya si es de su
//    cola. Solo en esas se leen los avisos de espera, y sus descargas van con la página que abrió Atarraya.
// 2. Cómo va una descarga que baja el navegador y la extensión sigue (cuando el sitio no deja que otro programa la
//    baje): el aviso para Atarraya. El aviso final se guarda hasta que Atarraya lo confirma.
// 3. Quién baja una parte mientras Atarraya prueba seguir la descarga del navegador: nunca quedan las dos.

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

// Mientras Atarraya prueba seguir la descarga del navegador: qué hace la extensión con cada respuesta. Nunca deja dos
// descargas del mismo archivo: espera a que Atarraya decida; si se acaba el plazo, le pide que suelte la suya y espera
// que conteste. Solo si Atarraya no contesta (está cerrado), sigue el navegador; al volver a abrirse, Atarraya quita la
// suya en cuanto sabe que el navegador siguió.
class TomaDelNavegador {
  constructor(inicio, plazo = 45000, plazoSoltar = 15000) {
    this.inicio = inicio;
    this.plazo = plazo;
    this.plazoSoltar = plazoSoltar;
    this.soltada = 0;
  }

  // «estado»: lo que contestó Atarraya («probando», «tomada», «navegador», «terminada», «ajena»: no es de una parte,
  // «desconocida»; "" si no contestó).
  // Devuelve «esperar», «soltar» (pedirle a Atarraya que suelte la suya), «cancelar» (la del navegador) o «seguir».
  paso(estado, ahora) {
    if (estado === "tomada") return "cancelar";
    if (estado === "navegador" || estado === "terminada" || estado === "ajena") return "seguir";
    if (!this.soltada) {
      if (ahora - this.inicio < this.plazo) return "esperar";
      this.soltada = ahora;
      return "soltar";
    }
    return ahora - this.soltada < this.plazoSoltar ? "esperar" : "seguir";
  }
}

// Después de pedir que se cancele la del navegador, cómo quedó: «cancelada» (ya no baja), «terminada» (el navegador ya
// la había terminado) o «sigue» (no se pudo cancelar). En los dos últimos casos sigue la del navegador y Atarraya quita
// la suya.
function resultadoCancelar(item) {
  if (!item) return "cancelada";
  if (item.state === "interrupted") return "cancelada";
  if (item.state === "complete") return "terminada";
  return "sigue";
}

// El aviso final de una descarga que sigue la extensión (terminó, falló o se canceló) se guarda con ella en
// storage.local hasta que Atarraya confirma que ya no espera nada de esa descarga. Si Atarraya está cerrado, se manda
// otra vez cada 30 segundos mientras el navegador está abierto, al despertar la extensión y al volver a conectar.
const REPETIR_FINAL = 30000;
const VIDA_SEGUIDA = 3 * 24 * 3600000;   // una descarga que se sigue se olvida a los 3 días
const VIDA_FINAL = 30 * 24 * 3600000;    // un aviso final que nunca se confirmó, al mes

// ¿Toca mandar (otra vez) el aviso final? «ya»: enseguida (al anotarlo, o porque se volvió a conectar con Atarraya)
function tocaFinal(final, ahora, ya) {
  if (!final || typeof final !== "object") return false;
  return !!ya || !final.intento || ahora - final.intento >= REPETIR_FINAL;
}

// Lo que contestó Atarraya a la consulta «toma» después del aviso final (null si no contestó): «listo» si ya no espera
// nada de esa descarga (lo anotó, o no es de su cola); si no, «repetir»
function finalEntregado(respuesta) {
  return respuesta && respuesta.ok === true && respuesta.espera === false ? "listo" : "repetir";
}

// Las descargas seguidas que se guardaron: las de los últimos días, y por un mes las que tienen un aviso final sin
// confirmar (Atarraya puede estar cerrado varios días)
function seguidasGuardadas(lista, ahora) {
  const r = [];
  for (const par of Array.isArray(lista) ? lista : []) {
    const [id, s] = Array.isArray(par) ? par : [];
    if (typeof id !== "number" || !s || typeof s !== "object" || typeof s.toma !== "string") continue;
    const final = s.final && typeof s.final === "object";
    if (ahora - (Number(final ? s.final.desde : s.desde) || 0) < (final ? VIDA_FINAL : VIDA_SEGUIDA)) r.push([id, s]);
  }
  return r;
}

if (typeof module !== "undefined") {
  module.exports = { PestanasDeLaCola, sitioBase, avisoDeSeguimiento, TomaDelNavegador, resultadoCancelar, tocaFinal, finalEntregado,
                     seguidasGuardadas };
}
