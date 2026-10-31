// Prueba de la extensión sin navegador (no se publica):   node pruebas/prueba-extension.js
// De qué página salió una descarga: la pestaña real donde nació su pedido, nunca la pestaña activa.

const enNode = typeof require === "function" && typeof module !== "undefined";
const { PestanasDePedidos: Pestanas, paginaDeOrigen: origen } = enNode
  ? require("../extension/comun/origen.js")
  : { PestanasDePedidos, paginaDeOrigen }; // en un navegador, origen.js se carga antes que este archivo

let fallas = 0;
function decir(texto) {
  console.log(texto);
  if (typeof document !== "undefined") document.body.append(texto + "\n");
}
function comprobar(ok, que) {
  decir((ok ? "bien " : "FALLA") + "  " + que);
  if (!ok) fallas++;
}

// En qué pestaña nació cada pedido
const p = new Pestanas(120000, 3);
const archivo = "https://s1.ddownload.com/d/abc/peli.part1.rar";
p.anotar(archivo, 7, 1000);
p.anotar("https://otro.org/a", -1, 1000); // un pedido que no es de una pestaña (de la propia extensión, por ejemplo)
comprobar(p.buscar([archivo], 2000) === 7, "extensión: la pestaña donde nació el pedido de la descarga");
comprobar(p.buscar(["https://otro.org/a"], 2000) === -1 && p.buscar(["https://no-se-vio.org/"], 2000) === -1,
  "extensión: un pedido sin pestaña o que no se vio no da ninguna");
comprobar(p.buscar(["", archivo], 2000) === 7, "extensión: sirve la dirección antes o después de las redirecciones");
comprobar(p.buscar([archivo], 1000 + 121000) === -1, "extensión: un pedido de hace más de dos minutos ya no cuenta");
p.anotar("https://x.org/1", 1, 5000);
p.anotar("https://x.org/2", 2, 5000);
p.anotar("https://x.org/3", 3, 5000);
comprobar(p.mapa.size <= 3 && p.buscar([archivo], 5000) === -1 && p.buscar(["https://x.org/3"], 5000) === 3,
  "extensión: recuerda pocos pedidos y olvida los más viejos");

// La página de origen: la de esa pestaña o, si la descarga abrió una pestaña nueva, la de la que la abrió
const item = { url: archivo, finalUrl: "https://cdn.ddownload.com/d/abc/peli.part1.rar" };
const pagina = { url: "https://ddownload.com/abc123def/peli.part1.rar" };
comprobar(origen(item, pagina, null) === pagina.url, "extensión: la página de la pestaña donde se pulsó descargar");
comprobar(origen(item, { url: "about:blank" }, pagina) === pagina.url && origen(item, { url: archivo }, pagina) === pagina.url,
  "extensión: si la descarga abrió una pestaña nueva, la página de la que la abrió");
comprobar(origen(item, null, null) === "" && origen(item, { url: "chrome://newtab/" }, null) === "",
  "extensión: si no se sabe, nada (no se adivina con la pestaña activa)");

// background.js nunca pregunta por la pestaña activa ni por la última ventana
if (enNode) {
  const fs = require("fs");
  const path = require("path");
  const fondo = fs.readFileSync(path.join(__dirname, "../extension/comun/background.js"), "utf8");
  comprobar(!/tabs\.query|active\s*:\s*true|lastFocusedWindow|currentWindow/.test(fondo),
    "extensión: background.js no usa la pestaña activa");
}

decir(fallas ? `\n${fallas} FALLAS` : "\nTODO BIEN");
if (enNode) process.exitCode = fallas ? 1 : 0;
