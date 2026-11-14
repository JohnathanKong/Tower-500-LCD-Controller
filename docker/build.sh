#!/usr/bin/env bash
set -euo pipefail

cd /workspace
cmake -S /workspace -B /tmp/thermaltake500lcd-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/workspace/docker/mingw64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/thermaltake500lcd-build --parallel "$(nproc)"

# Never delete the installer's input or user files in the project root.
mkdir -p /workspace/dist
install -m 0644 /tmp/thermaltake500lcd-build/thermaltake500lcd.exe /workspace/dist/
install -m 0644 /tmp/thermaltake500lcd-build/thermaltake500lcd.service.exe /workspace/dist/
cp /workspace/Install.cmd /workspace/Install.ps1 /workspace/README.md /workspace/LICENSE /workspace/dist/

for exe in /workspace/dist/thermaltake500lcd.exe /workspace/dist/thermaltake500lcd.service.exe; do
    file "$exe"
    if x86_64-w64-mingw32-objdump -p "$exe" | grep -Ei 'DLL Name: (libstdc\+\+|libgcc_s|libwinpthread)'; then
        echo "ERROR: Non-system MinGW runtime DLL dependency detected in $exe" >&2
        exit 1
    fi
done

echo "Build complete: ./dist/thermaltake500lcd.exe and ./dist/thermaltake500lcd.service.exe"
