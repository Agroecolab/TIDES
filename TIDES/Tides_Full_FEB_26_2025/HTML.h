const char* main_html = R"rawliteral(

<!DOCTYPE html><html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="10">

<style>
html {background-color:Gainsboro; font-family: Helvetica; display: inline; margin: auto auto; text-align: center;}
table {width: 100%%; text-align: center;}
td {width: 50%%; text-align: center; padding: 0 6px;}
</style>
</head>


<body><h1>TIDES</h1>

<font size ="4">

<u><h3>System Status</h3></u>
%SYSTEMSTATUS%

<u><h3>Grow Bin</h3></u>
<table style="margin-left:auto; margin-right:auto;">
%BINSTATUS%
</table>

<u><h3>Flow Parameters</h3></u>
<table><table style="margin-left:auto; margin-right:auto;">
%FLOW%
</table>
<center>Average Flow: %FLOWAVG% L/min</center>

<u><h3>Sensors</h3></u>
<table style="margin-left:auto; margin-right:auto;">
%SENSORS%
</table>

</body>
</html>            


)rawliteral";

const char* admin_html = R"rawliteral(
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
html {background-color: PaleTurquoise; font-family: Helvetica; display: inline; margin: auto auto; text-align: center;}
table {width: 100%%; text-align: center;}

/* if the browser window is at least 800px-s wide: */
@media screen and (min-width: 800px) {
  table {
  width: 70%%;}
}

/* if the browser window is at least 1000px-s wide: */
@media screen and (min-width: 1000px) {
  table {
  width: 40%%;}
}

td {width: 50%%; text-align: center; padding: 0 6px;}

  .chart-container {
    position: relative;
    height: 380px;
    margin: 20px 0 40px 0;
    border: 1px solid #ddd;
    border-radius: 8px;
    padding: 15px;
    background: #fff;
    touch-action: none;
  }

  .controls {
    text-align: center;
    margin: 12px 0 24px 0;
  }

  .latest-reading {
    background: #f8f8f8;
    padding: 12px;
    font-size: 15px;
    border-radius: 5px;
  }
</style>

<script src="/static/chart.umd.min.js"></script>
<script src="/static/hammer.min.js"></script>
<script src="/static/chartjs-plugin-zoom.min.js"></script>

</head><body>
<font size ="4">

<u><h3>System Status</h3></u>

<table style="width: 100%%; margin-left:auto; margin-right:auto; border-radius: 10px; padding: 3px 3px; background-color: #D3D3D3;" > <tr><td>
  <form action="/get">
    %WLDATA%
</table>

<table style="margin-left:auto; margin-right:auto;">
<tr style ="height:35px"><td><form action="/get" target="hidden-form">System Name: <input type="text" name="SystemName" value="%SYSNAME%" style="width: 100px;"></form></td>
<td>MAC: %MAC%</td></tr>

<br>

<tr style ="height:35px"><td>System:  <button type="button" type="button" onclick="window.location.href='/SysPause'">%PAUSE%</button></td>
<td>SD Card:  <button type="button" onclick="window.location.href='/SDrestart'">%SDSTATUS%</button></td></tr>

<tr style ="height:35px"><td>SPIFFS: <button type="button" onclick="window.location.href='/SPIFFSrestart'">%SPIFFSTATUS%</button></td>
<td>PSRAM:  <button type="button" onclick="window.location.href='/PSRAMreset'">%PSRAMSTAT%</button></td></tr>

<tr style ="height:35px"><td>WiFi: <button type="button" onclick="window.location.href='/WIFIreset'">%WIFI%</button></td>
<td>AWS: <button type="button" onclick="window.location.href='/AWSreset'">%AWSstatus%</button></td></tr>
%RAMSTATUS%
</table>



<h3><u>Grow Bin</u></h3>
<form action="/get" target="hidden-form">
<table style="margin-left:auto; margin-right:auto;">
<tr style ="height:35px"><td>Bin Status:  <button type="button" onclick="window.location.href='/switchstatus'"><b>%aSTATUS%</b></button></td>
<td>Bin Volume: <input type="number" name="binVolume" value="%BINVOL%" step="0.01" min="0.00" style="width: 60px;"></td></tr>
<tr style ="height:35px"><td>Valve 1:  <button type="button" onclick="window.location.href='/valve1'"><b>%aVALVE1%</b></button></td>
<td>Valve 2:  <button type="button" onclick="window.location.href='/valve2'"><b>%aVALVE2%</b></button></td></tr></form>
<tr style ="height:35px"><td><form action="/get" target="hidden-form">CSV Line: <input type="number" name="csvLinecount" value="%CSVLINE%" min="0.00" style="width: 60px;"></form></td>
<td><form action="/get" target="hidden-form">WL Set: <input type="number" name="setWL" value="%FUTUREWL%" step="0.01" min="0.00" style="width: 60px;"></form></td></tr>

<tr style ="height:35px"><td><form action="/get" target="hidden-form">TOF Measure: <input type="number" name="TOFmeasure" value="%TOFMM%" min="0.00" style="width: 60px;"></form></td>
<td><form action="/get" target="hidden-form">Empty Offset: <input type="number" name="Emptyoffset" value="%EMPTYOFF%" step="0.01" min="0" style="width: 60px;"></form></td></tr>


<tr style ="height:35px"><td><form action="/get" target="hidden-form">Bin Depth: <input type="number" name="binDepth" value="%BINDEPTH%" min="0.00" step="0.01" style="width: 60px;"></form></td>
<td>Pump: <button type="button" onclick="window.location.href='/pumpswitch'"><b>%PUMP%</b></button></td></tr>
<tr style ="height:35px"><td>Upper: %UPDETECT%</td>
<td>Lower: %LOWDETECT%</td></tr>
</table>
</form>


<h3><u>Flow Parameters</u></h3>
<form action="/get" target="hidden-form">
<table style="margin-left:auto; margin-right:auto;">
<tr style ="height:35px"><td style="text-align: center;"><b>Water In:</b></td>
<td style="text-align: center;"><b>Water Out:</b></td></tr>
<tr style ="height:35px"><td>Flow: %FLOWIN%</td>
<td>Flow:  %FLOWOUT%</td></tr>
<tr style ="height:35px"><td><form action="/get" target="hidden-form">Set: <input type="number" name="SetFlowIn" value="%FLOWINSET%" step="0.01" min="0.00" style="width: 60px;"></form></td>
<td><form action="/get" target="hidden-form">Set: <input type="number" name="SetFlowOut" value="%FLOWOUTSET%" step="0.1" min="0.00" style="width: 60px;"></form></td></tr>
<tr style ="height:35px"><td><form action="/get" target="hidden-form">Cycle: <input type="number" name="SetCycleIn" value="%CYCLEIN%" step="0.01" min="0" style="width: 60px;"></form></td>
<td><form action="/get" target="hidden-form">Cycle: <input type="number" name="SetCycleOut" value="%CYCLEOUT%" step="0.01" min="0" style="width: 60px;"></form></td></tr>
<tr style ="height:35px"><td><form action="/get" target="hidden-form">Pulse: <input type="number" name="In_Pulse_Tot" value="%TOTINPULSE%" min="0" style="width: 60px;"></form></td>
<td><form action="/get" target="hidden-form">Pulse: <input type="number" name="Out_Pulse_Tot" value="%TOTOUTPULSE%" step="0.01" min="0" style="width: 60px;"></form></td></tr>
<tr style ="height:35px"><td><form action="/get" target="hidden-form">Cal Factor: <input type="number" name="In_Cal_Factor" value="%CALIN%" step="0.01" min="0" style="width: 60px;"></form></td>
<td><form action="/get" target="hidden-form">Cal Factor: <input type="number" name="Out_Cal_Factor" value="%CALOUT%" step="0.01" min="0" style="width: 60px;"></form></td></tr>
</table>
</form>
<center>Average Flow: %FLOWAVG% L/min</center>
<br>

<u><h3>Functions</h3></u>
<table style="margin-left:auto; margin-right:auto;">
<tr style ="height:35px"><td style="text-align: center;"><button type="button" onclick="window.location.href='/demo'">%DEMOMODE%</button></td>  
<td></td></tr>
%CALMSG%
<tr style ="height:35px"><td style="text-align: center;"><button type="button" onclick="window.location.href='/CalFlow'">Calibrate Flow</button></td>
<td style="text-align: center;"><button type="button" onclick="window.location.href='/CalVol'">Calibrate Volume</button></td></tr>
<tr style ="height:35px"><td style="text-align: center;"><button type="button" onclick="window.location.href='/Prefsave'">Save Preferences</button></td>
<td><button type="button" onclick="window.location.href='/Prefreset'">Reset Preferences</button></td></tr>
</table>
%CALTABLE%
<u><h3>Sensors</h3></u>
<p><table style="margin-left:auto; margin-right:auto;"><font size ="4">%SENSORLIST%</p>

<h2>Water Level</h2>
<div class="chart-container">
  <canvas id="waterLevelChart"></canvas>
</div>

<div class="controls">
  <button onclick="loadWaterLevelData()">Refresh Water Level Chart</button>
  <button onclick="resetWaterLevelZoom()">Reset Zoom</button>
</div>

<pre id="waterLevelLatest" class="latest-reading"></pre>

<p><font size ="4">%FILELIST%</p>
<p>%ERRORLIST%</p>
<p><p align="center"><font size ="3"><a href="../ClearErrorLog">Clear the Error Log</a></p>

<p><p align="center"><font size ="3"><a href="/NextLevel">Next Level</a></p><br>
<p><p align="center"><font size ="3"><a href="../">Back to Data Logger Page</a></p><br>
<p><p align="center"><font size ="3"><a href="../resetESP">Reset ESP32</a></p>

<h3>To Do List:</h3>
<p>Wifi credentials? Client only? > save to SPIFFS</p>
<p>Textbox to rename data file</p>
<p>Auto update data fields from server</p>
<p>Sensors & NTP server? / reinitialize on admin page</p>

<script>
  let binVolumeChart;
  let waterLevelRows = [];

  function createBinVolumeChart() {
    const zoomOptions = {
      zoom: {
        wheel: { enabled: true },
        pinch: { enabled: true },
        drag: { enabled: true },
        mode: 'x'
      },
      pan: {
        enabled: true,
        mode: 'x',
        threshold: 5
      }
    };

    binVolumeChart = new Chart(document.getElementById('waterLevelChart'), {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'Bin Volume (L)',
            borderColor: '#0077cc',
            backgroundColor: 'rgba(0, 119, 204, 0.12)',
            tension: 0.2,
            pointRadius: 1,
            fill: true,
            data: []
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: {
          mode: 'index',
          intersect: false
        },
        scales: {
          x: {
            title: {
              display: true,
              text: 'Timestamp'
            }
          },
          y: {
            title: {
              display: true,
              text: 'Bin Volume (L)'
            },
            beginAtZero: true
          }
        },
        plugins: {
          zoom: zoomOptions,
          legend: {
            display: true
          }
        }
      }
    });

    document.getElementById('waterLevelChart').addEventListener('dblclick', () => {
      binVolumeChart.resetZoom();
    });
  }

  async function loadWaterLevelData() {
    try {
      const response = await fetch('/waterlevel.csv?nocache=' + Date.now());

      if (!response.ok) {
        document.getElementById('waterLevelLatest').textContent = 'Could not load bin volume data.';
        return;
      }

      const csvText = await response.text();
      waterLevelRows = parseTidesCSV(csvText);

      if (waterLevelRows.length === 0) {
        document.getElementById('waterLevelLatest').textContent = 'No bin volume data logged yet.';
        return;
      }

      // Last 48 hours at 5-minute logging interval: 48 * 60 / 5 = 576 points.
      const displayData = waterLevelRows.slice(-576);

      const labels = displayData.map(row => `${row.date} ${row.time}`);
      const volumeValues = displayData.map(row => toNumber(row.bin_volume));

      binVolumeChart.data.labels = labels;
      binVolumeChart.data.datasets[0].data = volumeValues;
      binVolumeChart.update();

      const last = waterLevelRows[waterLevelRows.length - 1];
      document.getElementById('waterLevelLatest').innerHTML =
        `Bin Volume: ${last.bin_volume} L<br>` +
        `Bin Status: ${last.bin_status}<br>` +
        `Water Depth: ${last.bin_depth} mm<br>` +
        `TOF Raw Distance: ${last.TOF_mm} mm<br>` +
        `<strong>${last.date} ${last.time}</strong>`;

    } catch (error) {
      console.error(error);
      document.getElementById('waterLevelLatest').textContent = 'Error loading bin volume data.';
    }
  }

  function parseTidesCSV(csvText) {
    const lines = csvText.trim().split(/\r?\n/);
    const rows = [];

    if (lines.length < 2) return rows;

    const headers = lines[0].split(',').map(h => h.trim());

    for (let i = 1; i < lines.length; i++) {
      const line = lines[i].trim();
      if (!line) continue;

      const values = line.split(',');
      const row = {};

      headers.forEach((header, index) => {
        row[header] = values[index] !== undefined ? values[index].trim() : '';
      });

      // Only keep rows that have usable bin volume data.
      if (row.bin_volume !== undefined && row.bin_volume !== '') {
        rows.push(row);
      }
    }

    return rows;
  }

  function toNumber(value) {
    const numberValue = parseFloat(value);
    return Number.isFinite(numberValue) ? numberValue : null;
  }

  function resetWaterLevelZoom() {
    if (binVolumeChart) binVolumeChart.resetZoom();
  }

  window.addEventListener('load', () => {
    if (typeof Chart === 'undefined') {
      document.getElementById('waterLevelLatest').textContent =
        'Chart.js did not load. Check /static/chart.umd.min.js or CDN/internet access.';
      return;
    }

    createBinVolumeChart();
    loadWaterLevelData();

    // Refresh every 30 seconds. Your SD log updates every 5 minutes.
    setInterval(loadWaterLevelData, 30000);
  });
</script>
</body>
)rawliteral";