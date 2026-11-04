// Botón «Descargar este video» sobre los videos de la página, como el de IDM. Corre en cada marco, también en los de
// otro sitio (el reproductor suele estar dentro de uno). No toca el reproductor ni la página: solo muestra el botón
// arriba a la derecha del video (los controles van abajo) y, cuando la persona lo pulsa, le pide a la extensión que
// se lo pase a Atarraya. También le cuenta a la extensión qué videos hay en este marco, para su ventanita.
//
// Nada en YouTube ni en sitios que protegen sus videos (DRM); un video con DRM dice «Este video está protegido».
// Usa medios.js, que se carga antes.

(() => {
  "use strict";
  const api = globalThis.browser ?? globalThis.chrome;
  if (!api || !api.runtime || !api.runtime.id) return;
  if (globalThis.__atarrayaVideos) return;   // una sola vez por marco
  globalThis.__atarrayaVideos = true;

  // La dirección de este marco (en un about:blank, la del marco que lo contiene)
  function urlPropia() {
    if (/^https?:/i.test(location.href)) return location.href;
    try {
      if (window.parent !== window && /^https?:/i.test(window.parent.location.href)) return window.parent.location.href;
    } catch (e) {
      // El de arriba es de otro sitio
    }
    const ancestros = location.ancestorOrigins;
    return ancestros && ancestros.length ? ancestros[0] + "/" : "";
  }

  // Ni en YouTube ni en sitios con DRM: tampoco si alguno de los marcos de arriba es de ellos
  function bloqueado() {
    const ancestros = location.ancestorOrigins ? Array.from(location.ancestorOrigins) : [];
    return cadenaBloqueada([location.href, urlPropia(), ...ancestros]);
  }
  if (bloqueado()) return;

  // --- Los videos de este marco -----------------------------------------------------------------------------------
  const cifrados = new WeakSet();   // los que dispararon el evento «encrypted» (DRM)
  function protegido(v) {
    try {
      return cifrados.has(v) || !!v.mediaKeys;
    } catch (e) {
      return false;
    }
  }

  function srcDe(v) {
    let s = v.currentSrc || v.src || "";
    if (!s) {
      const fuente = v.querySelector && v.querySelector("source[src]");
      if (fuente) s = fuente.src;
    }
    return s || "";
  }

  // Reproductores hechos con componentes: el <video> puede estar dentro de su «shadow root» abierto. Se buscan de vez
  // en cuando, no en cada movimiento del ratón.
  let anfitriones = [];
  let revisarAnfitriones = true;
  let revisadoEn = 0;
  function todosLosVideos() {
    const r = Array.from(document.getElementsByTagName("video"));
    if (revisarAnfitriones && Date.now() - revisadoEn > 2000) {
      revisarAnfitriones = false;
      revisadoEn = Date.now();
      anfitriones = [];
      const todos = document.getElementsByTagName("*");
      for (let i = 0; i < todos.length && i < 20000; i++) if (todos[i].shadowRoot) anfitriones.push(todos[i]);
    }
    for (const a of anfitriones) {
      try {
        if (a.shadowRoot) r.push(...a.shadowRoot.querySelectorAll("video"));
      } catch (e) {}
    }
    return r;
  }

  // --- El botón -------------------------------------------------------------------------------------------------
  // Dentro de un «shadow root» cerrado y con estilos puestos desde aquí: la página no lo cambia y su CSP no lo frena
  let anfitrion = null;
  let boton = null;
  let actual = null;
  let ocultando = 0;
  let textoHasta = 0;

  function crearBoton() {
    if (anfitrion) return;
    anfitrion = document.createElement("atarraya-boton");
    const s = anfitrion.style;
    s.setProperty("all", "initial", "important");
    s.setProperty("position", "fixed", "important");
    s.setProperty("z-index", "2147483647", "important");
    s.setProperty("display", "none", "important");
    s.setProperty("top", "0px", "important");
    s.setProperty("left", "0px", "important");
    const raiz = anfitrion.attachShadow({ mode: "closed" });
    boton = document.createElement("button");
    boton.type = "button";
    const b = boton.style;
    b.font = "600 13px/1.25 'Segoe UI', system-ui, sans-serif";
    b.color = "#ffffff";
    b.background = "rgba(24, 95, 165, 0.94)";
    b.border = "1px solid rgba(255, 255, 255, 0.55)";
    b.borderRadius = "6px";
    b.padding = "6px 10px";
    b.margin = "0";
    b.cursor = "pointer";
    b.boxShadow = "0 2px 8px rgba(0, 0, 0, 0.35)";
    // El texto se parte en varias líneas (nunca se corta): como mucho 280 px de ancho, y menos si la ventana es angosta
    b.display = "block";
    b.boxSizing = "border-box";
    b.maxWidth = "min(280px, calc(100vw - 16px))";
    b.height = "auto";
    b.overflow = "visible";
    b.textAlign = "left";
    b.whiteSpace = "normal";
    b.overflowWrap = "anywhere";
    b.wordBreak = "normal";
    raiz.append(boton);
    // Los clics en el botón no llegan al reproductor (no lo pausan ni lo ponen en pantalla completa)
    for (const tipo of ["mousedown", "mouseup", "pointerdown", "pointerup", "click", "dblclick", "contextmenu"]) {
      anfitrion.addEventListener(tipo, (e) => e.stopPropagation());
    }
    boton.addEventListener("click", alPulsar);
    anfitrion.addEventListener("mouseenter", () => clearTimeout(ocultando));
    anfitrion.addEventListener("mouseleave", () => programarOcultar());
    (document.documentElement || document.body).appendChild(anfitrion);
  }

  function etiqueta() {
    if (!boton || !actual || Date.now() < textoHasta) return;
    const p = protegido(actual);
    boton.textContent = p ? "Este video está protegido" : "Descargar este video";
    boton.style.background = p ? "rgba(90, 90, 90, 0.94)" : "rgba(24, 95, 165, 0.94)";
    boton.style.cursor = p ? "default" : "pointer";
  }

  function mostrarTexto(texto, ms) {
    if (!boton) return;
    boton.textContent = texto;
    textoHasta = Date.now() + ms;
    if (actual) colocar(actual);
    setTimeout(() => {
      if (Date.now() >= textoHasta) etiqueta();
    }, ms + 50);
  }

  // Arriba a la derecha, dentro del video: los controles del reproductor van abajo. Siempre entero dentro de la ventana
  // (con el ancho y el alto visibles, sin las barras de desplazamiento), para que su texto nunca quede cortado.
  function colocar(v) {
    const r = v.getBoundingClientRect();
    anfitrion.style.setProperty("display", "block", "important");
    // Primero a la izquierda, para medirlo sin que el borde de la ventana lo achique
    anfitrion.style.setProperty("left", "0px", "important");
    const ancho = boton.offsetWidth || 160;
    const alto = boton.offsetHeight || 30;
    const visible = (document.documentElement && document.documentElement.clientWidth) || window.innerWidth;
    const visibleAlto = (document.documentElement && document.documentElement.clientHeight) || window.innerHeight;
    let izquierda = Math.max(Math.min(r.right, visible) - ancho - 8, r.left + 4);
    izquierda = Math.max(Math.min(izquierda, visible - ancho - 4), 4);
    const arriba = Math.max(Math.min(Math.max(r.top, 0) + 8, visibleAlto - alto - 4), 4);
    anfitrion.style.setProperty("top", Math.round(arriba) + "px", "important");
    anfitrion.style.setProperty("left", Math.round(izquierda) + "px", "important");
  }

  function mostrarPara(v) {
    if (document.fullscreenElement) return;   // en pantalla completa no se puede mostrar encima
    crearBoton();
    clearTimeout(ocultando);
    if (actual !== v) {
      actual = v;
      etiqueta();
    }
    colocar(v);
  }

  // Se esconde un poco después de que el ratón sale del video, para que se alcance a pulsar
  function programarOcultar() {
    clearTimeout(ocultando);
    ocultando = setTimeout(() => {
      if (Date.now() < textoHasta) return programarOcultar();
      if (anfitrion) anfitrion.style.setProperty("display", "none", "important");
      actual = null;
    }, 1200);
  }

  async function alPulsar(e) {
    e.preventDefault();
    e.stopPropagation();
    if (!e.isTrusted || !actual) return;   // solo si lo pulsó la persona
    const v = actual;
    if (protegido(v)) {
      mostrarTexto("Este video está protegido", 4000);
      return;
    }
    const src = srcDe(v);
    mostrarTexto("Enviando a Atarraya...", 15000);
    let r = null;
    let fallo = null;
    try {
      r = await api.runtime.sendMessage({
        tipo: "bajarVideo",
        src: /^https?:/i.test(src) ? src : "",
        alto: v.videoHeight || 0,
        // Cuánto dura: en una página con varios videos, distingue la lista de este de las de los otros
        duracion: Number.isFinite(v.duration) && v.duration > 0 ? v.duration : 0,
        protegido: false,
        marco: urlPropia(),
      });
    } catch (err) {
      fallo = err || new Error("");
    }
    if (r && r.ok) mostrarTexto("Enviado a Atarraya", 3000);
    else if (r && r.texto) mostrarTexto(r.texto, 7000);
    // La extensión no contestó (por ejemplo, se actualizó mientras esta página seguía abierta): eso no es Atarraya
    else mostrarTexto(textoSinRespuesta(fallo, extensionViva()), 7000);
  }

  // ¿Este marco todavía habla con la extensión? Si la extensión se actualizó o se recargó, el de antes se queda sin ella
  function extensionViva() {
    try {
      return !!(api.runtime && api.runtime.id);
    } catch (e) {
      return false;
    }
  }

  // Antes de mostrar el botón se le pregunta a la extensión si el video viene de YouTube o de un sitio con DRM aunque
  // su dirección no lo diga (una redirección a googlevideo.com, o un marco que reproduce algo de ellos). Mientras no
  // contesta, no hay botón; la respuesta se recuerda unos segundos.
  const revisados = new WeakMap();   // video -> { src, bloqueado: true | false | null, cuando }
  function revisar(v) {
    const src = srcDe(v);
    const antes = revisados.get(v);
    if (antes && antes.src === src && (antes.bloqueado === null || Date.now() - antes.cuando < 10000)) return antes;
    const r = { src, bloqueado: antes && antes.src === src ? antes.bloqueado : null, cuando: Date.now() };
    revisados.set(v, r);
    const listo = (bloqueado) => {
      r.bloqueado = bloqueado;
      r.cuando = Date.now();
      if (raton && videoBajo(raton.x, raton.y) === v) {
        if (!bloqueado) mostrarPara(v);
        else if (actual === v) ocultarYa();
      }
    };
    try {
      const p = api.runtime.sendMessage({ tipo: "revisarVideo", src: /^https?:/i.test(src) ? src : "" });
      Promise.resolve(p).then((resp) => listo(!!(resp && resp.bloqueado)), () => listo(false));
    } catch (e) {
      listo(false);
    }
    return r;
  }

  function ocultarYa() {
    clearTimeout(ocultando);
    if (anfitrion) anfitrion.style.setProperty("display", "none", "important");
    actual = null;
  }

  // El video que está bajo el ratón (por su lugar en la pantalla: muchos reproductores ponen una capa encima)
  function videoBajo(x, y) {
    let mejor = null;
    let area = Infinity;
    for (const v of todosLosVideos()) {
      const r = v.getBoundingClientRect();
      if (r.width < 120 || r.height < 70) continue;
      if (x < r.left || x > r.right || y < r.top || y > r.bottom) continue;
      const estilo = getComputedStyle(v);
      if (estilo.visibility === "hidden" || estilo.display === "none") continue;
      if (r.width * r.height < area) {
        mejor = v;
        area = r.width * r.height;
      }
    }
    return mejor;
  }

  let raton = null;
  let pendiente = false;
  function revisarRaton() {
    pendiente = false;
    if (!raton) return;
    const v = videoBajo(raton.x, raton.y);
    const r = v ? revisar(v) : null;
    if (r && r.bloqueado === false) mostrarPara(v);
    else if (r && r.bloqueado && actual === v) ocultarYa();
    else if (!v && anfitrion && actual) programarOcultar();
  }
  document.addEventListener(
    "mousemove",
    (e) => {
      raton = { x: e.clientX, y: e.clientY };
      if (pendiente) return;
      pendiente = true;
      requestAnimationFrame(revisarRaton);
    },
    { capture: true, passive: true }
  );
  const recolocar = () => {
    if (actual && anfitrion && anfitrion.style.getPropertyValue("display") === "block") colocar(actual);
  };
  window.addEventListener("scroll", recolocar, { capture: true, passive: true });
  window.addEventListener("resize", recolocar, { passive: true });

  // --- Lo que se le cuenta a la extensión (para su ventanita) ---------------------------------------------------------
  let ultimoInforme = "";
  let informe = 0;
  function programarInforme(pronto) {
    if (informe) return;
    informe = setTimeout(informar, pronto ? 150 : 900);
  }
  function informar() {
    informe = 0;
    const videos = todosLosVideos()
      .slice(0, 50)
      .map((v) => {
        const s = srcDe(v);
        return {
          src: /^https?:/i.test(s) ? s : "",
          blob: /^blob:/i.test(s),
          protegido: protegido(v),
          ancho: v.videoWidth || 0,
          alto: v.videoHeight || 0,
          duracion: Number.isFinite(v.duration) ? Math.round(v.duration) : 0,
        };
      });
    if (!videos.length && !ultimoInforme) return;   // un marco sin videos no dice nada
    const texto = JSON.stringify(videos);
    if (texto === ultimoInforme) return;
    ultimoInforme = texto;
    try {
      const r = api.runtime.sendMessage({ tipo: "videosDeMarco", marco: urlPropia(), videos });
      if (r && typeof r.catch === "function") r.catch(() => {});
    } catch (e) {
      // La extensión se actualizó o se quitó: este marco ya no puede hablar con ella
    }
  }

  // DRM: el evento «encrypted» no sube por la página, pero se ve al bajar (captura)
  document.addEventListener(
    "encrypted",
    (e) => {
      const v = e.target;
      if (!v || !(v instanceof HTMLMediaElement)) return;
      cifrados.add(v);
      if (v === actual) etiqueta();
      programarInforme(true);
    },
    true
  );
  for (const tipo of ["loadstart", "loadedmetadata", "playing", "emptied", "waitingforkey"]) {
    document.addEventListener(tipo, (e) => {
      if (e.target instanceof HTMLMediaElement) programarInforme(false);
    }, true);
  }
  new MutationObserver((cambios) => {
    let hay = false;
    for (const c of cambios) {
      if (c.type === "childList") {
        revisarAnfitriones = true;
        for (const n of c.addedNodes) {
          if (n.nodeName === "VIDEO" || (n.querySelector && n.querySelector("video"))) hay = true;
        }
      } else if (c.target && (c.target.nodeName === "VIDEO" || c.target.nodeName === "SOURCE")) {
        hay = true;
      }
    }
    if (hay) programarInforme(false);
  }).observe(document.documentElement || document, { childList: true, subtree: true, attributes: true, attributeFilter: ["src"] });
  programarInforme(false);
})();
