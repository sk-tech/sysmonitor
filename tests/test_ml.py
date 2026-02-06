"""
Tests for ML module
"""

import pytest
import numpy as np
import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from sysmon.ml.anomaly_detector import AnomalyDetector, IsolationForestDetector
from sysmon.ml.predictor import MetricsPredictor


class TestAnomalyDetector:
    """Test suite for anomaly detection"""
    
    def test_isolation_forest_creation(self):
        """Test creating isolation forest detector"""
        detector = IsolationForestDetector(contamination=0.1)
        assert detector is not None
    
    def test_train_detector(self):
        """Test training anomaly detector"""
        detector = IsolationForestDetector(contamination=0.1)
        
        # Generate normal training data
        normal_data = np.random.normal(50, 10, (1000, 1))
        
        detector.train(normal_data)
        assert detector.is_trained()
    
    def test_detect_anomalies(self):
        """Test anomaly detection"""
        detector = IsolationForestDetector(contamination=0.1)
        
        # Train on normal data
        normal_data = np.random.normal(50, 5, (1000, 1))
        detector.train(normal_data)
        
        # Test with normal and anomalous data
        test_normal = np.array([[50.0], [52.0], [48.0]])
        test_anomalies = np.array([[95.0], [5.0], [100.0]])
        
        normal_predictions = detector.predict(test_normal)
        anomaly_predictions = detector.predict(test_anomalies)
        
        # Normal data should mostly be classified as normal (1)
        assert np.mean(normal_predictions == 1) > 0.5
        
        # Anomalous data should mostly be classified as anomalies (-1)
        assert np.mean(anomaly_predictions == -1) > 0.3
    
    def test_get_anomaly_scores(self):
        """Test getting anomaly scores"""
        detector = IsolationForestDetector(contamination=0.1)
        
        # Train
        normal_data = np.random.normal(50, 5, (1000, 1))
        detector.train(normal_data)
        
        # Get scores
        test_data = np.array([[50.0], [95.0]])
        scores = detector.get_anomaly_scores(test_data)
        
        assert len(scores) == 2
        # Anomalous point should have more negative score
        assert scores[1] < scores[0]
    
    def test_multivariate_detection(self):
        """Test anomaly detection with multiple features"""
        detector = IsolationForestDetector(contamination=0.1)
        
        # Multi-dimensional normal data
        normal_data = np.random.normal(50, 10, (1000, 3))
        detector.train(normal_data)
        
        # Test data
        test_data = np.array([[50, 50, 50], [95, 95, 95]])
        predictions = detector.predict(test_data)
        
        assert len(predictions) == 2
    
    def test_save_load_model(self, tmp_path):
        """Test saving and loading trained model"""
        detector = IsolationForestDetector(contamination=0.1)
        
        # Train
        normal_data = np.random.normal(50, 5, (1000, 1))
        detector.train(normal_data)
        
        # Save
        model_path = tmp_path / "model.pkl"
        detector.save(str(model_path))
        assert model_path.exists()
        
        # Load
        new_detector = IsolationForestDetector.load(str(model_path))
        assert new_detector.is_trained()
        
        # Test predictions match
        test_data = np.array([[50.0], [95.0]])
        pred1 = detector.predict(test_data)
        pred2 = new_detector.predict(test_data)
        np.testing.assert_array_equal(pred1, pred2)


class TestMetricsPredictor:
    """Test suite for metrics prediction"""
    
    def test_predictor_creation(self):
        """Test creating metrics predictor"""
        predictor = MetricsPredictor(window_size=10)
        assert predictor is not None
    
    def test_train_predictor(self):
        """Test training predictor"""
        predictor = MetricsPredictor(window_size=5)
        
        # Generate time series data (sine wave)
        t = np.linspace(0, 10, 100)
        data = 50 + 10 * np.sin(t)
        
        predictor.train(data)
        assert predictor.is_trained()
    
    def test_predict_next_values(self):
        """Test predicting next values"""
        predictor = MetricsPredictor(window_size=10)
        
        # Train on linear trend
        data = np.arange(100).astype(float)
        predictor.train(data)
        
        # Predict next values
        predictions = predictor.predict(steps=5)
        assert len(predictions) == 5
        
        # Should follow trend
        assert predictions[0] > data[-1]
    
    def test_predict_with_confidence(self):
        """Test prediction with confidence intervals"""
        predictor = MetricsPredictor(window_size=10)
        
        data = np.random.normal(50, 5, 100)
        predictor.train(data)
        
        predictions, lower, upper = predictor.predict_with_confidence(steps=5)
        
        assert len(predictions) == 5
        assert len(lower) == 5
        assert len(upper) == 5
        
        # Confidence intervals should bracket predictions
        assert np.all(lower <= predictions)
        assert np.all(predictions <= upper)
    
    def test_seasonal_pattern(self):
        """Test prediction with seasonal patterns"""
        predictor = MetricsPredictor(window_size=24, seasonal_period=24)
        
        # Generate daily pattern
        t = np.linspace(0, 10 * 2 * np.pi, 240)  # 10 days
        data = 50 + 20 * np.sin(t)
        
        predictor.train(data)
        
        predictions = predictor.predict(steps=24)
        assert len(predictions) == 24
    
    def test_trend_detection(self):
        """Test detecting trends in data"""
        predictor = MetricsPredictor(window_size=10)
        
        # Upward trend
        upward_data = np.arange(100).astype(float) + np.random.normal(0, 1, 100)
        trend = predictor.detect_trend(upward_data)
        assert trend > 0  # Positive trend
        
        # Downward trend
        downward_data = -np.arange(100).astype(float) + np.random.normal(0, 1, 100)
        trend = predictor.detect_trend(downward_data)
        assert trend < 0  # Negative trend


class TestIntegration:
    """Integration tests for ML components"""
    
    def test_anomaly_detection_on_real_metrics(self):
        """Test anomaly detection on realistic metric data"""
        detector = IsolationForestDetector(contamination=0.1)
        
        # Simulate CPU usage: normal around 30-50%, with occasional spikes
        normal_usage = np.random.normal(40, 5, (900, 1))
        spike_usage = np.random.uniform(80, 95, (100, 1))
        
        all_data = np.vstack([normal_usage, spike_usage])
        np.random.shuffle(all_data)
        
        # Train on first 80%
        train_size = int(0.8 * len(all_data))
        detector.train(all_data[:train_size])
        
        # Test on remaining
        predictions = detector.predict(all_data[train_size:])
        
        # Should detect some anomalies
        anomaly_count = np.sum(predictions == -1)
        assert anomaly_count > 0
    
    def test_prediction_accuracy(self):
        """Test prediction accuracy on known pattern"""
        predictor = MetricsPredictor(window_size=20)
        
        # Generate predictable pattern
        t = np.linspace(0, 10, 200)
        true_data = 50 + 10 * np.sin(t)
        
        # Train on first 80%
        train_size = 160
        predictor.train(true_data[:train_size])
        
        # Predict next 10 points
        predictions = predictor.predict(steps=10)
        actual = true_data[train_size:train_size+10]
        
        # Calculate RMSE
        rmse = np.sqrt(np.mean((predictions - actual) ** 2))
        
        # Should be reasonably accurate
        assert rmse < 5.0  # Within 5 units


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
