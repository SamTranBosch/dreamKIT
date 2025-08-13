#!/bin/bash

# Description   : define the script to set up the default VSS configuration for the SDV runtime
# Usage         : sudo ./setup_default_vss.sh
# Output        : ${HOME_DIR}/.dk/sdv-runtime/vss.json

# Colors and formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m' # No Color

# Unicode symbols
CHECKMARK="✓"
CROSS="✗"
ARROW="→"
STAR="★"
GEAR="⚙"
ROCKET="🚀"
DREAM="💭"

# Animation frames
SPINNER_FRAMES=("⠋" "⠙" "⠹" "⠸" "⠼" "⠴" "⠦" "⠧" "⠇" "⠏")
PROGRESS_CHARS=("▱" "▰")


# Use variables from parent script or defaults
# echo -e "${BLUE} ${ARROW} Setting up VSS for user: $DK_USER.${NC}"
# echo -e "${BLUE} ${ARROW} Home directory: $HOME_DIR.${NC}"

# Create the host directory and file using dynamic variables
sudo mkdir -p "${HOME_DIR}/.dk/sdv-runtime/"
sudo touch "${HOME_DIR}/.dk/sdv-runtime/vss.json"

# Get the current script directory to find the manifest
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST_PATH="${SCRIPT_DIR}/../manifests/default_vss.json"

# Copy the default VSS configuration
if [ -f "$MANIFEST_PATH" ]; then
    sudo cp "$MANIFEST_PATH" "${HOME_DIR}/.dk/sdv-runtime/vss.json"
    echo -e "${BLUE} ${ARROW} VSS configuration copied successfully.${NC}"
else
    echo -e "${BLUE} ${ARROW} Warning: default_vss.json not found at $MANIFEST_PATH.${NC}"
    echo -e "${BLUE} ${ARROW} Creating empty VSS configuration.${NC}"
    echo '{}' | sudo tee "${HOME_DIR}/.dk/sdv-runtime/vss.json" > /dev/null
fi

# Set proper permissions
sudo chown -R "${DK_USER}:${DK_USER}" "${HOME_DIR}/.dk/"
sudo chmod -R 755 "${HOME_DIR}/.dk/sdv-runtime/"
sudo chmod 666 "${HOME_DIR}/.dk/sdv-runtime/vss.json"

echo -e "${BLUE} ${ARROW} VSS setup completed for $DK_USER at $HOME_DIR.${NC}"
