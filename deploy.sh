#!/usr/bin/env bash
set -e

PORT=${1:-5500}

# Detect directory
if [ -f "./Makefile" ] && [ -d "./src" ]; then
    FASTDB_DIR="$(pwd)"
elif [ -d "./fastdb" ]; then
    FASTDB_DIR="$(pwd)/fastdb"
else
    FASTDB_DIR="$(pwd)"
fi

echo "=========================================="
echo " Deploying FastDB C++ Server on Port $PORT"
echo " Working directory: $FASTDB_DIR"
echo "=========================================="

# 1. Detect OS and install dependencies
if command -v apt-get &>/dev/null; then
    echo "Installing build dependencies (Debian/Ubuntu)..."
    sudo apt-get update -y
    sudo apt-get install -y build-essential g++ make
elif command -v dnf &>/dev/null; then
    echo "Installing build dependencies (Oracle Linux/RHEL)..."
    sudo dnf groupinstall -y "Development Tools" || true
    sudo dnf install -y gcc-c++ make
fi

# 2. Build FastDB
echo "Building FastDB C++ binaries..."
cd "$FASTDB_DIR"
make clean
make -j$(nproc)

# 3. Configure Firewall
echo "Configuring firewall for port $PORT..."
if command -v ufw &>/dev/null; then
    sudo ufw allow $PORT/tcp || true
elif command -v firewall-cmd &>/dev/null; then
    sudo firewall-cmd --permanent --add-port=$PORT/tcp || true
    sudo firewall-cmd --reload || true
fi

# Oracle Linux iptables rule
if sudo iptables -L &>/dev/null; then
    sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport $PORT -j ACCEPT 2>/dev/null || true
fi

# 4. Install systemd service
SERVICE_USER=$(whoami)
echo "Installing systemd service under user: $SERVICE_USER..."

sudo bash -c "cat << EOF > /etc/systemd/system/fastdb.service
[Unit]
Description=FastDB C++ Real-Time Database Server
After=network.target

[Service]
Type=simple
User=$SERVICE_USER
WorkingDirectory=$FASTDB_DIR
ExecStart=$FASTDB_DIR/fastdb_server --port $PORT --public $FASTDB_DIR/public
Restart=always
RestartSec=5
LimitNOFILE=65536
Environment=PORT=$PORT
Environment=FASTDB_PUBLIC_DIR=$FASTDB_DIR/public

[Install]
WantedBy=multi-user.target
EOF"

# 5. Start and enable service
sudo systemctl daemon-reload
sudo systemctl enable fastdb
sudo systemctl restart fastdb

echo "=========================================="
echo " FastDB C++ successfully deployed!"
echo " Check status with: sudo systemctl status fastdb"
echo " View logs with:   sudo journalctl -u fastdb -f"
echo " Test with:        curl http://localhost:$PORT/"
echo "=========================================="
