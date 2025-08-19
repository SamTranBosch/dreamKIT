#!/bin/bash

# Updated install_dependencies.sh - Standalone execution with parent script compatibility
# Can be called both from dk_install.sh and as standalone sudo script

# Check if we're being called from dk_install.sh or standalone
if [[ -n "$CURRENT_DIR" && -n "$DK_USER" ]]; then
    # Called from dk_install.sh - functions and variables are available
    SCRIPT_MODE="integrated"
else
    # Called standalone - need to set up environment
    SCRIPT_MODE="standalone"
    
    # Detect user (handle sudo execution)
    if [ -n "$SUDO_USER" ]; then
        DK_USER=$SUDO_USER
    else
        DK_USER=$USER
    fi
    
    # Get script directory
    CURRENT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    
    # Colors and formatting for standalone mode
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
    
    # Utility functions for standalone mode
    show_info() {
        local message=$1
        echo -e "${BLUE} ${ARROW} ${message}${NC}"
    }
    
    show_success() {
        local message=$1
        echo -e "${GREEN}${BOLD} ${CHECKMARK} ${message}${NC}"
    }
    
    show_error() {
        local message=$1
        echo -e "${RED}${BOLD} ${CROSS} ${message}${NC}"
    }
    
    show_warning() {
        local message=$1
        echo -e "${YELLOW}${BOLD} ⚠ ${message}${NC}"
    }
    
    run_with_feedback() {
        local command=$1
        local success_msg=$2
        local error_msg=$3
        local show_output=${4:-false}
        
        if [ "$show_output" = "true" ]; then
            echo -e "${DIM}${CYAN}Running: $command${NC}"
            if eval "$command"; then
                show_success "$success_msg"
                return 0
            else
                show_error "$error_msg"
                return 1
            fi
        else
            eval "$command" >/dev/null 2>&1
            local exit_code=$?
            
            if [ $exit_code -eq 0 ]; then
                show_success "$success_msg"
                return 0
            else
                show_error "$error_msg"
                return 1
            fi
        fi
    }
    
    echo -e "${BLUE}${BOLD}dreamOS Dependencies Installation${NC}"
    echo -e "${DIM}Installing system dependencies and k3s offline tools...${NC}"
    echo
fi

# Dependencies installation script
# This handles all system dependencies for dreamOS

# ---------------------------------------------------------------------------
# Helper: update package index
# ---------------------------------------------------------------------------
update_package_index() {
    show_info "Updating package index..."
    run_with_feedback \
        "apt-get update -y" \
        "Package index updated" \
        "Failed to update package index" \
        $([[ "$SCRIPT_MODE" == "standalone" ]] && echo "true" || echo "false")
}

# ---------------------------------------------------------------------------
# Helper: install a single deb package if the related command is absent
#   $1   binary/command to look for
#   $2   deb package to install (defaults to same as $1)
# ---------------------------------------------------------------------------
install_if_missing() {
    local cmd_name="$1"
    local deb_name="${2:-$1}"

    if command -v "$cmd_name" >/dev/null 2>&1; then
        show_info "$cmd_name is already installed"
    else
        show_info "Installing $deb_name..."
        run_with_feedback \
            "apt-get install -y $deb_name" \
            "$deb_name installed successfully" \
            "Failed to install $deb_name" \
            $([[ "$SCRIPT_MODE" == "standalone" ]] && echo "true" || echo "false")
    fi
}

# ---------------------------------------------------------------------------
# Helper: prepare host for offline k3s operation
# ---------------------------------------------------------------------------
prepare_offline_k3s_tools() {
    show_info "Preparing host for offline k3s operation..."
    
    # Install required tools for k3s offline operation
    local k3s_tools=(
        "util-linux"
        "netcat-openbsd" 
        "iputils-ping"
        "coreutils"
        "procps"
    )
    
    show_info "Installing k3s offline operation dependencies..."
    for tool in "${k3s_tools[@]}"; do
        install_if_missing "${tool%% *}" "$tool"
    done
    
    # Create tools directory for container mounting
    local tools_dir="/opt/k3s-tools/bin"
    show_info "Creating k3s tools directory: $tools_dir"
    
    run_with_feedback \
        "mkdir -p $tools_dir" \
        "k3s tools directory created" \
        "Failed to create k3s tools directory"
    
    # Copy essential binaries to tools directory
    show_info "Copying essential binaries for k3s offline operation..."
    
    local binaries=(
        "/usr/bin/nsenter"
        "/bin/ping"
        "/bin/nc" 
        "/usr/bin/timeout"
        "/bin/ps"
        "/usr/bin/pgrep"
        "/usr/bin/pkill"
        "/bin/kill"
        "/usr/bin/nohup"
        "/bin/sleep"
        "/usr/bin/tee"
        "/bin/cat"
        "/bin/echo"
        "/usr/bin/which"
    )
    
    local copied_tools=()
    local failed_tools=()
    
    for binary in "${binaries[@]}"; do
        local binary_name=$(basename "$binary")
        
        if [ -f "$binary" ]; then
            if cp "$binary" "$tools_dir/" 2>/dev/null; then
                copied_tools+=("$binary_name")
            else
                failed_tools+=("$binary_name")
            fi
        else
            # Try alternative locations
            local alt_binary=""
            case "$binary_name" in
                "nc")
                    for alt in "/usr/bin/nc" "/bin/nc.openbsd" "/usr/bin/nc.openbsd"; do
                        [ -f "$alt" ] && alt_binary="$alt" && break
                    done
                    ;;
                "ping")
                    for alt in "/usr/bin/ping" "/bin/ping"; do
                        [ -f "$alt" ] && alt_binary="$alt" && break
                    done
                    ;;
            esac
            
            if [ -n "$alt_binary" ] && [ -f "$alt_binary" ]; then
                if cp "$alt_binary" "$tools_dir/$binary_name" 2>/dev/null; then
                    copied_tools+=("$binary_name")
                else
                    failed_tools+=("$binary_name")
                fi
            else
                failed_tools+=("$binary_name")
            fi
        fi
    done
    
    # Set proper permissions
    run_with_feedback \
        "chmod +x $tools_dir/*" \
        "Set executable permissions for k3s tools" \
        "Failed to set permissions for k3s tools"
    
    # Report results
    if [ ${#copied_tools[@]} -gt 0 ]; then
        show_success "Copied ${#copied_tools[@]} tools for k3s offline operation"
        show_info "Available tools: ${copied_tools[*]}"
    fi
    
    if [ ${#failed_tools[@]} -gt 0 ]; then
        show_warning "Failed to copy ${#failed_tools[@]} tools: ${failed_tools[*]}"
    fi
    
    # Verify the setup
    show_info "Verifying k3s tools setup..."
    if [ -d "$tools_dir" ]; then
        local tool_count=$(find "$tools_dir" -type f -executable 2>/dev/null | wc -l)
        show_success "k3s tools directory prepared with $tool_count executable tools"
        
        # List available tools for debugging (only in standalone mode)
        if [[ "$SCRIPT_MODE" == "standalone" ]]; then
            show_info "Tools available in $tools_dir:"
            ls -la "$tools_dir/" | while IFS= read -r line; do
                echo -e "${DIM}  $line${NC}"
            done
        fi
    else
        show_error "k3s tools directory setup failed"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Helper: install additional system tools for k3s
# ---------------------------------------------------------------------------
install_k3s_system_dependencies() {
    show_info "Installing additional system dependencies for k3s..."
    
    # Install packages that provide required tools
    local packages=(
        "systemd"
        "util-linux"
        "mount"
        "iptables"
        "iproute2"
        "cgroup-tools"
    )
    
    for package in "${packages[@]}"; do
        install_if_missing "${package%% *}" "$package"
    done
    
    # Ensure cgroups are properly mounted
    show_info "Checking cgroups configuration..."
    if ! mount | grep -q cgroup; then
        show_info "Mounting cgroups..."
        run_with_feedback \
            "mount -t cgroup -o all cgroup /sys/fs/cgroup || true" \
            "cgroups mounted successfully" \
            "cgroups mount failed (may be normal)"
    else
        show_info "cgroups already mounted"
    fi
}

# ---------------------------------------------------------------------------
# Helper: install k9s when missing
# ---------------------------------------------------------------------------
install_k9s() {
    # Version to pull (override via env K9S_VERSION)
    local VERSION="${K9S_VERSION:-0.50.9}"

    # Detect architecture
    local ARCH
    local UNAME_ARCH="$(uname -m)"
    
    case "$UNAME_ARCH" in
        x86_64|amd64)   
            ARCH="x86_64" 
            ;;
        armv7l|armv7)   
            ARCH="armv7"  
            ;;
        aarch64|arm64)  
            ARCH="arm64"
            show_info "Detected ARM64 architecture (Jetson Orin compatible)"
            ;;
        *)
            show_error "Unsupported architecture: $UNAME_ARCH"
            show_error "k9s supports: x86_64, armv7, arm64"
            return 1
            ;;
    esac

    # Compose download URL
    local OS="linux"
    local TARBALL="k9s_${OS}_${ARCH}.tar.gz"
    local URL="https://github.com/derailed/k9s/releases/download/v${VERSION}/${TARBALL}"

    # Verify URL exists before downloading
    show_info "Verifying k9s release availability for ${ARCH}..."
    if ! curl -fsSL --head "$URL" >/dev/null 2>&1; then
        show_error "k9s v${VERSION} not available for ${ARCH} architecture"
        show_error "URL: $URL"
        return 1
    fi

    # Download & install
    show_info "Downloading k9s v${VERSION} for ${ARCH}..."
    run_with_feedback \
        "tmp_dir=\$(mktemp -d) && \
         curl -fsSL \"${URL}\" -o \"\$tmp_dir/${TARBALL}\" && \
         tar -C /usr/local/bin -xzf \"\$tmp_dir/${TARBALL}\" k9s && \
         chmod +x /usr/local/bin/k9s && \
         rm -rf \"\$tmp_dir\"" \
        "k9s v${VERSION} installed successfully for ${ARCH}" \
        "Failed to install k9s v${VERSION} for ${ARCH}" \
        $([[ "$SCRIPT_MODE" == "standalone" ]] && echo "true" || echo "false")
}

# ---------------------------------------------------------------------------
# Helper: install Node.js via NodeSource repository
# ---------------------------------------------------------------------------
install_nodejs_nodesource() {
    local target_version="${NODE_VERSION:-20}"
    
    show_info "Installing Node.js v${target_version} via NodeSource repository..."
    
    # Remove any existing Node.js installations from apt
    run_with_feedback \
        "apt-get remove -y nodejs npm" \
        "Removed existing Node.js packages" \
        "No existing Node.js packages to remove"
    
    # Install NodeSource repository
    run_with_feedback \
        "curl -fsSL https://deb.nodesource.com/setup_${target_version}.x | bash -" \
        "NodeSource repository added successfully" \
        "Failed to add NodeSource repository" \
        $([[ "$SCRIPT_MODE" == "standalone" ]] && echo "true" || echo "false")
    
    # Install Node.js
    run_with_feedback \
        "apt-get install -y nodejs" \
        "Node.js v${target_version} installed successfully" \
        "Failed to install Node.js v${target_version}" \
        $([[ "$SCRIPT_MODE" == "standalone" ]] && echo "true" || echo "false")
}

# Configure Docker service and user permissions
configure_docker_service() {
    local user="$1"
    
    # Ensure Docker service is enabled & running
    if systemctl is-active --quiet docker; then
        show_info "Docker service already running"
    else
        show_info "Starting Docker service..."
        run_with_feedback \
            "systemctl enable --now docker" \
            "Docker service enabled and started" \
            "Failed to start Docker service"
    fi

    # Add current user to docker group
    if groups "$user" | grep -q '\bdocker\b'; then
        show_info "User $user is already in the docker group"
    else
        show_info "Adding user to Docker group..."
        run_with_feedback \
            "usermod -aG docker $user" \
            "Added $user to docker group (logout/login for effect)" \
            "Failed to add $user to docker group"
        
        show_warning "Group changes will take effect after logout/login"
    fi
}

# Install additional system utilities
install_system_utilities() {
    local utilities=(
        "curl"
        "wget"
        "jq"
        "htop"
        "tree"
        "unzip"
        "ca-certificates"
        "gnupg"
        "lsb-release"
        "rsync"
        "tar"
        "gzip"
    )
    
    for util in "${utilities[@]}"; do
        install_if_missing "$util"
    done
    
    # Install yq separately
    install_yq_if_missing
}

# Install yq (YAML processor) if missing
install_yq_if_missing() {
    if command -v yq >/dev/null 2>&1; then
        show_info "yq is already installed"
    else
        show_info "Installing yq YAML processor..."
        local VERSION="${YQ_VERSION:-4.35.2}"
        local ARCH
        case "$(uname -m)" in
            x86_64|amd64)   ARCH="amd64" ;;
            aarch64|arm64)  ARCH="arm64" ;;
            armv7l|armv7)   ARCH="arm" ;;
            *)
                show_warning "Unsupported architecture for yq: $(uname -m)"
                return 1
                ;;
        esac
        
        local URL="https://github.com/mikefarah/yq/releases/download/v${VERSION}/yq_linux_${ARCH}"
        run_with_feedback \
            "curl -fsSL \"${URL}\" -o /usr/local/bin/yq && chmod +x /usr/local/bin/yq" \
            "yq installed successfully" \
            "Failed to install yq" \
            $([[ "$SCRIPT_MODE" == "standalone" ]] && echo "true" || echo "false")
    fi
}

# Main installation function
install_dependencies() {
    show_info "Starting dependencies installation for user: ${BOLD}${DK_USER}${NC}"
    
    # Update repositories first
    update_package_index
    
    # Core dependencies
    show_info "Installing core dependencies..."
    install_if_missing git
    install_if_missing docker docker.io
    install_if_missing sshpass
    
    # Configure Docker
    configure_docker_service "$DK_USER"
    
    # Node.js
    show_info "Installing Node.js..."
    if ! command -v node >/dev/null 2>&1; then
        install_nodejs_nodesource
    else
        show_info "Node.js already installed: $(node --version)"
    fi
    
    # Ensure npm is available
    install_if_missing npm
    
    # k3s dependencies and offline tools
    show_info "Installing k3s system dependencies..."
    install_k3s_system_dependencies
    prepare_offline_k3s_tools
    
    # Kubernetes tools
    show_info "Installing Kubernetes management tools..."
    if ! command -v k9s >/dev/null 2>&1; then
        install_k9s
    else
        show_info "k9s already installed: $(k9s version --short 2>/dev/null | head -n1)"
    fi
    
    # System utilities
    show_info "Installing system utilities..."
    install_system_utilities
    
    show_success "Dependencies installation completed successfully!"
    
    # Quick verification
    show_info "Verifying key installations..."
    local key_tools=("git" "docker" "node" "npm" "k9s" "jq" "yq")
    local failed_count=0
    
    for tool in "${key_tools[@]}"; do
        if command -v "$tool" >/dev/null 2>&1; then
            show_success "$tool: installed"
        else
            show_error "$tool: missing"
            ((failed_count++))
        fi
    done
    
    # Check k3s tools
    if [ -d "/opt/k3s-tools/bin" ]; then
        local tool_count=$(find "/opt/k3s-tools/bin" -type f -executable 2>/dev/null | wc -l)
        show_success "k3s offline tools: $tool_count tools ready"
    else
        show_error "k3s offline tools: missing"
        ((failed_count++))
    fi
    
    if [ $failed_count -eq 0 ]; then
        show_success "All dependencies verified successfully!"
        return 0
    else
        show_error "$failed_count dependencies failed verification"
        return 1
    fi
}

# Main execution
main() {
    # Ensure we're running as root for system-wide installations
    if [ "$EUID" -ne 0 ]; then
        echo -e "${RED}${BOLD}This script must be run as root (use sudo)${NC}"
        exit 1
    fi
    
    install_dependencies
    exit $?
}

# Run main function if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi