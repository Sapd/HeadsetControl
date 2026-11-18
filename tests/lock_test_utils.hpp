#pragma once

#include <memory>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace headsetcontrol::testing {

/**
 * Put an object of another kind where ProcessLock(name) would put its lock,
 * so that the lock is Unavailable while the returned handle lives: an event
 * of the same name on Windows, a directory at the lock path elsewhere.
 */
inline std::shared_ptr<void> blockLockName(const std::string& name)
{
#ifdef _WIN32
    std::wstring wide = L"Global\\HeadsetControl-";
    for (const char c : name) {
        wide.push_back(static_cast<wchar_t>(c));
    }
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, wide.c_str());
    return std::shared_ptr<void>(event, [](void* h) {
        if (h) {
            CloseHandle(h);
        }
    });
#else
    auto path = std::make_shared<std::string>("/tmp/headsetcontrol-" + name + ".lock");
    mkdir(path->c_str(), 0700);
    return std::shared_ptr<void>(path.get(), [path](void*) { rmdir(path->c_str()); });
#endif
}

/// Remove the file a ProcessLock left behind (POSIX; Windows has none)
inline void removeLockFile([[maybe_unused]] const std::string& name)
{
#ifndef _WIN32
    unlink(("/tmp/headsetcontrol-" + name + ".lock").c_str());
#endif
}

} // namespace headsetcontrol::testing
