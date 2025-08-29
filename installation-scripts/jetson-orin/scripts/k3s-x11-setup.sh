#!/bin/bash
# k3s-x11-setup.sh - Enhanced version with platform detection

set -e

# Configuration
DEFAULT_DISPLAY_COUNT=4
MAX_DISPLAY_NUM=20

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Detect platform type
detect_platform() {
    local platform="unknown"
    
    # Check for Jetson Orin
    if [[ -f /proc/device-tree/model ]] && grep -qi "orin" /proc/device-tree/model 2>/dev/null; then
        platform="jetson-orin"
    elif [[ -f /proc/device-tree/compatible ]] && grep -qi "tegra" /proc/device-tree/compatible 2>/dev/null; then
        platform="jetson"
    elif [[ -f /etc/nv_tegra_release ]] || [[ -d /usr/lib/aarch64-linux-gnu/tegra ]]; then
        platform="jetson"
    # Check for standard Ubuntu
    elif [[ -f /etc/os-release ]] && grep -qi "ubuntu" /etc/os-release; then
        platform="ubuntu"
    # Check for other Debian-based
    elif [[ -f /etc/debian_version ]]; then
        platform="debian"
    fi
    
    echo "$platform"
}

# Auto-detect the real user (not root)
get_real_user() {
    local real_user=""
    
    # Try different methods in order of preference
    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != "root" ]]; then
        real_user="$SUDO_USER"
    elif [[ -n "${DISPLAY:-}" ]]; then
        # Get user from who is logged into X session
        real_user=$(who | grep "(:0)" | awk '{print $1}' | head -n1 2>/dev/null | tr -d '\n')
    fi
    
    # Fallback methods
    if [[ -z "$real_user" && -n "${USER:-}" && "$USER" != "root" ]]; then
        real_user="$USER"
    fi
    
    if [[ -z "$real_user" && -n "${LOGNAME:-}" && "$LOGNAME" != "root" ]]; then
        real_user="$LOGNAME"
    fi
    
    # Last resort - find any non-root user currently logged in
    if [[ -z "$real_user" ]]; then
        real_user=$(who | grep -v "^root " | awk '{print $1}' | head -n1 2>/dev/null | tr -d '\n')
    fi
    
    # Clean any whitespace/newlines
    real_user=$(echo "$real_user" | tr -d ' \n\r\t')
    
    echo "$real_user"
}

# Validate user exists and get user info
validate_user() {
    local username="$1"
    
    if [[ -z "$username" ]]; then
        return 1
    fi
    
    if ! id "$username" >/dev/null 2>&1; then
        log_error "User '$username' does not exist on this system"
        return 1
    fi
    
    local user_home=$(getent passwd "$username" | cut -d: -f6)
    if [[ ! -d "$user_home" ]]; then
        log_error "User home directory '$user_home' does not exist"
        return 1
    fi
    
    USER_ID=$(id -u "$username")
    USER_GID=$(id -g "$username")
    USER_HOME="$user_home"
    
    return 0
}

# Create X authority file with platform-specific method
create_xauth_file() {
    local display_num="$1"
    local username="$2"
    local platform="$3"
    local xauth_file="/tmp/.X${display_num}-auth"
    
    # Remove existing file
    rm -f "$xauth_file"
    
    case "$platform" in
        "jetson-orin"|"jetson")
            # Jetson Orin method - create as root, then change ownership
            log_info "Using Jetson-optimized auth file creation"
            
            # Create temporary file as root
            local temp_file="/tmp/.X${display_num}-auth-root-$$"
            touch "$temp_file"
            
            # Extract auth data as user into temp file
            if sudo -u "$username" env DISPLAY=:$display_num xauth list 2>/dev/null | grep ":$display_num " > "$temp_file" 2>/dev/null; then
                # Move temp file to final location
                mv "$temp_file" "$xauth_file"
                chmod 644 "$xauth_file"
                log_success "X11 authentication file created (Jetson method)"
                return 0
            else
                rm -f "$temp_file"
                log_warning "Could not extract auth data, creating empty auth file"
                touch "$xauth_file"
                chmod 644 "$xauth_file"
                return 1
            fi
            ;;
            
        "ubuntu"|"debian"|*)
            # Standard Ubuntu method
            log_info "Using standard auth file creation"
            
            # Create file with proper ownership
            if [[ "$USER_ID" != "0" ]]; then
                touch "$xauth_file"
                chown "$USER_ID:$USER_GID" "$xauth_file"
                chmod 644 "$xauth_file"
            else
                touch "$xauth_file"
                chmod 644 "$xauth_file"
            fi
            
            # Extract auth data directly
            if sudo -u "$username" env DISPLAY=:$display_num xauth list 2>/dev/null | grep ":$display_num " > "$xauth_file" 2>/dev/null; then
                log_success "X11 authentication file created (standard method)"
                return 0
            else
                log_warning "Could not extract auth data, but file created"
                return 1
            fi
            ;;
    esac
}

# Setup X11 for a specific display with platform detection
setup_x11_display() {
    local display_num="$1"
    local username="$2"
    local platform="$3"
    
    log_info "Setting up X11 for display :$display_num (user: $username, platform: $platform)"
    
    # 1. Check if display exists
    if ! DISPLAY=:$display_num timeout 3 xdpyinfo >/dev/null 2>&1; then
        log_error "Display :$display_num is not running"
        return 1
    fi
    
    # 2. Generate X11 authentication
    log_info "Generating X11 authentication for display :$display_num"
    if sudo -u "$username" env DISPLAY=:$display_num xauth generate :$display_num . trusted timeout 3600 2>/dev/null; then
        log_success "X11 authentication generated successfully"
    else
        log_warning "Failed to generate new auth, will extract existing..."
    fi
    
    # 3. Create X authority file using platform-specific method
    local xauth_file="/tmp/.X${display_num}-auth"
    create_xauth_file "$display_num" "$username" "$platform"
    
    # 4. Set broad permissions for containers
    log_info "Setting X11 permissions for containers"
    
    sudo -u "$username" env DISPLAY=:$display_num xhost +local: 2>/dev/null || log_warning "Failed to set +local:"
    sudo -u "$username" env DISPLAY=:$display_num xhost +inet:localhost 2>/dev/null || log_warning "Failed to set +inet:localhost"
    sudo -u "$username" env DISPLAY=:$display_num xhost +si:localuser:root 2>/dev/null || log_warning "Failed to set +si:localuser:root"
    sudo -u "$username" env DISPLAY=:$display_num xhost +si:localuser:$username 2>/dev/null || log_warning "Failed to set +si:localuser:$username"
    
    # 5. Update socket permissions
    local x11_socket="/tmp/.X11-unix/X${display_num}"
    if [[ -S "$x11_socket" ]]; then
        chmod 777 "$x11_socket" 2>/dev/null || log_warning "Could not update socket permissions"
        log_success "Socket permissions updated: $x11_socket"
    else
        log_error "X11 socket not found: $x11_socket"
        return 1
    fi
    
    # 6. Test container access
    log_info "Testing container X11 access for display :$display_num"
    if test_container_x11_access "$display_num" "$xauth_file"; then
        log_success "✓ Container X11 test successful for display :$display_num"
    else
        log_warning "⚠ Container X11 test failed for display :$display_num, but K3s might still work"
    fi
    
    log_success "X11 setup complete for display :$display_num"
    echo "  Authority file: $xauth_file"
    echo "  Socket: $x11_socket"
    echo "  Platform: $platform"
    
    return 0
}

# Test container X11 access
test_container_x11_access() {
    local display_num="$1"
    local xauth_file="$2"
    
    # Quick test without pulling large images
    if command -v docker >/dev/null 2>&1; then
        local test_result=0
        
        # Test 1: Basic connection test
        docker run --rm \
            -e DISPLAY=:$display_num \
            -e XAUTHORITY=/tmp/.X-auth \
            -v /tmp/.X11-unix:/tmp/.X11-unix \
            -v "$xauth_file:/tmp/.X-auth:ro" \
            --network host \
            alpine:latest \
            sh -c "timeout 5 ls /tmp/.X11-unix/X$display_num >/dev/null 2>&1" 2>/dev/null || test_result=1
        
        return $test_result
    else
        log_warning "Docker not available for testing"
        return 0
    fi
}

# List available displays
list_available_displays() {
    log_info "Scanning for available displays..."
    
    local available_displays=()
    for ((i=0; i<=MAX_DISPLAY_NUM; i++)); do
        if DISPLAY=:$i timeout 2 xdpyinfo >/dev/null 2>&1; then
            available_displays+=($i)
        fi
    done
    
    if [[ ${#available_displays[@]} -eq 0 ]]; then
        log_error "No X11 displays found"
        return 1
    fi
    
    echo "Available displays: ${available_displays[*]}"
    return 0
}

# Parse command line arguments
parse_arguments() {
    DISPLAY_NUMS=()
    FORCE_USER=""
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            -u|--user)
                FORCE_USER="$2"
                shift 2
                ;;
            -d|--displays)
                IFS=',' read -ra DISPLAY_NUMS <<< "$2"
                shift 2
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                # Treat as display numbers
                if [[ "$1" =~ ^[0-9,]+$ ]]; then
                    IFS=',' read -ra DISPLAY_NUMS <<< "$1"
                else
                    log_error "Unknown argument: $1"
                    show_usage
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
    # Set default display numbers if none provided
    if [[ ${#DISPLAY_NUMS[@]} -eq 0 ]]; then
        for ((i=1; i<=DEFAULT_DISPLAY_COUNT; i++)); do
            DISPLAY_NUMS+=($i)
        done
    fi
}

# Show usage information
show_usage() {
    echo "K3s X11 Setup Script - Platform-Aware Version"
    echo ""
    echo "Supports: Ubuntu, Debian, Jetson Orin, Jetson Xavier"
    echo ""
    echo "Usage: sudo $0 [OPTIONS] [DISPLAY_NUMBERS]"
    echo ""
    echo "Options:"
    echo "  -u, --user USERNAME     Force specific username (auto-detected if not provided)"
    echo "  -d, --displays NUMS     Comma-separated display numbers (default: 1,2,3,4)"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  sudo $0                          # Setup displays 1,2,3,4 for auto-detected user"
    echo "  sudo $0 1,2,5                    # Setup displays 1,2,5"
    echo "  sudo $0 -u developer 1,3        # Setup displays 1,3 for user 'developer'"
    echo "  sudo $0 -d 1,2,3,4,5,6           # Setup 6 displays"
    echo "  sudo $0 2                        # Setup only display 2"
}

# Main execution
main() {
    echo "=== K3s X11 Setup Script - Platform-Aware Version ==="
    
    # Detect platform
    PLATFORM=$(detect_platform)
    log_info "Detected platform: $PLATFORM"
    
    # Check if running as root
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        show_usage
        exit 1
    fi
    
    # Parse arguments
    parse_arguments "$@"
    
    # Determine username
    if [[ -n "$FORCE_USER" ]]; then
        USERNAME="$FORCE_USER"
        log_info "Using forced username: $USERNAME"
    else
        USERNAME=$(get_real_user)
        if [[ -z "$USERNAME" ]]; then
            log_error "Could not auto-detect username. Available users:"
            who 2>/dev/null || echo "  (no users found)"
            log_error "Use -u option to specify username manually"
            show_usage
            exit 1
        fi
        log_info "Auto-detected username: $USERNAME"
    fi
    
    # Validate user
    if ! validate_user "$USERNAME"; then
        exit 1
    fi
    
    log_info "User details: $USERNAME (UID: $USER_ID, GID: $USER_GID, Home: $USER_HOME)"
    
    # Show available displays
    if ! list_available_displays; then
        log_error "No displays available. Please start virtual displays first."
        exit 1
    fi
    
    # Setup each requested display
    log_info "Setting up displays: ${DISPLAY_NUMS[*]}"
    
    local success_count=0
    local total_count=${#DISPLAY_NUMS[@]}
    
    for display_num in "${DISPLAY_NUMS[@]}"; do
        # Validate display number
        if ! [[ "$display_num" =~ ^[0-9]+$ ]] || [[ "$display_num" -gt $MAX_DISPLAY_NUM ]]; then
            log_error "Invalid display number: $display_num (must be 0-$MAX_DISPLAY_NUM)"
            continue
        fi
        
        echo ""
        if setup_x11_display "$display_num" "$USERNAME" "$PLATFORM"; then
            success_count=$((success_count + 1))
        else
            log_error "Failed to setup display :$display_num"
        fi
    done
    
    echo ""
    echo "=== Summary ==="
    log_info "Platform: $PLATFORM"
    log_info "Successfully configured $success_count out of $total_count displays"
    
    if [[ $success_count -gt 0 ]]; then
        echo ""
        echo "Usage in K3s deployment:"
        echo "  env:"
        echo "  - name: DISPLAY"
        echo "    value: \":${DISPLAY_NUMS[0]}\""
        echo "  - name: XAUTHORITY"
        echo "    value: \"/tmp/.X${DISPLAY_NUMS[0]}-auth\""
        echo ""
        echo "  volumeMounts:"
        echo "  - name: x11-unix"
        echo "    mountPath: /tmp/.X11-unix"
        echo "  - name: x11-auth"
        echo "    mountPath: /tmp/.X${DISPLAY_NUMS[0]}-auth"
        echo "    readOnly: true"
        echo ""
        echo "  volumes:"
        echo "  - name: x11-auth"
        echo "    hostPath:"
        echo "      path: /tmp/.X${DISPLAY_NUMS[0]}-auth"
        echo "      type: File"
    fi
    
    if [[ $success_count -eq $total_count ]]; then
        log_success "All displays configured successfully!"
        exit 0
    else
        log_warning "Some displays failed to configure"
        exit 1
    fi
}

# Run main function with all arguments
main "$@"
