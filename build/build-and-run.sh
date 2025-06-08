
# Simple script to build and run the OS using QEMU
set -e



# Default options
IGNORE_NO_QEMU=0
CLEAN=0
VERBOSE=0
DRY_RUN=0
QEMU_ARGS=""
ARCH="x86_64"
LOG_FILE=""
TIMEOUT=""
CUSTOM_BUILD_SCRIPT=""

print_help() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  --ignore-no-qemu         Skip QEMU presence check and try to build and run anyway."
    echo "  --clean                  Clean build artifacts before building."
    echo "  --verbose                Print each command as it runs."
    echo "  --dry-run                Show what would be done without executing commands."
    echo "  --qemu-args=ARGS         Pass extra arguments to QEMU."
    echo "  --arch=ARCH              Select architecture (default: x86_64)."
    echo "  --log-file=FILE          Log build and run output to FILE."
    echo "  --timeout=SECONDS        Kill QEMU after SECONDS."
    echo "  --build-script=SCRIPT    Use a custom build script."
    echo "  -h, --help               Show this help message and exit."
}

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --ignore-no-qemu)
            IGNORE_NO_QEMU=1
            ;;
        --clean)
            CLEAN=1
            ;;
        --verbose)
            VERBOSE=1
            ;;
        --dry-run)
            DRY_RUN=1
            ;;
        --qemu-args=*)
            QEMU_ARGS="${arg#*=}"
            ;;
        --arch=*)
            ARCH="${arg#*=}"
            ;;
        --log-file=*)
            LOG_FILE="${arg#*=}"
            ;;
        --timeout=*)
            TIMEOUT="${arg#*=}"
            ;;
        --build-script=*)
            CUSTOM_BUILD_SCRIPT="${arg#*=}"
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            print_help
            exit 1
            ;;
    esac
done


OS_TYPE="$(uname)"
SCRIPT_DIR="$(dirname "$0")"

# Color output helpers
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color
info() { echo -e "${YELLOW}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Verbose and dry-run helpers
run_cmd() {
    if [ $DRY_RUN -eq 1 ]; then
        echo "+ $*"
        return 0
    fi
    if [ $VERBOSE -eq 1 ]; then
        echo "+ $*"
    fi
    if [ -n "$LOG_FILE" ]; then
        "$@" 2>&1 | tee -a "$LOG_FILE"
        return ${PIPESTATUS[0]}
    else
        "$@"
        return $?
    fi
}

# Clean build artifacts
if [ $CLEAN -eq 1 ]; then
    info "Cleaning build artifacts..."
    run_cmd rm -rf "$SCRIPT_DIR/serotonin.bin" "$SCRIPT_DIR/serotonin.iso" "$SCRIPT_DIR/iso/boot/serotonin.bin" "$SCRIPT_DIR/iso/boot/grub/grub.cfg"
    # Add more clean steps as needed
fi

# Check for build dependencies
check_dep() {
    if ! command -v "$1" >/dev/null 2>&1; then
        error "$1 is required but not installed."
        exit 1
    fi
}
for dep in gcc make; do
    check_dep "$dep"
done
# Optionally check for nasm, etc.

# Check for QEMU unless --ignore-no-qemu is passed

# Function to print QEMU install instructions
print_qemu_install_info() {
    case "$1" in
        Darwin)
            echo "QEMU not found. Install it with: brew install qemu" ;;
        Linux)
            echo "QEMU not found. Install it with: sudo apt install qemu-system-x86 || sudo dnf install qemu-system-x86 || sudo pacman -S qemu" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT)
            echo "QEMU not found. Download and install from: https://www.qemu.org/download/ or use: choco install qemu (if using Chocolatey)" ;;
        *)
            echo "QEMU not found. Please install QEMU for your platform from https://www.qemu.org/download/" ;;
    esac
}

# Check for QEMU unless --ignore-no-qemu is passed

if [ $IGNORE_NO_QEMU -eq 0 ]; then
    QEMU_OK=0
    QEMU_CMD="qemu-system-$ARCH"
    if [ "$OS_TYPE" = "Darwin" ]; then
        command -v "$QEMU_CMD" >/dev/null 2>&1 && QEMU_OK=1
    elif [ "$OS_TYPE" = "Linux" ]; then
        command -v "$QEMU_CMD" >/dev/null 2>&1 && QEMU_OK=1
    elif [[ "$OS_TYPE" =~ MINGW.*|MSYS.*|CYGWIN.*|Windows_NT ]]; then
        command -v "$QEMU_CMD.exe" >/dev/null 2>&1 && QEMU_OK=1
    else
        error "Unsupported OS: $OS_TYPE"
        exit 1
    fi
    if [ $QEMU_OK -ne 1 ]; then
        print_qemu_install_info "$OS_TYPE"
        exit 1
    fi
    # QEMU version check (optional, warn if too old)

    if command -v "$QEMU_CMD" >/dev/null 2>&1; then
        QEMU_VERSION=$("$QEMU_CMD" --version | head -n1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?')
        if [ -n "$QEMU_VERSION" ]; then
            info "QEMU version: $QEMU_VERSION"
        fi
    fi
fi

# Use custom build script if provided
BUILD_SCRIPT="$SCRIPT_DIR/build.sh"
if [ -n "$CUSTOM_BUILD_SCRIPT" ]; then
    BUILD_SCRIPT="$CUSTOM_BUILD_SCRIPT"
fi

# Run build script
info "Running build script: $BUILD_SCRIPT"
if [ $DRY_RUN -eq 1 ]; then
    echo "+ $BUILD_SCRIPT"
    BUILD_STATUS=0
else
    if [ -n "$LOG_FILE" ]; then
        bash "$BUILD_SCRIPT" 2>&1 | tee -a "$LOG_FILE"
        BUILD_STATUS=${PIPESTATUS[0]}
    else
        bash "$BUILD_SCRIPT"
        BUILD_STATUS=$?
    fi
fi

if [ $BUILD_STATUS -eq 0 ]; then
    success "Build succeeded."
    # Select QEMU run script based on OS
    QEMU_RUN_SCRIPT=""
    if [ "$OS_TYPE" = "Darwin" ]; then
        QEMU_RUN_SCRIPT="$SCRIPT_DIR/run-qemu-darwin.sh"
    elif [ "$OS_TYPE" = "Linux" ]; then
        QEMU_RUN_SCRIPT="$SCRIPT_DIR/run-qemu-gnu-linux.sh"
    elif [[ "$OS_TYPE" =~ MINGW.*|MSYS.*|CYGWIN.*|Windows_NT ]]; then
        QEMU_RUN_SCRIPT="$SCRIPT_DIR/run-qemu-windows.sh"
    else
        error "Unsupported OS: $OS_TYPE"
        exit 1
    fi

    info "Running QEMU script: $QEMU_RUN_SCRIPT"
    # Timeout support
    if [ -n "$TIMEOUT" ]; then
        if command -v timeout >/dev/null 2>&1; then
            run_cmd timeout "$TIMEOUT" bash "$QEMU_RUN_SCRIPT" $QEMU_ARGS
        else
            error "timeout command not found, cannot enforce timeout."
            run_cmd bash "$QEMU_RUN_SCRIPT" $QEMU_ARGS
        fi
    else
        run_cmd bash "$QEMU_RUN_SCRIPT" $QEMU_ARGS
    fi
fi
