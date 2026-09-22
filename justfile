default:
    @just --list

# Install toolchain (cmake, ninja, aqt) and the pinned Qt
setup:
    mise install
    mise run qt

# Oldest macOS the build runs on. Homebrew's OpenSSL, linked statically, needs 26.
macos_min := "26.0"

# Configure the build
configure: setup
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET={{macos_min}}

# Build Deskflow
build: configure
    cmake --build build

# Run the test suite
test: build
    ctest --test-dir build --output-on-failure

# Remove build output (keeps the downloaded Qt)
clean:
    rm -rf build

# Bundle Qt into the .app and re-sign, making it relocatable
bundle: build
    "$CMAKE_PREFIX_PATH/bin/macdeployqt" build/bin/Deskflow.app
    # macdeployqt rewrites the binaries, which invalidates the signature.
    codesign --force --deep --sign - build/bin/Deskflow.app
    codesign --verify --deep --strict build/bin/Deskflow.app

# Install the bundled app to /Applications
install: bundle
    rm -rf /Applications/Deskflow.app
    cp -R build/bin/Deskflow.app /Applications/
