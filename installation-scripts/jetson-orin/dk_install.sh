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

# Global variables for progress tracking
TOTAL_STEPS=12
CURRENT_STEP=0

# Function to show animated banner
show_banner() {
    clear
    echo -e "${PURPLE}${BOLD}"
    cat << "EOF"
    ╔══════════════════════════════════════════════════════════════════════╗
    ║                                                                      ║
    ║    ████████╗ ██████╗  ███████╗  █████╗  ███╗   ███╗  ██████╗ ███████╗║
    ║    ██╔═══██║██╔══██╗ ██╔════╝ ██╔══██╗ ████╗ ████║ ██╔═══██╗██╔════╝║
    ║    ██║   ██║██████╔╝ █████╗   ███████║ ██╔████╔██║ ██║   ██║███████╗║
    ║    ██║   ██║██╔══██╗ ██╔══╝   ██╔══██║ ██║╚██╔╝██║ ██║   ██║╚════██║║
    ║    ████████║██║  ██║ ███████╗ ██║  ██║ ██║ ╚═╝ ██║ ╚██████╔╝███████║║
    ║    ╚═══════╝╚═╝  ╚═╝ ╚══════╝ ╚═╝  ╚═╝ ╚═╝     ╚═╝  ╚═════╝ ╚══════╝║
    ║                                                                      ║
    ║                    Professional Installation Suite                   ║
    ║                          Version 2.0 - Next Gen                     ║
    ╚══════════════════════════════════════════════════════════════════════╝
EOF
    echo -e "${NC}"
    
    # Animated subtitle
    local subtitle="Initializing dreamOS installation environment..."
    echo -e "${CYAN}${DIM}"
    for ((i=0; i<${#subtitle}; i++)); do
        echo -n "${subtitle:$i:1}"
        sleep 0.03
    done
    echo -e "${NC}\n"
}

# Function to show progress bar
show_progress() {
    local current=$1
    local total=$2
    local width=50
    local percentage=$((current * 100 / total))
    local filled=$((current * width / total))
    local empty=$((width - filled))
    
    printf "\r${BLUE}${BOLD}Progress: [${GREEN}"
    printf "%*s" $filled | tr ' ' '█'
    printf "${DIM}"
    printf "%*s" $empty | tr ' ' '░'
    printf "${BLUE}${BOLD}] %3d%% (%d/%d)${NC}" $percentage $current $total
}

# Function for animated spinner
spinner() {
    local pid=$1
    local message=$2
    local i=0
    
    while kill -0 $pid 2>/dev/null; do
        printf "\r${YELLOW}${SPINNER_FRAMES[i]} ${WHITE}%s${NC}" "$message"
        i=$(((i + 1) % ${#SPINNER_FRAMES[@]}))
        sleep 0.1
    done
    printf "\r"
}

# Function to show step header
show_step() {
    local step_num=$1
    local step_name=$2
    local description=$3
    
    CURRENT_STEP=$step_num
    echo -e "\n${BLUE}${BOLD}[$step_num/$TOTAL_STEPS] $step_name${NC}"
    echo -e "${DIM}$description${NC}"
    show_progress $CURRENT_STEP $TOTAL_STEPS
    echo
}

# Function to show success message
show_success() {
    local message=$1
    echo -e "${GREEN}${BOLD} ${CHECKMARK} ${message}${NC}"
}

# Function to show error message
show_error() {
    local message=$1
    echo -e "${RED}${BOLD} ${CROSS} ${message}${NC}"
}

# Function to show info message
show_info() {
    local message=$1
    echo -e "${BLUE} ${ARROW} ${message}${NC}"
}

# Function to show warning message
show_warning() {
    local message=$1
    echo -e "${YELLOW}${BOLD} ⚠ ${message}${NC}"
}

# Function for typing animation
type_text() {
    local text=$1
    local delay=${2:-0.02}
    echo -e "${WHITE}"
    for ((i=0; i<${#text}; i++)); do
        echo -n "${text:$i:1}"
        sleep $delay
    done
    echo -e "${NC}"
}

# Function to run docker pull with detailed info
docker_pull_with_info() {
    local image=$1
    local description=$2
    local registry_info=$3
    
    echo -e "${CYAN}${BOLD}Downloading: ${WHITE}$image${NC}"
    echo -e "${DIM}Description: $description${NC}"
    echo -e "${DIM}Registry: $registry_info${NC}"
    echo -e "${DIM}$(printf '─%.0s' {1..60})${NC}"
    
    # Show docker pull output
    docker pull "$image" 2>&1 | while IFS= read -r line; do
        if [[ "$line" == *"Pulling"* ]]; then
            echo -e "${BLUE} → $line${NC}"
        elif [[ "$line" == *"Download complete"* ]]; then
            echo -e "${GREEN} ✓ $line${NC}"
        elif [[ "$line" == *"Pull complete"* ]]; then
            echo -e "${GREEN} ✓ $line${NC}"
        elif [[ "$line" == *"Status:"* ]]; then
            echo -e "${GREEN}${BOLD} $line${NC}"
        elif [[ "$line" == *"Error"* ]] || [[ "$line" == *"error"* ]]; then
            echo -e "${RED} ✗ $line${NC}"
        else
            echo -e "${DIM} $line${NC}"
        fi
    done
    
    # Get image size info
    local image_size=$(docker images --format "table {{.Repository}}:{{.Tag}}\t{{.Size}}" | grep "$image" | awk '{print $2}' | head -1)
    if [ -n "$image_size" ]; then
        echo -e "${GREEN}${BOLD} ✓ Download completed - Image size: $image_size${NC}"
    else
        echo -e "${GREEN}${BOLD} ✓ Download completed${NC}"
    fi
    echo
}

force_deployment_update() {
    local deployment_name=$1
    local namespace=${2:-default}
    local image_name=$3  # Optional: specific image to verify
    
    show_info "Forcing update for deployment: $deployment_name"
    
    # Step 1: Delete existing deployment to ensure fresh start
    run_with_feedback \
        "sudo kubectl delete deployment/$deployment_name -n $namespace --ignore-not-found --wait=true" \
        "Existing deployment removed" \
        "Deployment cleanup completed"
    
    # Step 2: Wait for complete cleanup
    show_info "Waiting for cleanup to complete..."
    sleep 3
    
    # Step 3: Verify pods are terminated
    local pod_count=$(kubectl get pods -l app=$deployment_name -n $namespace --no-headers 2>/dev/null | wc -l)
    if [ "$pod_count" -gt 0 ]; then
        show_warning "Force deleting remaining pods..."
        kubectl delete pods -l app=$deployment_name -n $namespace --force --grace-period=0 --ignore-not-found
        sleep 5
    fi
    
    # Step 4: Clear any cached images if specified
    if [ -n "$image_name" ]; then
        show_info "Clearing cached image: $image_name"
        docker rmi "$image_name" 2>/dev/null || true
    fi
    
    return 0
}

apply_manifest() {
    # -----------------------------------------------------------------
    # make all placeholders available to envsubst
    # -----------------------------------------------------------------
    export DOCKER_HUB_NAMESPACE ARCH DK_USER RUNTIME_NAME HOME_DIR \
        dk_vip_demo DISPLAY XDG_RUNTIME_DIR

    # -----------------------------------------------------------------
    MANIFEST_DIR="${CURRENT_DIR}/manifests"
    local yaml="$1"
    local tmp_dir="tmp/dk_manifests"
    local parsed_yaml="${tmp_dir}/parsed_${yaml}"
    
    # Create tmp directory for parsed manifests
    mkdir -p "$tmp_dir"
    
    local VARS='${DOCKER_HUB_NAMESPACE} ${ARCH} ${DK_USER} ${RUNTIME_NAME} \
                ${HOME_DIR} ${dk_vip_demo} ${DISPLAY} ${XDG_RUNTIME_DIR}'
    
    show_info "Processing manifest: ${BOLD}${yaml}${NC}"
    show_info "Creating parsed version in: ${DIM}${parsed_yaml}${NC}"
    
    # Parse template and save to tmp folder
    if envsubst "${VARS}" < "${MANIFEST_DIR}/${yaml}" > "${parsed_yaml}"; then
        show_success "Manifest parsed successfully"
        show_info "Parsed manifest saved to: ${CYAN}${parsed_yaml}${NC}"
        
        # Show some key information from the parsed manifest
        if command -v yq >/dev/null 2>&1; then
            local kind=$(yq eval '.kind' "${parsed_yaml}" 2>/dev/null || echo "Unknown")
            local name=$(yq eval '.metadata.name' "${parsed_yaml}" 2>/dev/null || echo "Unknown")
            show_info "Resource type: ${BOLD}${kind}${NC}, Name: ${BOLD}${name}${NC}"
        fi
        
        # Apply the parsed manifest
    run_with_feedback \
            "kubectl apply -f '${parsed_yaml}'" \
      "Applied manifest ${yaml}" \
      "Failed to apply ${yaml}"
            
        # Optional: Show what was applied
        if [ $? -eq 0 ]; then
            show_info "Manifest applied from: ${DIM}${parsed_yaml}${NC}"
            show_info "You can inspect the parsed manifest for debugging"
        fi
    else
        show_error "Failed to parse manifest ${yaml}"
        return 1
    fi
}

# Enhanced manifest application with force update
apply_manifest_with_force_update() {
    local yaml="$1"
    local deployment_name="$2"
    local image_name="$3"  # Optional
    
    # Step 1: Force update if deployment exists
    if kubectl get deployment "$deployment_name" -n default >/dev/null 2>&1; then
        show_info "Deployment exists, forcing update..."
        force_deployment_update "$deployment_name" "default" "$image_name"
    fi
    
    # Step 2: Apply manifest
    apply_manifest "$yaml"
    
    # Step 3: Wait for deployment with extended timeout
    run_with_feedback \
        "sudo kubectl rollout status deployment/$deployment_name --timeout=600s" \
        "$deployment_name is READY with latest image" \
        "$deployment_name failed to start"
    
    # Step 4: Verify image version (if provided)
    if [ -n "$image_name" ]; then
        show_info "Verifying deployed image..."
        local deployed_image=$(kubectl get deployment "$deployment_name" -o jsonpath='{.spec.template.spec.containers[0].image}')
        show_info "Deployed image: $deployed_image"
        
        # Optional: Get image digest for verification
        local image_digest=$(kubectl get deployment "$deployment_name" -o jsonpath='{.spec.template.spec.containers[0].image}' | xargs docker inspect --format='{{index .RepoDigests 0}}' 2>/dev/null || echo "N/A")
        if [ "$image_digest" != "N/A" ]; then
            show_info "Image digest: $image_digest"
        fi
    fi
}

# Enhanced function to clean up tmp manifests if needed
cleanup_tmp_manifests() {
    local tmp_dir="/tmp/dk_manifests"
    if [ -d "$tmp_dir" ]; then
        show_info "Cleaning up temporary manifest files..."
        rm -rf "$tmp_dir"
        show_success "Temporary manifests cleaned up"
    fi
}

# Enhanced function to show parsed manifest content (for debugging)
show_parsed_manifest() {
    local yaml="$1"
    local tmp_dir="/tmp/dk_manifests"
    local parsed_yaml="${tmp_dir}/parsed_${yaml}"
    
    if [ -f "$parsed_yaml" ]; then
        echo -e "\n${CYAN}${BOLD}Parsed manifest content for ${yaml}:${NC}"
        echo -e "${DIM}$(printf '─%.0s' {1..60})${NC}"
        cat "$parsed_yaml"
        echo -e "${DIM}$(printf '─%.0s' {1..60})${NC}\n"
    else
        show_warning "Parsed manifest not found: $parsed_yaml"
    fi
}

# Function to source sub-scripts
source_subscript() {
    local script_name="$1"
    local script_path="$CURRENT_DIR/scripts/$script_name"
    
    if [ -f "$script_path" ]; then
        show_info "Loading ${BOLD}${script_name}${NC}..."
        source "$script_path"
        return 0
    else
        show_error "Required script not found: $script_path"
        return 1
    fi
}

run_with_feedback() {
    local command=$1
    local success_msg=$2
    local error_msg=$3
    local show_output=${4:-false}
    local needs_sudo=${5:-false}
    
    if [ "$show_output" = "true" ]; then
        echo -e "${DIM}${CYAN}Running: $command${NC}"
        if [ "$needs_sudo" = "true" ]; then
            echo -e "${YELLOW}[sudo] password for $DK_USER: ${NC}"
        fi
        if eval "$command"; then
            show_success "$success_msg"
            return 0
        else
            show_error "$error_msg"
            return 1
        fi
    else
        # For sudo commands, show password prompt clearly
        if [ "$needs_sudo" = "true" ]; then
            echo -e "${YELLOW}[sudo] password for $DK_USER: ${NC}"
            eval "$command" 2>&1 | while IFS= read -r line; do
                if [[ "$line" == *"password"* ]]; then
                    echo -e "\r${YELLOW}[sudo] password for $DK_USER: ${NC}"
                fi
            done
        else
            # Run command in background and show spinner
            eval "$command" >/dev/null 2>&1 &
            local cmd_pid=$!
            spinner $cmd_pid "Processing..."
            wait $cmd_pid
        fi
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

# Function to create fancy separator
separator() {
    echo -e "${DIM}$(printf '─%.0s' {1..50})${NC}"
}

# Main installation function
main() {
    # Show banner
    show_banner
    
    # Welcome message with animation
    echo -e "${CYAN}${BOLD}${DREAM} Welcome to the dreamOS Installation Experience! ${DREAM}${NC}\n"
    type_text "This installer will set up your complete dreamOS environment with all required components." 0.01
    echo -e "\n${YELLOW}${BOLD}${ROCKET} Ready to begin your journey? ${ROCKET}${NC}\n"
    
    read -p "Press Enter to continue or Ctrl+C to cancel..."
    
    # Step 1: Environment Detection
    show_step 1 "Environment Detection" "Analyzing system configuration and user environment"
    
    # Determine the user who ran the command
    if [ -n "$SUDO_USER" ]; then
        DK_USER=$SUDO_USER
    else
        DK_USER=$USER
    fi
    show_info "Detected user: ${BOLD}$DK_USER${NC}"
    
    # Get the current install script path
    CURRENT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    show_info "Installation directory: ${BOLD}$CURRENT_DIR${NC}"
    
    # Detect architecture
    ARCH_DETECT=$(uname -m)
    if [[ "$ARCH_DETECT" == "x86_64" ]]; then
        ARCH="amd64"
    elif [[ "$ARCH_DETECT" == "aarch64" ]]; then
        ARCH="arm64"
    else
        ARCH="unknown"
    fi
    show_info "System architecture: ${BOLD}$ARCH${NC} (${ARCH_DETECT})"
    
    # Create the serial_number file, which will be referred by dk_manager, sdv-runtime, dk_ivi
    serial_file="/home/$DK_USER/.dk/dk_manager/serial-number"
    # Ensure the directory exists
    sudo mkdir -p "$(dirname "$serial_file")"
    # If the file doesn't exist or is empty, generate a random 16-character hex string
    if [[ ! -s "$serial_file" ]]; then
        serial_number=$(openssl rand -hex 8)  # 8 bytes = 16 hex chars
        echo "$serial_number" > "$serial_file"
    else
        serial_number=$(tail -n 1 "$serial_file")
    fi
    # Get last 8 characters (if the line is shorter, will print the whole line)
    RUNTIME_NAME="dreamKIT-${serial_number: -8}"

    sleep 1
    show_success "Environment detection completed"
    
    # Step 2: Docker Configuration
    show_step 2 "Docker Setup" "Configuring Docker environment and user permissions"
    
    # Check if docker group exists
    if getent group docker > /dev/null 2>&1; then
        show_info "Docker group already exists"
    else
        run_with_feedback "sudo groupadd docker" "Docker group created successfully" "Failed to create docker group" false true
    fi
    
    # Add user to docker group
    run_with_feedback "sudo usermod -aG docker '$DK_USER'" "User '$DK_USER' added to docker group" "Failed to add user to docker group" false true
    show_warning "Please log out and back in for group changes to take effect"
    
    # Step 3: System Architecture & Runtime
    show_step 3 "Runtime Configuration" "Setting up XDG runtime and audio parameters"
    
    # Get XDG_RUNTIME_DIR
    XDG_RUNTIME_DIR=$(sudo -u "$DK_USER" env | grep XDG_RUNTIME_DIR | cut -d= -f2)
    if [ -z "$XDG_RUNTIME_DIR" ]; then
        XDG_RUNTIME_DIR="/run/user/$(id -u "$DK_USER")"
    fi
    show_info "XDG Runtime Directory: ${BOLD}$XDG_RUNTIME_DIR${NC}"
    
    # Set environment variables
    HOME_DIR="/home/$DK_USER"
    DOCKER_SHARE_PARAM="-v /var/run/docker.sock:/var/run/docker.sock -v /usr/bin/docker:/usr/bin/docker"
    DOCKER_AUDIO_PARAM="--device /dev/snd --group-add audio -e PULSE_SERVER=unix:${XDG_RUNTIME_DIR}/pulse/native -v ${XDG_RUNTIME_DIR}/pulse/native:${XDG_RUNTIME_DIR}/pulse/native -v $HOME_DIR/.config/pulse/cookie:/root/.config/pulse/cookie"
    K3S_SHARE_PARAM=" -v /usr/local/bin/kubectl:/usr/local/bin/kubectl:ro -v ~/.kube/config:/root/.kube/config:ro"
    LOG_LIMIT_PARAM="--log-opt max-size=10m --log-opt max-file=3"
    DOCKER_HUB_NAMESPACE="ghcr.io/eclipse-autowrx"
    
    show_success "Runtime configuration completed"
    
    # Step 4: Directory Structure
    show_step 4 "Directory Structure" "Creating dreamOS directory hierarchy"
    
    run_with_feedback "mkdir -p /home/$DK_USER/.dk/dk_swupdate /home/$DK_USER/.dk/dk_swupdate/dk_patch /home/$DK_USER/.dk/dk_swupdate/dk_current /home/$DK_USER/.dk/dk_swupdate/dk_current_patch" "Directory structure created successfully" "Failed to create directory structure"
    
    # Step 5: Network Setup
    show_step 5 "Network Setup" "Establishing Docker network infrastructure"
    
    run_with_feedback "docker network create dk_network 2>/dev/null || true" "Docker network 'dk_network' ready" "Network setup encountered issues"
    
    # Step 6: Dependencies Installation (moved to sub-script)
    show_step 6 "Dependencies" "Installing required system utilities and tools"
    
    # Source and execute dependencies installation script
    if source_subscript "install_dependencies.sh"; then
        install_dependencies "$CURRENT_DIR" "$DK_USER"
        if [ $? -eq 0 ]; then
            show_success "Dependencies installation completed"
    else
            show_error "Dependencies installation failed"
            exit 1
        fi
    else
        show_error "Failed to load dependencies installation script"
        exit 1
    fi

    ###############################################################################
    # Step 7   local Docker registry
    ###############################################################################
    show_step 7 "Docker local registry" "VIP installation"
    show_info "Setup local registry..."
    run_with_feedback \
        "sudo $CURRENT_DIR/scripts/setup_local_docker_registry.sh" \
        "Docker local host enabled.\
        \n ✓ You can now use the local Docker registry for your images.\
        \n ✓ To push images, use: docker push localhost:5000/your-image-name" \
        "Docker local setup failed"

    ###############################################################################
    # Step 8   K3s-based installation
    ###############################################################################
    show_step 8 "K3s-based installation" "k3s master installation & preparation for local registry"
    sudo scripts/k3s-master-prepare.sh eth0
    if [ $? -ne 0 ]; then
        show_error "Failed to prepare K3s master. Please check the logs."
        exit 1
    fi
    show_success "K3s master prepared successfully"
    
    ###############################################################################
    # Step-9   NXP-S32G setup (k3s-agent & friends)
    ###############################################################################
    show_step 9 "NXP-S32G setup" "k3s-agent installation & relavant stuff"

    TARGET_IP="192.168.56.49"
    PING_COUNT=3      # how many echo-requests we send
    PING_TIMEOUT=2    # wait time (seconds) for each reply

    show_info "Checking reachability of ECU at ${TARGET_IP} ..."

    if ping -c "${PING_COUNT}" -W "${PING_TIMEOUT}" "${TARGET_IP}" >/dev/null 2>&1; then
        show_success "ECU reachable."
        show_info "Proceed with the NXP-S32G setup? [y/N]: "
    else
        show_warning "Could NOT reach ${TARGET_IP}. Is the ECU powered on and connected?"
        show_info "Attempt the NXP-S32G setup anyway? [y/N]: "
    fi

    read -r nxp_s32g_setup
    
    if [[ "$nxp_s32g_setup" =~ ^[Yy]$ ]]; then
        show_info "Calling NXP-S32G setup script..."
        run_with_feedback "sudo $CURRENT_DIR/scripts/k3s-agent-offline-install.sh" \
                        "NXP-S32G setup completed" \
                        "NXP-S32G setup failed"
    else
        show_info "NXP-S32G setup skipped (you can run it later with './scripts/k3s-agent-offline-install.sh')"
    fi

    ###############################################################################
    # Step 10   SDV Runtime
    ###############################################################################
    show_step 10 "SDV Runtime" "Setting up Software Defined Vehicle runtime environment"

    # Export variables for sub-scripts
    export HOME_DIR
    export DK_USER
    scripts/setup_default_vss.sh

    # Enhanced SDV Runtime deployment
    show_info "Deploying SDV Runtime with force update..."

    # Pull latest image first
    apply_manifest sdv-runtime-pull.yaml
    run_with_feedback \
        "sudo kubectl wait --for=condition=complete --timeout=300s job/sdv-runtime-pull" \
        "Latest SDV Runtime image pulled" \
        "SDV Runtime image pull failed"

    # Clean up pull job
    run_with_feedback \
        "sudo kubectl delete job sdv-runtime-pull --ignore-not-found" \
        "Pull job cleaned up" \
        "Cleanup completed"

    # Apply with force update
    apply_manifest_with_force_update "sdv-runtime.yaml" "sdv-runtime" "${DOCKER_HUB_NAMESPACE}/sdv-runtime:latest"

    ###############################################################################
    # Step 11   DreamKit Manager
    ###############################################################################
    show_step 11 "DreamKit Manager" "Installing core management services"

    # Pull latest image first
    apply_manifest dk-manager-pull.yaml
    run_with_feedback \
        "sudo kubectl wait --for=condition=complete --timeout=300s job/dk-manager-pull" \
        "Latest DreamKit Manager image pulled" \
        "DreamKit Manager image pull failed"

    # Clean up pull job
    run_with_feedback \
        "sudo kubectl delete job dk-manager-pull --ignore-not-found" \
        "Pull job cleaned up" \
        "Cleanup completed"

    # Apply with force update
    apply_manifest_with_force_update "dk-manager.yaml" "dk-manager" "${DOCKER_HUB_NAMESPACE}/dk-manager:latest"

    ###############################################################################
    # Step 12   IVI Interface (optional)
    ###############################################################################
    show_step 12 "IVI Interface" "Configuring In-Vehicle Infotainment system"

    # Check for dk_ivi parameter
    dk_ivi_value=""
    for arg in "$@"; do
        if [[ "$arg" == dk_ivi=* ]]; then
            dk_ivi_value="${arg#*=}"
        fi
    done

    DOCKER_HUB_NAMESPACE="ghcr.io/samtranbosch"

    if [[ "$dk_ivi_value" == "true" ]]; then
        show_info "Installing IVI interface …"

        run_with_feedback "sudo $CURRENT_DIR/scripts/dk_enable_xhost.sh" \
                            "X11 forwarding enabled" "X11 setup failed"
        run_with_feedback "xhost +local:docker" "Docker X11 access granted" "X11 access failed"

        # Pull latest image first
        apply_manifest dk-ivi-pull.yaml
        run_with_feedback \
            "sudo kubectl wait --for=condition=complete --timeout=300s job/dk-ivi-pull" \
            "Latest IVI image pulled" \
            "IVI image pull failed"
        
        # Clean up pull job
        run_with_feedback \
            "sudo kubectl delete job dk-ivi-pull --ignore-not-found" \
            "Pull job cleaned up" \
            "Cleanup completed"

        # Decide which manifest to apply and force update
        if [ -f "/etc/nv_tegra_release" ]; then
            apply_manifest_with_force_update "dk-ivi-jetson.yaml" "dk-ivi" "${DOCKER_HUB_NAMESPACE}/dk_ivi:latest"
        else
            apply_manifest_with_force_update "dk-ivi.yaml" "dk-ivi" "${DOCKER_HUB_NAMESPACE}/dk_ivi:latest"
        fi
    else
        show_info "IVI installation skipped (you can install later with './dk_install dk_ivi=true')"
    fi

    ###############################################################################
    # Final steps
    ###############################################################################
    separator
    echo -e "\n${BLUE}${BOLD}Finalizing installation...${NC}\n"
    
    # Save environment variables
    show_info "Saving environment configuration..."
    mkdir -p $HOME_DIR/.dk/dk_swupdate
    DK_ENV_FILE="$HOME_DIR/.dk/dk_swupdate/dk_swupdate_env.sh"
    cat <<EOF > "${DK_ENV_FILE}"
#!/bin/bash

DK_USER="${DK_USER}"
ARCH="${ARCH}"
HOME_DIR="${HOME_DIR}"
DOCKER_SHARE_PARAM="${DOCKER_SHARE_PARAM}"
XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}"
DOCKER_AUDIO_PARAM="${DOCKER_AUDIO_PARAM}"
LOG_LIMIT_PARAM="${LOG_LIMIT_PARAM}"
DOCKER_HUB_NAMESPACE="${DOCKER_HUB_NAMESPACE}"
dk_ivi_value="${dk_ivi_value}"
EOF
    chmod +x "${DK_ENV_FILE}"
    
    # Create additional services
    run_with_feedback "$CURRENT_DIR/scripts/create_dk_xiphost_service.sh" "Additional services configured" "Service configuration warning"
    
    # Cleanup
    show_info "Cleaning up temporary files..."
    run_with_feedback "docker image prune -f" "Docker cleanup completed" "Cleanup warning"
    
    # Success message
    echo -e "\n${GREEN}${BOLD}Installation completed successfully!${NC}\n"
    
    # Installation summary
    echo -e "${CYAN}${BOLD}Installation Summary:${NC}"
    echo -e "${GREEN} ${CHECKMARK} Environment configured for user: ${BOLD}$DK_USER${NC}"
    echo -e "${GREEN} ${CHECKMARK} System architecture: ${BOLD}$ARCH${NC}"
    echo -e "${GREEN} ${CHECKMARK} Docker environment ready${NC}"
    echo -e "${GREEN} ${CHECKMARK} All core services installed${NC}"
    echo -e "${GREEN} ${CHECKMARK} Network infrastructure ready${NC}"
    if [[ "$dk_ivi_value" == "true" ]]; then
        echo -e "${GREEN} ${CHECKMARK} IVI interface installed${NC}"
    fi
    
    echo -e "\n${YELLOW}${BOLD}Important:${NC}"
    echo -e " • Please reboot your system for all changes to take effect"
    echo -e " • Log out and back in to apply Docker group permissions"
    echo -e " • Your dreamOS environment will be ready after reboot"
    
    if [[ "$dk_ivi_value" == "true" ]]; then
        echo -e "\n${CYAN}${BOLD}To start the IVI interface:${NC}"
        echo -e "${WHITE} • Run: ${CYAN}./dk_run.sh${NC}"
        echo -e "${DIM} • This will launch the In-Vehicle Infotainment dashboard${NC}"
    fi
    
    echo -e "\n${GREEN}Thank you for choosing dreamOS!${NC}"
}

# Run main function
main "$@"