#!/usr/bin/env python3
"""
Simple HTTP server for testing without FastAPI dependencies.
Uses standard library only with direct SQLite access.
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import os
import sys
import sqlite3
import threading
import time
from datetime import datetime, timedelta
from urllib.parse import urlparse, parse_qs

DB_PATH = os.path.expanduser("~/.sysmon/data.db")
HAS_STORAGE = os.path.exists(DB_PATH)

# WebSocket-like streaming state
streaming_clients = []
streaming_lock = threading.Lock()

# ML module (lazy loading)
ML_AVAILABLE = False
anomaly_detector = None

try:
    from sysmon.ml import AnomalyDetector
    ML_AVAILABLE = True
except ImportError:
    print("Warning: ML module not available. Install with: pip install -r requirements-ml.txt")

def get_anomaly_detector():
    """Lazy initialization of anomaly detector"""
    global anomaly_detector
    if anomaly_detector is None and ML_AVAILABLE:
        anomaly_detector = AnomalyDetector(db_path=DB_PATH)
    return anomaly_detector

def query_db_latest(metric_type):
    """Query latest metric directly from SQLite"""
    if not HAS_STORAGE:
        return None
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        cursor.execute("""
            SELECT timestamp, metric_type, host, tags, value 
            FROM metrics 
            WHERE metric_type = ? 
            ORDER BY timestamp DESC 
            LIMIT 1
        """, (metric_type,))
        row = cursor.fetchone()
        conn.close()
        
        if row:
            return {
                'timestamp': row[0],
                'metric_type': row[1],
                'host': row[2],
                'tags': row[3],
                'value': row[4]
            }
        return None
    except Exception as e:
        print(f"Database error: {e}")
        return None

def query_db_range(metric_type, start_ts, end_ts, limit=100):
    """Query metric range directly from SQLite"""
    if not HAS_STORAGE:
        return []
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        cursor.execute("""
            SELECT timestamp, metric_type, host, tags, value 
            FROM metrics 
            WHERE metric_type = ? AND timestamp >= ? AND timestamp <= ?
            ORDER BY timestamp DESC 
            LIMIT ?
        """, (metric_type, start_ts, end_ts, limit))
        rows = cursor.fetchall()
        conn.close()
        
        return [{
            'timestamp': row[0],
            'metric_type': row[1],
            'host': row[2],
            'tags': row[3],
            'value': row[4],
            'datetime': datetime.fromtimestamp(row[0]).isoformat()
        } for row in rows]
    except Exception as e:
        print(f"Database error: {e}")
        return []

def query_db_metric_types():
    """Get all metric types from database"""
    if not HAS_STORAGE:
        return []
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        cursor.execute("SELECT DISTINCT metric_type FROM metrics ORDER BY metric_type")
        rows = cursor.fetchall()
        conn.close()
        return [row[0] for row in rows]
    except Exception as e:
        print(f"Database error: {e}")
        return []

class SysMonitorHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)
        
        if path == '/':
            self.send_dashboard()
        elif path == '/api/health':
            self.send_json({'status': 'ok', 'storage': HAS_STORAGE, 'ml_available': ML_AVAILABLE})
        elif path == '/api/metrics/latest':
            self.handle_latest(query)
        elif path == '/api/metrics/history':
            self.handle_history(query)
        elif path == '/api/metrics/types':
            self.handle_metric_types()
        elif path == '/api/stream':
            self.handle_stream()
        elif path == '/api/ml/detect':
            self.handle_ml_detect(query)
        elif path == '/api/ml/baseline':
            self.handle_ml_baseline(query)
        elif path == '/api/ml/predict':
            self.handle_ml_predict(query)
        else:
            self.send_error(404, "Not found")
    
    def do_POST(self):
        """Handle POST requests"""
        parsed = urlparse(self.path)
        path = parsed.path
        
        # Read request body
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8') if content_length > 0 else '{}'
        
        try:
            data = json.loads(body)
        except json.JSONDecodeError:
            self.send_json({'error': 'Invalid JSON'}, 400)
            return
        
        if path == '/api/ml/train':
            self.handle_ml_train(data)
        else:
            self.send_error(404, "Not found")
    
    def do_OPTIONS(self):
        """Handle CORS preflight requests"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()
    
    def send_json(self, data, status=200):
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())
    
    def send_html(self, html):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.end_headers()
        self.wfile.write(html.encode())
    
    def handle_latest(self, query):
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        metric = query.get('metric', [''])[0]
        if not metric:
            self.send_json({'error': 'metric parameter required'}, 400)
            return
        
        try:
            result = query_db_latest(metric)
            if result:
                result['datetime'] = datetime.fromtimestamp(result['timestamp']).isoformat()
                self.send_json(result)
            else:
                self.send_json({'error': 'No data found'}, 404)
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
    
    def handle_history(self, query):
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        metric = query.get('metric', [''])[0]
        duration = query.get('duration', ['1h'])[0]
        limit = int(query.get('limit', ['100'])[0])
        
        if not metric:
            self.send_json({'error': 'metric parameter required'}, 400)
            return
        
        try:
            # Parse duration
            seconds = 3600
            if duration:
                unit = duration[-1]
                value = int(duration[:-1])
                if unit == 'h':
                    seconds = value * 3600
                elif unit == 'm':
                    seconds = value * 60
                elif unit == 'd':
                    seconds = value * 86400
            
            end_ts = int(datetime.now().timestamp())
            start_ts = end_ts - seconds
            
            results = query_db_range(metric, start_ts, end_ts, limit)
            self.send_json({'data': results})
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
    
    def handle_metric_types(self):
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        try:
            types = query_db_metric_types()
            self.send_json({'metrics': types})
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
    
    def handle_stream(self):
        """Server-Sent Events for real-time metrics"""
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        
        try:
            last_timestamp = 0
            while True:
                # Query key metrics
                metrics_data = {}
                for metric in ['cpu.total_usage', 'memory.usage_percent', 'cpu.load_avg_1m']:
                    result = query_db_latest(metric)
                    if result and result['timestamp'] > last_timestamp:
                        metrics_data[metric] = result
                        last_timestamp = max(last_timestamp, result['timestamp'])
                
                if metrics_data:
                    event_data = f"data: {json.dumps(metrics_data)}\n\n"
                    self.wfile.write(event_data.encode())
                    self.wfile.flush()
                
                time.sleep(2)  # Poll every 2 seconds
        except (BrokenPipeError, ConnectionResetError):
            pass  # Client disconnected
    
    def handle_ml_train(self, data):
        """POST /api/ml/train - Train ML models"""
        if not ML_AVAILABLE:
            self.send_json({'error': 'ML module not available'}, 501)
            return
        
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        detector = get_anomaly_detector()
        if detector is None:
            self.send_json({'error': 'Failed to initialize anomaly detector'}, 500)
            return
        
        try:
            metric_type = data.get('metric', None)
            host = data.get('host', 'localhost')
            hours = data.get('hours', 24)
            
            if metric_type:
                # Train specific metric
                success = detector.train_metric(metric_type, host, hours)
                self.send_json({
                    'status': 'success' if success else 'failed',
                    'metric': metric_type,
                    'host': host,
                    'hours': hours
                })
            else:
                # Train all metrics
                results = detector.train_all_metrics(hours)
                self.send_json({
                    'status': 'success',
                    'trained': sum(1 for v in results.values() if v),
                    'failed': sum(1 for v in results.values() if not v),
                    'details': results
                })
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
    
    def handle_ml_detect(self, query):
        """GET /api/ml/detect - Run anomaly detection"""
        if not ML_AVAILABLE:
            self.send_json({'error': 'ML module not available'}, 501)
            return
        
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        metric = query.get('metric', [''])[0]
        if not metric:
            self.send_json({'error': 'metric parameter required'}, 400)
            return
        
        host = query.get('host', ['localhost'])[0]
        
        detector = get_anomaly_detector()
        if detector is None:
            self.send_json({'error': 'Failed to initialize anomaly detector'}, 500)
            return
        
        try:
            # Get latest value
            latest = query_db_latest(metric)
            if not latest:
                self.send_json({'error': 'No data found for metric'}, 404)
                return
            
            # Run detection
            results = detector.detect(
                metric,
                latest['value'],
                latest['timestamp'],
                host
            )
            
            # Get consensus
            is_anomaly, confidence = detector.get_consensus(results)
            
            # Format results
            response = {
                'metric': metric,
                'host': host,
                'timestamp': latest['timestamp'],
                'value': latest['value'],
                'is_anomaly': is_anomaly,
                'confidence': confidence,
                'methods': {}
            }
            
            for method, result in results.items():
                response['methods'][method] = {
                    'is_anomaly': result.is_anomaly,
                    'score': result.score,
                    'threshold': result.threshold,
                    'expected_value': result.expected_value
                }
            
            self.send_json(response)
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
    
    def handle_ml_baseline(self, query):
        """GET /api/ml/baseline - Get learned baseline"""
        if not ML_AVAILABLE:
            self.send_json({'error': 'ML module not available'}, 501)
            return
        
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        metric = query.get('metric', [''])[0]
        if not metric:
            self.send_json({'error': 'metric parameter required'}, 400)
            return
        
        host = query.get('host', ['localhost'])[0]
        
        detector = get_anomaly_detector()
        if detector is None:
            self.send_json({'error': 'Failed to initialize anomaly detector'}, 500)
            return
        
        try:
            baseline = detector.get_baseline(metric, host)
            if baseline:
                lower, upper = baseline.get_threshold()
                response = {
                    'metric': metric,
                    'host': host,
                    'baseline': baseline.to_dict(),
                    'thresholds': {
                        'lower': lower,
                        'upper': upper
                    }
                }
                self.send_json(response)
            else:
                self.send_json({'error': 'No baseline available for metric'}, 404)
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
    
    def handle_ml_predict(self, query):
        """GET /api/ml/predict - Forecast future values"""
        if not ML_AVAILABLE:
            self.send_json({'error': 'ML module not available'}, 501)
            return
        
        if not HAS_STORAGE:
            self.send_json({'error': 'Storage not available'}, 500)
            return
        
        metric = query.get('metric', [''])[0]
        if not metric:
            self.send_json({'error': 'metric parameter required'}, 400)
            return
        
        host = query.get('host', ['localhost'])[0]
        horizon = query.get('horizon', ['1h'])[0]
        
        # Parse horizon (e.g., '1h', '2h')
        try:
            horizon_hours = int(horizon.rstrip('h'))
        except ValueError:
            self.send_json({'error': 'Invalid horizon format (use: 1h, 2h, etc.)'}, 400)
            return
        
        detector = get_anomaly_detector()
        if detector is None:
            self.send_json({'error': 'Failed to initialize anomaly detector'}, 500)
            return
        
        try:
            predictions = detector.forecast(metric, horizon_hours, host)
            if predictions:
                response = {
                    'metric': metric,
                    'host': host,
                    'horizon_hours': horizon_hours,
                    'predictions': [
                        {'timestamp': ts, 'value': val}
                        for ts, val in predictions
                    ]
                }
                self.send_json(response)
            else:
                self.send_json({'error': 'Insufficient data for prediction'}, 404)
        except Exception as e:
            self.send_json({'error': str(e)}, 500)
    
    def send_dashboard(self):
        html = """
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SysMonitor Dashboard</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif; background: #0f0f23; color: #fff; }
        .header { background: #1a1a2e; padding: 20px; box-shadow: 0 2px 10px rgba(0,0,0,0.5); }
        h1 { color: #00d4ff; font-size: 24px; }
        .status { font-size: 12px; color: #888; margin-top: 5px; }
        .container { max-width: 1400px; margin: 20px auto; padding: 0 20px; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; margin-bottom: 20px; }
        .metric-card { background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); padding: 24px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.3); border: 1px solid #2a2a3e; transition: transform 0.2s; }
        .metric-card:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(0,212,255,0.2); }
        .metric-value { font-size: 48px; font-weight: bold; color: #00d4ff; text-shadow: 0 0 10px rgba(0,212,255,0.5); margin: 10px 0; }
        .metric-label { font-size: 14px; color: #888; text-transform: uppercase; letter-spacing: 1px; }
        .metric-change { font-size: 12px; color: #4caf50; margin-top: 5px; }
        .metric-change.negative { color: #f44336; }
        .timestamp { font-size: 11px; color: #666; margin-top: 8px; }
        .chart-container { background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); padding: 20px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.3); border: 1px solid #2a2a3e; margin-bottom: 20px; }
        .chart-title { color: #00d4ff; font-size: 18px; margin-bottom: 15px; }
        canvas { max-width: 100%; }
        .live-indicator { display: inline-block; width: 8px; height: 8px; background: #4caf50; border-radius: 50%; margin-right: 8px; animation: pulse 2s infinite; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
        .error { color: #f44336; }
        .api-endpoints { background: #1a1a2e; padding: 20px; border-radius: 12px; margin-top: 20px; border: 1px solid #2a2a3e; }
        .api-endpoints h3 { color: #00d4ff; margin-bottom: 10px; }
        .api-endpoints code { background: #0f0f23; padding: 4px 8px; border-radius: 4px; color: #4caf50; }
    </style>
</head>
<body>
    <div class="header">
        <h1>SysMonitor Dashboard</h1>
        <div class="status" id="status"><span class="live-indicator"></span>Live monitoring active</div>
    </div>
    <div class="container">
        <div class="grid" id="metrics"></div>
        <div class="chart-container">
            <div class="chart-title">CPU & Memory Trends (Last 5 Minutes)</div>
            <canvas id="metricsChart" width="800" height="300"></canvas>
        </div>
    
    <script>
        const metrics = [
            {type: 'cpu.total_usage', label: 'CPU Usage', unit: '%'},
            {type: 'memory.usage_percent', label: 'Memory Usage', unit: '%'},
            {type: 'cpu.load_avg_1m', label: 'Load Average (1m)', unit: ''},
            {type: 'memory.used_bytes', label: 'Memory Used', unit: 'bytes', autoScale: true}
        ];
        
        const chartData = {
            cpu: [],
            memory: [],
            timestamps: []
        };
        let previousValues = {};
        
        // Format bytes to human-readable units
        function formatBytes(bytes) {
            if (bytes < 1024) return {value: bytes, unit: 'B'};
            if (bytes < 1024 * 1024) return {value: (bytes / 1024).toFixed(2), unit: 'KB'};
            if (bytes < 1024 * 1024 * 1024) return {value: (bytes / (1024 * 1024)).toFixed(2), unit: 'MB'};
            return {value: (bytes / (1024 * 1024 * 1024)).toFixed(2), unit: 'GB'};
        }
        
        // Simple canvas chart
        function drawChart() {
            const canvas = document.getElementById('metricsChart');
            const ctx = canvas.getContext('2d');
            const width = canvas.width;
            const height = canvas.height;
            
            ctx.clearRect(0, 0, width, height);
            
            if (chartData.cpu.length === 0) return;
            
            const maxPoints = 30;
            const cpu = chartData.cpu.slice(-maxPoints);
            const memory = chartData.memory.slice(-maxPoints);
            
            // Draw grid
            ctx.strokeStyle = '#2a2a3e';
            ctx.lineWidth = 1;
            for (let i = 0; i <= 4; i++) {
                const y = (height - 40) * (i / 4) + 20;
                ctx.beginPath();
                ctx.moveTo(40, y);
                ctx.lineTo(width - 20, y);
                ctx.stroke();
                
                ctx.fillStyle = '#666';
                ctx.font = '10px monospace';
                ctx.fillText(`${100 - (i * 25)}%`, 5, y + 3);
            }
            
            // Draw CPU line
            if (cpu.length > 1) {
                ctx.strokeStyle = '#00d4ff';
                ctx.lineWidth = 2;
                ctx.beginPath();
                cpu.forEach((val, i) => {
                    const x = 40 + ((width - 60) * i / (maxPoints - 1));
                    const y = 20 + ((height - 40) * (1 - val / 100));
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                });
                ctx.stroke();
            }
            
            // Draw Memory line
            if (memory.length > 1) {
                ctx.strokeStyle = '#4caf50';
                ctx.lineWidth = 2;
                ctx.beginPath();
                memory.forEach((val, i) => {
                    const x = 40 + ((width - 60) * i / (maxPoints - 1));
                    const y = 20 + ((height - 40) * (1 - val / 100));
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                });
                ctx.stroke();
            }
            
            // Legend
            ctx.fillStyle = '#00d4ff';
            ctx.fillRect(width - 180, 10, 15, 3);
            ctx.fillStyle = '#fff';
            ctx.font = '12px Arial';
            ctx.fillText('CPU', width - 160, 15);
            
            ctx.fillStyle = '#4caf50';
            ctx.fillRect(width - 100, 10, 15, 3);
            ctx.fillStyle = '#fff';
            ctx.fillText('Memory', width - 80, 15);
        }
        
        async function fetchMetric(metric) {
            try {
                const resp = await fetch(`/api/metrics/latest?metric=${metric.type}`);
                const data = await resp.json();
                
                let value = data.value || 'N/A';
                let displayValue = value;
                let displayUnit = metric.unit;
                
                if (metric.autoScale && typeof value === 'number') {
                    // Auto-scale bytes to appropriate unit
                    const formatted = formatBytes(value);
                    displayValue = formatted.value;
                    displayUnit = formatted.unit;
                } else if (metric.scale && typeof value === 'number') {
                    displayValue = (value / metric.scale).toFixed(2);
                } else if (typeof value === 'number') {
                    displayValue = value.toFixed(2);
                }
                
                // Calculate change
                const prev = previousValues[metric.type];
                let changeHtml = '';
                if (prev !== undefined && typeof value === 'number') {
                    const change = value - prev;
                    const changeClass = change >= 0 ? '' : 'negative';
                    const changeSymbol = change >= 0 ? '▲' : '▼';
                    
                    // Format change value
                    let changeDisplay = Math.abs(change);
                    let changeUnit = displayUnit;
                    if (metric.autoScale) {
                        const formatted = formatBytes(Math.abs(change));
                        changeDisplay = formatted.value;
                        changeUnit = formatted.unit;
                    } else {
                        changeDisplay = changeDisplay.toFixed(2);
                    }
                    
                    changeHtml = `<div class="metric-change ${changeClass}">${changeSymbol} ${changeDisplay}${changeUnit}</div>`;
                }
                previousValues[metric.type] = value;
                
                // Update chart data
                if (metric.type === 'cpu.total_usage' && typeof value === 'number') {
                    chartData.cpu.push(value);
                    chartData.timestamps.push(new Date());
                } else if (metric.type === 'memory.usage_percent' && typeof value === 'number') {
                    chartData.memory.push(value);
                }
                
                return `
                    <div class="metric-card">
                        <div class="metric-label">${metric.label}</div>
                        <div class="metric-value">${displayValue}${displayUnit}</div>
                        ${changeHtml}
                        <div class="timestamp">${data.datetime || ''}</div>
                    </div>
                `;
            } catch (e) {
                return `
                    <div class="metric-card">
                        <div class="metric-label">${metric.label}</div>
                        <div class="error">Error: ${e.message}</div>
                    </div>
                `;
            }
        }
        
        async function updateMetrics() {
            const html = await Promise.all(metrics.map(fetchMetric));
            document.getElementById('metrics').innerHTML = html.join('');
            drawChart();
        }
        
        // Server-Sent Events for real-time updates
        function setupSSE() {
            const eventSource = new EventSource('/api/stream');
            eventSource.onmessage = (event) => {
                const data = JSON.parse(event.data);
                console.log('SSE update:', data);
                // SSE provides real-time updates, but we'll keep polling for simplicity
            };
            eventSource.onerror = () => {
                console.log('SSE connection lost, using polling fallback');
                eventSource.close();
            };
        }
        
        // Initialize
        updateMetrics();
        setInterval(updateMetrics, 2000);
        // setupSSE();  // Optional: Enable for SSE streaming
    </script>
    
    <div class="api-endpoints">
        <h3>API Endpoints</h3>
        <p><code>GET /api/health</code> - Server health status</p>
        <p><code>GET /api/metrics/latest?metric=cpu.total_usage</code> - Latest metric value</p>
        <p><code>GET /api/metrics/history?metric=cpu.total_usage&duration=1h&limit=100</code> - Historical data</p>
        <p><code>GET /api/metrics/types</code> - List all available metrics</p>
        <p><code>GET /api/stream</code> - Server-Sent Events stream (real-time)</p>
        <h4 style="margin-top: 15px; color: #00d4ff;">ML Endpoints (Week 7)</h4>
        <p><code>POST /api/ml/train</code> - Train ML models (body: {metric, host, hours})</p>
        <p><code>GET /api/ml/detect?metric=X</code> - Run anomaly detection</p>
        <p><code>GET /api/ml/baseline?metric=X</code> - Get learned baseline</p>
        <p><code>GET /api/ml/predict?metric=X&horizon=1h</code> - Forecast future values</p>
    </div>
    </div>
</body>
</html>
        """
        self.send_html(html)
    
    def log_message(self, format, *args):
        # Custom logging
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] {format % args}")

def run_server(port=8000):
    server = HTTPServer(('0.0.0.0', port), SysMonitorHandler)
    print(f"SysMonitor API Server v0.1.0")
    print(f"Listening on http://0.0.0.0:{port}")
    print(f"Dashboard: http://localhost:{port}/")
    print(f"Database: {DB_PATH}")
    print(f"Storage available: {HAS_STORAGE}")
    print("\nPress Ctrl+C to stop\n")
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        server.shutdown()

if __name__ == '__main__':
    import sys
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    run_server(port)
