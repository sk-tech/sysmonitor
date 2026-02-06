"""Authentication module for aggregator server"""

import os
import hashlib
import hmac


class TokenAuthenticator:
    """Simple token-based authentication"""

    def __init__(self, token: str = None):
        """
        Initialize authenticator with token
        
        Args:
            token: Authentication token (if None, read from SYSMON_AGGREGATOR_TOKEN env)
        """
        if token:
            self.token = token
        else:
            self.token = os.environ.get('SYSMON_AGGREGATOR_TOKEN', '')
        
        if not self.token:
            raise ValueError("No authentication token configured. Set SYSMON_AGGREGATOR_TOKEN environment variable.")
    
    def validate(self, provided_token: str) -> bool:
        """
        Validate provided token using constant-time comparison
        
        Args:
            provided_token: Token to validate
            
        Returns:
            True if token is valid, False otherwise
        """
        if not provided_token:
            return False
        
        # Use HMAC for constant-time comparison to prevent timing attacks
        return hmac.compare_digest(
            provided_token.encode('utf-8'),
            self.token.encode('utf-8')
        )
    
    def extract_from_header(self, headers: dict) -> str:
        """
        Extract token from HTTP headers
        
        Args:
            headers: HTTP request headers
            
        Returns:
            Token string or empty string if not found
        """
        # Check for X-SysMon-Token header
        token = headers.get('X-SysMon-Token', headers.get('x-sysmon-token', ''))
        
        # Also support Authorization: Bearer <token>
        if not token:
            auth = headers.get('Authorization', headers.get('authorization', ''))
            if auth.startswith('Bearer '):
                token = auth[7:]
        
        return token
