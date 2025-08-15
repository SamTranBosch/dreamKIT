#!/bin/bash
# prepare-offline.sh - Run this on the host before going offline

echo "Preparing host for offline k3s operation..."

# Ensure required tools are available
sudo apt-get update
sudo apt-get install -y util-linux netcat-openbsd iputils-ping

# Create a tools directory for container mounting
sudo mkdir -p /opt/k3s-tools/bin
sudo cp /usr/bin/nsenter /opt/k3s-tools/bin/
sudo cp /bin/ping /opt/k3s-tools/bin/
sudo cp /bin/nc /opt/k3s-tools/bin/
sudo cp /usr/bin/timeout /opt/k3s-tools/bin/
sudo chmod +x /opt/k3s-tools/bin/*

echo "Host prepared for offline operation"
echo "Tools available in /opt/k3s-tools/bin/"
ls -la /opt/k3s-tools/bin/
