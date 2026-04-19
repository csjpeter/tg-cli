#!/bin/bash

# tg-cli management script
# A user-friendly interface for building, testing, and running the project

set -e

PROJECT_NAME="tg-cli"
BUILD_DIR="./build"
BIN_DIR="./bin"
# Binaries produced by this project (see docs/adr/0005-three-binary-architecture.md)
BINARIES=(tg-cli-ro tg-tui)

show_help() {
    echo "Usage: ./manage.sh [command]"
    echo ""
    echo "Commands:"
    echo "  deps           Install system dependencies (supports Ubuntu 24.04, Rocky 9)"
    echo "  build          Build the project in Release mode"
    echo "  debug          Build the project in Debug mode (with ASAN)"
    echo "  run            Build and run the application"
    echo "  test [filter]  Build and run unit tests (with ASAN); optional substring filter"
    echo "  valgrind       Build and run unit tests with Valgrind"
    echo "  coverage       Run tests and generate coverage report"
    echo "  clean-logs     Purge all application log files"
    echo "  clean          Remove all build artifacts"
    echo "  help           Show this help message"
}

install_deps() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
            ubuntu)
                if [[ "$VERSION_ID" == "24.04" ]]; then
                    echo "Detected Ubuntu 24.04. Installing dependencies..."
                    sudo apt-get update
                    sudo apt-get install -y build-essential cmake libssl-dev lcov valgrind
                else
                    echo "Unsupported Ubuntu version: $VERSION_ID. Only 24.04 is explicitly supported."
                    exit 1
                fi
                ;;
            rocky)
                if [[ "$VERSION_ID" == 9* ]]; then
                    echo "Detected Rocky Linux 9. Installing dependencies..."
                    sudo dnf install -y epel-release
                    sudo dnf groupinstall -y "Development Tools"
                    sudo dnf install -y cmake openssl-devel lcov valgrind
                else
                    echo "Unsupported Rocky version: $VERSION_ID. Only 9.x is explicitly supported."
                    exit 1
                fi
                ;;
            *)
                echo "Unsupported OS: $ID. Please install dependencies manually."
                exit 1
                ;;
        esac
    else
        echo "Could not detect OS. Please install dependencies manually."
        exit 1
    fi
}

cmake_configure() {
    local build_type="$1"
    local extra_flags="${2:-}"
    mkdir -p "$BUILD_DIR" "$BIN_DIR"
    cd "$BUILD_DIR"
    cmake -DCMAKE_BUILD_TYPE="$build_type" $extra_flags ..
    cd ..
}

cmake_build() {
    cmake --build "$BUILD_DIR"
    for b in "${BINARIES[@]}"; do
        if [ -f "$BUILD_DIR/$b" ]; then
            cp "$BUILD_DIR/$b" "$BIN_DIR/"
        fi
    done
}

build_release() {
    cmake_configure Release
    cmake_build
    echo "Build complete: ${BINARIES[*]/#/$BIN_DIR/}"
}

build_debug() {
    cmake_configure Debug
    cmake_build
    echo "Debug build (with ASAN) complete: ${BINARIES[*]/#/$BIN_DIR/}"
}

build_test_runner() {
    cmake --build "$BUILD_DIR" --target test-runner
}

build_functional_runner() {
    cmake --build "$BUILD_DIR" --target functional-test-runner
}

case "$1" in
    deps)
        install_deps
        ;;
    build)
        build_release
        ;;
    debug)
        build_debug
        ;;
    run)
        build_release
        echo "Launching tg-cli-ro..."
        "$BIN_DIR/tg-cli-ro" "${@:2}"
        ;;
    test)
        echo "Running unit tests with ASAN..."
        build_debug
        build_test_runner
        "$BUILD_DIR/tests/unit/test-runner" ${2:+"$2"}
        ;;
    valgrind)
        echo "Running unit tests with Valgrind..."
        build_release
        build_test_runner
        valgrind --leak-check=full --error-exitcode=1 "$BUILD_DIR/tests/unit/test-runner" ${2:+"$2"}
        ;;
    coverage)
        cmake_configure Debug "-DENABLE_COVERAGE=ON"
        cmake_build
        build_test_runner
        build_functional_runner

        # Pass 1 — unit suite. Wipe .gcda first so the capture only
        # reflects what the unit runner exercised.
        find "$BUILD_DIR" -name "*.gcda" -delete
        "$BUILD_DIR/tests/unit/test-runner"
        echo "Capturing unit coverage..."
        lcov --capture --directory . \
             --output-file "$BUILD_DIR/coverage-unit.info"

        # Pass 2 — functional suite. Fresh .gcda so this capture is
        # functional-only; we'll merge with unit below for the combined
        # report.
        find "$BUILD_DIR" -name "*.gcda" -delete
        "$BUILD_DIR/tests/functional/functional-test-runner"
        echo "Capturing functional coverage..."
        lcov --capture --directory . \
             --output-file "$BUILD_DIR/coverage-functional.info"

        # Combined = unit ∪ functional (covered if either suite hit it).
        lcov --add-tracefile "$BUILD_DIR/coverage-unit.info" \
             --add-tracefile "$BUILD_DIR/coverage-functional.info" \
             --output-file "$BUILD_DIR/coverage.info"

        genhtml "$BUILD_DIR/coverage.info" \
                --output-directory "$BUILD_DIR/coverage_report"
        genhtml "$BUILD_DIR/coverage-functional.info" \
                --output-directory "$BUILD_DIR/coverage_functional_report"

        echo "Combined coverage:   $BUILD_DIR/coverage_report/index.html"
        echo "Functional coverage: $BUILD_DIR/coverage_functional_report/index.html"
        ;;
    clean-logs)
        rm -rf ~/.cache/tg-cli/logs/*
        echo "Logs cleaned."
        ;;
    clean)
        rm -rf "$BUILD_DIR" "$BIN_DIR"
        echo "Cleaned."
        ;;
    help|*)
        show_help
        ;;
esac
