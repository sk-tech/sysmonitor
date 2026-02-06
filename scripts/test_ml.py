#!/usr/bin/env python3
"""
Test ML module functionality with mock data.
This script works even without ML dependencies installed.
"""

import sys
import os
import sqlite3
import time
from datetime import datetime, timedelta
import random

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

DB_PATH = os.path.expanduser("~/.sysmon/data.db")

def generate_mock_data(hours=24, anomaly=False):
    """Generate mock CPU usage data"""
    print(f"Generating {hours} hours of mock data...")
    
    if not os.path.exists(DB_PATH):
        print(f"Error: Database not found at {DB_PATH}")
        print("Please run sysmond first to create the database.")
        return False
    
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # Generate data points (one per minute)
    end_time = int(time.time())
    start_time = end_time - (hours * 3600)
    
    data_points = []
    for timestamp in range(start_time, end_time, 60):
        # Normal behavior: oscillate between 20-40%
        base_value = 30 + 10 * (0.5 + 0.5 * ((timestamp % 3600) / 3600))
        noise = random.gauss(0, 2)
        value = base_value + noise
        
        # Add anomaly in recent data if requested
        if anomaly and timestamp > end_time - 600:  # Last 10 minutes
            value += 50  # Spike to 80%+
        
        data_points.append((
            timestamp,
            'cpu.total_usage',
            'localhost',
            '{}',
            value
        ))
    
    # Insert data
    cursor.executemany("""
        INSERT OR REPLACE INTO metrics (timestamp, metric_type, host, tags, value)
        VALUES (?, ?, ?, ?, ?)
    """, data_points)
    
    conn.commit()
    conn.close()
    
    print(f"✓ Generated {len(data_points)} data points")
    return True

def test_ml_module():
    """Test ML module functionality"""
    print("\n" + "="*60)
    print("Testing ML Module")
    print("="*60 + "\n")
    
    try:
        from sysmon.ml import AnomalyDetector, BaselineLearner, StatisticalDetector
        print("✓ ML module imports successful")
    except ImportError as e:
        print(f"✗ Failed to import ML module: {e}")
        print("\nTo use ML features, install dependencies:")
        print("  python3 -m pip install --user numpy scipy scikit-learn")
        print("  OR: sudo apt install python3-numpy python3-scipy python3-sklearn")
        return False
    
    # Test baseline learner
    print("\n--- Testing Baseline Learner ---")
    try:
        learner = BaselineLearner(DB_PATH)
        print("✓ BaselineLearner initialized")
        
        baseline = learner.learn_baseline('cpu.total_usage', 'localhost', hours=1)
        if baseline:
            print(f"✓ Baseline learned:")
            print(f"  Mean: {baseline.mean:.2f}%")
            print(f"  Std Dev: {baseline.stddev:.2f}%")
            print(f"  Range: {baseline.min_value:.1f}% - {baseline.max_value:.1f}%")
            print(f"  Samples: {baseline.sample_count}")
            
            lower, upper = baseline.get_threshold()
            print(f"  Thresholds: {lower:.2f}% - {upper:.2f}%")
        else:
            print("! No baseline (need more data)")
    except Exception as e:
        print(f"✗ Baseline learner failed: {e}")
        import traceback
        traceback.print_exc()
    
    # Test statistical detector
    print("\n--- Testing Statistical Detector ---")
    try:
        detector = StatisticalDetector(window_size=50, z_threshold=3.0)
        print("✓ StatisticalDetector initialized")
        
        # Feed it some values
        values = [30 + random.gauss(0, 2) for _ in range(60)]
        values.append(80)  # Anomaly
        
        results = []
        for i, val in enumerate(values):
            result = detector.detect(val, int(time.time()) + i)
            results.append(result)
        
        anomalies = sum(1 for r in results if r.is_anomaly)
        print(f"✓ Detected {anomalies} anomalies in {len(results)} points")
        
        # Show last result (should be anomaly)
        last = results[-1]
        print(f"  Last point: value={last.value:.2f}, anomaly={last.is_anomaly}, score={last.score:.2f}")
        
    except Exception as e:
        print(f"✗ Statistical detector failed: {e}")
        import traceback
        traceback.print_exc()
    
    # Test anomaly detector (integration)
    print("\n--- Testing Anomaly Detector (Integration) ---")
    try:
        detector = AnomalyDetector(
            db_path=DB_PATH,
            use_ml=False,  # Skip ML if sklearn not available
            use_statistical=True,
            use_baseline=True
        )
        print("✓ AnomalyDetector initialized")
        
        # Train on CPU metric
        success = detector.train_metric('cpu.total_usage', 'localhost', hours=1)
        if success:
            print("✓ Model trained")
            
            # Run detection on current value
            conn = sqlite3.connect(DB_PATH)
            cursor = conn.cursor()
            cursor.execute("""
                SELECT timestamp, value FROM metrics 
                WHERE metric_type = 'cpu.total_usage' 
                ORDER BY timestamp DESC LIMIT 1
            """)
            row = cursor.fetchone()
            conn.close()
            
            if row:
                timestamp, value = row
                results = detector.detect('cpu.total_usage', value, timestamp)
                
                print(f"✓ Detection completed:")
                print(f"  Value: {value:.2f}%")
                print(f"  Methods: {', '.join(results.keys())}")
                
                for method, result in results.items():
                    print(f"  {method}: anomaly={result.is_anomaly}, score={result.score:.2f}")
                
                is_anomaly, confidence = detector.get_consensus(results)
                print(f"  Consensus: anomaly={is_anomaly}, confidence={confidence:.2%}")
        else:
            print("! Training skipped (insufficient data)")
            
    except Exception as e:
        print(f"✗ Anomaly detector failed: {e}")
        import traceback
        traceback.print_exc()
    
    print("\n" + "="*60)
    print("ML Module Test Complete")
    print("="*60)
    return True

def main():
    print("SysMonitor ML Module Test")
    print("="*60)
    
    if not os.path.exists(DB_PATH):
        print(f"\nError: Database not found at {DB_PATH}")
        print("Please run sysmond first to create the database:")
        print("  ./build/bin/sysmond &")
        print("  sleep 10")
        print("  python3 scripts/test_ml.py")
        return 1
    
    # Generate test data
    if len(sys.argv) > 1 and sys.argv[1] == '--generate':
        anomaly = '--anomaly' in sys.argv
        generate_mock_data(hours=2, anomaly=anomaly)
    
    # Test ML module
    success = test_ml_module()
    
    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
