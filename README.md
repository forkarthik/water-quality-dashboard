# Smart Water Quality Monitoring Dashboard (IoT Early-Warning System)

A web-based IoT telemetry dashboard designed for rural community water monitoring. It interfaces with **ESP32**, ingest data via **ThingsBoard Community / Cloud**, and renders real-time physical-chemical screening indicators (**Total Dissolved Solids** and **Turbidity**) against **WHO** and **IS 10500:2012** regulatory drinking standards.

Hosted natively on **GitHub Pages**.

---

## 1. System Architecture & End-to-End Data Flow

```
[ Water Source: Well / Tank ]
         │
         ├── Analog TDS Probe (Ion Conductivity - GPIO 34)
         └── Optical Turbidity Probe (Light Attenuation - GPIO 35 via 10k/22k divider)
         │
         ▼
     [ ESP32 ]
  - 12-bit Multisampling ADC
  - 16x2 I2C Local LCD Screen
  - Red Warning LED + 2N2222 Buzzer Alarm
         │
         ▼ (Wi-Fi 802.11 b/g/n / MQTT / HTTP POST)
 [ ThingsBoard Cloud / CE ]
  - Telemetry Ingestion (`tds`, `turbidity`, `status`)
  - Long-term Time-Series Storage
  - Rule Engine Alarm Evaluation
         │
         ▼ (REST Telemetry API / Public Read Token)
 [ Web Dashboard on GitHub Pages ]
  - Chart.js Historical Visualization (1h, 6h, 24h, 7d, 30d)
  - Threshold Breach Alert Logging
  - Safe / Caution / Alert / Device Offline Risk Matrix
         │
         ▼
 [ Community Operator / Public User ]
```

---

## 2. Security Architecture for GitHub Pages

> [!WARNING]
> GitHub Pages is a purely static, client-side web hosting service. Any API key, token, or password committed to your repository's HTML/JS files is visible to the public.

### How this Dashboard Solves the Security Constraint:
1. **Zero Hardcoded Secrets**: No private credentials exist in the source code.
2. **Local Storage Sandboxing**: User-configured ThingsBoard URLs and Device IDs configured via the Settings modal (`⚙`) are persisted exclusively in the client's browser `localStorage`.
3. **Public Token Architecture**: For public deployment, use ThingsBoard's **"Make Dashboard Public"** feature. ThingsBoard generates a restricted read-only public token that allows telemetry read without granting administrative access to your tenant or devices.
4. **Backend Proxy (Optional Enterprise Expansion)**:
   ```
   ESP32 ──> ThingsBoard ──> Cloudflare Worker (Holds Secret API Key) ──> GitHub Pages
   ```

---

## 3. ThingsBoard Integration Procedure

### ESP32 Telemetry Format
The ESP32 transmits telemetry packets to ThingsBoard as a JSON payload:
```json
{
  "tds": 320,
  "turbidity": 2.4,
  "status": "NORMAL"
}
```

### ThingsBoard REST API Query
The dashboard retrieves telemetry via the standard ThingsBoard REST API:
```
GET {thingsboard_host}/api/plugins/telemetry/DEVICE/{deviceId}/values/timeseries?keys=tds,turbidity,status
```
Headers:
```
X-Authorization: Bearer <Read_Only_Or_Public_Token>
```

---

## 4. GitHub Pages Deployment Steps

1. Commit and push the repository to GitHub:
   ```bash
   git add .
   git commit -m "Deploy IoT Water Quality Monitoring Dashboard"
   git push origin main
   ```
2. In your GitHub repository:
   - Go to **Settings** &rarr; **Pages**.
   - Under **Build and deployment** &gt; **Source**, select **Deploy from a branch**.
   - Under **Branch**, select `main` and folder `/ (root)` or `/water-quality-dashboard`.
   - Click **Save**.
3. In 1–2 minutes, your website is live at:
   `https://<username>.github.io/<repo-name>/`

---

## 5. Important Project Limitation

> [!IMPORTANT]
> **TDS and turbidity measurements alone cannot determine whether water is microbiologically safe or unsafe.**
> This system is designed as an **early-warning screening system** to detect physical intrusion, silt runoff, pipe bursts, or salinization. Pathogens (*E. coli*, cholera, norovirus) and heavy metals require laboratory certified testing.
