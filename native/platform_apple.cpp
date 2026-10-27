// The Apple hosts' platform part (macOS for building and testing, iOS): storage, the folders the game is looked for in,
// the system's log. BLACK_DATA_DIR and BLACK_GAME_DIR point a development build at a folder of its own.
#include "platform.h"

#include <TargetConditionals.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <string>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

#if TARGET_OS_IPHONE
#include <os/log.h>

#include <thread>
#endif

namespace fs = std::filesystem;

namespace platform {

namespace {

std::string Sysctl(const char* name) {
    char v[256] = {};
    size_t n = sizeof(v) - 1;
    if (sysctlbyname(name, v, &n, nullptr, 0)) return "?";
    return v;
}

fs::path Env(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? fs::path(v) : fs::path();
}

#if TARGET_OS_IPHONE
fs::path Documents() {
    const char* d = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    return d ? fs::path(d) : fs::path();
}
#endif

}  // namespace

// iOS shows no stderr: the log's lines go to os_log, read with the Console or devicectl. On the Mac stderr is the terminal
void RedirectLog() {
#if TARGET_OS_IPHONE
    int fds[2];
    if (pipe(fds)) return;
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    std::thread([fd = fds[0]] {
        os_log_t log = os_log_create("com.muhammad.black", "game");
        char buf[1024];
        std::string line;
        for (ssize_t n; (n = read(fd, buf, sizeof(buf))) > 0;) {
            line.append(buf, size_t(n));
            for (size_t nl; (nl = line.find('\n')) != std::string::npos; line.erase(0, nl + 1))
                os_log(log, "%{public}s", line.substr(0, nl).c_str());
        }
    }).detach();
#endif
}

void SetHints() {}

fs::path DataDir() {
    if (fs::path p = Env("BLACK_DATA_DIR"); !p.empty()) {
        std::error_code ec;
        fs::create_directories(p, ec);
        return p;
    }
    // Application Support, in the app's sandbox on iOS
    const char* p = SDL_GetPrefPath("com.muhammad", "Black");
    return fs::path(p ? p : ".");
}

std::vector<fs::path> GameFolders() {
    std::vector<fs::path> out;
    if (fs::path p = Env("BLACK_GAME_DIR"); !p.empty()) {
        out.push_back(p);
        out.push_back(p / "assets");
    }
#if TARGET_OS_IPHONE
    // Documents/Black in the Files app (UIFileSharingEnabled)
    if (fs::path d = Documents(); !d.empty()) {
        out.push_back(d / "Black");
        out.push_back(d);
    }
#else
    // a checkout's own folders
    out.push_back(fs::current_path() / "assets");
    out.push_back(fs::current_path() / "iso");
#endif
    out.push_back(app::ExeDir() / "assets");
    return out;
}

void LogGameMissing() {
    std::string where;
    for (const fs::path& p : GameFolders()) where += (where.empty() ? "" : ", ") + p.string();
    LOGE("game: no game files in %s, waiting for them (default.xbe of the USA release, or its .iso)", where.c_str());
}

void LogDevice() {
#if TARGET_OS_IPHONE
    LOGR("Device: %s, iOS %s", Sysctl("hw.machine").c_str(), Sysctl("kern.osproductversion").c_str());
#else
    LOGR("Device: %s, macOS %s", Sysctl("hw.model").c_str(), Sysctl("kern.osproductversion").c_str());
#endif
    LOGR("CPU: %s (%d threads)", Sysctl("machdep.cpu.brand_string").c_str(), SDL_GetNumLogicalCPUCores());
}

bool SurfaceReady(SDL_Window* window) { return window != nullptr; }

// next to the executable, or in the bundle's resources
fs::path AssetPath(const char* relative) {
    const char* base = SDL_GetBasePath();
    return (base ? fs::path(base) : fs::path()) / relative;
}

void MergeLooseController(input::Pad&) {}

}  // namespace platform

namespace host {

// no release check outside Android's: the options hide the row
bool UpdatesSupported() { return false; }
void CheckForUpdates() {}

}  // namespace host
