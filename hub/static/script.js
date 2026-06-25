const statusEl = document.getElementById("status");
const img = document.getElementById("frame");
const emptyState = document.getElementById("emptyState");
const captureBtn = document.getElementById("captureBtn");
const sendEspNowBtn = document.getElementById("sendEspNowBtn");
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
const lidarBaseDistanceEl = document.getElementById("lidarBaseDistance");
const lidarCurrentDistanceEl = document.getElementById("lidarCurrentDistance");
const lidarStatusEl = document.getElementById("lidarStatus");
const lidarEnabledToggle = document.getElementById("lidarEnabledToggle");
const lidarBaseMinusBtn = document.getElementById("lidarBaseMinusBtn");
const lidarBasePlusBtn = document.getElementById("lidarBasePlusBtn");
const applyLidarBaseBtn = document.getElementById("applyLidarBaseBtn");
const setLidarBaseBtn = document.getElementById("setLidarBaseBtn");
const lidarSampleCountEl = document.getElementById("lidarSampleCount");
const lidarSampleDelayEl = document.getElementById("lidarSampleDelay");
const applyLidarSampleConfigBtn = document.getElementById("applyLidarSampleConfigBtn");
const lidarPostFrameDelayEl = document.getElementById("lidarPostFrameDelay");
const tCaptureEl = document.getElementById("tCapture");
const tTransitEl = document.getElementById("tTransit");
const tTotalEl = document.getElementById("tTotal");

const LIDAR_OUT_OF_RANGE_DISTANCE_MM = 8191;

let lastFrameCounter = 0;
let eventSource = null;
let lidarPollInFlight = false;
let globalBusy = false;

function setText(element, text) {
  if (element) element.textContent = text;
}

function signedLabel(value) {
  const n = Number(value);
  return n > 0 ? "+" + n : String(n);
}

function setStatus(message) {
  setText(statusEl, message);
}

function setBusy(isBusy) {
  globalBusy = isBusy;
  const controls = Array.from(document.querySelectorAll("button, input, select"));
  controls.forEach((el) => {
    if (el) el.disabled = isBusy;
  });
}

function refuseIfBusy() {
  if (!globalBusy) return false;
  setStatus("Attendi: comunicazione ESP in corso.");
  return true;
}

function bindRangeValue(input, output, formatter = String) {
  if (!input || !output) return;

  const update = () => {
    output.textContent = formatter(input.value);
  };

  input.addEventListener("input", update);
  update();
}

function showEmpty() {
  if (img) {
    img.style.display = "none";
    img.removeAttribute("src");
  }
  if (emptyState) emptyState.style.display = "grid";
}

function showFrame() {
  if (emptyState) emptyState.style.display = "none";
  if (img) {
    img.style.display = "block";
    img.src = "/latest.jpg?t=" + Date.now();
  }
}

function formatLidarDistance(distanceMm) {
  if (distanceMm == null) return "-";
  if (Number(distanceMm) >= LIDAR_OUT_OF_RANGE_DISTANCE_MM) return "fuori range";
  return String(distanceMm) + " mm";
}

function updateFrameUi(data, sourceLabel) {
  if (!data || !data.ok) return;

  if (typeof data.counter === "number") {
    lastFrameCounter = Math.max(lastFrameCounter, data.counter);
  }

  const timing = data.timing || {};
  const cap = timing.esp_capture_ms ?? "-";
  const transit = timing.network_transit_ms ?? "-";
  const total = timing.total_ms ?? "-";
  const reason = data.reason || "manual";

  setText(tCaptureEl, cap + " ms");
  setText(tTransitEl, transit + " ms");
  setText(tTotalEl, total + " ms");
  setText(filenameEl, "File: " + (data.filename || "-"));
  setText(bytesEl, "Dimensione: " + (data.bytes || "-") + " bytes");

  if (reason === "lidar") {
    const distance = data.distance_mm != null ? " · distanza " + data.distance_mm + " mm" : "";
    setStatus("Frame LiDAR ricevuto" + distance + ".");
  } else if (sourceLabel) {
    setStatus(sourceLabel);
  }

  showFrame();
}

function connectFrameEvents() {
  if (!window.EventSource || eventSource) return;

  eventSource = new EventSource("/events");

  eventSource.addEventListener("frame", (event) => {
    try {
      const data = JSON.parse(event.data || "{}");
      if (typeof data.counter === "number" && data.counter <= lastFrameCounter) return;
      updateFrameUi(data);
      setBusy(false);
    } catch (err) {
      // Evento non valido: lo ignoriamo senza disturbare la UI.
    }
  });

  eventSource.addEventListener("busy", (event) => {
    try {
      const data = JSON.parse(event.data || "{}");
      setBusy(Boolean(data.busy));
      if (data.busy) setStatus("ESP sta inviando un frame: controlli bloccati.");
    } catch (err) {
      // Evento non valido: lo ignoriamo.
    }
  });

  eventSource.onerror = () => {
    // EventSource riconnette da solo. Non facciamo polling /frame-status.
  };
}

async function loadEspHost() {
  if (!espHostEl) return;

  try {
    const res = await fetch("/esp-host", { cache: "no-store" });
    const data = await res.json();

    if (!data.ok) {
      setText(espHostStatusEl, "Errore lettura IP ESP.");
      return;
    }

    espHostEl.value = data.host || "";
    setText(espPortEl, String(data.port || "-"));
    setText(
      espHostStatusEl,
      data.host ? "ESP " + data.host + ":" + data.port : "IP ESP non configurato.",
    );
  } catch (err) {
    setText(espHostStatusEl, "Errore web IP ESP: " + err);
  }
}

async function saveEspHost() {
  if (refuseIfBusy()) return;
  if (!espHostEl) return;

  const host = espHostEl.value.trim();

  if (!host) {
    setText(espHostStatusEl, "Inserisci l'IP dell'ESP.");
    return;
  }

  setBusy(true);
  setText(espHostStatusEl, "Salvataggio IP ESP...");

  try {
    const res = await fetch("/esp-host", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ host }),
    });
    const data = await res.json();

    if (!data.ok) {
      setText(espHostStatusEl, "Errore IP ESP: " + data.error);
      return;
    }

    espHostEl.value = data.host;
    setText(
      espHostStatusEl,
      "ESP " + data.host + ":" + data.port + (data.persisted ? "" : " · non salvato su .env"),
    );
    setStatus("Connessione ESP resettata. Il prossimo comando userà il nuovo IP.");
  } catch (err) {
    setText(espHostStatusEl, "Errore web IP ESP: " + err);
  } finally {
    setBusy(false);
  }
}

function updateLidarUi(data, message) {
  if (!data || !data.ok) return;

  setText(lidarCurrentDistanceEl, formatLidarDistance(data.current_mm));

  if (lidarEnabledToggle && typeof data.enabled === "boolean") {
    lidarEnabledToggle.checked = data.enabled;
  }

  if (lidarBaseDistanceEl && data.base_mm != null && document.activeElement !== lidarBaseDistanceEl) {
    lidarBaseDistanceEl.value = data.base_mm;
  }

  if (lidarSampleCountEl && data.good_sample_count != null && document.activeElement !== lidarSampleCountEl) {
    lidarSampleCountEl.value = data.good_sample_count;
  }

  if (lidarSampleDelayEl && data.sample_delay_ms != null && document.activeElement !== lidarSampleDelayEl) {
    lidarSampleDelayEl.value = data.sample_delay_ms;
  }

  if (lidarPostFrameDelayEl && data.post_frame_delay_ms != null && document.activeElement !== lidarPostFrameDelayEl) {
    lidarPostFrameDelayEl.value = data.post_frame_delay_ms;
  }

  const base = data.baseline_ready ? formatLidarDistance(data.base_mm) : "non impostata";
  const current = data.current_valid ? formatLidarDistance(data.current_mm) : "-";
  const threshold = data.baseline_ready && data.threshold_mm != null ? formatLidarDistance(data.threshold_mm) : "-";
  const thresholdPercent = data.threshold_percent != null ? data.threshold_percent + "%" : "80%";
  const sampleCount = data.good_sample_count != null ? data.good_sample_count : "-";
  const sampleDelay = data.sample_delay_ms != null ? data.sample_delay_ms + " ms" : "-";
  const postFrameDelay = data.post_frame_delay_ms != null ? data.post_frame_delay_ms + " ms" : "-";
  const enabledText = data.enabled === false ? "OFF" : "ON";

  setText(
    lidarStatusEl,
    message ||
      "LiDAR: " +
        enabledText +
        " · Baseline: " +
        base +
        " · Corrente: " +
        current +
        " · Soglia trigger: " +
        threshold +
        " (" +
        thresholdPercent +
        ") · Campioni: " +
        sampleCount +
        " × " +
        sampleDelay +
        " · Stop dopo frame: " +
        postFrameDelay,
  );
}

function getLidarBaseInput() {
  if (!lidarBaseDistanceEl) throw new Error("Input baseline LiDAR non trovato.");

  const rounded = Math.round(Number(lidarBaseDistanceEl.value));

  if (!Number.isFinite(rounded) || rounded <= 0 || rounded > LIDAR_OUT_OF_RANGE_DISTANCE_MM) {
    throw new Error("La baseline deve essere tra 1 e " + LIDAR_OUT_OF_RANGE_DISTANCE_MM + " mm.");
  }

  return rounded;
}

function getLidarSampleConfigInput() {
  if (!lidarSampleCountEl || !lidarSampleDelayEl || !lidarPostFrameDelayEl) {
    throw new Error("Input timing LiDAR non trovati.");
  }

  const sampleCount = Math.round(Number(lidarSampleCountEl.value));
  const delayMs = Math.round(Number(lidarSampleDelayEl.value));
  const postFrameDelayMs = Math.round(Number(lidarPostFrameDelayEl.value));

  if (!Number.isFinite(sampleCount) || sampleCount < 1 || sampleCount > 25) {
    throw new Error("Le misure valide devono essere tra 1 e 25.");
  }

  if (!Number.isFinite(delayMs) || delayMs < 0 || delayMs > 500) {
    throw new Error("Il delay LiDAR deve essere tra 0 e 500 ms.");
  }

  if (!Number.isFinite(postFrameDelayMs) || postFrameDelayMs < 0 || postFrameDelayMs > 30000) {
    throw new Error("Il delay post-frame LiDAR deve essere tra 0 e 30000 ms.");
  }

  return { sampleCount, delayMs, postFrameDelayMs };
}

async function loadLidarStatus(silent = false) {
  if (!lidarStatusEl || lidarPollInFlight) return;
  lidarPollInFlight = true;

  try {
    const res = await fetch("/lidar", { cache: "no-store" });
    const data = await res.json();

    if (!data.ok) {
      if (!silent) setText(lidarStatusEl, "Errore LiDAR: " + data.error);
      return;
    }

    updateLidarUi(data);
  } catch (err) {
    if (!silent) setText(lidarStatusEl, "Errore web LiDAR: " + err);
  } finally {
    lidarPollInFlight = false;
  }
}

async function updateLidarBase(baseMm, messagePrefix = "Baseline LiDAR aggiornata") {
  setBusy(true);
  setText(lidarStatusEl, "Aggiornamento baseline LiDAR...");

  try {
    const res = await fetch("/lidar/base", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ base_mm: baseMm }),
    });
    const data = await res.json();

    if (!data.ok) {
      setText(lidarStatusEl, "Errore LiDAR: " + data.error);
      return;
    }

    updateLidarUi(data, messagePrefix + ": " + formatLidarDistance(data.base_mm));
  } catch (err) {
    setText(lidarStatusEl, "Errore web LiDAR: " + err);
  } finally {
    setBusy(false);
  }
}

async function applyLidarBase() {
  if (refuseIfBusy()) return;
  try {
    await updateLidarBase(getLidarBaseInput());
  } catch (err) {
    setText(lidarStatusEl, err.message || String(err));
  }
}

async function changeLidarBase(delta) {
  if (!lidarBaseDistanceEl) return;

  try {
    const current = getLidarBaseInput();
    const next = Math.min(LIDAR_OUT_OF_RANGE_DISTANCE_MM, Math.max(1, current + delta));
    lidarBaseDistanceEl.value = next;
    setText(lidarStatusEl, "Baseline modificata localmente: premi Aggiorna baseline per inviarla all'ESP.");
  } catch (err) {
    setText(lidarStatusEl, err.message || String(err));
  }
}

async function setLidarBaseFromCurrent() {
  if (refuseIfBusy()) return;
  setBusy(true);
  setText(lidarStatusEl, "Lettura distanza LiDAR corrente...");

  try {
    const res = await fetch("/lidar/base/current", { method: "POST" });
    const data = await res.json();

    if (!data.ok) {
      setText(lidarStatusEl, "Errore LiDAR: " + data.error);
      return;
    }

    updateLidarUi(data, "Baseline impostata dalla distanza corrente: " + formatLidarDistance(data.base_mm));
  } catch (err) {
    setText(lidarStatusEl, "Errore web LiDAR: " + err);
  } finally {
    setBusy(false);
  }
}

async function setLidarEnabled(enabled) {
  if (refuseIfBusy()) return;
  setBusy(true);
  setText(lidarStatusEl, enabled ? "Riattivazione trigger LiDAR..." : "Disattivazione trigger LiDAR...");

  try {
    const res = await fetch("/lidar/enabled", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled }),
    });
    const data = await res.json();

    if (!data.ok) {
      setText(lidarStatusEl, "Errore toggle LiDAR: " + data.error);
      if (lidarEnabledToggle) lidarEnabledToggle.checked = !enabled;
      return;
    }

    updateLidarUi(data, enabled ? "Trigger LiDAR riattivato." : "Trigger LiDAR disattivato.");
  } catch (err) {
    setText(lidarStatusEl, "Errore web toggle LiDAR: " + err);
    if (lidarEnabledToggle) lidarEnabledToggle.checked = !enabled;
  } finally {
    setBusy(false);
  }
}

async function applyLidarSampleConfig() {
  if (refuseIfBusy()) return;
  let config;

  try {
    config = getLidarSampleConfigInput();
  } catch (err) {
    setText(lidarStatusEl, err.message || String(err));
    return;
  }

  setBusy(true);
  setText(lidarStatusEl, "Aggiornamento timing LiDAR...");

  try {
    const res = await fetch("/lidar/sample-config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        sample_count: config.sampleCount,
        delay_ms: config.delayMs,
      }),
    });
    const data = await res.json();

    if (!data.ok) {
      setText(lidarStatusEl, "Errore LiDAR: " + data.error);
      return;
    }

    const delayRes = await fetch("/lidar/post-frame-delay", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ delay_ms: config.postFrameDelayMs }),
    });
    const delayData = await delayRes.json();

    if (!delayData.ok) {
      setText(lidarStatusEl, "Errore delay post-frame LiDAR: " + delayData.error);
      return;
    }

    updateLidarUi(
      delayData,
      "Timing LiDAR aggiornato: " +
        data.good_sample_count +
        " misure, delay " +
        data.sample_delay_ms +
        " ms, stop post-frame " +
        delayData.post_frame_delay_ms +
        " ms",
    );
  } catch (err) {
    setText(lidarStatusEl, "Errore web LiDAR: " + err);
  } finally {
    setBusy(false);
  }
}


async function loadDeviceStatus() {
  try {
    const res = await fetch("/device-status", { cache: "no-store" });
    const data = await res.json();
    if (!data.ok || !data.snapshot) return;

    const snapshot = data.snapshot;
    if (snapshot.camera) {
      applyPresetToControls({
        framesize: snapshot.camera.framesize || framesizeEl.value,
        quality: snapshot.camera.quality ?? qualityEl.value,
        ae: snapshot.camera.ae ?? aeLevelEl.value,
        contrast: snapshot.camera.contrast ?? contrastLevelEl.value,
        saturation: snapshot.camera.saturation ?? saturationLevelEl.value,
        brightness: snapshot.camera.brightness ?? brightnessLevelEl.value,
      });
    }

    if (snapshot.lidar) {
      updateLidarUi(snapshot.lidar, "Configurazione ESP caricata all'avvio.");
    }
  } catch (err) {
    // Snapshot non disponibile: la UI resta sui valori di default.
  }
}

async function checkExistingFrame() {
  try {
    const res = await fetch("/has-frame", { cache: "no-store" });
    const data = await res.json();

    if (data.exists) {
      showFrame();
      setStatus("Ultimo frame caricato.");
    } else {
      showEmpty();
    }
  } catch (err) {
    showEmpty();
    setStatus("Pronto.");
  }
}

async function applyConfig() {
  if (refuseIfBusy()) return;
  setBusy(true);
  setStatus("Invio configurazione camera...");

  try {
    const payload = {
      framesize: framesizeEl.value,
      quality: Number(qualityEl.value),
      ae: Number(aeLevelEl.value),
      contrast: Number(contrastLevelEl.value),
      saturation: Number(saturationLevelEl.value),
      brightness: Number(brightnessLevelEl.value),
    };

    const res = await fetch("/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const data = await res.json();

    if (!data.ok) {
      setStatus("Errore configurazione: " + data.error);
      return;
    }

    setStatus(
      "Configurazione applicata: " +
        data.framesize +
        ", quality " +
        data.quality +
        ", AE " +
        signedLabel(data.ae) +
        ", contrasto " +
        signedLabel(data.contrast) +
        ", saturazione " +
        signedLabel(data.saturation) +
        ", luminosità " +
        signedLabel(data.brightness),
    );
  } catch (err) {
    setStatus("Errore web config: " + err);
  } finally {
    setBusy(false);
  }
}

async function sendLastFrameEspNow() {
  if (refuseIfBusy()) return;
  setBusy(true);
  setStatus("Invio ultimo frame via ESP-NOW...");

  try {
    const res = await fetch("/espnow/send-last", { method: "POST" });
    const data = await res.json();

    if (!data.ok) {
      setStatus("Errore ESP-NOW: " + data.error);
      return;
    }

    const chunks = data.chunk_count != null ? " · " + data.chunk_count + " chunk" : "";
    const bytes = data.bytes != null ? " · " + data.bytes + " bytes" : "";
    setStatus("Ultimo frame inviato via ESP-NOW" + bytes + chunks + ".");
  } catch (err) {
    setStatus("Errore web ESP-NOW: " + err);
  } finally {
    setBusy(false);
  }
}

async function captureFrame() {
  if (refuseIfBusy()) return;
  setBusy(true);
  setStatus("Richiesta frame in corso...");

  try {
    const res = await fetch("/capture", { method: "POST" });
    const data = await res.json();

    if (!data.ok) {
      setStatus("Errore: " + data.error);
      return;
    }

    updateFrameUi(data, "Frame manuale ricevuto con successo.");
  } catch (err) {
    setStatus("Errore web: " + err);
  } finally {
    setBusy(false);
  }
}

async function loadPresetList() {
  if (!presetSelect) return;

  try {
    const res = await fetch("/presets", { cache: "no-store" });
    const data = await res.json();
    presetSelect.innerHTML = "";

    if (!data.ok) {
      presetSelect.appendChild(new Option("Errore caricamento preset", ""));
      setStatus("Errore caricamento preset: " + data.error);
      return;
    }

    const presets = Array.isArray(data.presets) ? data.presets : [];

    if (presets.length === 0) {
      presetSelect.appendChild(new Option("Nessun preset salvato", ""));
      return;
    }

    presetSelect.appendChild(new Option("Seleziona preset", ""));
    presets.forEach((preset) => presetSelect.appendChild(new Option(preset.name, preset.name)));
  } catch (err) {
    presetSelect.innerHTML = "";
    presetSelect.appendChild(new Option("Errore caricamento", ""));
    setStatus("Errore caricamento preset: " + err);
  }
}

function applyPresetToControls(preset) {
  framesizeEl.value = preset.framesize;
  qualityEl.value = preset.quality;
  aeLevelEl.value = preset.ae;
  contrastLevelEl.value = preset.contrast;
  saturationLevelEl.value = preset.saturation;
  brightnessLevelEl.value = preset.brightness;

  setText(qualityValueEl, preset.quality);
  setText(aeLevelValueEl, signedLabel(preset.ae));
  setText(contrastLevelValueEl, signedLabel(preset.contrast));
  setText(saturationLevelValueEl, signedLabel(preset.saturation));
  setText(brightnessLevelValueEl, signedLabel(preset.brightness));
}

async function handlePresetChange() {
  if (refuseIfBusy()) return;
  const presetName = presetSelect.value;
  if (!presetName) return;

  try {
    setStatus("Caricamento preset...");
    const res = await fetch("/preset/" + encodeURIComponent(presetName), { cache: "no-store" });
    const data = await res.json();

    if (!data.ok) {
      setStatus("Errore caricamento preset: " + data.error);
      return;
    }

    applyPresetToControls(data.preset);
    await applyConfig();
    setStatus("Preset applicato: " + presetName);
  } catch (err) {
    setStatus("Errore caricamento preset: " + err);
  }
}

async function deletePreset() {
  if (refuseIfBusy()) return;
  const presetName = presetSelect.value;
  if (!presetName) return;

  try {
    const res = await fetch("/preset/delete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: presetName }),
    });
    const data = await res.json();

    if (!data.ok) {
      setStatus("Errore eliminazione: " + data.error);
      return;
    }

    await loadPresetList();
    setStatus("Preset eliminato: " + presetName);
  } catch (err) {
    setStatus("Errore web: " + err);
  }
}

async function savePreset() {
  if (refuseIfBusy()) return;
  const presetName = prompt("Nome preset:");
  if (!presetName) return;

  try {
    const res = await fetch("/preset/save", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
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
      setStatus("Errore salvataggio: " + data.error);
      return;
    }

    await loadPresetList();
    if (presetSelect) presetSelect.value = presetName;
    setStatus("Preset salvato: " + presetName);
  } catch (err) {
    setStatus("Errore web: " + err);
  }
}


function initTabs() {
  const tabButtons = Array.from(document.querySelectorAll("[data-tab]"));
  const tabPanels = Array.from(document.querySelectorAll("[data-tab-panel]"));

  if (tabButtons.length === 0 || tabPanels.length === 0) return;

  const activateTab = (tabName) => {
    tabButtons.forEach((button) => {
      const isActive = button.dataset.tab === tabName;
      button.classList.toggle("is-active", isActive);
      button.setAttribute("aria-selected", isActive ? "true" : "false");
    });

    tabPanels.forEach((panel) => {
      const isActive = panel.dataset.tabPanel === tabName;
      panel.classList.toggle("is-active", isActive);
      panel.hidden = !isActive;
    });
  };

  tabButtons.forEach((button) => {
    button.addEventListener("click", () => activateTab(button.dataset.tab));
  });
}

function bindEvents() {
  bindRangeValue(qualityEl, qualityValueEl);
  bindRangeValue(aeLevelEl, aeLevelValueEl, signedLabel);
  bindRangeValue(contrastLevelEl, contrastLevelValueEl, signedLabel);
  bindRangeValue(saturationLevelEl, saturationLevelValueEl, signedLabel);
  bindRangeValue(brightnessLevelEl, brightnessLevelValueEl, signedLabel);

  if (captureBtn) captureBtn.addEventListener("click", captureFrame);
  if (sendEspNowBtn) sendEspNowBtn.addEventListener("click", sendLastFrameEspNow);
  if (applyBtn) applyBtn.addEventListener("click", applyConfig);
  if (saveEspHostBtn) saveEspHostBtn.addEventListener("click", saveEspHost);
  if (applyLidarBaseBtn) applyLidarBaseBtn.addEventListener("click", applyLidarBase);
  if (setLidarBaseBtn) setLidarBaseBtn.addEventListener("click", setLidarBaseFromCurrent);
  if (applyLidarSampleConfigBtn) applyLidarSampleConfigBtn.addEventListener("click", applyLidarSampleConfig);
  if (lidarEnabledToggle) lidarEnabledToggle.addEventListener("change", () => setLidarEnabled(lidarEnabledToggle.checked));
  if (lidarBaseMinusBtn) lidarBaseMinusBtn.addEventListener("click", () => changeLidarBase(-5));
  if (lidarBasePlusBtn) lidarBasePlusBtn.addEventListener("click", () => changeLidarBase(5));
  if (presetSelect) presetSelect.addEventListener("change", handlePresetChange);
  if (deletePresetBtn) deletePresetBtn.addEventListener("click", deletePreset);
  if (savePresetBtn) savePresetBtn.addEventListener("click", savePreset);

  if (lidarBaseDistanceEl) {
    lidarBaseDistanceEl.addEventListener("keydown", (event) => {
      if (event.key === "Enter") applyLidarBase();
    });
  }

  [lidarSampleCountEl, lidarSampleDelayEl, lidarPostFrameDelayEl].forEach((el) => {
    if (!el) return;
    el.addEventListener("keydown", (event) => {
      if (event.key === "Enter") applyLidarSampleConfig();
    });
  });

  if (espHostEl) {
    espHostEl.addEventListener("keydown", (event) => {
      if (event.key === "Enter") saveEspHost();
    });
  }
}

document.addEventListener("DOMContentLoaded", () => {
  initTabs();
  bindEvents();
  loadEspHost();
  if (presetSelect) loadPresetList();
  loadDeviceStatus();
  checkExistingFrame();
  connectFrameEvents();
});

window.applyConfig = applyConfig;
window.captureFrame = captureFrame;
window.sendLastFrameEspNow = sendLastFrameEspNow;
window.connectFrameEvents = connectFrameEvents;
