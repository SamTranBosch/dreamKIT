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
    # https://github.com/derailed/k9s/releases/download/v0.50.9/k9s_Linux_arm64.tar.gz

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

install_dependencies() {
    local CURRENT_DIR="$1"
    local DK_USER="$2"
    
    echo -e "${BLUE}${BOLD}Installing System Dependencies${NC}"
    echo -e "${DIM}This will install all required packages and tools${NC}"
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
    
    # 4) Node.js and npm
    show_info "Installing Node.js development tools..."
    install_if_missing npm
    
    # 5) Kubernetes tools
    show_info "Installing Kubernetes management tools..."
    install_k9s_if_missing
    
    # 6) Additional system utilities
    show_info "Installing additional system utilities..."
    install_system_utilities
    
    show_success "All system dependencies installed successfully"
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

# Verify all installations
verify_dependencies() {
    echo -e "\n${CYAN}${BOLD}Verifying Dependencies Installation:${NC}"
    
    local tools=(
        "git:Git version control"
        "docker:Docker containerization"
        "sshpass:SSH password authentication"
        "npm:Node.js package manager"
        "k9s:Kubernetes management"
        "curl:HTTP client"
        "jq:JSON processor"
        "yq:YAML processor"
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
    
    if [ ${#failed_tools[@]} -eq 0 ]; then
        echo -e "\n${GREEN}${BOLD}${CHECKMARK} All dependencies verified successfully!${NC}"
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