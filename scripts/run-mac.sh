#!/bin/sh
# runs the macOS build (cmake -S . -B out/build/mac -G Ninja && ninja -C out/build/mac) with the game from the folder
# beside the repo, "Black (USA)", and its settings, saves and logs in out/run-data. BLACK_GAME_DIR / BLACK_DATA_DIR override.
cd "$(dirname "$0")/.."
export BLACK_GAME_DIR=${BLACK_GAME_DIR:-"$PWD/../Black (USA)"}
export BLACK_DATA_DIR=${BLACK_DATA_DIR:-"$PWD/out/run-data"}
exec out/build/mac/black "$@"
