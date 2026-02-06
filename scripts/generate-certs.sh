#!/bin/bash
# Generate self-signed TLS certificates for SysMonitor aggregator

set -e

CERT_DIR="${1:-$HOME/.sysmon/certs}"
DAYS="${2:-365}"

mkdir -p "$CERT_DIR"

echo "Generating self-signed TLS certificates..."
echo "Output directory: $CERT_DIR"
echo "Valid for: $DAYS days"

# Generate private key
openssl genrsa -out "$CERT_DIR/server.key" 2048

# Generate certificate signing request
openssl req -new -key "$CERT_DIR/server.key" \
    -out "$CERT_DIR/server.csr" \
    -subj "/C=US/ST=State/L=City/O=SysMonitor/CN=localhost"

# Generate self-signed certificate
openssl x509 -req -days "$DAYS" \
    -in "$CERT_DIR/server.csr" \
    -signkey "$CERT_DIR/server.key" \
    -out "$CERT_DIR/server.crt" \
    -extfile <(printf "subjectAltName=DNS:localhost,IP:127.0.0.1")

# Clean up CSR
rm "$CERT_DIR/server.csr"

# Set permissions
chmod 600 "$CERT_DIR/server.key"
chmod 644 "$CERT_DIR/server.crt"

echo ""
echo "✓ Certificates generated:"
echo "  - Private key: $CERT_DIR/server.key"
echo "  - Certificate: $CERT_DIR/server.crt"
echo ""
echo "To use with aggregator, update config/sysmon.yaml:"
echo "  tls:"
echo "    enabled: true"
echo "    cert_path: $CERT_DIR/server.crt"
echo "    key_path: $CERT_DIR/server.key"
