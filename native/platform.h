#pragma once
// What differs between the hosts the parts of native/ run on: platform_android.cpp and platform_apple.cpp (macOS, iOS)
#include <vulkan/vulkan.h>

#include <filesystem>
#include <vector>

struct SDL_Window;
typedef uint64_t SDL_WindowFlags;

namespace input {
struct Pad;
}

namespace platform {

// the log's lines (stderr) to the system's log as well: logcat, os_log
void RedirectLog();
// SDL hints this host sets, before SDL_Init
void SetHints();
// app-specific storage: the settings, saves, logs and the shader cache
std::filesystem::path DataDir();
// where the game files or a disc image are looked for, in this order
std::vector<std::filesystem::path> GameFolders();
// the window's flags and size (0 for the display's): the phones' window is the screen, a Mac's can be a window
void WindowSetup(SDL_WindowFlags& flags, int& width, int& height);
// the log line for no game found, once; the place to look and what the player does about it
void LogGameMissing();
// the device, CPU and RAM lines of the log's header
void LogDevice();
// the Vulkan surface of the window, from any thread: macOS makes the window's Metal view on the main thread only, where
// a surface made on another shows nothing (black, no error anywhere)
bool CreateVulkanSurface(SDL_Window* window, VkInstance instance, VkSurfaceKHR* surface);
void DestroyVulkanSurface(VkInstance instance, VkSurfaceKHR surface);
// SDL may make the Vulkan surface now (Android hands the window over late and takes it away again)
bool SurfaceReady(SDL_Window* window);
// a file shipped with the app (the touch controls' skin), as a path SDL can open
std::filesystem::path AssetPath(const char* relative);
// the touch controls are for a touch screen: always on a phone, on a Mac only while a direct touch device is present
bool TouchControls();
// a finger event of this touch device (SDL_TouchID) is the player's: not a Mac trackpad's
bool AcceptsFinger(uint64_t touch_id);
// a controller the OS hands over without a device, on top of what SDL read of port 0 (Android's game services)
void MergeLooseController(input::Pad& pad);

}  // namespace platform

namespace host {
// the options' update check: a dialog says what GitHub has; false where the platform has none
bool UpdatesSupported();
void CheckForUpdates();
}  // namespace host
