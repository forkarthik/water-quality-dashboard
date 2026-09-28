/**
 * =========================================================================================
 * Smart Water Quality Monitoring System - Frontend Telemetry Engine
 * Designed for GitHub Pages & ThingsBoard IoT Community Edition / Cloud
 * =========================================================================================
 */

// =========================================================================================
// 1. CENTRAL CONFIGURATION & STATE
// =========================================================================================
const APP_CONFIG = {
  // Mode: 'demo' (realistic edge simulator) or 'live' (ThingsBoard REST/WebSocket API)
  mode: 'demo',

  // ThingsBoard API Configuration (Can be customized via Settings Modal and saved in localStorage)
  thingsboard: {
    serverUrl: 'https://thingsboard.cloud', // Default ThingsBoard Cloud / CE instance
    deviceId: '78a59480-7f91-11ee-b962-e95bb39c298b',
    publicToken: '', // Read-only public dashboard token or JWT
    pollIntervalMs: 5000,
    keys: {
      tds: 'tds',
      turbidity: 'turbidity',
      status: 'status',
      battery: 'battery',
      rssi: 'rssi'
    }
  },

  // Early-Warning Alert Thresholds (IS 10500:2012 / WHO Guidelines)
  thresholds: {
    tdsWarning: 300.0,    // Desirable drinking limit (ppm)
    tdsCritical: 500.0,   // Maximum permissible limit (ppm)
    turbWarning: 1.0,     // Desirable clarity limit (NTU)
    turbCritical: 5.0     // Maximum permissible limit (NTU)
  }
};

// Application State
const state = {
  currentMetrics: {
    tds: 245,
    turbidity: 0.8,
    status: 'NORMAL',
    timestamp: new Date(),
    deviceOnline: true,
    rssi: -62,
    latency: 38
  },
  alerts: [],
  selectedTimeframe: '1h',
  isPolling: false,
  pollTimer: null,
  tdsChart: null,
  turbidityChart: null
};

// =========================================================================================
// 2. DOM ELEMENT REFERENCES
// =========================================================================================
const elements = {
  // Connection Header
  connectionStatusPill: document.getElementById('connectionStatusPill'),
  connectionDot: document.getElementById('connectionDot'),
  connectionStatusText: document.getElementById('connectionStatusText'),
  btnToggleMode: document.getElementById('btnToggleMode'),
  modeLabel: document.getElementById('modeLabel'),
  btnRefresh: document.getElementById('btnRefresh'),
  btnSettings: document.getElementById('btnSettings'),

  // Metric Cards
  tdsValue: document.getElementById('tdsValue'),
  tdsBadge: document.getElementById('tdsBadge'),
  tdsGaugeFill: document.getElementById('tdsGaugeFill'),
  tdsThresholdDisplay: document.getElementById('tdsThresholdDisplay'),
  tdsUpdated: document.getElementById('tdsUpdated'),

  turbValue: document.getElementById('turbValue'),
  turbBadge: document.getElementById('turbBadge'),
  turbGaugeFill: document.getElementById('turbGaugeFill'),
  turbThresholdDisplay: document.getElementById('turbThresholdDisplay'),
  turbUpdated: document.getElementById('turbUpdated'),

  overallIndicatorDot: document.getElementById('overallIndicatorDot'),
  overallStatusHeading: document.getElementById('overallStatusHeading'),
  overallStatusDesc: document.getElementById('overallStatusDesc'),
  overallAdvisory: document.getElementById('overallAdvisory'),
  statusUpdated: document.getElementById('statusUpdated'),

  deviceBadge: document.getElementById('deviceBadge'),
  deviceLatency: document.getElementById('deviceLatency'),
  deviceRssi: document.getElementById('deviceRssi'),
  deviceHeartbeat: document.getElementById('deviceHeartbeat'),

  // Alerts & Device Info
  alertContainer: document.getElementById('alertContainer'),
  alertEmptyState: document.getElementById('alertEmptyState'),
  btnClearAlerts: document.getElementById('btnClearAlerts'),

  infoDeviceName: document.getElementById('infoDeviceName'),
  infoDeviceId: document.getElementById('infoDeviceId'),
  infoPollInterval: document.getElementById('infoPollInterval'),
  platformBadge: document.getElementById('platformBadge'),

  // Settings Modal
  settingsModal: document.getElementById('settingsModal'),
  btnCloseSettings: document.getElementById('btnCloseSettings'),
  btnSaveSettings: document.getElementById('btnSaveSettings'),
  btnResetDefaults: document.getElementById('btnResetDefaults'),
  cfgServerUrl: document.getElementById('cfgServerUrl'),
  cfgDeviceId: document.getElementById('cfgDeviceId'),
  cfgPublicToken: document.getElementById('cfgPublicToken'),
  cfgTdsKey: document.getElementById('cfgTdsKey'),
  cfgTurbKey: document.getElementById('cfgTurbKey'),
  cfgTdsWarn: document.getElementById('cfgTdsWarn'),
  cfgTdsAlert: document.getElementById('cfgTdsAlert'),
  cfgTurbWarn: document.getElementById('cfgTurbWarn'),
  cfgTurbAlert: document.getElementById('cfgTurbAlert'),
  toastContainer: document.getElementById('toastContainer')
};

// =========================================================================================
// 3. INITIALIZATION
// =========================================================================================
document.addEventListener('DOMContentLoaded', () => {
  loadStoredPreferences();
  initCharts();
  bindEventListeners();
  updateThresholdDisplays();

  // Initial Telemetry Fetch / Simulation Bootstrap
  fetchTelemetry();
  startPolling();

  showToast('Water Quality Monitoring System initialized.', 'info');
});

// =========================================================================================
// 4. CHART INITIALIZATION & MANAGEMENT (Chart.js)
// =========================================================================================
function initCharts() {
  const ctxTds = document.getElementById('tdsChart').getContext('2d');
  const ctxTurb = document.getElementById('turbidityChart').getContext('2d');

  // Chart styling constants
  const gridColor = 'rgba(255, 255, 255, 0.05)';
  const textColor = '#9ca3af';

  // Gradient Fills
  const gradientTds = ctxTds.createLinearGradient(0, 0, 0, 260);
  gradientTds.addColorStop(0, 'rgba(6, 182, 212, 0.4)');
  gradientTds.addColorStop(1, 'rgba(6, 182, 212, 0.0)');

  const gradientTurb = ctxTurb.createLinearGradient(0, 0, 0, 260);
  gradientTurb.addColorStop(0, 'rgba(56, 189, 248, 0.4)');
  gradientTurb.addColorStop(1, 'rgba(56, 189, 248, 0.0)');

  // Initial Historical Seed Data
  const initialData = generateHistoricalData('1h');

  // TDS Chart Configuration
  state.tdsChart = new Chart(ctxTds, {
    type: 'line',
    data: {
      labels: initialData.labels,
      datasets: [
        {
          label: 'TDS (ppm)',
          data: initialData.tdsValues,
          borderColor: '#06b6d4',
          backgroundColor: gradientTds,
          borderWidth: 2.2,
          fill: true,
          tension: 0.35,
          pointRadius: 2,
          pointHoverRadius: 6,
          pointBackgroundColor: '#06b6d4'
        },
        {
          label: 'Alert Limit (500 ppm)',
          data: initialData.labels.map(() => APP_CONFIG.thresholds.tdsCritical),
          borderColor: 'rgba(239, 68, 68, 0.65)',
          borderWidth: 1.5,
          borderDash: [5, 5],
          fill: false,
          pointRadius: 0
        }
      ]
    },
    options: getChartOptions('ppm', [0, 800], gridColor, textColor)
  });

  // Turbidity Chart Configuration
  state.turbidityChart = new Chart(ctxTurb, {
    type: 'line',
    data: {
      labels: initialData.labels,
      datasets: [
        {
          label: 'Turbidity (NTU)',
          data: initialData.turbValues,
          borderColor: '#38bdf8',
          backgroundColor: gradientTurb,
          borderWidth: 2.2,
          fill: true,
          tension: 0.35,
          pointRadius: 2,
          pointHoverRadius: 6,
          pointBackgroundColor: '#38bdf8'
        },
        {
          label: 'Alert Limit (5.0 NTU)',
          data: initialData.labels.map(() => APP_CONFIG.thresholds.turbCritical),
          borderColor: 'rgba(239, 68, 68, 0.65)',
          borderWidth: 1.5,
          borderDash: [5, 5],
          fill: false,
          pointRadius: 0
        }
      ]
    },
    options: getChartOptions('NTU', [0, 10], gridColor, textColor)
  });
}

function getChartOptions(unit, yRange, gridColor, textColor) {
  return {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: { display: false },
      tooltip: {
        backgroundColor: '#111827',
        titleColor: '#f9fafb',
        bodyColor: '#38bdf8',
        borderColor: 'rgba(255, 255, 255, 0.15)',
        borderWidth: 1,
        padding: 10,
        callbacks: {
          label: (context) => ` ${context.dataset.label}: ${context.parsed.y} ${unit}`
        }
      }
    },
    scales: {
      x: {
        grid: { color: gridColor },
        ticks: { color: textColor, maxTicksLimit: 7, font: { size: 11 } }
      },
      y: {
        suggestedMin: yRange[0],
        suggestedMax: yRange[1],
        grid: { color: gridColor },
        ticks: { color: textColor, font: { size: 11 } }
      }
    },
    animation: { duration: 600 }
  };
}

// =========================================================================================
// 5. TELEMETRY ACQUISITION (THINGSBOARD API & DEMO SIMULATOR)
// =========================================================================================
async function fetchTelemetry() {
  if (APP_CONFIG.mode === 'live') {
    await fetchThingsBoardLiveTelemetry();
  } else {
    simulateEdgeTelemetry();
  }
}

/**
 * Connects to ThingsBoard REST API using standard endpoint:
 * GET {serverUrl}/api/plugins/telemetry/DEVICE/{deviceId}/values/timeseries?keys=tds,turbidity,status
 * Or via Public Dashboard Token endpoint.
 */
async function fetchThingsBoardLiveTelemetry() {
  setConnectionStatus('SYNCING', 'warn');
  const cfg = APP_CONFIG.thingsboard;

  if (!cfg.serverUrl || !cfg.deviceId) {
    showToast('Incomplete ThingsBoard configuration. Switched to Demo Mode.', 'warn');
    setMode('demo');
    return;
  }

  // Sanitize server URL
  const baseUrl = cfg.serverUrl.replace(/\/+$/, '');
  const keys = `${cfg.keys.tds},${cfg.keys.turbidity},${cfg.keys.status}`;
  const url = `${baseUrl}/api/plugins/telemetry/DEVICE/${cfg.deviceId}/values/timeseries?keys=${keys}`;

  try {
    const headers = { 'Content-Type': 'application/json' };
    if (cfg.publicToken) {
      // If a JWT token or public token is provided
      headers['X-Authorization'] = `Bearer ${cfg.publicToken}`;
    }

    const response = await fetch(url, { method: 'GET', headers: headers });

    if (!response.ok) {
      throw new Error(`HTTP Error ${response.status}: ${response.statusText}`);
    }

    const data = await response.json();

    // Parse ThingsBoard timeseries response format:
    // { "tds": [ {"ts": 1695880000000, "value": "320"} ], "turbidity": [ ... ] }
    const rawTds = data[cfg.keys.tds] ? parseFloat(data[cfg.keys.tds][0].value) : null;
    const rawTurb = data[cfg.keys.turbidity] ? parseFloat(data[cfg.keys.turbidity][0].value) : null;
    const rawStatus = data[cfg.keys.status] ? data[cfg.keys.status][0].value : 'NORMAL';

    if (rawTds === null || isNaN(rawTds) || rawTurb === null || isNaN(rawTurb)) {
      throw new Error('Telemetry payload missing expected keys or has invalid numeric format.');
    }

    state.currentMetrics = {
      tds: rawTds,
      turbidity: rawTurb,
      status: rawStatus,
      timestamp: new Date(),
      deviceOnline: true,
      rssi: -58,
      latency: Math.floor(Math.random() * 25) + 35
    };

    setConnectionStatus('LIVE (TB)', 'safe');
    renderDashboard();
    appendChartPoint(state.currentMetrics.tds, state.currentMetrics.turbidity);
  } catch (error) {
    console.error('[ThingsBoard Error]:', error);
    setConnectionStatus('TB OFFLINE', 'alert');
    state.currentMetrics.deviceOnline = false;
    renderDeviceOffline();
    addAlert('ESP32 / ThingsBoard Offline', 'No Response', 'CRITICAL');
    showToast(`ThingsBoard sync failed: ${error.message}`, 'alert');
  }
}

/**
 * Realistic Simulation Engine:
 * Generates continuous realistic hydro-chemical drift, with occasional turbidity spikes.
 */
function simulateEdgeTelemetry() {
  const prevTds = state.currentMetrics.tds;
  const prevTurb = state.currentMetrics.turbidity;

  // Brownian random walk with gentle reversion to clean baseline
  const tdsDrift = (Math.random() - 0.49) * 8;
  const turbDrift = (Math.random() - 0.48) * 0.15;

  let newTds = Math.max(80, Math.min(650, prevTds + tdsDrift));
  let newTurb = Math.max(0.1, Math.min(12.0, prevTurb + turbDrift));

  // Occasional random runoff event for demonstration
  if (Math.random() < 0.05) {
    newTurb += (Math.random() * 4.0); // simulated runoff
  }

  newTds = parseFloat(newTds.toFixed(1));
  newTurb = parseFloat(newTurb.toFixed(2));

  // Determine overall status
  let calculatedStatus = 'NORMAL';
  if (newTds > APP_CONFIG.thresholds.tdsCritical || newTurb > APP_CONFIG.thresholds.turbCritical) {
    calculatedStatus = 'ALERT';
  } else if (newTds > APP_CONFIG.thresholds.tdsWarning || newTurb > APP_CONFIG.thresholds.turbWarning) {
    calculatedStatus = 'WARNING';
  }

  state.currentMetrics = {
    tds: newTds,
    turbidity: newTurb,
    status: calculatedStatus,
    timestamp: new Date(),
    deviceOnline: true,
    rssi: -60 - Math.floor(Math.random() * 8),
    latency: 35 + Math.floor(Math.random() * 15)
  };

  setConnectionStatus('DEMO ACTIVE', 'safe');
  renderDashboard();
  appendChartPoint(newTds, newTurb);
}

// =========================================================================================
// 6. UI RENDERING & STATUS EVALUATION
// =========================================================================================
function renderDashboard() {
  const m = state.currentMetrics;
  const timeString = m.timestamp.toLocaleTimeString();

  // Render TDS
  elements.tdsValue.textContent = Math.round(m.tds);
  elements.tdsUpdated.textContent = timeString;
  const tdsPercent = Math.min(100, (m.tds / 800) * 100);
  elements.tdsGaugeFill.style.width = `${tdsPercent}%`;

  if (m.tds > APP_CONFIG.thresholds.tdsCritical) {
    setBadge(elements.tdsBadge, 'CRITICAL', 'badge-alert');
    elements.tdsGaugeFill.style.background = 'linear-gradient(90deg, #f59e0b, #ef4444)';
    addAlert('TDS Exceeded Limit', `${m.tds} ppm (> 500)`, 'CRITICAL');
  } else if (m.tds > APP_CONFIG.thresholds.tdsWarning) {
    setBadge(elements.tdsBadge, 'ELEVATED', 'badge-warn');
    elements.tdsGaugeFill.style.background = 'linear-gradient(90deg, #10b981, #f59e0b)';
    addAlert('TDS Caution Level', `${m.tds} ppm (> 300)`, 'WARNING');
  } else {
    setBadge(elements.tdsBadge, 'DESIRABLE', 'badge-safe');
    elements.tdsGaugeFill.style.background = 'linear-gradient(90deg, #10b981, #06b6d4)';
  }

  // Render Turbidity
  elements.turbValue.textContent = m.turbidity.toFixed(1);
  elements.turbUpdated.textContent = timeString;
  const turbPercent = Math.min(100, (m.turbidity / 10) * 100);
  elements.turbGaugeFill.style.width = `${turbPercent}%`;

  if (m.turbidity > APP_CONFIG.thresholds.turbCritical) {
    setBadge(elements.turbBadge, 'TURBID', 'badge-alert');
    elements.turbGaugeFill.style.background = 'linear-gradient(90deg, #f59e0b, #ef4444)';
    addAlert('Turbidity Exceeded Limit', `${m.turbidity} NTU (> 5.0)`, 'CRITICAL');
  } else if (m.turbidity > APP_CONFIG.thresholds.turbWarning) {
    setBadge(elements.turbBadge, 'SLIGHT HAZE', 'badge-warn');
    elements.turbGaugeFill.style.background = 'linear-gradient(90deg, #10b981, #f59e0b)';
    addAlert('Turbidity Caution', `${m.turbidity} NTU (> 1.0)`, 'WARNING');
  } else {
    setBadge(elements.turbBadge, 'CLEAR', 'badge-safe');
    elements.turbGaugeFill.style.background = 'linear-gradient(90deg, #10b981, #38bdf8)';
  }

  // Render Overall Water Quality Risk Matrix
  evaluateOverallStatus(m);

  // Render ESP32 Node Status
  elements.deviceBadge.textContent = 'ONLINE';
  elements.deviceBadge.className = 'badge badge-safe';
  elements.deviceLatency.textContent = `${m.latency} ms`;
  elements.deviceRssi.textContent = `${m.rssi} dBm (Good)`;
  elements.deviceHeartbeat.textContent = timeString;
}

function evaluateOverallStatus(m) {
  const timeString = m.timestamp.toLocaleTimeString();
  elements.statusUpdated.textContent = timeString;

  if (m.status === 'ALERT' || m.tds > APP_CONFIG.thresholds.tdsCritical || m.turbidity > APP_CONFIG.thresholds.turbCritical) {
    elements.overallIndicatorDot.style.background = 'var(--status-alert)';
    elements.overallIndicatorDot.style.boxShadow = '0 0 14px var(--status-alert)';
    elements.overallStatusHeading.textContent = 'WARNING';
    elements.overallStatusHeading.style.color = '#f87171';
    elements.overallStatusDesc.textContent = 'Physical-chemical parameters exceed maximum permissible guidelines.';
    elements.overallAdvisory.textContent = 'Advisory: Filtration and disinfection required before consumption.';
    elements.overallAdvisory.style.color = '#f87171';
  } else if (m.status === 'WARNING' || m.tds > APP_CONFIG.thresholds.tdsWarning || m.turbidity > APP_CONFIG.thresholds.turbWarning) {
    elements.overallIndicatorDot.style.background = 'var(--status-warn)';
    elements.overallIndicatorDot.style.boxShadow = '0 0 14px var(--status-warn)';
    elements.overallStatusHeading.textContent = 'CAUTION';
    elements.overallStatusHeading.style.color = '#fbbf24';
    elements.overallStatusDesc.textContent = 'Parameters are within permissible limits but elevated above desirable baseline.';
    elements.overallAdvisory.textContent = 'Advisory: Regular monitoring advised; check for particulate runoff.';
    elements.overallAdvisory.style.color = '#fbbf24';
  } else {
    elements.overallIndicatorDot.style.background = 'var(--status-safe)';
    elements.overallIndicatorDot.style.boxShadow = '0 0 14px var(--status-safe)';
    elements.overallStatusHeading.textContent = 'NORMAL';
    elements.overallStatusHeading.style.color = '#34d399';
    elements.overallStatusDesc.textContent = 'All physical parameters are within ideal baseline drinking guidelines.';
    elements.overallAdvisory.textContent = 'Advisory: Water exhibits desirable clarity and dissolved solid levels.';
    elements.overallAdvisory.style.color = '#38bdf8';
  }
}

function renderDeviceOffline() {
  elements.tdsValue.textContent = '---';
  elements.turbValue.textContent = '---';
  elements.overallIndicatorDot.style.background = 'var(--status-offline)';
  elements.overallIndicatorDot.style.boxShadow = 'none';
  elements.overallStatusHeading.textContent = 'DEVICE OFFLINE';
  elements.overallStatusHeading.style.color = 'var(--status-offline)';
  elements.overallStatusDesc.textContent = 'No telemetry received from ESP32 gateway within timeout window.';
  elements.overallAdvisory.textContent = 'Advisory: Verify ESP32 power supply, USB port, or Wi-Fi connectivity.';
  elements.overallAdvisory.style.color = 'var(--text-muted)';
  elements.deviceBadge.textContent = 'OFFLINE';
  elements.deviceBadge.className = 'badge badge-alert';
  elements.deviceLatency.textContent = 'N/A';
  elements.deviceRssi.textContent = 'Disconnected';
}

function setBadge(el, text, className) {
  el.textContent = text;
  el.className = `badge ${className}`;
}

function setConnectionStatus(text, type) {
  elements.connectionStatusText.textContent = text;
  if (type === 'safe') {
    elements.connectionDot.style.background = 'var(--status-safe)';
    elements.connectionDot.style.boxShadow = '0 0 10px var(--status-safe)';
  } else if (type === 'warn') {
    elements.connectionDot.style.background = 'var(--status-warn)';
    elements.connectionDot.style.boxShadow = '0 0 10px var(--status-warn)';
  } else {
    elements.connectionDot.style.background = 'var(--status-alert)';
    elements.connectionDot.style.boxShadow = '0 0 10px var(--status-alert)';
  }
}

// =========================================================================================
// 7. ALERT LOGGING SYSTEM
// =========================================================================================
function addAlert(type, value, severity) {
  const timestamp = new Date();
  const timeString = timestamp.toLocaleTimeString();

  // Avoid spamming identical alerts within 30 seconds
  const isDuplicate = state.alerts.some(a => 
    a.type === type && (timestamp - a.rawTime) < 30000
  );
  if (isDuplicate) return;

  const alertObj = {
    id: Date.now(),
    type: type,
    value: value,
    severity: severity,
    time: timeString,
    rawTime: timestamp
  };

  state.alerts.unshift(alertObj);
  if (state.alerts.length > 20) state.alerts.pop();

  renderAlerts();
}

function renderAlerts() {
  if (state.alerts.length === 0) {
    elements.alertContainer.innerHTML = '';
    elements.alertContainer.appendChild(elements.alertEmptyState);
    return;
  }

  let html = '';
  state.alerts.forEach(item => {
    const isCritical = item.severity === 'CRITICAL';
    const alertClass = isCritical ? 'alert-critical' : 'alert-warn';
    const icon = isCritical ? 'fa-triangle-exclamation' : 'fa-circle-exclamation';

    html += `
      <div class="alert-item ${alertClass}">
        <div class="alert-content-left">
          <i class="fa-solid ${icon} alert-icon"></i>
          <div class="alert-text-group">
            <h5>${item.type}</h5>
            <span>Logged Event &bull; Early Warning Trigger</span>
          </div>
        </div>
        <div class="alert-meta-right">
          <strong>${item.value}</strong>
          <span>${item.time}</span>
        </div>
      </div>
    `;
  });

  elements.alertContainer.innerHTML = html;
}

// =========================================================================================
// 8. CHART STREAMING & TIMEFRAME SELECTION
// =========================================================================================
function appendChartPoint(tds, turb) {
  if (!state.tdsChart || !state.turbidityChart) return;

  const now = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });

  // Update TDS chart
  const tdsLabels = state.tdsChart.data.labels;
  const tdsData = state.tdsChart.data.datasets[0].data;
  const tdsLimit = state.tdsChart.data.datasets[1].data;

  tdsLabels.push(now);
  tdsData.push(tds);
  tdsLimit.push(APP_CONFIG.thresholds.tdsCritical);

  if (tdsLabels.length > 20) {
    tdsLabels.shift();
    tdsData.shift();
    tdsLimit.shift();
  }
  state.tdsChart.update('none');

  // Update Turbidity chart
  const turbLabels = state.turbidityChart.data.labels;
  const turbData = state.turbidityChart.data.datasets[0].data;
  const turbLimit = state.turbidityChart.data.datasets[1].data;

  turbLabels.push(now);
  turbData.push(turb);
  turbLimit.push(APP_CONFIG.thresholds.turbCritical);

  if (turbLabels.length > 20) {
    turbLabels.shift();
    turbData.shift();
    turbLimit.shift();
  }
  state.turbidityChart.update('none');
}

function updateChartRange(range) {
  state.selectedTimeframe = range;
  const generated = generateHistoricalData(range);

  state.tdsChart.data.labels = generated.labels;
  state.tdsChart.data.datasets[0].data = generated.tdsValues;
  state.tdsChart.data.datasets[1].data = generated.labels.map(() => APP_CONFIG.thresholds.tdsCritical);
  state.tdsChart.update();

  state.turbidityChart.data.labels = generated.labels;
  state.turbidityChart.data.datasets[0].data = generated.turbValues;
  state.turbidityChart.data.datasets[1].data = generated.labels.map(() => APP_CONFIG.thresholds.turbCritical);
  state.turbidityChart.update();

  showToast(`Loaded ${range.toUpperCase()} historical telemetry view.`, 'info');
}

function generateHistoricalData(range) {
  const pointsCount = 14;
  const labels = [];
  const tdsValues = [];
  const turbValues = [];
  const now = new Date();

  for (let i = pointsCount - 1; i >= 0; i--) {
    let t = new Date(now);
    let label = '';

    if (range === '1h') {
      t.setMinutes(now.getMinutes() - i * 4);
      label = t.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    } else if (range === '6h') {
      t.setMinutes(now.getMinutes() - i * 25);
      label = t.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    } else if (range === '24h') {
      t.setHours(now.getHours() - i * 1.7);
      label = t.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    } else if (range === '7d') {
      t.setDate(now.getDate() - i * 0.5);
      label = `${t.getDate()} ${t.toLocaleString('default', { month: 'short' })}`;
    } else { // 30d
      t.setDate(now.getDate() - i * 2.1);
      label = `${t.getDate()} ${t.toLocaleString('default', { month: 'short' })}`;
    }

    labels.push(label);
    tdsValues.push(Math.round(220 + Math.sin(i * 0.6) * 60 + Math.random() * 25));
    turbValues.push(parseFloat((0.7 + Math.sin(i * 0.5) * 0.5 + Math.random() * 0.4).toFixed(1)));
  }

  return { labels, tdsValues, turbValues };
}

// =========================================================================================
// 9. TIMING & EVENT LISTENERS
// =========================================================================================
function startPolling() {
  if (state.pollTimer) clearInterval(state.pollTimer);
  state.pollTimer = setInterval(fetchTelemetry, APP_CONFIG.thingsboard.pollIntervalMs);
  state.isPolling = true;
}

function bindEventListeners() {
  // Mode toggle (Demo vs Live ThingsBoard)
  elements.btnToggleMode.addEventListener('click', () => {
    const newMode = APP_CONFIG.mode === 'demo' ? 'live' : 'demo';
    setMode(newMode);
  });

  // Manual Refresh
  elements.btnRefresh.addEventListener('click', () => {
    fetchTelemetry();
    showToast('Refreshing live telemetry...', 'info');
  });

  // Settings Modal controls
  elements.btnSettings.addEventListener('click', openSettingsModal);
  elements.btnCloseSettings.addEventListener('click', closeSettingsModal);
  elements.settingsModal.addEventListener('click', (e) => {
    if (e.target === elements.settingsModal) closeSettingsModal();
  });

  // Modal Tabs
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
      btn.classList.add('active');
      document.getElementById(btn.dataset.tab).classList.add('active');
    });
  });

  // Save Settings
  elements.btnSaveSettings.addEventListener('click', savePreferences);
  elements.btnResetDefaults.addEventListener('click', resetPreferences);

  // Clear Alerts
  elements.btnClearAlerts.addEventListener('click', () => {
    state.alerts = [];
    renderAlerts();
    showToast('Alert log cleared.', 'info');
  });

  // Timeframe buttons
  document.querySelectorAll('.btn-timeframe').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.btn-timeframe').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      updateChartRange(btn.dataset.range);
    });
  });
}

function setMode(mode) {
  APP_CONFIG.mode = mode;
  elements.modeLabel.textContent = mode === 'demo' ? 'Demo Mode' : 'ThingsBoard';
  elements.platformBadge.textContent = mode === 'demo' ? 'Simulator (Active)' : 'ThingsBoard Cloud';
  showToast(`Switched mode to: ${mode.toUpperCase()}`, 'info');
  fetchTelemetry();
}

// =========================================================================================
// 10. CONFIGURATION PERSISTENCE & TOAST NOTIFICATIONS
// =========================================================================================
function openSettingsModal() {
  elements.cfgServerUrl.value = APP_CONFIG.thingsboard.serverUrl;
  elements.cfgDeviceId.value = APP_CONFIG.thingsboard.deviceId;
  elements.cfgPublicToken.value = APP_CONFIG.thingsboard.publicToken;
  elements.cfgTdsKey.value = APP_CONFIG.thingsboard.keys.tds;
  elements.cfgTurbKey.value = APP_CONFIG.thingsboard.keys.turbidity;

  elements.cfgTdsWarn.value = APP_CONFIG.thresholds.tdsWarning;
  elements.cfgTdsAlert.value = APP_CONFIG.thresholds.tdsCritical;
  elements.cfgTurbWarn.value = APP_CONFIG.thresholds.turbWarning;
  elements.cfgTurbAlert.value = APP_CONFIG.thresholds.turbCritical;

  elements.settingsModal.classList.add('open');
}

function closeSettingsModal() {
  elements.settingsModal.classList.remove('open');
}

function savePreferences() {
  APP_CONFIG.thingsboard.serverUrl = elements.cfgServerUrl.value.trim();
  APP_CONFIG.thingsboard.deviceId = elements.cfgDeviceId.value.trim();
  APP_CONFIG.thingsboard.publicToken = elements.cfgPublicToken.value.trim();
  APP_CONFIG.thingsboard.keys.tds = elements.cfgTdsKey.value.trim() || 'tds';
  APP_CONFIG.thingsboard.keys.turbidity = elements.cfgTurbKey.value.trim() || 'turbidity';

  APP_CONFIG.thresholds.tdsWarning = parseFloat(elements.cfgTdsWarn.value) || 300;
  APP_CONFIG.thresholds.tdsCritical = parseFloat(elements.cfgTdsAlert.value) || 500;
  APP_CONFIG.thresholds.turbWarning = parseFloat(elements.cfgTurbWarn.value) || 1.0;
  APP_CONFIG.thresholds.turbCritical = parseFloat(elements.cfgTurbAlert.value) || 5.0;

  localStorage.setItem('wqc_dashboard_config', JSON.stringify({
    thingsboard: APP_CONFIG.thingsboard,
    thresholds: APP_CONFIG.thresholds
  }));

  updateThresholdDisplays();
  closeSettingsModal();
  showToast('Settings saved to browser local storage.', 'info');
  fetchTelemetry();
}

function resetPreferences() {
  localStorage.removeItem('wqc_dashboard_config');
  loadStoredPreferences();
  openSettingsModal();
  updateThresholdDisplays();
  showToast('Reset configuration to factory defaults.', 'warn');
}

function loadStoredPreferences() {
  const saved = localStorage.getItem('wqc_dashboard_config');
  if (saved) {
    try {
      const parsed = JSON.parse(saved);
      if (parsed.thingsboard) Object.assign(APP_CONFIG.thingsboard, parsed.thingsboard);
      if (parsed.thresholds) Object.assign(APP_CONFIG.thresholds, parsed.thresholds);
    } catch (e) {
      console.warn('Failed to parse saved config from localStorage', e);
    }
  }

  // Update DOM readouts
  elements.infoDeviceId.textContent = APP_CONFIG.thingsboard.deviceId;
  elements.infoPollInterval.textContent = `${APP_CONFIG.thingsboard.pollIntervalMs} ms`;
}

function updateThresholdDisplays() {
  elements.tdsThresholdDisplay.innerHTML = `&le; ${APP_CONFIG.thresholds.tdsCritical} ppm`;
  elements.turbThresholdDisplay.innerHTML = `&le; ${APP_CONFIG.thresholds.turbCritical.toFixed(1)} NTU`;

  // Update chart alert threshold lines
  if (state.tdsChart && state.turbidityChart) {
    state.tdsChart.data.datasets[1].data = state.tdsChart.data.labels.map(() => APP_CONFIG.thresholds.tdsCritical);
    state.tdsChart.data.datasets[1].label = `Alert Limit (${APP_CONFIG.thresholds.tdsCritical} ppm)`;
    state.tdsChart.update();

    state.turbidityChart.data.datasets[1].data = state.turbidityChart.data.labels.map(() => APP_CONFIG.thresholds.turbCritical);
    state.turbidityChart.data.datasets[1].label = `Alert Limit (${APP_CONFIG.thresholds.turbCritical} NTU)`;
    state.turbidityChart.update();
  }
}

function showToast(message, type = 'info') {
  const toast = document.createElement('div');
  toast.className = 'toast';
  
  let icon = 'fa-circle-info';
  let color = 'var(--cyan-primary)';
  if (type === 'warn') { icon = 'fa-triangle-exclamation'; color = 'var(--status-warn)'; }
  if (type === 'alert') { icon = 'fa-circle-exclamation'; color = 'var(--status-alert)'; }

  toast.innerHTML = `
    <i class="fa-solid ${icon}" style="color: ${color}"></i>
    <span>${message}</span>
  `;

  elements.toastContainer.appendChild(toast);
  setTimeout(() => {
    toast.style.opacity = '0';
    toast.style.transform = 'translateY(10px)';
    toast.style.transition = 'all 0.3s ease';
    setTimeout(() => toast.remove(), 300);
  }, 4000);
}
