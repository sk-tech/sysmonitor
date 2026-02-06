# Week 7: Machine Learning Anomaly Detection - CODE EXAMPLES

## Quick Code Examples

### 1. Python: Using the ML Module

```python
#!/usr/bin/env python3
"""Example: ML anomaly detection in Python"""
import sys
sys.path.insert(0, 'python')

from sysmon.ml import AnomalyDetector
import time

# Initialize detector
detector = AnomalyDetector(
    db_path="~/.sysmon/data.db",
    use_ml=True,
    use_statistical=True,
    use_baseline=True
)

# Train on historical data
print("Training models on last 24 hours...")
detector.train_metric('cpu.total_usage', hours=24)

# Detect anomaly on current value
current_value = 85.3  # Example high CPU
timestamp = int(time.time())

results = detector.detect(
    metric_type='cpu.total_usage',
    value=current_value,
    timestamp=timestamp
)

# Get consensus
is_anomaly, confidence = detector.get_consensus(results)

print(f"Value: {current_value}%")
print(f"Anomaly: {is_anomaly}")
print(f"Confidence: {confidence:.1%}")
print("\nDetection Methods:")
for method, result in results.items():
    status = "⚠️ " if result.is_anomaly else "✓"
    print(f"  {status} {method}: score={result.score:.2f}")
```

### 2. Bash: API Usage

```bash
#!/bin/bash
# Example: Using ML API endpoints

# Train models
curl -X POST http://localhost:8000/api/ml/train \
  -H "Content-Type: application/json" \
  -d '{"metric": "cpu.total_usage", "hours": 24}' \
  | jq .

# Detect anomaly
curl "http://localhost:8000/api/ml/detect?metric=cpu.total_usage" \
  | jq .

# Get baseline
curl "http://localhost:8000/api/ml/baseline?metric=cpu.total_usage" \
  | jq '.baseline | {mean, stddev, thresholds}'

# Get forecast
curl "http://localhost:8000/api/ml/predict?metric=cpu.total_usage&horizon=1h" \
  | jq '.predictions[:5]'
```

### 3. Python: Baseline Learning

```python
from sysmon.ml import BaselineLearner

# Initialize
learner = BaselineLearner(db_path="~/.sysmon/data.db")

# Learn baseline from last 24 hours
baseline = learner.learn_baseline(
    metric_type='cpu.total_usage',
    host='localhost',
    hours=24
)

if baseline:
    print(f"Baseline learned:")
    print(f"  Mean: {baseline.mean:.2f}%")
    print(f"  Std Dev: {baseline.stddev:.2f}%")
    print(f"  Range: {baseline.min_value:.1f}%-{baseline.max_value:.1f}%")
    print(f"  95th percentile: {baseline.percentile_95:.2f}%")
    
    # Get dynamic thresholds
    lower, upper = baseline.get_threshold(sigma=3.0)
    print(f"  Thresholds: {lower:.2f}%-{upper:.2f}%")

# Check if value is anomalous
is_anomalous, baseline = learner.is_anomalous(
    metric_type='cpu.total_usage',
    value=85.0,
    sigma=3.0
)

if is_anomalous:
    print("⚠️  Value exceeds baseline threshold!")
```

### 4. Python: Statistical Detection

```python
from sysmon.ml.models import StatisticalDetector

# Initialize detector
detector = StatisticalDetector(
    window_size=100,  # Track last 100 points
    z_threshold=3.0   # 3 standard deviations
)

# Feed historical data
for value in historical_values:
    detector.update(value)

# Detect on new value
result = detector.detect(value=85.3, timestamp=int(time.time()))

if result.is_anomaly:
    print(f"⚠️  Anomaly detected!")
    print(f"  Value: {result.value:.2f}")
    print(f"  Expected: {result.expected_value:.2f}")
    print(f"  Z-score: {result.score:.2f}")
    print(f"  Confidence: {result.confidence:.1%}")
```

### 5. Python: Isolation Forest

```python
from sysmon.ml.models import IsolationForestDetector
import numpy as np

# Initialize
detector = IsolationForestDetector(
    contamination=0.1,  # Expect 10% outliers
    n_estimators=100    # 100 trees
)

# Train on historical data
training_data = np.array([...])  # Your time series
detector.train(training_data)

# Detect
result = detector.detect(
    value=85.3,
    timestamp=int(time.time())
)

if result.is_anomaly:
    print(f"⚠️  ML detected anomaly: score={result.score:.2f}")
```

### 6. Python: Monitoring Loop

```python
import time
from sysmon.ml import AnomalyDetector

detector = AnomalyDetector()
detector.train_metric('cpu.total_usage', hours=24)

# Real-time monitoring
while True:
    # Get current value (from your monitoring system)
    current = get_cpu_usage()  # Your function
    
    # Detect
    results = detector.detect(
        'cpu.total_usage',
        current,
        int(time.time())
    )
    
    # Check consensus
    is_anomaly, confidence = detector.get_consensus(results)
    
    if is_anomaly and confidence > 0.5:
        # Send alert
        send_alert(f"CPU anomaly: {current:.1f}% (confidence: {confidence:.1%})")
    
    time.sleep(60)  # Check every minute
```

### 7. JavaScript: Dashboard Integration

```javascript
// Fetch and display anomaly detection
async function checkAnomaly(metric) {
    const response = await fetch(
        `/api/ml/detect?metric=${metric}`
    );
    const data = await response.json();
    
    // Update UI
    const card = document.getElementById('anomalyCard');
    if (data.is_anomaly) {
        card.classList.add('anomaly');
        card.querySelector('.status').textContent = '⚠️ ANOMALY';
        card.style.borderColor = '#ff6b6b';
    } else {
        card.classList.remove('anomaly');
        card.querySelector('.status').textContent = '✓ NORMAL';
        card.style.borderColor = '#4caf50';
    }
    
    // Update confidence
    document.getElementById('confidence').textContent = 
        `${(data.confidence * 100).toFixed(1)}%`;
    
    // Update methods
    for (const [method, result] of Object.entries(data.methods)) {
        const elem = document.getElementById(`method-${method}`);
        elem.textContent = result.is_anomaly ? '⚠️' : '✓';
        elem.style.color = result.is_anomaly ? '#ff6b6b' : '#4caf50';
    }
}

// Auto-refresh
setInterval(() => checkAnomaly('cpu.total_usage'), 10000);
```

### 8. Bash: Automated Training Script

```bash
#!/bin/bash
# automated_training.sh - Periodic model retraining

METRICS=("cpu.total_usage" "memory.usage_percent" "cpu.load_avg_1m")
API_URL="http://localhost:8000"

echo "Starting automated training..."

for metric in "${METRICS[@]}"; do
    echo "Training $metric..."
    
    curl -X POST "$API_URL/api/ml/train" \
      -H "Content-Type: application/json" \
      -d "{\"metric\": \"$metric\", \"hours\": 24}" \
      -s | jq -r '.status'
    
    sleep 2
done

echo "Training complete!"
```

### 9. Python: Batch Detection

```python
from sysmon.ml import AnomalyDetector

detector = AnomalyDetector()

# Get historical data
values = [
    (timestamp1, value1),
    (timestamp2, value2),
    # ...
]

# Batch detection (faster than individual)
results = detector.detect_batch(
    metric_type='cpu.total_usage',
    values=values
)

# Analyze results
anomaly_count = sum(
    1 for method_results in results.values()
    for result in method_results
    if result.is_anomaly
)

print(f"Found {anomaly_count} anomalies in {len(values)} points")
```

### 10. Python: Forecasting

```python
from sysmon.ml import AnomalyDetector

detector = AnomalyDetector()

# Generate forecast
predictions = detector.forecast(
    metric_type='cpu.total_usage',
    horizon_hours=2,
    host='localhost'
)

if predictions:
    print("CPU Forecast (next 2 hours):")
    for timestamp, value in predictions[:10]:
        from datetime import datetime
        dt = datetime.fromtimestamp(timestamp)
        print(f"  {dt.strftime('%H:%M:%S')}: {value:.2f}%")
```

## Integration Examples

### With AlertManager (Future)

```python
from sysmon.alert_manager import AlertManager
from sysmon.ml import AnomalyDetector

# Initialize both systems
alert_mgr = AlertManager()
ml_detector = AnomalyDetector()

# Custom handler for ML anomalies
class MLAnomalyHandler:
    def __init__(self, detector):
        self.detector = detector
    
    def check_metric(self, metric_name, value):
        results = self.detector.detect(
            metric_name, value, int(time.time())
        )
        is_anomaly, confidence = self.detector.get_consensus(results)
        
        if is_anomaly and confidence > 0.6:
            # Fire alert
            alert_mgr.FireAlert(
                alert_name=f"ml_{metric_name}",
                current_value=value,
                threshold=0.0,  # ML-based, no fixed threshold
                message=f"ML detected anomaly (confidence: {confidence:.1%})"
            )

# Use in monitoring loop
handler = MLAnomalyHandler(ml_detector)
handler.check_metric('cpu.total_usage', 85.3)
```

## Testing Examples

### Unit Test

```python
import unittest
from sysmon.ml.models import StatisticalDetector

class TestStatisticalDetector(unittest.TestCase):
    def setUp(self):
        self.detector = StatisticalDetector(window_size=50, z_threshold=3.0)
    
    def test_normal_values(self):
        """Test that normal values don't trigger anomaly"""
        for i in range(60):
            value = 30 + random.gauss(0, 2)  # Normal around 30%
            result = self.detector.detect(value, int(time.time()))
            self.assertFalse(result.is_anomaly)
    
    def test_anomaly_detection(self):
        """Test that outliers are detected"""
        # Feed normal data
        for i in range(60):
            self.detector.update(30 + random.gauss(0, 2))
        
        # Test with anomaly
        result = self.detector.detect(80.0, int(time.time()))
        self.assertTrue(result.is_anomaly)
        self.assertGreater(result.score, 3.0)

if __name__ == '__main__':
    unittest.main()
```

## Documentation

Full documentation available in:
- [week7-implementation.md](../docs/week7-implementation.md) - Complete implementation details
- [week7-quickstart.md](../docs/week7-quickstart.md) - Quick start guide
- [week7-summary.md](../docs/week7-summary.md) - Week 7 summary

## Demo

Run the complete demo:
```bash
./scripts/demo-ml.sh
```

## Support

For issues or questions:
1. Check [week7-implementation.md](../docs/week7-implementation.md) troubleshooting section
2. Verify dependencies: `python3 -c "import numpy, scipy, sklearn"`
3. Test ML module: `python3 scripts/test_ml.py`
