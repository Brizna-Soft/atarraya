// Los avisos de espera de los sitios de descargas: «You have to wait 9 minutes, 41 seconds till next download»,
// «Blocked: wait 10 minutes», «Download limit reached», «Debes esperar 5 minutos», «Vous devez attendre 15 minutes»,
// relojes de cuenta regresiva («Next download in 09:41», «Please wait 30 seconds»)...
//
// Lo usa cola.js solo en las páginas que Atarraya abrió desde su cola de «de una en una». Solo lee el texto que se ve:
// no toca la página, no hace clics y no se salta ni acorta ninguna espera. Está aparte para poder probarlo sin
// navegador (pruebas/prueba-extension.js).
//
// Hecho desde cero con frases genéricas (inglés, español, francés, portugués, alemán e italiano) y las de los sitios
// más comunes (ddownload, Rapidgator, katfile, nitroflare, 1fichier, turbobit...).
//
// reconocerEspera(texto) devuelve null o { clase, segundos, frase }:
//   clase «limite»: el sitio no deja bajar hasta que pase el tiempo (entre descargas, límite diario, «Blocked»...)
//   clase «pagina»: la cuenta regresiva de la página antes de mostrar el botón o el captcha
//   segundos: lo que falta (-1 si el sitio no lo dice)

// Minúsculas, sin acentos y con los espacios juntos (los saltos de línea se quedan)
function normalizarTexto(t) {
  return String(t || "")
    .normalize("NFD")
    .replace(/[̀-ͯ]/g, "")
    .toLowerCase()
    .replace(/[ \t   ]+/g, " ");
}

const UNIDAD_DE_TIEMPO =
  "(hours?|hrs?|horas?|heures?|stunden?|h|ore|ora|minutes?|mins?|minutos?|minuten|minuti|m|seconds?|secs?|segundos?|seg|secondes?|sekunden?|secondi|s)";

function segundosDeUnidad(u) {
  if (/^(hours?|hrs?|horas?|heures?|stunden?|h|ore|ora)$/.test(u)) return 3600;
  if (/^(minutes?|mins?|minutos?|minuten|minuti|m)$/.test(u)) return 60;
  return 1;
}

// Los segundos de la primera duración de un texto: «9 minutes, 41 seconds», «1 hour 5 min», «2h 03m», «120 sec»,
// «09:41», «1:02:03». -1 si no hay ninguna.
function segundosDe(texto) {
  const t = normalizarTexto(texto);
  const re = new RegExp("(\\d{1,5}) ?" + UNIDAD_DE_TIEMPO + "\\b", "g");
  let total = -1;
  let fin = -1;
  let m;
  while ((m = re.exec(t))) {
    // Los trozos de una misma duración van seguidos («9 minutes, 41 seconds», «1 hora y 5 minutos»)
    if (total >= 0 && !/^[\s,]*((and|y|e|et|und)\s+)?$/.test(t.slice(fin, m.index))) break;
    total = (total < 0 ? 0 : total) + Number(m[1]) * segundosDeUnidad(m[2]);
    fin = m.index + m[0].length;
  }
  if (total >= 0) return total;
  const r = /(^|[^\d:])(\d{1,2}):(\d{2})(?::(\d{2}))?(?![\d:])/.exec(t);
  if (r) return r[4] !== undefined ? Number(r[2]) * 3600 + Number(r[3]) * 60 + Number(r[4]) : Number(r[2]) * 60 + Number(r[3]);
  return -1;
}

// Una instrucción de esperar (o de volver más tarde)
const ESPERA_DIRECTIVA = new RegExp(
  [
    "\\byou (have|need|must|will have|will need) to wait\\b",
    "\\byou must wait\\b",
    "\\bplease wait\\b",
    "\\bwait (for )?\\d",
    "\\bwait (until|till)\\b",
    "\\btry again (in|after|later)\\b",
    "\\bcome back (in|after|later)\\b",
    "\\bnext download (in|after|available)\\b",
    "\\b(debes|tienes que|hay que|necesitas|deberas|tendras que) esperar\\b",
    "\\bespera(r|n)? \\d",
    "\\bespere \\d",
    "\\bvuelve a intentar",
    "\\bintentalo (de nuevo|otra vez|mas tarde)",
    "\\bvous devez attendre\\b",
    "\\bveuillez (patienter|attendre)\\b",
    "\\battendez\\b",
    "\\bbitte warten\\b",
    "\\bwarten sie\\b",
    "\\baguarde\\b",
    "\\bvoce (deve|precisa) (esperar|aguardar)\\b",
    "\\b(devi|dovete) attendere\\b",
    "\\battendere \\d",
  ].join("|")
);

// El motivo es un límite del sitio (entre descargas, por día, por IP...), no la cuenta de la página
const ESPERA_LIMITE = new RegExp(
  [
    "next (free )?download",
    "next file",
    "another download",
    "between (two |your )?downloads",
    "downloads? limit",
    "limit of downloads",
    "(limit|quota) (has been |was |is )?(reached|exceeded)",
    "(daily|hourly|traffic|bandwidth) (downloads? )?(limit|quota)",
    "too many (downloads|connections)",
    "(one|1) (file|download)s? at a time",
    "\\bblocked\\b",
    "(proxima|siguiente|otra) descarga",
    "entre descargas",
    "limite de (descargas|trafico)",
    "bloquead[oa]",
    "(telecharger|telechargement) (a|de) nouveau",
    "prochain telechargement",
    "limite de telechargement",
    "nachsten download",
    "proximo download",
    "prossimo download",
  ].join("|")
);

// Lo dice como un estado («Blocked», «Download limit reached»...), aunque no diga cuánto falta
const ESPERA_BLOQUEO = new RegExp(
  [
    "^blocked\\b",
    "\\bblocked[:!.]",
    "\\byou (have )?(reached|exceeded) (the|your)\\b",
    "(limit|quota) (has been |was )?(reached|exceeded)",
    "download limit reached",
    "too many downloads",
    "you can(no|')t download (any ?more|another)",
    "no puedes (descargar|bajar) (mas|otro)",
    "has (alcanzado|superado) (el|tu) limite",
    "limite (de descargas )?alcanzado",
    "vous avez (atteint|depasse)",
    "sie haben (das|ihr) .{0,20}limit",
  ].join("|")
);

// La cuenta regresiva de la página («Download link available in 30 seconds», «25 seconds left»)
const ESPERA_CUENTA = new RegExp(
  [
    "(seconds?|secs?|segundos?|secondes?|sekunden?) (left|remaining|restantes?|restants?)\\b",
    "\\b(available|ready|starts?|appears?|begins?|unlocks?|unlocked) in\\b",
    "(estara|estaran|sera|seran) (lista|listo|listos|disponibles?) en\\b",
    "(aparecera|empezara|comenzara) en\\b",
    "\\bdisponible (en|dans)\\b",
    "\\bdownload (button|link) (will )?(appear|be available)",
  ].join("|")
);

// ¿Es mejor aviso que el que ya se tenía? Un límite gana a una cuenta; uno con tiempo, a uno sin tiempo; si no, el más
// largo
function esperaMejor(a, b) {
  if (!b) return true;
  if (a.clase !== b.clase) return a.clase === "limite";
  if ((a.segundos >= 0) !== (b.segundos >= 0)) return a.segundos >= 0;
  return a.segundos > b.segundos;
}

function reconocerEspera(texto) {
  const t = normalizarTexto(texto).slice(0, 200000);
  const lineas = t
    .split(/\n+/)
    .map((s) => s.trim())
    .filter(Boolean)
    .slice(0, 3000);
  let mejor = null;
  for (let i = 0; i < lineas.length; i++) {
    // Cada línea sola y con la siguiente: la frase y el reloj suelen ir en líneas aparte («Next download in» / «09:41»)
    const trozos = [lineas[i]];
    if (i + 1 < lineas.length && lineas[i].length <= 200 && lineas[i + 1].length <= 80) trozos.push(lineas[i] + " " + lineas[i + 1]);
    for (const trozo of trozos) {
      if (trozo.length > 600) continue; // un párrafo largo (condiciones, preguntas frecuentes) no es un aviso
      const directiva = ESPERA_DIRECTIVA.test(trozo);
      const cuenta = ESPERA_CUENTA.test(trozo);
      const bloqueo = ESPERA_BLOQUEO.test(trozo);
      if (!directiva && !cuenta && !bloqueo) continue;
      const segundos = segundosDe(trozo);
      let r = null;
      if ((directiva || cuenta) && segundos > 0) {
        r = { clase: ESPERA_LIMITE.test(trozo) || bloqueo || segundos >= 180 ? "limite" : "pagina", segundos };
      } else if (bloqueo) {
        r = { clase: "limite", segundos: segundos > 0 ? segundos : -1 };
      }
      if (!r) continue;
      r.frase = lineas[i].slice(0, 160);
      if (esperaMejor(r, mejor)) mejor = r;
    }
  }
  return mejor;
}

if (typeof module !== "undefined") module.exports = { normalizarTexto, segundosDe, reconocerEspera };
