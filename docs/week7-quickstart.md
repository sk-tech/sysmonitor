# Week 7 Quick Start: ML Anomaly Detection

## Installation (5 minutes)

### 1. Install ML Dependencies

**Option A: System packages (recommended for Ubuntu/Debian)**
```bash
sudo apt update
sudo apt install python3-numpy python3-scipy python3-sklearn
```

**Option B: pip (if available)**
```bash
pip3 install numpy scipy scikit-learn
```

**Verify installation:**
```bash
python3 -c "import numpy, scipy, sklearn; print('✓ ML packages ready')"
```

### 2. Ensure System is Running

```bash
# Build if needed
./build.sh

# Start daemon
./build/bin/sysmond &

# Wait for data collection (at least 60 seconds)
echo "Collecting baseline data (60s)..."
sleep 60
```

## Quick Demo (2 minutes)

### Run the automated demo:
```bash
./scripts/demo-ml.sh
```

This will:
- ✅ Check/install dependencies
- ✅ Start daemon + API server
- ✅ Collect baseline data
- ✅ Train ML models
- ✅ Generate synthetic anomaly (CPU stress)
- ✅ Detect anomaly using 3 methods
- ✅ Show forecast

**Expected output:**
```
⚠️  ANOMALY DETECTED!
  Current Value: 87.32%
  Is Anomaly: True
  Confidence: 66.7%
  Detection Methods:
    ⚠️  Statistical: score=4.23
    ⚠️  Baseline: score=5.12
    ✓ Ml: score=0.45
```

## Manual Testing (5 minutes)

### 1. Start API Server
```bash
python3 python/sysmon/api/server.py 8000 &
```

### 2. Train Models
```bash
curl -X POST http://localhost:8000/api/ml/train \
  -H "Content-Type: application/json" \
  -d '{"metric": "cpu.total_usage", "hours": 1}' | jq
```

**Response:**
```json
{
  "status": "success",
  "metric": "cpu.total_usage",
  "host": "localhost",
  "hours": 1
}
```

### 3. Check Baseline
```bash
curl "http://localhost:8000/api/ml/baseline?metric=cpu.total_usage" | jq
```

**Response:**
```json
{
  "metric": "cpu.total_usage",
  "baseline": {
    "mean": 28.5,
    "stddev": 3.2,
    "min_value": 18.3,
    "max_value": 42.1,
    "sample_count": 120
  },
  "thresholds": {
    "lower": 18.9,
    "upper": 38.1
  }
}
```

### 4. Run Detection
```bash
curl "http://localhost:8000/api/ml/detect?metric=cpu.total_usage" | jq
```

**Response:**
```json
{
  "metric": "cpu.total_usage",
  "value": 32.1,
  "is_anomaly": false,
  "confidence": 0.0,
  "methods": {
    "statistical": {
      "is_anomaly": false,
      "score": 1.12
    },
    "baseline": {
      "is_anomaly": false,
      "score": 1.23
    }
  }
}
```

### 5. Generate Anomaly (Optional)
```bash
# Start CPU stress
stress-ng --cpu 8 --timeout 30s &

# Wait a bit
sleep 10

# Detect again
curl "http://localhost:8000/api/ml/detect?metric=cpu.total_usage" | jq
```

**Response (anomaly detected):**
```json
{
  "metric": "cpu.total_usage",
  "value": 89.7,
  "is_anomaly": true,
  "confidence": 0.667,
  "methods": {
    "statistical": {"is_anomaly": true, "score": 4.56},
    "baseline": {"is_anomaly": true, "score": 5.12}
  }
}
```

### 6. Get Forecast
```bash
curl "http://localhost:8000/api/ml/predict?metric=cpu.total_usage&horizon=1h" | jq '.predictions[:5]'
```

## Web Dashboard

Open the ML dashboard in your browser:
```bash
# Option 1: Direct file
firefox ~/sysmonitor/python/sysmon/api/dashboard-ml.html

# Option 2: Via HTTP (if served)
# Navigate to: http://localhost:8000/../dashboard-ml.html
```

**Dashboard features:**
- 📊 Real-time anomaly detection
- 📈 Historical data with threshold visualization
- 🔮 1-hour forecast
- 🎯 Detection methods comparison
- ⚠️ Visual anomaly alerts (red cards)

## API Endpoints Summary

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/ml/train` | Train ML models |
| GET | `/api/ml/detect?metric=X` | Run anomaly detection |
| GET | `/api/ml/baseline?metric=X` | Get learned baseline |
| GET | `/api/ml/predict?metric=X&horizon=1h` | Forecast future values |

## Troubleshooting

### "No module named numpy"
Install dependencies:
```bash
sudo apt install python3-numpy python3-scipy python3-sklearn
```

### "Insufficient data for training"
Wait longer or generate mock data:
```bash
python3 scripts/test_ml.py --generate
```

### "No baseline available"
Check if daemon is collecting data:
```bash
sqlite3 ~/.sysmon/data.db "SELECT COUNT(*) FROM metrics;"
# Should show > 50 rows
```

### "Model must be trained"
Train first:
```bash
curl -X POST http://localhost:8000/api/ml/train -d '{}'
```

## Next Steps

1. **Integrate with Alerts**: Connect ML detection to AlertManager
2. **Tune Parameters**: Adjust z_threshold, contamination based on your metrics
3. **Add More Metrics**: Train models for memory, disk, network
4. **Monitor Performance**: Track false positives/negatives

## Code Integration Example

```python
# In your monitoring code
from sysmon.ml import AnomalyDetector

detector = AnomalyDetector()
detector.train_metric('cpu.total_usage', hours=24)

# Real-time monitoring loop
while True:
    value = get_current_cpu_usage()
    results = detector.detect('cpu.total_usage', value, int(time.time()))
    
    is_anomaly, confidence = detector.get_consensus(results)
    if is_anomaly:
        print(f"⚠️  Anomaly detected! Value: {value:.2f}, Confidence: {confidence:.2%}")
        # Trigger alert, log, etc.
    
    time.sleep(60)
```

## Performance Tips

1. **Training**: Run once hourly, not every detection
2. **Window size**: Use 100-200 points for statistical detector
3. **Thresholds**: Start with 3σ, adjust based on false positive rate
4. **Methods**: Enable all 3 methods for best accuracy

## Documentation

- **Full docs**: [docs/week7-implementation.md](week7-implementation.md)
- **Architecture**: [docs/architecture/system-design.md](architecture/system-design.md)
- **API Reference**: [docs/API.md](API.md)

---

**Ready to use!** Start with `./scripts/demo-ml.sh` for a complete demo.
