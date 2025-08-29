#!/bin/bash
# k3s-x11-setup.sh

set -e

USERNAME="developer"
DISPLAY_NUM="1"

echo "=== Setting up X11 for K3s containers ==="

# 1. Ensure virtual display is running
if ! DISPLAY=:$DISPLAY_NUM timeout 3 xdpyinfo >/dev/null 2>&1; then
    echo "Error: Display :$DISPLAY_NUM is not running"
    echo "Run your display setup script first"
    exit 1
fi

# 2. Create shared X authority file
XAUTH_FILE="/tmp/.X${DISPLAY_NUM}-auth"
sudo rm -f "$XAUTH_FILE"
sudo -u "$USERNAME" touch "$XAUTH_FILE"
sudo -u "$USERNAME" chmod 644 "$XAUTH_FILE"

# 3. Generate and add X11 authentication
sudo -u "$USERNAME" env DISPLAY=:$DISPLAY_NUM xauth generate :$DISPLAY_NUM . trusted timeout 3600
sudo -u "$USERNAME" env DISPLAY=:$DISPLAY_NUM xauth list | grep ":$DISPLAY_NUM " > "$XAUTH_FILE"

# 4. Set broad permissions for containers
sudo -u "$USERNAME" env DISPLAY=:$DISPLAY_NUM xhost +local:
sudo -u "$USERNAME" env DISPLAY=:$DISPLAY_NUM xhost +inet:localhost
sudo -u "$USERNAME" env DISPLAY=:$DISPLAY_NUM xhost +si:localuser:root

# 5. Update socket permissions
sudo chmod 777 "/tmp/.X11-unix/X${DISPLAY_NUM}"
sudo chmod 644 "$XAUTH_FILE"

echo "✓ X11 setup complete for display :$DISPLAY_NUM"
echo "✓ Authority file: $XAUTH_FILE"
echo "✓ Socket permissions updated"

# 6. Test container access
echo "Testing container X11 access..."
if docker run --rm \
    -e DISPLAY=:$DISPLAY_NUM \
    -e XAUTHORITY=/tmp/.X1-auth \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v "$XAUTH_FILE:/tmp/.X1-auth:ro" \
    --network host \
    alpine:latest \
    sh -c "apk add --no-cache xdpyinfo >/dev/null 2>&1 && timeout 5 xdpyinfo >/dev/null 2>&1" 2>/dev/null; then
    echo "✓ Container X11 test successful"
else
    echo "⚠ Container X11 test failed, but K3s might still work"
fi
