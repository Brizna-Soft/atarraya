// En las páginas que Atarraya abrió desde su cola de «de una en una» (y solo en esas), lee los avisos de espera del
// sitio y se los pasa a Atarraya, que muestra la cuenta regresiva y abre la página otra vez cuando se cumple.
//
// Solo lee el texto que se ve: no toca la página, no hace clics, no resuelve captchas y no se salta ni acorta ninguna
// espera. En cualquier otra página no hace nada (le pregunta a la extensión si es de la cola y termina ahí).
// Usa esperas.js, que se carga antes.

(() => {
  "use strict";
  const api = globalThis.browser ?? globalThis.chrome;
  if (!api || !api.runtime || !api.runtime.id) return;
  if (window.top !== window || globalThis.__atarrayaCola) return;   // solo la página de arriba, una sola vez
  globalThis.__atarrayaCola = true;

  let anterior = null;   // la última lectura con una cuenta de la página: { segundos, cuando }
  let enviado = "";

  function leer() {
    const texto = document.body ? document.body.innerText || "" : "";
    const e = reconocerEspera(texto);
    const ahora = Date.now();
    if (!e) {
      anterior = null;
      return;
    }
    if (e.clase === "pagina") {
      // Solo una cuenta que de verdad baja: un texto fijo («los usuarios gratis esperan 30 segundos») no es una espera
      const baja = anterior && e.segundos < anterior.segundos && anterior.segundos - e.segundos <= (ahora - anterior.cuando) / 1000 + 3;
      anterior = { segundos: e.segundos, cuando: ahora };
      if (!baja) return;
    } else {
      anterior = null;
    }
    // Solo cuando cambia (de clase, o en más de medio minuto)
    const clave = e.clase + ":" + (e.segundos < 0 ? "?" : Math.round(e.segundos / 30));
    if (clave === enviado) return;
    enviado = clave;
    try {
      const p = api.runtime.sendMessage({ tipo: "esperaVista", clase: e.clase, segundos: e.segundos });
      if (p && typeof p.catch === "function") p.catch(() => {});
    } catch (x) {
      // La extensión se recargó: ya no hay a quién avisar
    }
  }

  function empezar() {
    leer();
    const reloj = setInterval(leer, 2500);
    setTimeout(() => clearInterval(reloj), 30 * 60000);
  }

  try {
    const p = api.runtime.sendMessage({ tipo: "soyDeLaCola" });
    if (p && typeof p.then === "function")
      p.then((r) => {
        if (r && r.cola) empezar();
      }).catch(() => {});
  } catch (x) {
    // Sin la extensión, nada
  }
})();
