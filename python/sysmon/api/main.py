"""
SysMonitor FastAPI Web Server

Provides REST and WebSocket endpoints for metrics.
"""

from fastapi import FastAPI, WebSocket, Query
from fastapi.responses import HTMLResponse
from sysmon.storage import query_api
import os
from datetime import datetime, timedelta

app = FastAPI(title="SysMonitor API", version="0.1.0")

DB_PATH = os.path.expanduser("~/.sysmon/data.db")

@app.get("/api/metrics/latest")
def get_latest_metric(metric: str, host: str = None):
    """Get the latest value for a metric"""
    result = query_api.query_latest(DB_PATH, metric, host)
    if not result:
        return {"error": "No data found"}
    result["datetime"] = datetime.fromtimestamp(result["timestamp"]).isoformat()
    return result

@app.get("/api/metrics/history")
def get_metric_history(
    metric: str,
    duration: str = Query("1h", description="e.g. 1h, 30m, 24h, 7d"),
    limit: int = 100,
    host: str = None
):
    """Get historical data for a metric"""
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
    end = datetime.now()
    start = end - timedelta(seconds=seconds)
    df = query_api.query_range(DB_PATH, metric, start, end, limit=limit, host=host)
    if df.empty:
        return {"data": []}
    return {"data": df.to_dict(orient="records")}

@app.get("/dashboard", response_class=HTMLResponse)
def dashboard():
    """Serve minimal dashboard HTML"""
    return """
    <html>
    <head><title>SysMonitor Dashboard</title></head>
    <body>
    <h1>SysMonitor Dashboard</h1>
    <div id="metrics"></div>
    <script>
    async function fetchLatest() {
      const resp = await fetch('/api/metrics/latest?metric=cpu.total_usage');
      const data = await resp.json();
      document.getElementById('metrics').innerText = 'CPU Usage: ' + (data.value || 'N/A') + '%';
    }
    setInterval(fetchLatest, 2000);
    fetchLatest();
    </script>
    </body>
    </html>
    """
