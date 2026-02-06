"""
Machine Learning module for SysMonitor
Provides anomaly detection, baseline learning, and forecasting capabilities.

Note: Requires numpy, scipy, and scikit-learn packages.
Install with: pip install -r requirements-ml.txt
"""

__version__ = '0.7.0'

# Check dependencies
try:
    import numpy
    import scipy
    DEPENDENCIES_AVAILABLE = True
except ImportError:
    DEPENDENCIES_AVAILABLE = False

if DEPENDENCIES_AVAILABLE:
    from .anomaly_detector import AnomalyDetector
    from .baseline_learner import BaselineLearner
    from .models import IsolationForestDetector, StatisticalDetector
    
    __all__ = [
        'AnomalyDetector',
        'BaselineLearner',
        'IsolationForestDetector',
        'StatisticalDetector',
    ]
else:
    # Provide helpful error message
    def _missing_deps(*args, **kwargs):
        raise ImportError(
            "ML module requires numpy and scipy. Install with:\n"
            "  sudo apt install python3-numpy python3-scipy python3-sklearn\n"
            "  OR: pip3 install numpy scipy scikit-learn"
        )
    
    AnomalyDetector = _missing_deps
    BaselineLearner = _missing_deps
    IsolationForestDetector = _missing_deps
    StatisticalDetector = _missing_deps
    
    __all__ = []
