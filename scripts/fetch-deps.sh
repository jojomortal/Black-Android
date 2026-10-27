#!/bin/sh
# SDL3 into third_party/SDL at the tag the Android build uses (app/build.gradle's fetchSDL does the same for Gradle)
set -e
cd "$(dirname "$0")/.."
TAG=$(sed -n 's/.*sdlTag *= *"\(.*\)".*/\1/p' app/build.gradle | head -1)
[ -n "$TAG" ] || TAG=release-3.4.16
[ -f third_party/SDL/CMakeLists.txt ] || git clone --depth 1 --branch "$TAG" https://github.com/libsdl-org/SDL.git third_party/SDL
echo "SDL $TAG in third_party/SDL"
