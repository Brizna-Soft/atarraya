const api = globalThis.browser ?? globalThis.chrome;
const AJUSTES = { capturar: true, minimoMB: 0 };

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
