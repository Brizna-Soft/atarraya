// De qué página salió una descarga. Lo usa background.js; está aparte para poder probarlo sin navegador
// (pruebas/prueba-extension.js).
//
// Se anota en qué pestaña nace cada pedido (webRequest dice la pestaña de cada uno). La página de una descarga es la
// de esa pestaña; si la descarga abrió una pestaña nueva (que quedó vacía o con la dirección del archivo), la de la
// pestaña que la abrió. Nunca se usa la pestaña activa: puede ser otra.

class PestanasDePedidos {
  constructor(vida = 2 * 60000, tope = 500) {
    this.vida = vida; // milisegundos que se recuerda un pedido
    this.tope = tope; // cuántos pedidos como mucho
    this.mapa = new Map(); // url -> { tabId, cuando }, del más viejo al más nuevo
  }

  anotar(url, tabId, ahora = Date.now()) {
    if (!url || typeof tabId !== "number" || tabId < 0) return; // pedidos que no son de una pestaña
    this.mapa.delete(url);
    this.mapa.set(url, { tabId, cuando: ahora });
    if (this.mapa.size <= this.tope) return;
    for (const [u, d] of this.mapa) {
      if (this.mapa.size <= this.tope && ahora - d.cuando <= this.vida) break;
      this.mapa.delete(u);
    }
  }

  // La pestaña del primer pedido reciente con alguna de esas direcciones; -1 si no se vio
  buscar(urls, ahora = Date.now()) {
    for (const u of urls) {
      const d = u ? this.mapa.get(u) : undefined;
      if (d && ahora - d.cuando <= this.vida) return d.tabId;
    }
    return -1;
  }
}

// «pestana»: la pestaña donde nació el pedido; «abridora»: la que la abrió (o null). Vacío si no se sabe.
function paginaDeOrigen(item, pestana, abridora) {
  const esPagina = (t) => !!t && /^https?:/i.test(t.url || "") && t.url !== item.url && t.url !== item.finalUrl;
  if (esPagina(pestana)) return pestana.url;
  if (esPagina(abridora)) return abridora.url;
  return "";
}

if (typeof module !== "undefined") module.exports = { PestanasDePedidos, paginaDeOrigen };
