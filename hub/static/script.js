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

    status.textContent = "Frame salvato correttamente.";
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

checkExistingFrame();
