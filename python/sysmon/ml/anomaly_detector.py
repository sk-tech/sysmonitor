"""
Main anomaly detection orchestrator.
Combines statistical, ML-based, and baseline-based detection.
"""

import numpy as np
from typing import List, Dict, Optional, Tuple
from datetime import datetime, timedelta
import sqlite3
import os

from .models import StatisticalDetector, IsolationForestDetector, AnomalyResult
from .baseline_learner import BaselineLearner, Baseline


class AnomalyDetector:
    """
    Main anomaly detection system.
    Orchestrates multiple detection methods and provides unified interface.
    """
    
    def __init__(
        self,
        db_path: Optional[str] = None,
        use_ml: bool = True,
        use_statistical: bool = True,
        use_baseline: bool = True
    ):
        """
        Initialize anomaly detector.
        
        Args:
            db_path: Path to metrics database
            use_ml: Enable ML-based detection (Isolation Forest)
            use_statistical: Enable statistical detection (z-score)
            use_baseline: Enable baseline-based detection
        """
        if db_path is None:
            db_path = os.path.expanduser("~/.sysmon/data.db")
        
        self.db_path = db_path
        self.use_ml = use_ml
        self.use_statistical = use_statistical
        self.use_baseline = use_baseline
        
        # Initialize detectors
        self.statistical_detectors: Dict[str, StatisticalDetector] = {}
        self.ml_detectors: Dict[str, IsolationForestDetector] = {}
        self.baseline_learner: Optional[BaselineLearner] = None
        
        if use_baseline:
            self.baseline_learner = BaselineLearner(db_path)
        
        # Training status
        self.trained_metrics: Dict[str, bool] = {}
    
    def train_metric(
        self,
        metric_type: str,
        host: str = "localhost",
        hours: int = 24
    ) -> bool:
        """
        Train ML models for a metric using historical data.
        
        Args:
            metric_type: Type of metric to train on
            host: Hostname
            hours: Hours of historical data to use
            
        Returns:
            True if training successful
        """
        key = f"{host}:{metric_type}"
        
        # Get training data
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cutoff_time = int((datetime.now() - timedelta(hours=hours)).timestamp())
        
        cursor.execute("""
            SELECT timestamp, value
            FROM metrics
            WHERE metric_type = ? AND host = ? AND timestamp >= ?
            ORDER BY timestamp ASC
        """, (metric_type, host, cutoff_time))
        
        rows = cursor.fetchall()
        conn.close()
        
        if len(rows) < 50:  # Need minimum samples for ML
            print(f"Insufficient data for {key}: {len(rows)} samples")
            return False
        
        timestamps = np.array([row[0] for row in rows])
        values = np.array([row[1] for row in rows])
        
        # Initialize statistical detector
        if self.use_statistical:
            self.statistical_detectors[key] = StatisticalDetector(
                window_size=min(100, len(values) // 2),
                z_threshold=3.0
            )
            # Warm up with historical data
            for val in values:
                self.statistical_detectors[key].update(val)
        
        # Train ML detector
        if self.use_ml:
            try:
                ml_detector = IsolationForestDetector(
                    contamination=0.1,
                    n_estimators=100
                )
                ml_detector.train(values)
                self.ml_detectors[key] = ml_detector
            except Exception as e:
                print(f"ML training failed for {key}: {e}")
                # Continue without ML
        
        # Learn baseline
        if self.use_baseline and self.baseline_learner:
            self.baseline_learner.learn_baseline(metric_type, host, hours)
        
        self.trained_metrics[key] = True
        print(f"Trained models for {key} with {len(rows)} samples")
        return True
    
    def detect(
        self,
        metric_type: str,
        value: float,
        timestamp: int,
        host: str = "localhost"
    ) -> Dict[str, AnomalyResult]:
        """
        Run anomaly detection using all enabled methods.
        
        Args:
            metric_type: Type of metric
            value: Current value
            timestamp: Unix timestamp
            host: Hostname
            
        Returns:
            Dictionary of detection results by method
        """
        key = f"{host}:{metric_type}"
        results = {}
        
        # Ensure models are trained
        if key not in self.trained_metrics:
            self.train_metric(metric_type, host)
        
        # Statistical detection
        if self.use_statistical and key in self.statistical_detectors:
            detector = self.statistical_detectors[key]
            results['statistical'] = detector.detect(value, timestamp)
        
        # ML detection
        if self.use_ml and key in self.ml_detectors:
            try:
                detector = self.ml_detectors[key]
                results['ml'] = detector.detect(value, timestamp)
            except Exception as e:
                print(f"ML detection failed for {key}: {e}")
        
        # Baseline detection
        if self.use_baseline and self.baseline_learner:
            is_anomalous, baseline = self.baseline_learner.is_anomalous(
                metric_type, value, host
            )
            if baseline:
                results['baseline'] = AnomalyResult(
                    is_anomaly=is_anomalous,
                    score=abs(value - baseline.mean) / baseline.stddev if baseline.stddev > 0 else 0,
                    threshold=3.0,
                    timestamp=timestamp,
                    value=value,
                    expected_value=baseline.mean
                )
        
        return results
    
    def detect_batch(
        self,
        metric_type: str,
        values: List[Tuple[int, float]],
        host: str = "localhost"
    ) -> Dict[str, List[AnomalyResult]]:
        """
        Run batch anomaly detection.
        
        Args:
            metric_type: Type of metric
            values: List of (timestamp, value) tuples
            host: Hostname
            
        Returns:
            Dictionary of detection results by method
        """
        key = f"{host}:{metric_type}"
        results = {}
        
        # Ensure models are trained
        if key not in self.trained_metrics:
            self.train_metric(metric_type, host)
        
        # Statistical detection
        if self.use_statistical and key in self.statistical_detectors:
            detector = self.statistical_detectors[key]
            results['statistical'] = detector.detect_batch(values)
        
        # ML detection
        if self.use_ml and key in self.ml_detectors:
            try:
                detector = self.ml_detectors[key]
                results['ml'] = detector.detect_batch(values)
            except Exception as e:
                print(f"ML batch detection failed for {key}: {e}")
        
        # Baseline detection
        if self.use_baseline and self.baseline_learner:
            baseline = self.baseline_learner.get_baseline(metric_type, host)
            if baseline:
                baseline_results = []
                for timestamp, value in values:
                    is_anomalous, _ = self.baseline_learner.is_anomalous(
                        metric_type, value, host
                    )
                    baseline_results.append(AnomalyResult(
                        is_anomaly=is_anomalous,
                        score=abs(value - baseline.mean) / baseline.stddev if baseline.stddev > 0 else 0,
                        threshold=3.0,
                        timestamp=timestamp,
                        value=value,
                        expected_value=baseline.mean
                    ))
                results['baseline'] = baseline_results
        
        return results
    
    def get_consensus(self, results: Dict[str, AnomalyResult]) -> Tuple[bool, float]:
        """
        Get consensus anomaly detection from multiple methods.
        
        Args:
            results: Dictionary of detection results
            
        Returns:
            Tuple of (is_anomaly, confidence)
        """
        if not results:
            return (False, 0.0)
        
        # Count votes
        anomaly_votes = sum(1 for r in results.values() if r.is_anomaly)
        total_votes = len(results)
        
        # Majority voting
        is_anomaly = anomaly_votes > total_votes / 2
        confidence = anomaly_votes / total_votes
        
        return (is_anomaly, confidence)
    
    def train_all_metrics(self, hours: int = 24) -> Dict[str, bool]:
        """
        Train models for all available metrics.
        
        Args:
            hours: Hours of historical data to use
            
        Returns:
            Dictionary of training status by metric
        """
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Get all unique metric/host combinations
        cursor.execute("""
            SELECT DISTINCT metric_type, host
            FROM metrics
        """)
        
        combinations = cursor.fetchall()
        conn.close()
        
        training_status = {}
        for metric_type, host in combinations:
            key = f"{host}:{metric_type}"
            training_status[key] = self.train_metric(metric_type, host, hours)
        
        return training_status
    
    def get_baseline(
        self,
        metric_type: str,
        host: str = "localhost"
    ) -> Optional[Baseline]:
        """Get baseline for a metric"""
        if self.baseline_learner:
            return self.baseline_learner.get_baseline(metric_type, host)
        return None
    
    def forecast(
        self,
        metric_type: str,
        horizon_hours: int = 1,
        host: str = "localhost"
    ) -> Optional[List[Tuple[int, float]]]:
        """
        Forecast future values (simple linear extrapolation).
        For advanced forecasting, consider using Prophet or ARIMA.
        
        Args:
            metric_type: Type of metric
            horizon_hours: Hours to forecast ahead
            host: Hostname
            
        Returns:
            List of (timestamp, predicted_value) tuples
        """
        # Get recent data
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cutoff_time = int((datetime.now() - timedelta(hours=24)).timestamp())
        
        cursor.execute("""
            SELECT timestamp, value
            FROM metrics
            WHERE metric_type = ? AND host = ? AND timestamp >= ?
            ORDER BY timestamp ASC
        """, (metric_type, host, cutoff_time))
        
        rows = cursor.fetchall()
        conn.close()
        
        if len(rows) < 10:
            return None
        
        timestamps = np.array([row[0] for row in rows])
        values = np.array([row[1] for row in rows])
        
        # Simple linear trend
        coeffs = np.polyfit(timestamps, values, 1)
        
        # Generate future timestamps
        last_timestamp = timestamps[-1]
        interval = int(np.median(np.diff(timestamps)))  # Estimate collection interval
        
        predictions = []
        for i in range(1, horizon_hours * 3600 // interval):
            future_ts = last_timestamp + i * interval
            predicted_value = coeffs[0] * future_ts + coeffs[1]
            predictions.append((int(future_ts), float(predicted_value)))
        
        return predictions
