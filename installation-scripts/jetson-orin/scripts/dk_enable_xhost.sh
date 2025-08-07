#!/bin/bash

# Improved xhost setup script for Linux environments
# Supports multiple Linux distributions and desktop environments

set -uo pipefail  # Remove -e to prevent exit on non-critical errors

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

# Get the actual user (not root) - simplified version
get_real_user() {
    local real_user=""
    
    # Try different methods in order of preference
    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != "root" ]]; then
        real_user="$SUDO_USER"
    elif [[ -n "${DISPLAY:-}" ]]; then
        real_user=$(who | grep "(:0)" | awk '{print $1}' | head -n1 2>/dev/null | tr -d '\n')
    fi
    
    if [[ -z "$real_user" && -n "${USER:-}" && "$USER" != "root" ]]; then
        real_user="$USER"
    fi
    
    if [[ -z "$real_user" && -n "${LOGNAME:-}" && "$LOGNAME" != "root" ]]; then
        real_user="$LOGNAME"
    fi
    
    if [[ -z "$real_user" ]]; then
        real_user=$(who | grep -v "^root " | awk '{print $1}' | head -n1 2>/dev/null | tr -d '\n')
    fi
    
    # Clean any whitespace/newlines
    real_user=$(echo "$real_user" | tr -d ' \n\r\t')
    
    echo "$real_user"
}

# Simplified check_root function
check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        log_info "Usage: sudo $0"
        exit 1
    fi
    
    # Get the real user
    local detected_user=$(get_real_user)
    
    if [[ -z "$detected_user" ]]; then
        log_error "Could not determine the real user (non-root user)"
        log_error "Available users logged in:"
        who 2>/dev/null || echo "  (no users found)"
        log_error "Environment variables:"
        log_error "  SUDO_USER: ${SUDO_USER:-'not set'}"
        log_error "  USER: ${USER:-'not set'}"
        log_error "  LOGNAME: ${LOGNAME:-'not set'}"
        exit 1
    fi
    
    log_info "Detected user: $detected_user"
    
    # Verify the user exists and has a home directory
    if ! id "$detected_user" >/dev/null 2>&1; then
        log_error "User '$detected_user' does not exist on this system"
        exit 1
    fi
    
    local user_home=$(getent passwd "$detected_user" | cut -d: -f6)
    if [[ ! -d "$user_home" ]]; then
        log_error "User home directory '$user_home' does not exist"
        exit 1
    fi
    
    # Set the global USERNAME variable
    USERNAME="$detected_user"
    USER_HOME="$user_home"
    
    log_info "User home directory: $USER_HOME"
}

# Detect Linux distribution
detect_distro() {
    if [[ -f "/etc/os-release" ]]; then
        . /etc/os-release
        echo "$ID"
    elif [[ -f "/etc/redhat-release" ]]; then
        echo "rhel"
    elif [[ -f "/etc/debian_version" ]]; then
        echo "debian"
    else
        echo "unknown"
    fi
}

# Get distribution version
get_distro_version() {
    if [[ -f "/etc/os-release" ]]; then
        . /etc/os-release
        echo "${VERSION_ID:-unknown}"
    else
        echo "unknown"
    fi
}

# Detect desktop environment
detect_desktop_environment() {
    if [[ -n "${XDG_CURRENT_DESKTOP:-}" ]]; then
        echo "$XDG_CURRENT_DESKTOP" | tr '[:upper:]' '[:lower:]'
    elif [[ -n "${DESKTOP_SESSION:-}" ]]; then
        echo "$DESKTOP_SESSION" | tr '[:upper:]' '[:lower:]'
    elif command -v gnome-session >/dev/null 2>&1; then
        echo "gnome"
    elif command -v kde-session >/dev/null 2>&1; then
        echo "kde"
    elif command -v xfce4-session >/dev/null 2>&1; then
        echo "xfce"
    else
        echo "unknown"
    fi
}

# Check if X11 is available
check_x11() {
    if ! command -v xhost >/dev/null 2>&1; then
        log_error "xhost command not found. Please install X11 utilities."
        case "$DISTRO" in
            ubuntu|debian)
                log_info "Install with: sudo apt-get install x11-xserver-utils"
                ;;
            fedora|centos|rhel)
                log_info "Install with: sudo dnf install xorg-x11-server-utils"
                ;;
            arch)
                log_info "Install with: sudo pacman -S xorg-xhost"
                ;;
        esac
        return 1
    fi
    return 0
}

# Create systemd service
create_systemd_service() {
    local service_path="/etc/systemd/system/dk-xhost-allow.service"
    log_info "Creating systemd service at $service_path"

    cat <<EOF > "$service_path"
[Unit]
Description=Allow local connections to X server
After=display-manager.service graphical.target
PartOf=graphical.target
Requisite=display-manager.service

[Service]
Type=oneshot
ExecStart=/usr/bin/xhost +local:
User=$USERNAME
Environment=DISPLAY=:0
Environment=XAUTHORITY=/home/$USERNAME/.Xauthority
RemainAfterExit=yes
TimeoutSec=30

[Install]
WantedBy=graphical.target
EOF

    # Set proper permissions
    chmod 644 "$service_path"
    
    # Reload systemd and enable service (don't fail script on errors)
    if systemctl daemon-reload 2>/dev/null; then
        log_success "Systemd daemon reloaded"
    else
        log_warning "Failed to reload systemd daemon, continuing..."
        return 0  # Don't fail the script
    fi

    if systemctl enable dk-xhost-allow.service 2>/dev/null; then
        log_success "Service enabled successfully"
    else
        log_warning "Failed to enable dk-xhost-allow.service, continuing..."
        return 0  # Don't fail the script
    fi
}

# Update create_xhost_script to use USER_HOME
create_xhost_script() {
    local script_path="$USER_HOME/xhost-allow.sh"
    log_info "Creating xhost script at $script_path"

    cat <<EOF > "$script_path"
#!/bin/bash
# Allow local connections to X server
# Auto-generated by dk-xhost-allow setup script

# Wait for X server to be ready
timeout=30
while [ \$timeout -gt 0 ]; do
    if xset q >/dev/null 2>&1; then
        break
    fi
    sleep 1
    timeout=\$((timeout - 1))
done

# Set display if not set
if [[ -z "\${DISPLAY:-}" ]]; then
    export DISPLAY=:0
fi

# Allow local connections
if command -v xhost >/dev/null 2>&1; then
    xhost +local: >/dev/null 2>&1 || {
        echo "Warning: Failed to execute xhost +local:"
        exit 1
    }
    echo "X server local access enabled"
else
    echo "Error: xhost command not found"
    exit 1
fi
EOF

    chmod +x "$script_path"
    chown "$USERNAME:$USERNAME" "$script_path"
    log_success "xhost script created and made executable"
}

# Update create_autostart_entry to use USER_HOME
create_autostart_entry() {
    local autostart_dir="$USER_HOME/.config/autostart"
    local desktop_file="$autostart_dir/xhost-allow.desktop"
    
    log_info "Creating autostart entry for desktop environment: $DESKTOP_ENV"
    
    # Create autostart directory
    mkdir -p "$autostart_dir"
    
    case "$DESKTOP_ENV" in
        gnome|unity|cinnamon|mate|xfce|lxde|lxqt)
            # Standard XDG autostart
            cat <<EOF > "$desktop_file"
[Desktop Entry]
Type=Application
Exec=$USER_HOME/xhost-allow.sh
Hidden=false
NoDisplay=false
X-GNOME-Autostart-enabled=true
X-KDE-autostart-after=panel
X-MATE-Autostart-enabled=true
StartupNotify=false
Name=Xhost Allow Local Access
Comment=Allow local X server connections for containerized applications
Categories=System;
EOF
            ;;
        kde|plasma)
            # KDE Plasma autostart
            cat <<EOF > "$desktop_file"
[Desktop Entry]
Type=Application
Exec=$USER_HOME/xhost-allow.sh
Hidden=false
NoDisplay=false
X-KDE-autostart-after=panel
X-KDE-StartupNotify=false
Name=Xhost Allow Local Access
Comment=Allow local X server connections for containerized applications
Categories=System;
EOF
            ;;
        *)
            # Generic autostart entry
            log_warning "Unknown desktop environment, creating generic autostart entry"
            cat <<EOF > "$desktop_file"
[Desktop Entry]
Type=Application
Exec=$USER_HOME/xhost-allow.sh
Hidden=false
NoDisplay=false
StartupNotify=false
Name=Xhost Allow Local Access
Comment=Allow local X server connections for containerized applications
Categories=System;
EOF
            ;;
    esac
    
    # Set proper ownership and permissions
    chown "$USERNAME:$USERNAME" "$desktop_file"
    chmod 644 "$desktop_file"
    chown -R "$USERNAME:$USERNAME" "$autostart_dir"
    
    log_success "Autostart entry created"
}

# Update create_user_session_script to use USER_HOME
create_user_session_script() {
    local profile_script="$USER_HOME/.profile_xhost"
    
    log_info "Creating user session script"
    
    cat <<EOF > "$profile_script"
# Auto-generated xhost configuration
# Source this file or add to your shell profile

# Function to enable xhost local access
enable_xhost_local() {
    if [[ -n "\${DISPLAY:-}" ]] && command -v xhost >/dev/null 2>&1; then
        xhost +local: >/dev/null 2>&1 && echo "X server local access enabled"
    fi
}

# Enable on login if X session is active
if [[ -n "\${DISPLAY:-}" ]]; then
    enable_xhost_local
fi
EOF

    chown "$USERNAME:$USERNAME" "$profile_script"
    chmod 644 "$profile_script"
    
    log_success "User session script created"
}

# Main execution with better error handling
main() {
    log_info "Starting xhost setup for Linux environment"
    
    # Initial checks (these should still fail the script)
    check_root
    
    log_info "Setting up for user: $USERNAME"
    log_info "User home directory: $USER_HOME"
    
    # Detect system information
    DISTRO=$(detect_distro)
    DISTRO_VERSION=$(get_distro_version)
    DESKTOP_ENV=$(detect_desktop_environment)
    
    log_info "Detected distribution: $DISTRO $DISTRO_VERSION"
    log_info "Detected desktop environment: $DESKTOP_ENV"
    
    # Check X11 availability (this should fail the script)
    if ! check_x11; then
        exit 1
    fi
    
    # Track if any critical component failed
    local setup_success=true
    
    # Create components based on system capabilities
    case "$DISTRO" in
        ubuntu|debian|fedora|centos|rhel|arch|opensuse*)
            log_info "Supported distribution detected, proceeding with full setup"
            
            # Create systemd service (non-critical)
            if command -v systemctl >/dev/null 2>&1; then
                if ! create_systemd_service; then
                    log_warning "Systemd service creation had issues, but continuing"
                fi
            else
                log_warning "systemctl not found, skipping systemd service creation"
            fi
            
            # Create xhost script (critical)
            if ! create_xhost_script; then
                log_error "Failed to create xhost script"
                setup_success=false
            fi
            
            # Create autostart entry (critical)
            if ! create_autostart_entry; then
                log_error "Failed to create autostart entry"
                setup_success=false
            fi
            
            # Create user session script as backup (non-critical)
            if ! create_user_session_script; then
                log_warning "Failed to create user session script, but continuing"
            fi
            ;;
        *)
            log_warning "Unknown or unsupported distribution: $DISTRO"
            log_info "Proceeding with basic setup"
            
            if ! create_xhost_script; then
                setup_success=false
            fi
            if ! create_autostart_entry; then
                setup_success=false
            fi
            create_user_session_script || true  # Don't fail on this
            ;;
    esac
    
    # Check overall success
    if [[ "$setup_success" == "true" ]]; then
        # Final instructions
        log_success "Setup completed successfully!"
        echo
        log_info "Next steps:"
        echo "  1. Reboot your system to apply all changes"
        echo "  2. Log into your desktop session"
        echo "  3. Verify xhost is working: xhost"
        echo "  4. Test with: docker run --rm -e DISPLAY=\$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix <image>"
        echo
        log_info "Manual activation (if needed):"
        echo "  - Run: $USER_HOME/xhost-allow.sh"
        echo "  - Or source: source $USER_HOME/.profile_xhost"
        
        exit 0
    else
        log_error "Setup completed with errors. Some components may not work properly."
        exit 1
    fi
}

# Run main function
main "$@"
