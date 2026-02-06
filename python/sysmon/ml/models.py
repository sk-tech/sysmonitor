"""
ML models for anomaly detection.
Implements both statistical and ML-based approaches.
"""

import numpy as np
from typing import List, Tuple, Optional, Dict
from dataclasses import dataclass
from datetime import datetime, timedelta

try:
    from sklearn.ensemble import IsolationForest
    from sklearn.preprocessing import StandardScaler
    SKLEARN_AVAILABLE = True
except ImportError:
    SKLEARN_AVAILABLE = False

try:
    from scipy import signal
    from scipy import stats
    SCIPY_AVAILABLE = True
except ImportError:
    SCIPY_AVAILABLE = False


@dataclass
class AnomalyResult:
    """Result of anomaly detection"""
    is_anomaly: bool
    score: float  # Anomaly score (higher = more anomalous)
    threshold: float
    timestamp: int
    value: float
    expected_value: Optional[float] = None
    confidence: Optional[float] = None


class StatisticalDetector:
    """
    Statistical anomaly detection using z-score and moving statistics.
    Uses numpy/scipy only, no ML models.
    """
    
    def __init__(self, window_size: int = 100, z_threshold: float = 3.0):
        """
        Initialize statistical detector.
        
        Args:
            window_size: Number of recent points for moving statistics
            z_threshold: Number of standard deviations for anomaly detection
        """
        self.window_size = window_size
        self.z_threshold = z_threshold
        self.history: List[float] = []
        self.mean: Optional[float] = None
        self.std: Optional[float] = None
        
    def update(self, value: float) -> None:
        """Update history with new value"""
        self.history.append(value)
        if len(self.history) > self.window_size:
            self.history.pop(0)
            
        # Recalculate statistics
        if len(self.history) >= 10:  # Minimum points for meaningful stats
            self.mean = np.mean(self.history)
            self.std = np.std(self.history)
    
    def detect(self, value: float, timestamp: int) -> AnomalyResult:
        """
        Detect if value is anomalous using z-score.
        
        Args:
            value: Current metric value
            timestamp: Unix timestamp
            
        Returns:
            AnomalyResult with detection outcome
        """
        self.update(value)
        
        if self.mean is None or self.std is None or self.std < 1e-6:
            # Not enough data or no variance
            return AnomalyResult(
                is_anomaly=False,
                score=0.0,
                threshold=self.z_threshold,
                timestamp=timestamp,
                value=value,
                expected_value=self.mean
            )
        
        # Calculate z-score
        z_score = abs((value - self.mean) / self.std)
        is_anomaly = z_score > self.z_threshold
        
        return AnomalyResult(
            is_anomaly=is_anomaly,
            score=z_score,
            threshold=self.z_threshold,
            timestamp=timestamp,
            value=value,
            expected_value=self.mean,
            confidence=1.0 - (1.0 / (1.0 + z_score))  # Simple confidence metric
        )
    
    def detect_batch(self, values: List[Tuple[int, float]]) -> List[AnomalyResult]:
        """
        Detect anomalies in a batch of values.
        
        Args:
            values: List of (timestamp, value) tuples
            
        Returns:
            List of AnomalyResult objects
        """
        results = []
        for timestamp, value in values:
            result = self.detect(value, timestamp)
            results.append(result)
        return results
    
    def seasonal_decompose(self, values: np.ndarray, period: int = 24) -> Dict[str, np.ndarray]:
        """
        Simple seasonal decomposition using moving averages.
        
        Args:
            values: Time series values
            period: Seasonal period (e.g., 24 for hourly data with daily seasonality)
            
        Returns:
            Dictionary with 'trend', 'seasonal', 'residual' components
        """
        if not SCIPY_AVAILABLE or len(values) < 2 * period:
            return {
                'trend': values,
                'seasonal': np.zeros_like(values),
                'residual': np.zeros_like(values)
            }
        
        # Calculate trend using moving average
        trend = np.convolve(values, np.ones(period) / period, mode='same')
        
        # Detrend
        detrended = values - trend
        
        # Calculate seasonal component
        seasonal = np.zeros_like(values)
        for i in range(period):
            seasonal[i::period] = np.mean(detrended[i::period])
        
        # Residual
        residual = values - trend - seasonal
        
        return {
            'trend': trend,
            'seasonal': seasonal,
            'residual': residual
        }


class IsolationForestDetector:
    """
    ML-based anomaly detection using Isolation Forest.
    Requires scikit-learn.
    """
    
    def __init__(self, contamination: float = 0.1, n_estimators: int = 100):
        """
        Initialize Isolation Forest detector.
        
        Args:
            contamination: Expected proportion of outliers in the dataset
            n_estimators: Number of trees in the forest
        """
        if not SKLEARN_AVAILABLE:
            raise ImportError("scikit-learn is required for IsolationForestDetector")
        
        self.contamination = contamination
        self.n_estimators = n_estimators
        self.model: Optional[IsolationForest] = None
        self.scaler = StandardScaler()
        self.is_trained = False
        
    def train(self, values: np.ndarray, features: Optional[np.ndarray] = None) -> None:
        """
        Train the Isolation Forest model.
        
        Args:
            values: Time series values for training
            features: Optional additional features (e.g., time-of-day, day-of-week)
        """
        if len(values) < 50:
            raise ValueError("Need at least 50 samples for training")
        
        # Create feature matrix
        if features is None:
            # Use sliding window features
            X = self._create_features(values)
        else:
            X = features
        
        # Scale features
        X_scaled = self.scaler.fit_transform(X)
        
        # Train model
        self.model = IsolationForest(
            contamination=self.contamination,
            n_estimators=self.n_estimators,
            random_state=42,
            n_jobs=-1  # Use all CPU cores
        )
        self.model.fit(X_scaled)
        self.is_trained = True
        
    def detect(self, value: float, timestamp: int, context: Optional[np.ndarray] = None) -> AnomalyResult:
        """
        Detect if value is anomalous.
        
        Args:
            value: Current metric value
            timestamp: Unix timestamp
            context: Optional context values for feature creation
            
        Returns:
            AnomalyResult with detection outcome
        """
        if not self.is_trained:
            raise RuntimeError("Model must be trained before detection")
        
        # Create features for single point
        if context is None:
            X = np.array([[value]])
        else:
            X = np.array([context])
        
        X_scaled = self.scaler.transform(X)
        
        # Predict
        prediction = self.model.predict(X_scaled)[0]  # -1 for anomaly, 1 for normal
        anomaly_score = -self.model.score_samples(X_scaled)[0]  # Higher = more anomalous
        
        is_anomaly = prediction == -1
        
        return AnomalyResult(
            is_anomaly=is_anomaly,
            score=anomaly_score,
            threshold=0.0,  # Threshold is implicit in contamination parameter
            timestamp=timestamp,
            value=value,
            confidence=1.0 if is_anomaly else 0.0
        )
    
    def detect_batch(self, values: List[Tuple[int, float]]) -> List[AnomalyResult]:
        """
        Detect anomalies in a batch of values.
        
        Args:
            values: List of (timestamp, value) tuples
            
        Returns:
            List of AnomalyResult objects
        """
        if not self.is_trained:
            raise RuntimeError("Model must be trained before detection")
        
        # Extract values and create features
        timestamps, vals = zip(*values)
        X = self._create_features(np.array(vals))
        X_scaled = self.scaler.transform(X)
        
        # Predict
        predictions = self.model.predict(X_scaled)
        scores = -self.model.score_samples(X_scaled)
        
        results = []
        for i, (ts, val) in enumerate(values):
            results.append(AnomalyResult(
                is_anomaly=predictions[i] == -1,
                score=scores[i],
                threshold=0.0,
                timestamp=ts,
                value=val
            ))
        
        return results
    
    @staticmethod
    def _create_features(values: np.ndarray, window_size: int = 5) -> np.ndarray:
        """
        Create features from time series using sliding window.
        
        Args:
            values: Time series values
            window_size: Size of sliding window
            
        Returns:
            Feature matrix
        """
        n = len(values)
        features = []
        
        for i in range(n):
            # Current value
            feat = [values[i]]
            
            # Recent values (sliding window)
            for j in range(1, min(window_size, i + 1)):
                feat.append(values[i - j])
            
            # Pad if not enough history
            while len(feat) < window_size + 1:
                feat.append(0.0)
            
            # Basic statistics
            recent = values[max(0, i - window_size):i + 1]
            feat.extend([
                np.mean(recent),
                np.std(recent) if len(recent) > 1 else 0.0,
                np.min(recent),
                np.max(recent)
            ])
            
            features.append(feat)
        
        return np.array(features)
