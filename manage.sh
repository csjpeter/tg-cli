#!/bin/bash

# tg-cli management script
# A user-friendly interface for building, testing, and running the project

set -e

PROJECT_NAME="tg-cli"
BUILD_DIR="./build"
BIN_DIR="./bin"
# Binaries produced by this project (see docs/adr/0005-three-binary-architecture.md)
BINARIES=(tg-cli-ro tg-cli tg-tui)

show_help() {
    echo "Usage: ./manage.sh [command]"
    echo ""
    echo "Commands:"
    echo "  deps               Install system dependencies (supports Ubuntu 24.04, Rocky 9)"
    echo "  build              Build the project in Release mode"
    echo "  debug              Build the project in Debug mode (with ASAN)"
    echo "  run                Build and run the application"
    echo "  test [filter]      Build and run unit + functional tests (with ASAN); optional substring filter"
    echo "  valgrind           Build and run unit + functional tests with Valgrind"
  echo "  valgrind-unit      Build and run unit tests only with Valgrind"
  echo "  valgrind-functional Build and run functional tests only with Valgrind"
    echo "  test-login         One-shot interactive login; saves session to test.ini:session_bin"
    echo "                     Reads credentials from ~/.config/tg-cli/test.ini"
    echo "  integration [flt]  Build and run integration tests against Telegram test DC"
    echo "                     Reads credentials from ~/.config/tg-cli/test.ini"
    echo "  coverage           Run tests and generate coverage report"
    echo "  tidy               Run clang-tidy static analysis on src/ (warn-only)"
    echo "  check-ro-isolation Verify tg-cli-ro contains no write-domain symbols (ADR-0005)"
    echo "  clean-logs         Purge all application log files"
    echo "  fuzz               Build and run libFuzzer harness for TL parser (requires clang)"
    echo "  clean              Remove all build artifacts"
    echo "  help               Show this help message"
}

install_deps() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
            ubuntu)
                if [[ "$VERSION_ID" == "24.04" ]]; then
                    echo "Detected Ubuntu 24.04. Installing dependencies..."
                    sudo apt-get update
                    sudo apt-get install -y build-essential cmake libssl-dev lcov valgrind clang-tidy
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
                    sudo dnf install -y cmake openssl-devel lcov valgrind clang-tools-extra
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

run_tidy() {
    # Ensure compile_commands.json exists (Release build, no ASAN)
    cmake_configure Release "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

    echo "Running clang-tidy on src/ (excluding vendor and windows platform)..."
    # Exclude vendor (third-party) and windows/ platform sources — the Windows
    # headers are not available on Linux, so clang-tidy would emit hard errors.
    TIDY_FILES=$(find src -name "*.c" \
        ! -path "*/vendor/*" \
        ! -path "*/platform/windows/*")
    # shellcheck disable=SC2086
    clang-tidy -p "$BUILD_DIR" $TIDY_FILES
    echo "clang-tidy complete (warn-only; non-zero exit only on hard errors)."
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

build_integration_runner() {
    cmake --build "$BUILD_DIR" --target tg-integration-test-runner
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
        echo "Running functional tests with ASAN..."
        build_functional_runner
        "$BUILD_DIR/tests/functional/functional-test-runner"
        ;;
    valgrind)
        echo "Running unit tests with Valgrind..."
        build_release
        build_test_runner
        valgrind --leak-check=full --error-exitcode=1 "$BUILD_DIR/tests/unit/test-runner" ${2:+"$2"}
        echo "Running functional tests with Valgrind (crypto-heavy; may be slow)..."
        build_functional_runner
        valgrind --leak-check=full --error-exitcode=1 "$BUILD_DIR/tests/functional/functional-test-runner"
        ;;
    valgrind-unit)
        echo "Running unit tests with Valgrind..."
        build_release
        build_test_runner
        valgrind --leak-check=full --error-exitcode=1 "$BUILD_DIR/tests/unit/test-runner" ${2:+"$2"}
        ;;
    valgrind-functional)
        echo "Running functional tests with Valgrind (crypto-heavy; may be slow)..."
        build_release
        build_functional_runner
        valgrind --leak-check=full --error-exitcode=1 "$BUILD_DIR/tests/functional/functional-test-runner"
        ;;
    test-login)
        build_debug
        cmake --build "$BUILD_DIR" --target tg-test-login
        "$BUILD_DIR/tests/integration/tg-test-login"
        ;;
    integration)
        TEST_INI="$HOME/.config/tg-cli/test.ini"
        if [ ! -f "$TEST_INI" ]; then
            echo "integration tests skipped — $TEST_INI not found"
            echo "  Run: ./manage.sh test-login   to set up credentials"
            exit 0
        fi
        echo "Running integration tests with ASAN (Telegram test DC)..."
        build_debug
        build_integration_runner
        "$BUILD_DIR/tests/integration/tg-integration-test-runner" ${2:+"$2"}
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

        # Pass 3 — PTY suite (terminal.c, readline, password prompt).
        # Runs after the functional .gcda have been captured so the PTY
        # binaries accumulate their own .gcda on top.  lcov merges below.
        if [ "$(uname)" != "Windows_NT" ]; then
            echo "Running PTY test suites for coverage..."
            PTY_RUNNERS=(
                "$BUILD_DIR/tests/functional/pty/pty-terminal-coverage-test-runner"
                "$BUILD_DIR/tests/functional/pty/pty-password-test-runner"
                "$BUILD_DIR/tests/functional/pty/pty-readline-test-runner"
                "$BUILD_DIR/tests/functional/pty/pty-tui-test-runner"
                "$BUILD_DIR/tests/functional/pty/pty-tui-resize-test-runner"
                "$BUILD_DIR/tests/functional/pty/pty-ctrl-c-test-runner"
                "$BUILD_DIR/tests/functional/pty/pty-wizard-test-runner"
            )
            for runner in "${PTY_RUNNERS[@]}"; do
                if [ -x "$runner" ]; then
                    "$runner" || true   # non-zero is a test failure, not a build error
                fi
            done
            echo "Capturing PTY coverage..."
            lcov --capture --directory . \
                 --output-file "$BUILD_DIR/coverage-pty.info"
        fi

        # Combined = unit ∪ functional ∪ PTY (covered if any suite hit it).
        if [ -f "$BUILD_DIR/coverage-pty.info" ]; then
            lcov --add-tracefile "$BUILD_DIR/coverage-unit.info" \
                 --add-tracefile "$BUILD_DIR/coverage-functional.info" \
                 --add-tracefile "$BUILD_DIR/coverage-pty.info" \
                 --output-file "$BUILD_DIR/coverage.info"
        else
            lcov --add-tracefile "$BUILD_DIR/coverage-unit.info" \
                 --add-tracefile "$BUILD_DIR/coverage-functional.info" \
                 --output-file "$BUILD_DIR/coverage.info"
        fi

        genhtml "$BUILD_DIR/coverage.info" \
                --output-directory "$BUILD_DIR/coverage_report"
        genhtml "$BUILD_DIR/coverage-functional.info" \
                --output-directory "$BUILD_DIR/coverage_functional_report"

        echo "Combined coverage:   $BUILD_DIR/coverage_report/index.html"
        echo "Functional coverage: $BUILD_DIR/coverage_functional_report/index.html"
        ;;
    tidy)
        run_tidy
        ;;
    fuzz)
        FUZZ_BUILD_DIR="./build-fuzz"
        FUZZ_SECS="${2:-30}"
        echo "Building fuzz target with clang + libFuzzer (ASAN)..."
        mkdir -p "$FUZZ_BUILD_DIR" "$BIN_DIR"
        cd "$FUZZ_BUILD_DIR"
        cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
              -DCMAKE_C_COMPILER=clang \
              -DENABLE_FUZZ=ON \
              ..
        cd ..
        cmake --build "$FUZZ_BUILD_DIR" --target fuzz-tl-parse
        echo "Running fuzzer for ${FUZZ_SECS}s (corpus: tests/fuzz/corpus/)..."
        mkdir -p tests/fuzz/findings
        "$FUZZ_BUILD_DIR/tests/fuzz/fuzz-tl-parse" \
            tests/fuzz/corpus/ \
            tests/fuzz/findings/ \
            -max_total_time="$FUZZ_SECS" \
            -print_final_stats=1
        echo "Fuzz run complete. New corpus saved to tests/fuzz/findings/."
        ;;
    check-ro-isolation)
        bash ci/check-ro-isolation.sh "${2:-$BIN_DIR/tg-cli-ro}"
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
