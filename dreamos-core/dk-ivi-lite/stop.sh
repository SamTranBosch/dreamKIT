#!/bin/bash

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

# Function to show info message
show_info() {
    local message=$1
    echo -e "${YELLOW} ${ARROW} ${message}${NC}"
}

show_info "Stopping dk_ivi Docker container..."

docker kill dk_ivi; docker rm dk_ivi ;

show_info "Docker container dk_ivi:latest stop successfully."
show_info "You can restart it using the run.sh script or manually with Docker commands."