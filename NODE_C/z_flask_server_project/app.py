import os
import json
import time
from datetime import datetime
from flask import Flask, request, jsonify, render_template_string, send_from_directory
from flask_sqlalchemy import SQLAlchemy

app = Flask(__name__)

# --- KONFIGURACJA ---
BASE_DIR = r"S:\My program files\sqlite"
DB_FILE_PATH = os.path.join(BASE_DIR, "measurements.db")
IMAGES_DIR = os.path.join(BASE_DIR, "obrazy")
os.makedirs(IMAGES_DIR, exist_ok=True)

app.config['SQLALCHEMY_DATABASE_URI'] = f'sqlite:///{DB_FILE_PATH}'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

db = SQLAlchemy(app)

# --- MODEL BAZY ---
class Measurement(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    timestamp = db.Column(db.DateTime, default=datetime.now)
    session_name = db.Column(db.String(50))
    frame_number = db.Column(db.Integer) # To będziemy wyliczać w Pythonie
    size_kb = db.Column(db.Float)
    snr = db.Column(db.Float)
    rssi = db.Column(db.Float)
    toa = db.Column(db.Integer)
    file_path = db.Column(db.String(200))
    # Dodajemy pole na typ (Full/Diff), żeby baza to trzymała
    img_type = db.Column(db.Integer) 

with app.app_context():
    # Jeśli dodaliśmy nową kolumnę img_type, a tabela istnieje,
    # Python może rzucić błąd. Najlepiej usuń plik .db przed uruchomieniem.
    db.create_all()

# --- ENDPOINTY ---

@app.route('/files/<path:filename>')
def serve_file(filename):
    return send_from_directory(IMAGES_DIR, filename)

@app.route('/upload', methods=['POST'])
def handle_upload():
    print("-" * 40)
    print(f"[{time.strftime('%H:%M:%S')}] Otrzymano dane...")

    if 'stats' not in request.form or 'image' not in request.files:
        return "Missing data", 400

    try:
        stats = json.loads(request.form['stats'])
        file = request.files['image']
        
        # Zapis pliku
        timestamp_str = time.strftime("%Y%m%d_%H%M%S")
        filename = f"img_{timestamp_str}.bin" 
        full_path = os.path.join(IMAGES_DIR, filename)
        file.save(full_path)
        file_size_kb = os.path.getsize(full_path) / 1024.0

        # Sesja
        session_val = stats.get('session', f"Sesja_{time.strftime('%Y-%m-%d')}")
        
        # --- NOWA LOGIKA NUMEROWANIA (PYTHON) ---
        # Sprawdzamy, ile klatek ma już ta sesja w bazie
        existing_count = Measurement.query.filter_by(session_name=session_val).count()
        new_frame_number = existing_count + 1

        # Odczyt typu obrazu z JSON (wysyłanego przez ESP32)
        # Jeśli ESP nie wyśle, domyślnie 0
        img_type_val = stats.get('img_type', 0)

        new_meas = Measurement(
            session_name=session_val,
            frame_number=new_frame_number, # Używamy naszego licznika
            size_kb=round(file_size_kb, 2),
            snr=stats.get('snr_avg', 0),
            rssi=stats.get('rssi_avg', 0),
            toa=stats.get('toa_ms', 0),
            file_path=filename,
            img_type=img_type_val
        )
        
        db.session.add(new_meas)
        db.session.commit()
        
        print(f" -> Zapisano Klatkę #{new_frame_number} (Typ: {img_type_val})")
        return "Upload OK", 200

    except Exception as e:
        print(f" -> BŁĄD: {e}")
        return f"Error: {e}", 500

@app.route('/api/data/<session_name>')
def get_chart_data(session_name):
    data = Measurement.query.filter_by(session_name=session_name).order_by(Measurement.id).all()
    
    if not data:
        return jsonify({"labels": [], "values": [], "images": [], "avg_val": 0, "snr": [], "rssi": [], "toa": []})

    values = [d.size_kb for d in data]
    avg_val = sum(values) / len(values) if values else 0

    return jsonify({
        # Oś X: Numer klatki (wyliczony przez Pythona)
        "labels": [d.frame_number for d in data], 
        "values": values,
        "images": [d.file_path for d in data],
        "snr": [d.snr for d in data],
        "rssi": [d.rssi for d in data],
        "toa": [d.toa for d in data],
        # Przekazujemy też typ obrazu do frontendu
        "types": [d.img_type for d in data],
        "avg_val": avg_val
    })

# --- DASHBOARD HTML (Z AKTUALIZACJĄ TYPÓW) ---
HTML_TEMPLATE = """
<!DOCTYPE html>
<html lang="pl">
<head>
    <meta charset="UTF-8">
    <title>LoRa Advanced Dashboard</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: 'Segoe UI', sans-serif; background: #f0f2f5; padding: 20px; margin: 0; }
        .container { max-width: 1400px; margin: 0 auto; }
        .header-controls { background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.05); display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
        select { padding: 8px; font-size: 16px; min-width: 250px; }
        button { padding: 8px 20px; background: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; }
        
        .dashboard-grid { display: flex; flex-direction: column; gap: 20px; }
        .image-section { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.05); display: flex; gap: 20px; justify-content: center; align-items: flex-start; min-height: 400px; }
        .left-col { display: flex; flex-direction: column; gap: 10px; align-items: center; }
        .canvas-wrapper { border: 1px solid #ccc; box-shadow: 0 5px 15px rgba(0,0,0,0.2); background: #000; display: inline-block; }
        .image-info { width: 300px; padding: 15px; background: #f8f9fa; border-radius: 8px; font-size: 0.9em; height: 100%; }
        
        .charts-section { display: flex; flex-direction: column; gap: 15px; }
        .chart-card { background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.05); height: 250px; position: relative; }
        
        .badge { padding: 3px 8px; border-radius: 4px; color: white; font-weight: bold; font-size: 0.8em; }
        .badge-base { background: #28a745; } /* Zielony */
        .badge-diff { background: #ffc107; color: #333; } /* Żółty */
        
        label { cursor: pointer; display: flex; align-items: center; gap: 5px; font-weight: bold; color: #555; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header-controls">
            <h2>📡 Analiza: <span id="sessionTitle" style="color:#007bff">...</span></h2>
            <div>
                <select id="sessionSelect" onchange="loadData()">
                    {% for s in sessions %}
                    <option value="{{ s }}">{{ s }}</option>
                    {% endfor %}
                </select>
                <button onclick="loadData()">🔄 Odśwież</button>
            </div>
        </div>

        <div class="dashboard-grid">
            <div class="image-section">
                <div class="left-col">
                    <label>
                        <input type="checkbox" id="rawViewCheckbox" onchange="redrawLastImage()"> 
                        Pokaż surową klatkę
                    </label>
                    <div class="canvas-wrapper">
                        <canvas id="imgCanvas" width="320" height="240"></canvas>
                    </div>
                </div>
                <div class="image-info" id="imgInfo">
                    <h3>Kliknij na wykres...</h3>
                </div>
            </div>

            <div class="charts-section">
                <div class="chart-card"><canvas id="chartSize"></canvas></div>
                <div class="chart-card"><canvas id="chartToA"></canvas></div>
                <div class="chart-card"><canvas id="chartSNR"></canvas></div>
                <div class="chart-card"><canvas id="chartRSSI"></canvas></div>
            </div>
        </div>
    </div>

    <script>
        let charts = {};
        let currentData = {};
        let lastSelectedIndex = -1;
        
        function redrawLastImage() {
            if (lastSelectedIndex >= 0) reconstructImage(lastSelectedIndex);
        }

        async function reconstructImage(index) {
            lastSelectedIndex = index;
            const canvas = document.getElementById('imgCanvas');
            const ctx = canvas.getContext('2d');
            const infoBox = document.getElementById('imgInfo');
            const showRaw = document.getElementById('rawViewCheckbox').checked;

            const currentFile = currentData.images[index];
            const currentSize = currentData.values[index];
            
            // Pobieramy typ obrazu z danych (1=FULL, 2=DIFF)
            // Jeśli nie ma danych w bazie (stare rekordy), zgadujemy po rozmiarze
            let imgType = currentData.types ? currentData.types[index] : 0;
            
            // Fallback: jeśli typ nieznany, zgadnij: >8kB to BASE
            let isBaseFrame = (imgType === 1); 
            if (imgType === 0) isBaseFrame = (currentSize > 8.0 || index === 0);

            // Szukanie bazy dla DIFFa
            let baseIndex = 0;
            if (!isBaseFrame) {
                for (let i = index; i >= 0; i--) {
                    // Szukamy wstecz klatki typu 1 (FULL) lub dużej
                    let t = currentData.types ? currentData.types[i] : 0;
                    if (t === 1 || currentData.values[i] > 8.0 || i === 0) {
                        baseIndex = i;
                        break;
                    }
                }
            } else {
                baseIndex = index;
            }

            infoBox.innerHTML = `
                <h3>Klatka #${currentData.labels[index]}</h3>
                <p><span class="badge ${isBaseFrame ? 'badge-base' : 'badge-diff'}">${isBaseFrame ? 'BASE (Pełna)' : 'DIFF (Różnica)'}</span></p>
                <p><b>Plik:</b> ${currentFile}</p>
                <p><b>Rozmiar:</b> ${currentSize.toFixed(2)} kB</p>
                <p><b>SNR:</b> ${currentData.snr[index].toFixed(2)} dB</p>
                <p><b>ToA:</b> ${currentData.toa[index]} ms</p>
                ${!isBaseFrame ? `<hr><small>Baza: #${currentData.labels[baseIndex]}</small>` : ''}
            `;

            const imgCurrent = new Image();
            imgCurrent.src = '/files/' + currentFile;
            await new Promise(r => imgCurrent.onload = r);

            if (canvas.width !== imgCurrent.width) {
                canvas.width = imgCurrent.width;
                canvas.height = imgCurrent.height;
            }
            ctx.clearRect(0, 0, canvas.width, canvas.height);

            if (isBaseFrame || showRaw) {
                ctx.drawImage(imgCurrent, 0, 0);
            } else {
                // Rekonstrukcja
                const imgBase = new Image();
                imgBase.src = '/files/' + currentData.images[baseIndex];
                await new Promise(r => imgBase.onload = r);
                ctx.drawImage(imgBase, 0, 0);

                const tempCanvas = document.createElement('canvas');
                tempCanvas.width = imgCurrent.width;
                tempCanvas.height = imgCurrent.height;
                const tempCtx = tempCanvas.getContext('2d');
                tempCtx.drawImage(imgCurrent, 0, 0);

                const frameData = tempCtx.getImageData(0, 0, tempCanvas.width, tempCanvas.height);
                const data = frameData.data;
                for (let i = 0; i < data.length; i += 4) {
                    if (data[i] < 15 && data[i+1] < 15 && data[i+2] < 15) {
                        data[i+3] = 0; 
                    }
                }
                tempCtx.putImageData(frameData, 0, 0);
                ctx.drawImage(tempCanvas, 0, 0);
            }
        }

        function initCharts() {
            const commonOptions = {
                responsive: true, maintainAspectRatio: false,
                interaction: { mode: 'index', intersect: false },
                scales: { x: { display: false } },
                plugins: { legend: { display: false } },
                onClick: (e, els) => { if (els.length) reconstructImage(els[0].index); }
            };

            const createChart = (id, label, color, yLabel) => new Chart(document.getElementById(id), {
                type: 'line', data: { labels: [], datasets: [] },
                options: { ...commonOptions, plugins: { title: { display: true, text: label }, legend: {display: false} }, scales: { ...commonOptions.scales, y: { title: { display: true, text: yLabel } } } }
            });

            charts.size = createChart('chartSize', 'Rozmiar Klatki', '#007bff', 'kB');
            charts.toa = createChart('chartToA', 'Time on Air', '#ffc107', 'ms');
            charts.snr = createChart('chartSNR', 'SNR', '#28a745', 'dB');
            charts.rssi = createChart('chartRSSI', 'RSSI', '#dc3545', 'dBm');
            charts.rssi.options.scales.x = { title: { display: true, text: 'Numer Klatki' }, display: true };
        }

        async function loadData() {
            const session = document.getElementById('sessionSelect').value;
            document.getElementById('sessionTitle').innerText = session;
            const res = await fetch('/api/data/' + session);
            const data = await res.json();
            currentData = data;

            const update = (chart, vals, color) => {
                chart.data.labels = data.labels;
                chart.data.datasets = [{ data: vals, borderColor: color, backgroundColor: color+'20', borderWidth: 2, pointRadius: 2, tension: 0.2 }];
                chart.update();
            };

            update(charts.size, data.values, '#007bff');
            update(charts.toa, data.toa, '#fd7e14');
            update(charts.snr, data.snr, '#28a745');
            update(charts.rssi, data.rssi, '#dc3545');

            if (data.images.length > 0) reconstructImage(0);
        }

        window.onload = () => {
            initCharts();
            const sel = document.getElementById('sessionSelect');
            if(sel && sel.options.length > 0) { sel.selectedIndex = 0; loadData(); }
        };
    </script>
</body>
</html>
"""

@app.route('/')
def dashboard():
    with app.app_context():
        sessions = db.session.query(Measurement.session_name).distinct().all()
        sessions_list = sorted([s[0] for s in sessions if s[0]], reverse=True)
    return render_template_string(HTML_TEMPLATE, sessions=sessions_list)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)