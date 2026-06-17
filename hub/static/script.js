const status = document.getElementById("status");
const img = document.getElementById("frame");
const emptyState = document.getElementById("emptyState");
const captureBtn = document.getElementById("captureBtn");
const applyBtn = document.getElementById("applyBtn");
const filenameEl = document.getElementById("filename");
const bytesEl = document.getElementById("bytes");
const framesizeEl = document.getElementById("framesize");
const qualityEl = document.getElementById("quality");
const qualityValueEl = document.getElementById("qualityValue");

const aeLevelEl = document.getElementById("aeLevel");
const aeLevelValueEl = document.getElementById("aeLevelValue");

const contrastLevelEl = document.getElementById("contrastLevel");
const contrastLevelValueEl = document.getElementById("contrastLevelValue");

const saturationLevelEl = document.getElementById("saturationLevel");
const saturationLevelValueEl = document.getElementById("saturationLevelValue");

const brightnessLevelEl = document.getElementById("brightnessLevel");
const brightnessLevelValueEl = document.getElementById("brightnessLevelValue");

const deletePresetBtn = document.getElementById("elimPresetBtn");
const presetSelect = document.getElementById("presets");
const savePresetBtn = document.getElementById("savePresetBtn");

const espHostEl = document.getElementById("espHost");
const espPortEl = document.getElementById("espPort");
const espHostStatusEl = document.getElementById("espHostStatus");
const saveEspHostBtn = document.getElementById("saveEspHostBtn");

// ── NUOVI ELEMENTI METRICHE ───────────────────────────────────────
const tCaptureEl = document.getElementById("tCapture");
const tTransitEl = document.getElementById("tTransit");
const tTotalEl = document.getElementById("tTotal");

aeLevelEl.addEventListener("input", () => {
  const v = Number(aeLevelEl.value);
  aeLevelValueEl.textContent = v > 0 ? "+" + v : String(v);
});

contrastLevelEl.addEventListener("input", () => {
  const v = Number(contrastLevelEl.value);
  contrastLevelValueEl.textContent = v > 0 ? "+" + v : String(v);
});

saturationLevelEl.addEventListener("input", () => {
  const v = Number(saturationLevelEl.value);
  saturationLevelValueEl.textContent = v > 0 ? "+" + v : String(v);
});

brightnessLevelEl.addEventListener("input", () => {
  const v = Number(brightnessLevelEl.value);
  brightnessLevelValueEl.textContent = v > 0 ? "+" + v : String(v);
});

qualityEl.addEventListener("input", () => {
  qualityValueEl.textContent = qualityEl.value;
});

function setBusy(isBusy) {
  captureBtn.disabled = isBusy;
  applyBtn.disabled = isBusy;
  if (saveEspHostBtn) saveEspHostBtn.disabled = isBusy;
}

function showEmpty() {
  img.style.display = "none";
  img.removeAttribute("src");
  emptyState.style.display = "block";
}

function showFrame() {
  emptyState.style.display = "none";
  img.style.display = "block";
  img.src = "/latest.jpg?t=" + Date.now();
}


async function loadEspHost() {
  if (!espHostEl) return;

  try {
    const res = await fetch("/esp-host");
    const data = await res.json();

    if (!data.ok) {
      espHostStatusEl.textContent = "Errore lettura IP ESP.";
      return;
    }

    espHostEl.value = data.host || "";
    if (espPortEl) espPortEl.textContent = String(data.port || "-");
    espHostStatusEl.textContent = data.host
      ? "ESP configurato su " + data.host + ":" + data.port
      : "IP ESP non configurato.";
  } catch (err) {
    espHostStatusEl.textContent = "Errore web IP ESP: " + err;
  }
}

async function saveEspHost() {
  if (!espHostEl) return;

  const host = espHostEl.value.trim();

  if (!host) {
    espHostStatusEl.textContent = "Inserisci l'IP dell'ESP.";
    return;
  }

  setBusy(true);
  espHostStatusEl.textContent = "Salvataggio IP ESP...";

  try {
    const res = await fetch("/esp-host", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ host }),
    });

    const data = await res.json();

    if (!data.ok) {
      espHostStatusEl.textContent = "Errore IP ESP: " + data.error;
      return;
    }

    espHostEl.value = data.host;
    espHostStatusEl.textContent =
      "IP ESP aggiornato: " +
      data.host +
      ":" +
      data.port +
      (data.persisted ? "" : " (non salvato su .env)");
    status.textContent = "Connessione ESP resettata. Prossimo comando userà il nuovo IP.";
  } catch (err) {
    espHostStatusEl.textContent = "Errore web IP ESP: " + err;
  } finally {
    setBusy(false);
  }
}

async function checkExistingFrame() {
  try {
    const res = await fetch("/has-frame");
    const data = await res.json();

    if (data.exists) {
      showFrame();
      status.textContent = "Ultimo frame caricato.";
    } else {
      showEmpty();
    }
  } catch (err) {
    showEmpty();
    status.textContent = "Pronto.";
  }
}

async function applyConfig() {
  setBusy(true);

  const framesize = framesizeEl.value;
  const quality = Number(qualityEl.value);
  const ae = Number(aeLevelEl.value);
  const contrast = Number(contrastLevelEl.value);
  const saturation = Number(saturationLevelEl.value);
  const brightness = Number(brightnessLevelEl.value);

  status.textContent = "Invio configurazione camera...";

  try {
    const res = await fetch("/config", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        framesize: framesize,
        quality: quality,
        ae: ae,
        contrast: contrast,
        saturation: saturation,
        brightness: brightness,
      }),
    });

    const data = await res.json();

    if (!data.ok) {
      status.textContent = "Errore configurazione: " + data.error;
      return;
    }

    status.textContent =
      "Configurazione applicata: " +
      data.framesize +
      ", quality " +
      data.quality +
      ", esposizione " +
      data.ae +
      ", contrasto " +
      data.contrast +
      ", saturazione " +
      data.saturation +
      ", luminosità " +
      data.brightness;
  } catch (err) {
    status.textContent = "Errore web config: " + err;
  } finally {
    setBusy(false);
  }
}

async function captureFrame() {
  setBusy(true);
  status.textContent = "Richiesta frame in corso...";

  try {
    const res = await fetch("/capture", {
      method: "POST",
    });

    const data = await res.json();

    if (!data.ok) {
      status.textContent = "Errore: " + data.error;
      return;
    }

    // ── AGGIORNAMENTO DEL TESTO STATO CON TIMING ──────────────────────
    const cap = data.timing.esp_capture_ms;
    const tra = data.timing.network_transit_ms;
    const tot = data.timing.total_ms;

    status.textContent = "Frame ricevuto con successo!";

    // Aggiorna anche i singoli elementi della UI se presenti nel tuo HTML
    if (tCaptureEl) tCaptureEl.textContent = cap + " ms";
    if (tTransitEl) tTransitEl.textContent = tra + " ms";
    if (tTotalEl) tTotalEl.textContent = tot + " ms";

    filenameEl.textContent = "File: " + data.filename;
    bytesEl.textContent = "Dimensione: " + data.bytes + " bytes";

    showFrame();
  } catch (err) {
    status.textContent = "Errore web: " + err;
  } finally {
    setBusy(false);
  }
}

async function applyAeLevel() {
  setBusy(true);

  const ae_level = Number(aeLevelEl.value);
  status.textContent = "Invio compensazione esposizione...";

  try {
    const res = await fetch("/ae-level", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ae_level }),
    });

    const data = await res.json();

    if (!data.ok) {
      status.textContent = "Errore AE: " + data.error;
      return;
    }

    const label = ae_level > 0 ? "+" + ae_level : String(ae_level);
    status.textContent = "Esposizione applicata: AE Level " + label;
  } catch (err) {
    status.textContent = "Errore web AE: " + err;
  } finally {
    setBusy(false);
  }
}

async function loadPresetList() {
  try {
    const res = await fetch("/presets");
    const data = await res.json();

    presetSelect.innerHTML = "";

    if (!data.ok) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "Errore caricamento preset";
      presetSelect.appendChild(option);

      status.textContent = "Errore caricamento preset: " + data.error;
      return;
    }

    const presets = Array.isArray(data.presets) ? data.presets : [];

    if (presets.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "Nessun preset salvato";
      presetSelect.appendChild(option);
      return;
    }

    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Seleziona preset";
    presetSelect.appendChild(placeholder);

    presets.forEach((preset) => {
      const option = document.createElement("option");

      option.value = preset.name;
      option.textContent = preset.name;

      presetSelect.appendChild(option);
    });
  } catch (err) {
    presetSelect.innerHTML = "";

    const option = document.createElement("option");
    option.value = "";
    option.textContent = "Errore caricamento";
    presetSelect.appendChild(option);

    status.textContent = "Errore caricamento preset: " + err;
  }
}

presetSelect.addEventListener("change", async () => {
  const presetName = presetSelect.value;

  if (!presetName) {
    return;
  }

  try {
    status.textContent = "Caricamento preset...";

    const res = await fetch("/preset/" + encodeURIComponent(presetName));
    const data = await res.json();

    if (!data.ok) {
      status.textContent = "Errore caricamento preset: " + data.error;
      return;
    }

    const preset = data.preset;

    framesizeEl.value = preset.framesize;
    qualityEl.value = preset.quality;
    aeLevelEl.value = preset.ae;
    contrastLevelEl.value = preset.contrast;
    saturationLevelEl.value = preset.saturation;
    brightnessLevelEl.value = preset.brightness;

    qualityValueEl.textContent = preset.quality;
    aeLevelValueEl.textContent = preset.ae > 0 ? "+" + preset.ae : preset.ae;

    contrastLevelValueEl.textContent =
      preset.contrast > 0 ? "+" + preset.contrast : preset.contrast;

    saturationLevelValueEl.textContent =
      preset.saturation > 0 ? "+" + preset.saturation : preset.saturation;

    brightnessLevelValueEl.textContent =
      preset.brightness > 0 ? "+" + preset.brightness : preset.brightness;

    await applyConfig();

    status.textContent = "Preset applicato: " + presetName;
  } catch (err) {
    status.textContent = "Errore caricamento preset: " + err;
  }
});

deletePresetBtn.addEventListener("click", async () => {
  const presetName = presetSelect.value;

  if (!presetName) return;

  try {
    const res = await fetch("/preset/delete", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        name: presetName,
      }),
    });

    const data = await res.json();

    if (!data.ok) {
      status.textContent = "Errore eliminazione: " + data.error;
      return;
    }

    await loadPresetList();

    status.textContent = "Preset eliminato: " + presetName;
  } catch (err) {
    status.textContent = "Errore web: " + err;
  }
});

savePresetBtn.addEventListener("click", async () => {
  const presetName = prompt("Nome preset:");

  if (!presetName) return;

  try {
    const res = await fetch("/preset/save", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        name: presetName,
        framesize: framesizeEl.value,
        quality: Number(qualityEl.value),
        ae: Number(aeLevelEl.value),
        contrast: Number(contrastLevelEl.value),
        saturation: Number(saturationLevelEl.value),
        brightness: Number(brightnessLevelEl.value),
      }),
    });

    const data = await res.json();

    if (!data.ok) {
      status.textContent = "Errore salvataggio: " + data.error;
      return;
    }

    await loadPresetList();

    status.textContent = "Preset salvato.";
  } catch (err) {
    status.textContent = "Errore web: " + err;
  }
});

if (saveEspHostBtn) {
  saveEspHostBtn.addEventListener("click", saveEspHost);
}

if (espHostEl) {
  espHostEl.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      saveEspHost();
    }
  });
}

document.addEventListener("DOMContentLoaded", () => {
  loadEspHost();
  loadPresetList();
  checkExistingFrame();
});
