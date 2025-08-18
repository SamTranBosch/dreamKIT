#!/bin/bash

# Import colors and utilities from parent script
# This script should be called from dk_install.sh with proper environment

# Dependencies installation script
# This handles all system dependencies for dreamOS

# ---------------------------------------------------------------------------
# Helper   update package index
# ---------------------------------------------------------------------------
update_package_index() {
    show_info "Updating package index …"
    run_with_feedback \
        "sudo apt-get update -y" \
        "Package index updated" \
        "Failed to update package index" \
        true true
}

# ---------------------------------------------------------------------------
# Helper   install a single deb package if the related command is absent
#   $1   binary/command to look for
#   $2   deb package to install (defaults to same as $1)
# ---------------------------------------------------------------------------
install_if_missing() {
    local cmd_name="$1"
    local deb_name="${2:-$1}"

    if command -v "$cmd_name" >/dev/null 2>&1; then
        show_info "$cmd_name is already installed"
    else
        show_info "Installing $deb_name …"
        run_with_feedback \
            "sudo apt-get install -y $deb_name" \
            "$deb_name installed successfully" \
            "Failed to install $deb_name" \
            true true
    fi
}

# ---------------------------------------------------------------------------
# Helper   prepare host for offline k3s operation
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
        "sudo mkdir -p $tools_dir" \
        "k3s tools directory created" \
        "Failed to create k3s tools directory" \
        true true
    
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
            if sudo cp "$binary" "$tools_dir/" 2>/dev/null; then
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
                if sudo cp "$alt_binary" "$tools_dir/$binary_name" 2>/dev/null; then
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
        "sudo chmod +x $tools_dir/*" \
        "Set executable permissions for k3s tools" \
        "Failed to set permissions for k3s tools" \
        true true
    
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
        local tool_count=$(sudo find "$tools_dir" -type f -executable | wc -l)
        show_success "k3s tools directory prepared with $tool_count executable tools"
        
        # List available tools for debugging
        show_info "Tools available in $tools_dir:"
        sudo ls -la "$tools_dir/" | while IFS= read -r line; do
            echo -e "${DIM}  $line${NC}"
        done
    else
        show_error "k3s tools directory setup failed"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Helper   install additional system tools for k3s
# ---------------------------------------------------------------------------
install_k3s_system_dependencies() {
    show_info "Installing additional system dependencies for k3s..."
    
    local k3s_deps=(
        "systemd"
        "systemctl"
        "mount"
        "umount"
        "findmnt"
        "lsblk"
        "blkid"
        "iptables"
        "ip"
        "ss"
        "nsenter"
        "unshare"
        "cgroupfs-mount"
    )
    
    # Install packages that provide these tools
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
            "sudo mount -t cgroup -o all cgroup /sys/fs/cgroup || true" \
            "cgroups mounted successfully" \
            "cgroups mount failed (may be normal)" \
            false false
    else
        show_info "cgroups already mounted"
    fi
}

# ---------------------------------------------------------------------------
# Helper   install k9s when missing
# ---------------------------------------------------------------------------
install_k9s() {
    # ---- Version to pull (override via env K9S_VERSION) --------------------
    local VERSION="${K9S_VERSION:-0.50.9}"

    # ---- Detect architecture ----------------------------------------------
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

    # ---- Compose download URL ---------------------------------------------
    local OS="linux"
    local TARBALL="k9s_${OS}_${ARCH}.tar.gz"
    local URL="https://github.com/derailed/k9s/releases/download/v${VERSION}/${TARBALL}"

    # ---- Verify URL exists before downloading -----------------------------
    show_info "Verifying k9s release availability for ${ARCH}..."
    if ! curl -fsSL --head "$URL" >/dev/null 2>&1; then
        show_error "k9s v${VERSION} not available for ${ARCH} architecture"
        show_error "URL: $URL"
        return 1
    fi

    # ---- Download & install ------------------------------------------------
    show_info "Downloading k9s v${VERSION} for ${ARCH} (Jetson Orin)"
    run_with_feedback \
        "tmp_dir=\$(mktemp -d) && \
         echo \"Downloading from: ${URL}\" && \
         curl -fsSL \"${URL}\" -o \"\$tmp_dir/${TARBALL}\" && \
         echo \"Extracting k9s binary...\" && \
         sudo tar -C /usr/local/bin -xzf \"\$tmp_dir/${TARBALL}\" k9s && \
         sudo chmod +x /usr/local/bin/k9s && \
         echo \"Cleaning up temporary files...\" && \
         rm -rf \"\$tmp_dir\" && \
         echo \"Verifying installation...\" && \
         k9s version --short" \
        "k9s v${VERSION} installed successfully for ${ARCH}" \
        "Failed to install k9s v${VERSION} for ${ARCH}" \
        true true
}

# ---------------------------------------------------------------------------
# Helper   check Node.js version and compare with minimum required
# ---------------------------------------------------------------------------
check_node_version() {
    local min_version="$1"
    local current_version="$2"
    
    # Convert versions to comparable format (remove 'v' prefix and compare)
    local min_ver_clean="${min_version#v}"
    local current_ver_clean="${current_version#v}"
    
    # Use sort -V for version comparison
    if printf '%s\n%s\n' "$min_ver_clean" "$current_ver_clean" | sort -V -C; then
        return 0  # current >= minimum
    else
        return 1  # current < minimum
    fi
}

# ---------------------------------------------------------------------------
# Helper   install Node.js via NodeSource repository for latest versions
# ---------------------------------------------------------------------------
install_nodejs_nodesource() {
    local target_version="${NODE_VERSION:-20}"  # Default to Node.js 20 LTS
    
    show_info "Installing Node.js v${target_version} via NodeSource repository..."
    
    # Remove any existing Node.js installations from apt
    run_with_feedback \
        "sudo apt-get remove -y nodejs npm" \
        "Removed existing Node.js packages" \
        "Failed to remove existing Node.js (continuing anyway)" \
        false false
    
    # Install NodeSource repository
    run_with_feedback \
        "curl -fsSL https://deb.nodesource.com/setup_${target_version}.x | sudo -E bash -" \
        "NodeSource repository added successfully" \
        "Failed to add NodeSource repository" \
        true true
    
    # Install Node.js
    run_with_feedback \
        "sudo apt-get install -y nodejs" \
        "Node.js v${target_version} installed successfully" \
        "Failed to install Node.js v${target_version}" \
        true true
}

# ---------------------------------------------------------------------------
# Helper   install Node.js with version checking
# ---------------------------------------------------------------------------
install_nodejs_with_version_check() {
    local min_required_version="${MIN_NODE_VERSION:-18.5.0}"
    local target_version="${NODE_VERSION:-20}"
    
    show_info "Checking Node.js installation (minimum required: v${min_required_version})..."
    
    if command -v node >/dev/null 2>&1; then
        local current_version=$(node --version 2>/dev/null)
        show_info "Found Node.js ${current_version}"
        
        if check_node_version "$min_required_version" "$current_version"; then
            show_success "Node.js ${current_version} meets minimum requirement (v${min_required_version})"
            
            # Check if npm is available
            if command -v npm >/dev/null 2>&1; then
                local npm_version=$(npm --version 2>/dev/null)
                show_info "npm ${npm_version} is available"
                return 0
            else
                show_warning "npm not found, installing..."
                install_if_missing npm
                return $?
            fi
        else
            show_warning "Node.js ${current_version} is below minimum required version v${min_required_version}"
            show_info "Upgrading to Node.js v${target_version}..."
            install_nodejs_nodesource
        fi
    else
        show_info "Node.js not found, installing v${target_version}..."
        install_nodejs_nodesource
    fi
}

# ---------------------------------------------------------------------------
# Helper   install Node Version Manager (nvm) as alternative
# ---------------------------------------------------------------------------
install_nvm() {
    local nvm_version="${NVM_VERSION:-0.39.0}"
    
    if [ -s "$HOME/.nvm/nvm.sh" ]; then
        show_info "NVM is already installed"
        return 0
    fi
    
    show_info "Installing Node Version Manager (nvm) v${nvm_version}..."
    run_with_feedback \
        "curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v${nvm_version}/install.sh | bash" \
        "NVM installed successfully" \
        "Failed to install NVM" \
        true true
    
    # Source nvm for current session
    export NVM_DIR="$HOME/.nvm"
    [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
    [ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"
}

# ---------------------------------------------------------------------------
# Helper   install Node.js via NVM with specific version
# ---------------------------------------------------------------------------
install_nodejs_via_nvm() {
    local target_version="${NODE_VERSION:-20}"
    
    # Install NVM first
    install_nvm
    
    # Source nvm
    export NVM_DIR="$HOME/.nvm"
    [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
    
    if ! command -v nvm >/dev/null 2>&1; then
        show_error "NVM installation failed or not properly sourced"
        return 1
    fi
    
    show_info "Installing Node.js v${target_version} via NVM..."
    run_with_feedback \
        "nvm install ${target_version} && nvm use ${target_version} && nvm alias default ${target_version}" \
        "Node.js v${target_version} installed and set as default via NVM" \
        "Failed to install Node.js v${target_version} via NVM" \
        true true
}

# ---------------------------------------------------------------------------
# Main installation function with k3s offline preparation
# ---------------------------------------------------------------------------
install_dependencies() {
    local CURRENT_DIR="$1"
    local DK_USER="$2"
    
    echo -e "${BLUE}${BOLD}Installing System Dependencies${NC}"
    echo -e "${DIM}This will install all required packages and tools for dreamOS and k3s${NC}"
    echo
    
    # 0) Update repositories first (only once)
    show_info "Updating package repositories..."
    update_package_index
    
    # 1) Git
    show_info "Checking Git installation..."
    install_if_missing git
    
    # 2) Docker (package: docker.io)
    show_info "Checking Docker installation..."
    install_if_missing docker docker.io
    
    # Configure Docker service
    show_info "Configuring Docker service..."
    configure_docker_service "$DK_USER"
    
    # 3) SSH utilities
    show_info "Installing SSH utilities..."
    install_if_missing sshpass
    
    # 4) Node.js and npm with version management
    show_info "Installing Node.js development tools with version management..."
    
    # Try NodeSource first, fallback to NVM if needed
    if ! install_nodejs_with_version_check; then
        show_warning "NodeSource installation failed, trying NVM..."
        if ! install_nodejs_via_nvm; then
            show_error "Both NodeSource and NVM installation methods failed"
            show_info "Falling back to system package manager..."
            install_if_missing npm
        fi
    fi
    
    # 5) k3s system dependencies and offline preparation
    show_info "Installing k3s system dependencies..."
    install_k3s_system_dependencies
    
    show_info "Preparing host for offline k3s operation..."
    prepare_offline_k3s_tools
    
    # 6) Kubernetes tools
    show_info "Installing Kubernetes management tools..."
    install_k9s_if_missing
    
    # 7) Additional system utilities
    show_info "Installing additional system utilities..."
    install_system_utilities
    
    show_success "All system dependencies and k3s offline tools installed successfully"
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
            "sudo systemctl enable --now docker" \
            "Docker service enabled and started" \
            "Failed to start Docker service" \
            true true
    fi

    # Add current user to docker group (so `docker` can be run without sudo)
    if groups "$user" | grep -q '\bdocker\b'; then
        show_info "User $user is already in the docker group"
    else
        show_info "Adding user to Docker group..."
        run_with_feedback \
            "sudo usermod -aG docker $user" \
            "Added $user to docker group (log out/in for it to take effect)" \
            "Failed to add $user to docker group" \
            false true
        
        show_warning "Group changes will take effect after logout/login"
    fi
}

# Install k9s if missing
install_k9s_if_missing() {
    if command -v k9s >/dev/null 2>&1; then
        show_info "k9s is already installed"
        local version=$(k9s version --short 2>/dev/null | head -n1 || echo "unknown")
        local arch=$(uname -m)
        show_info "Current k9s version: ${BOLD}$version${NC} (${arch})"
    else
        show_info "Installing k9s Kubernetes management tool for Jetson Orin..."
        if install_k9s; then
            # Verify the installation worked
            if command -v k9s >/dev/null 2>&1; then
                local installed_version=$(k9s version --short 2>/dev/null | head -n1 || echo "unknown")
                show_success "k9s installed successfully: ${BOLD}$installed_version${NC}"
            else
                show_error "k9s installation completed but command not found in PATH"
                return 1
            fi
        else
            show_error "k9s installation failed"
            return 1
        fi
    fi
}

# Install additional system utilities
install_system_utilities() {
    local utilities=(
        "curl"
        "wget"
        "jq"
        "yq"
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
        case "$util" in
            "yq")
                install_yq_if_missing
                ;;
            *)
                install_if_missing "$util"
                ;;
        esac
    done
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
            "sudo curl -fsSL \"${URL}\" -o /usr/local/bin/yq && sudo chmod +x /usr/local/bin/yq" \
            "yq installed successfully" \
            "Failed to install yq" \
            true true
    fi
}

# Verify all installations including k3s tools
verify_dependencies() {
    echo -e "\n${CYAN}${BOLD}Verifying Dependencies Installation:${NC}"
    
    local tools=(
        "git:Git version control"
        "docker:Docker containerization"
        "sshpass:SSH password authentication"
        "node:Node.js runtime"
        "npm:Node.js package manager"
        "k9s:Kubernetes management"
        "curl:HTTP client"
        "jq:JSON processor"
        "yq:YAML processor"
        "nsenter:Namespace enter tool"
        "ping:Network connectivity test"
        "nc:Network connection tool"
    )
    
    local failed_tools=()

    for tool_info in "${tools[@]}"; do
        local tool="${tool_info%%:*}"
        local description="${tool_info#*:}"
        
        if command -v "$tool" >/dev/null 2>&1; then
            local version=$(get_tool_version "$tool")
            echo -e "${GREEN} ${CHECKMARK} ${tool}: ${BOLD}${version}${NC} ${DIM}(${description})${NC}"
        else
            echo -e "${RED} ${CROSS} ${tool}: ${BOLD}NOT FOUND${NC} ${DIM}(${description})${NC}"
            failed_tools+=("$tool")
        fi
    done
    
    # Verify k3s tools directory
    echo -e "\n${CYAN}${BOLD}Verifying k3s Offline Tools:${NC}"
    local k3s_tools_dir="/opt/k3s-tools/bin"
    if [ -d "$k3s_tools_dir" ]; then
        local tool_count=$(sudo find "$k3s_tools_dir" -type f -executable 2>/dev/null | wc -l)
        echo -e "${GREEN} ${CHECKMARK} k3s-tools: ${BOLD}${tool_count} tools${NC} ${DIM}(offline operation support)${NC}"
    else
        echo -e "${RED} ${CROSS} k3s-tools: ${BOLD}NOT FOUND${NC} ${DIM}(offline operation support)${NC}"
        failed_tools+=("k3s-tools")
    fi
    
    if [ ${#failed_tools[@]} -eq 0 ]; then
        echo -e "\n${GREEN}${BOLD}${CHECKMARK} All dependencies and k3s offline tools verified successfully!${NC}"
        return 0
    else
        echo -e "\n${RED}${BOLD}${CROSS} Failed tools: ${failed_tools[*]}${NC}"
        return 1
    fi
}

# Get version information for tools
get_tool_version() {
    local tool="$1"
    case "$tool" in
        "git")
            git --version 2>/dev/null | awk '{print $3}' || echo "unknown"
            ;;
        "docker")
            docker --version 2>/dev/null | awk '{print $3}' | tr -d ',' || echo "unknown"
            ;;
        "npm")
            npm --version 2>/dev/null || echo "unknown"
            ;;
        "k9s")
            k9s version --short 2>/dev/null | head -n1 || echo "unknown"
            ;;
        "curl"|"jq"|"yq")
            $tool --version 2>/dev/null | head -n1 | awk '{print $NF}' || echo "unknown"
            ;;
        "nsenter"|"ping"|"nc")
            if command -v "$tool" >/dev/null 2>&1; then
                echo "installed"
            else
                echo "not found"
            fi
            ;;
        *)
            echo "installed"
            ;;
    esac
}

# Main execution function
main() {
    # Source the functions if this script is run directly
    if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
        echo "This script should be called from dk_install.sh"
        exit 1
    fi
    
    install_dependencies "$@"
    verify_dependencies
}

# Export functions for use by parent script
export -f install_dependencies configure_docker_service install_k9s_if_missing
export -f install_system_utilities install_yq_if_missing verify_dependencies
export -f prepare_offline_k3s_tools install_k3s_system_dependencies