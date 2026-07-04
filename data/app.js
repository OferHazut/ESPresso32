// Pressure sensing/monitoring disabled for now (hardware not connected).
// Flip to true and uncomment the pressure <section> in index.html to re-enable.
var PRESSURE_ENABLED = false;

var tempStatusEl = document.getElementById("tempStatus");
var pidDiagStatusEl = document.getElementById("pidDiagStatus");
var pressureStatusEl = PRESSURE_ENABLED ? document.getElementById("pressureStatus") : null;
var metaEl = document.getElementById("meta");
var historyLenEl = document.getElementById("historyLen");
var historyLenValEl = document.getElementById("historyLenVal");
var yMinEl = document.getElementById("yMin");
var yMaxEl = document.getElementById("yMax");
var ssrSetpointEl = document.getElementById("ssrSetpoint");
var ssrDotEl = document.getElementById("ssrIndicator");
var ssrLabelEl = document.getElementById("ssrLabel");
var pidOutputEl = document.getElementById("pidOutput");
var pidKpEl = document.getElementById("pidKp");
var pidKiEl = document.getElementById("pidKi");
var pidKdEl = document.getElementById("pidKd");
var pidWindowEl = document.getElementById("pidWindow");
var pidDerivativeKEl = document.getElementById("pidDerivativeK");
var pidDerivativeSourceEl = document.getElementById("pidDerivativeSource");
var pidDerivativeIntervalEl = document.getElementById("pidDerivativeInterval");
var pidDcCapEl = document.getElementById("pidDcCap");
var pidDcCapDisableSlopeEl = document.getElementById("pidDcCapDisableSlope");
var pidDcCapDisableHoldEl = document.getElementById("pidDcCapDisableHold");
var pidApplyEl = document.getElementById("pidApply");
var pidResetEl = document.getElementById("pidReset");
var pidStatusLabelEl = document.getElementById("pidStatusLabel");
var integratorStatusLabelEl = document.getElementById("integratorStatusLabel");
var dcCapBypassLabelEl = document.getElementById("dcCapBypassLabel");
var slopeReadoutEl = document.getElementById("slopeReadout");
var sensorSampleIntervalEl = document.getElementById("sensorSampleInterval");
var sensorMedianWindowEl = document.getElementById("sensorMedianWindow");
var sensorApplyEl = document.getElementById("sensorApply");
var sensorLatencyReadoutEl = document.getElementById("sensorLatencyReadout");

// Fields the user can edit. A field stays "dirty" from the moment it's
// edited until the corresponding Apply succeeds, so a periodic refresh()
// can never clobber an in-progress edit (even after the field loses focus).
var dirtyFields = new Set();
[
  "pidKp", "pidKi", "pidKd", "pidWindow", "pidDerivativeK", "pidDerivativeSource", "pidDerivativeInterval", "pidDcCap",
  "pidDcCapDisableSlope", "pidDcCapDisableHold",
  "ssrSetpoint", "sensorSampleInterval", "sensorMedianWindow"
].forEach(function (id) {
  var el = document.getElementById(id);
  var markDirty = function () { dirtyFields.add(id); };
  el.addEventListener("input", markDirty);
  el.addEventListener("change", markDirty);
});

function isFresh(el, id) {
  return document.activeElement !== el && !dirtyFields.has(id);
}

var baseScaleX = { ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 10 } };

var tempChartOpts = {
  responsive: true,
  maintainAspectRatio: false,
  animation: false,
  scales: {
    x: baseScaleX,
    y: { beginAtZero: false, min: -10, max: 200, position: "left" },  // updated live by Y-axis inputs
    yDuty: {
      beginAtZero: true, min: 0, max: 100, position: "right",
      grid: { drawOnChartArea: false },
      title: { display: true, text: "Duty %" }
    }
  }
};

var pressureChartOpts = {
  responsive: true,
  maintainAspectRatio: false,
  animation: false,
  scales: {
    x: baseScaleX,
    y: { beginAtZero: false }
  }
};

var tempChart = new Chart(document.getElementById("tempChart"), {
  type: "line",
  data: {
    labels: [],
    datasets: [
      // `order` controls draw order: lower values are drawn last (on top).
      // Probe (raw, noisiest) sits at the back; Filtered is in front.
      { label: "Probe °C", data: [], borderColor: "#d97706", borderWidth: 1.5, tension: 0.2, pointRadius: 0, order: 3 },
      { label: "Median °C", data: [], borderColor: "#3b82f6", borderWidth: 1.5, tension: 0.2, pointRadius: 0, order: 2 },
      { label: "Filtered °C", data: [], borderColor: "#0f766e", borderWidth: 2, tension: 0.2, pointRadius: 0, order: 1 },
      { label: "Duty %", data: [], borderColor: "#9333ea", borderWidth: 1.5, tension: 0.1, pointRadius: 0, yAxisID: "yDuty", order: 4 }
    ]
  },
  options: tempChartOpts
});

var pidDiagChartOpts = {
  responsive: true,
  maintainAspectRatio: false,
  animation: false,
  scales: {
    x: baseScaleX,
    yError: { beginAtZero: false, position: "left", title: { display: true, text: "Error °C" } },
    yTerms: {
      beginAtZero: false, position: "right",
      grid: { drawOnChartArea: false },
      title: { display: true, text: "P / I / D terms (duty %)" }
    }
  }
};

var pidDiagChart = new Chart(document.getElementById("pidDiagChart"), {
  type: "line",
  data: {
    labels: [],
    datasets: [
      { label: "Error °C", data: [], borderColor: "#dc2626", borderWidth: 1.5, tension: 0.2, pointRadius: 0, yAxisID: "yError" },
      { label: "P term", data: [], borderColor: "#16a34a", borderWidth: 1.5, tension: 0.1, pointRadius: 0, yAxisID: "yTerms" },
      { label: "I term", data: [], borderColor: "#d97706", borderWidth: 1.5, tension: 0.1, pointRadius: 0, yAxisID: "yTerms" },
      { label: "D term", data: [], borderColor: "#2563eb", borderWidth: 1.5, tension: 0.1, pointRadius: 0, yAxisID: "yTerms" }
    ]
  },
  options: pidDiagChartOpts
});

var pressureChart = PRESSURE_ENABLED ? new Chart(document.getElementById("pressureChart"), {
  type: "line",
  data: {
    labels: [],
    datasets: [
      { label: "Raw bar", data: [], borderColor: "#2563eb", borderWidth: 1.5, tension: 0.2, pointRadius: 0 },
      { label: "Median bar", data: [], borderColor: "#7c3aed", borderWidth: 2, tension: 0.2, pointRadius: 0 }
    ]
  },
  options: pressureChartOpts
}) : null;

// Cached raw history from the server (full 30-second buffer).
var cachedTempPts = null;
var cachedPressurePts = null;

function windowSlice(pts) {
  var windowMs = parseInt(historyLenEl.value) * 1000;
  var latestTs = pts[pts.length - 1][0];
  return pts.filter(function (p) { return p[0] >= latestTs - windowMs; });
}

function timeLabels(pts) {
  var latestTs = pts[pts.length - 1][0];
  return pts.map(function (p) {
    return ((p[0] - latestTs) / 1000).toFixed(1) + "s";
  });
}

function renderTemp(pts) {
  if (!pts || pts.length === 0) return;
  var slice = windowSlice(pts);
  if (slice.length === 0) return;

  tempChart.data.labels = timeLabels(slice);
  tempChart.data.datasets[0].data = slice.map(function (p) { return p[1]; });
  tempChart.data.datasets[1].data = slice.map(function (p) { return p[2]; });
  tempChart.data.datasets[2].data = slice.map(function (p) { return p[3]; });
  tempChart.data.datasets[3].data = slice.map(function (p) { return p[4]; });
  tempChart.update();

  var latest = slice[slice.length - 1];
  var probeVals = slice.map(function (p) { return p[1]; });
  var minT = Math.min.apply(null, probeVals);
  var maxT = Math.max.apply(null, probeVals);
  tempStatusEl.textContent =
    "Probe " + latest[1].toFixed(1) + " °C" +
    " | Median " + latest[2].toFixed(1) + " °C" +
    " | Filtered " + latest[3].toFixed(1) + " °C" +
    " | Window min " + minT.toFixed(1) + " / max " + maxT.toFixed(1) + " °C";
  metaEl.textContent =
    slice.length + " points shown | " + historyLenEl.value + "s window" +
    " | Uptime " + Math.round(latest[0] / 1000) + "s";
}

function renderPidDiag(pts) {
  if (!pts || pts.length === 0) return;
  var slice = windowSlice(pts);
  if (slice.length === 0) return;

  // points are [timestamp, probe, median, filtered, duty, error, pTerm, iTerm, dTerm, pidEnabled, integratorEnabled]
  pidDiagChart.data.labels = timeLabels(slice);
  pidDiagChart.data.datasets[0].data = slice.map(function (p) { return p[5]; });
  pidDiagChart.data.datasets[1].data = slice.map(function (p) { return p[6]; });
  pidDiagChart.data.datasets[2].data = slice.map(function (p) { return p[7]; });
  pidDiagChart.data.datasets[3].data = slice.map(function (p) { return p[8]; });
  pidDiagChart.update();

  var latest = slice[slice.length - 1];
  pidDiagStatusEl.textContent =
    "Error " + latest[5].toFixed(2) + " °C | P " + latest[6].toFixed(1) +
    " | I " + latest[7].toFixed(1) + " | D " + latest[8].toFixed(1);

  // Update PID and integrator status badges
  updatePidStatusBadges(latest[9], latest[10]);
}

function updatePidStatusBadges(pidEnabled, integratorEnabled) {
  // Update PID status badge
  if (pidEnabled) {
    pidStatusLabelEl.textContent = "PID: ON";
    pidStatusLabelEl.className = "pid-status-badge pid-on";
  } else {
    pidStatusLabelEl.textContent = "PID: OFF";
    pidStatusLabelEl.className = "pid-status-badge pid-off";
  }
  
  // Update integrator status badge
  if (integratorEnabled) {
    integratorStatusLabelEl.textContent = "Integrator: ON";
    integratorStatusLabelEl.className = "pid-status-badge integrator-on";
  } else {
    integratorStatusLabelEl.textContent = "Integrator: OFF";
    integratorStatusLabelEl.className = "pid-status-badge integrator-off";
  }
}

function renderPressure(pts) {
  if (!PRESSURE_ENABLED || !pts || pts.length === 0) return;
  var slice = windowSlice(pts);
  if (slice.length === 0) return;

  pressureChart.data.labels = timeLabels(slice);
  pressureChart.data.datasets[0].data = slice.map(function (p) { return p[1]; });
  pressureChart.data.datasets[1].data = slice.map(function (p) { return p[2]; });
  pressureChart.update();

  var latest2 = slice[slice.length - 1];
  pressureStatusEl.textContent =
    "Pressure " + latest2[1].toFixed(2) + " bar | Median " + latest2[2].toFixed(2) + " bar";
}

historyLenEl.addEventListener("input", function () {
  historyLenValEl.textContent = historyLenEl.value;
  renderTemp(cachedTempPts);
  renderPidDiag(cachedTempPts);
  if (PRESSURE_ENABLED) renderPressure(cachedPressurePts);
});

function applyYAxis() {
  var mn = parseFloat(yMinEl.value);
  var mx = parseFloat(yMaxEl.value);
  if (isNaN(mn) || isNaN(mx) || mn >= mx) return;
  tempChart.options.scales.y.min = mn;
  tempChart.options.scales.y.max = mx;
  tempChart.update();
}
yMinEl.addEventListener("change", applyYAxis);
yMaxEl.addEventListener("change", applyYAxis);

// Updated whenever /api/pid responds — used to translate the live duty %
// into an actual on-time, so you can see whether it's comfortably above
// the polling/blocking floor (see PID tuning notes on minimum on-time).
var lastKnownWindowMs = 3000;

function updateLatencyReadout() {
  var interval = parseFloat(sensorSampleIntervalEl.value);
  var window = parseFloat(sensorMedianWindowEl.value);
  if (isNaN(interval) || isNaN(window)) return;
  sensorLatencyReadoutEl.textContent = "≈ " + Math.round(interval * window) + "ms latency";
}
sensorSampleIntervalEl.addEventListener("input", updateLatencyReadout);
sensorMedianWindowEl.addEventListener("input", updateLatencyReadout);
updateLatencyReadout();

function updateSsrIndicator(on, pidOut) {
  ssrDotEl.className = "ssr-dot " + (on ? "ssr-on" : "ssr-off");
  ssrLabelEl.textContent = on ? "SSR on" : "SSR off";
  if (pidOut !== undefined) {
    var onTimeMs = Math.round(pidOut / 100 * lastKnownWindowMs);
    pidOutputEl.textContent =
      "PID " + pidOut.toFixed(1) + "% (" + onTimeMs + " / " + lastKnownWindowMs + " ms)";
  }
}

var ssrSetpointTimer = null;
ssrSetpointEl.addEventListener("input", function () {
  clearTimeout(ssrSetpointTimer);
  ssrSetpointTimer = setTimeout(function () {
    var sp = parseFloat(ssrSetpointEl.value);
    if (isNaN(sp) || sp < 0 || sp > 160) return;
    fetch("/api/ssr", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: "setpoint=" + sp
    }).then(function (res) {
      if (res.ok) dirtyFields.delete("ssrSetpoint");
    });
  }, 500);
});

pidApplyEl.addEventListener("click", function () {
  var kp = parseFloat(pidKpEl.value);
  var ki = parseFloat(pidKiEl.value);
  var kd = parseFloat(pidKdEl.value);
  var windowMs = parseInt(pidWindowEl.value);
  var derivativeN = parseInt(pidDerivativeKEl.value);
  var derivativeSource = pidDerivativeSourceEl.value;
  var derivativeIntervalMs = parseInt(pidDerivativeIntervalEl.value);
  var dcCap = parseFloat(pidDcCapEl.value);
  var dcCapDisableSlope = parseFloat(pidDcCapDisableSlopeEl.value);
  var dcCapDisableHoldMs = parseInt(pidDcCapDisableHoldEl.value);
  if (isNaN(kp) || isNaN(ki) || isNaN(kd) || isNaN(windowMs) || isNaN(derivativeN) || isNaN(derivativeIntervalMs) || isNaN(dcCap) ||
      isNaN(dcCapDisableSlope) || isNaN(dcCapDisableHoldMs)) return;
  if (kp < 0 || ki < 0 || kd < 0 || windowMs < 250 || derivativeN < 2 || derivativeN > 50 ||
      derivativeIntervalMs < 0 || derivativeIntervalMs > 10000 || dcCap < 1 || dcCap > 100 ||
      dcCapDisableSlope < -20 || dcCapDisableSlope > 0 || dcCapDisableHoldMs < 0 || dcCapDisableHoldMs > 60000) return;
  fetch("/api/pid", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "kp=" + kp + "&ki=" + ki + "&kd=" + kd + "&window_ms=" + windowMs +
      "&derivative_k=" + derivativeN + "&derivative_source=" + derivativeSource +
      "&derivative_interval_ms=" + derivativeIntervalMs + "&dc_cap_percent=" + dcCap +
      "&dc_cap_disable_slope=" + dcCapDisableSlope + "&dc_cap_disable_hold_ms=" + dcCapDisableHoldMs
  }).then(function (res) {
    if (res.ok) {
      ["pidKp", "pidKi", "pidKd", "pidWindow", "pidDerivativeK", "pidDerivativeSource", "pidDerivativeInterval", "pidDcCap",
       "pidDcCapDisableSlope", "pidDcCapDisableHold"].forEach(function (id) {
        dirtyFields.delete(id);
      });
    }
  });
});

sensorApplyEl.addEventListener("click", function () {
  var sampleIntervalMs = parseInt(sensorSampleIntervalEl.value);
  var medianWindow = parseInt(sensorMedianWindowEl.value);
  if (isNaN(sampleIntervalMs) || isNaN(medianWindow)) return;
  if (sampleIntervalMs < 20 || sampleIntervalMs > 1000 || medianWindow < 1 || medianWindow > 10) return;
  fetch("/api/sensor", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "sample_interval_ms=" + sampleIntervalMs + "&median_window=" + medianWindow
  }).then(function (res) {
    if (res.ok) {
      dirtyFields.delete("sensorSampleInterval");
      dirtyFields.delete("sensorMedianWindow");
    }
  });
});

pidResetEl.addEventListener("click", function () {
  if (!confirm("Clear the PID's accumulated integral/derivative memory? Tunings are kept.")) return;
  fetch("/api/pid", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: "reset=1"
  });
});

async function refresh() {
  try {
    var res = await fetch("/api/temperature/history");
    if (!res.ok) throw new Error(res.statusText);
    var data = await res.json();
    if (data.points && data.points.length > 0) {
      cachedTempPts = data.points;
      renderTemp(cachedTempPts);
      renderPidDiag(cachedTempPts);
    } else {
      tempStatusEl.textContent = "Collecting data…";
    }
  } catch (e) {
    tempStatusEl.textContent = "API error: " + e.message;
  }

  try {
    var ssrRes = await fetch("/api/ssr");
    if (ssrRes.ok) {
      var ssrData = await ssrRes.json();
      updateSsrIndicator(ssrData.on, ssrData.pid_output);
      if (isFresh(ssrSetpointEl, "ssrSetpoint")) {
        ssrSetpointEl.value = ssrData.setpoint;
      }
    }
  } catch (e) { /* non-fatal */ }

  try {
    var pidRes = await fetch("/api/pid");
    if (pidRes.ok) {
      var pidData = await pidRes.json();
      if (isFresh(pidKpEl, "pidKp")) pidKpEl.value = pidData.kp.toFixed(3);
      if (isFresh(pidKiEl, "pidKi")) pidKiEl.value = pidData.ki.toFixed(3);
      if (isFresh(pidKdEl, "pidKd")) pidKdEl.value = pidData.kd.toFixed(3);
      if (pidData.window_ms !== undefined) {
        lastKnownWindowMs = pidData.window_ms;
        if (isFresh(pidWindowEl, "pidWindow")) pidWindowEl.value = pidData.window_ms;
      }
      if (isFresh(pidDerivativeKEl, "pidDerivativeK") && pidData.derivative_k !== undefined) {
        pidDerivativeKEl.value = pidData.derivative_k;
      }
      if (isFresh(pidDerivativeSourceEl, "pidDerivativeSource") && pidData.derivative_source !== undefined) {
        pidDerivativeSourceEl.value = pidData.derivative_source;
      }
      if (isFresh(pidDerivativeIntervalEl, "pidDerivativeInterval") && pidData.derivative_interval_ms !== undefined) {
        pidDerivativeIntervalEl.value = pidData.derivative_interval_ms;
      }
      if (isFresh(pidDcCapEl, "pidDcCap") && pidData.dc_cap_percent !== undefined) {
        pidDcCapEl.value = pidData.dc_cap_percent;
      }
      if (isFresh(pidDcCapDisableSlopeEl, "pidDcCapDisableSlope") && pidData.dc_cap_disable_slope !== undefined) {
        pidDcCapDisableSlopeEl.value = pidData.dc_cap_disable_slope;
      }
      if (isFresh(pidDcCapDisableHoldEl, "pidDcCapDisableHold") && pidData.dc_cap_disable_hold_ms !== undefined) {
        pidDcCapDisableHoldEl.value = pidData.dc_cap_disable_hold_ms;
      }
      if (pidData.pid_enabled !== undefined && pidData.integrator_enabled !== undefined) {
        updatePidStatusBadges(pidData.pid_enabled, pidData.integrator_enabled);
      }
      if (pidData.dc_cap_bypassed !== undefined) {
        if (pidData.dc_cap_bypassed) {
          dcCapBypassLabelEl.textContent = "Cap: BYPASSED";
          dcCapBypassLabelEl.className = "pid-status-badge integrator-on";
        } else {
          dcCapBypassLabelEl.textContent = "Cap: ACTIVE";
          dcCapBypassLabelEl.className = "pid-status-badge integrator-off";
        }
      }
      if (pidData.slope_c_per_sec !== undefined) {
        slopeReadoutEl.textContent = "Slope: " + pidData.slope_c_per_sec.toFixed(3) + "°C/s";
      }
    }
  } catch (e) { /* non-fatal */ }

  try {
    var sensorRes = await fetch("/api/sensor");
    if (sensorRes.ok) {
      var sensorData = await sensorRes.json();
      if (isFresh(sensorSampleIntervalEl, "sensorSampleInterval") && sensorData.sample_interval_ms !== undefined) {
        sensorSampleIntervalEl.value = sensorData.sample_interval_ms;
      }
      if (isFresh(sensorMedianWindowEl, "sensorMedianWindow") && sensorData.median_window !== undefined) {
        sensorMedianWindowEl.value = sensorData.median_window;
      }
      if (sensorData.median_latency_ms !== undefined) {
        sensorLatencyReadoutEl.textContent = "≈ " + sensorData.median_latency_ms + "ms latency";
      }
    }
  } catch (e) { /* non-fatal */ }

  if (PRESSURE_ENABLED) {
    try {
      var res2 = await fetch("/api/pressure/history");
      if (!res2.ok) throw new Error(res2.statusText);
      var data2 = await res2.json();
      if (data2.points && data2.points.length > 0) {
        cachedPressurePts = data2.points;
        renderPressure(cachedPressurePts);
      } else {
        pressureStatusEl.textContent = "Collecting data…";
      }
    } catch (e) {
      pressureStatusEl.textContent = "API error: " + e.message;
    }
  }
}

refresh();
setInterval(refresh, 500);
