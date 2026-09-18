default:
    @just --list

# Install toolchain (cmake, ninja, aqt) and the pinned Qt
setup:
    mise install
    mise run qt

# Configure the build
configure: setup
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

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
    "$MISE_CONFIG_ROOT/.qt/$QT_VERSION/macos/bin/macdeployqt" build/bin/Deskflow.app
    # macdeployqt rewrites the binaries, which invalidates the signature.
    codesign --force --deep --sign - build/bin/Deskflow.app
    codesign --verify --deep --strict build/bin/Deskflow.app

# Install the bundled app to /Applications
install: bundle
    rm -rf /Applications/Deskflow.app
    cp -R build/bin/Deskflow.app /Applications/
