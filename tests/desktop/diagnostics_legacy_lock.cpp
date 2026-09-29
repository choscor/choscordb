#include "diagnostics_legacy_lock.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace choscordb {
struct DiagnosticsFileLock::State {
    QString path;
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
    OVERLAPPED overlap{};
#else
    int descriptor = -1;
#endif
    bool held = false;
};
DiagnosticsFileLock::DiagnosticsFileLock(const QString& folder)
    : state_(std::make_unique<State>()) {
    state_->path = QDir(folder).filePath(QStringLiteral(".io.lock"));
}
DiagnosticsFileLock::~DiagnosticsFileLock() {
    unlock();
}

bool DiagnosticsFileLock::lock(int timeoutMs) {
    if (state_->held)
        return true;
    if (QFileInfo(state_->path).isSymLink())
        return false;
#if defined(_WIN32)
    const auto* wide = reinterpret_cast<const wchar_t*>(state_->path.utf16());
    const DWORD attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
    state_->handle =
        CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (state_->handle == INVALID_HANDLE_VALUE)
        return false;
#else
    const auto native = QFile::encodeName(state_->path);
    int flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    state_->descriptor = ::open(native.constData(), flags, 0600);
    if (state_->descriptor < 0)
        return false;
    struct stat info{};
    if (fstat(state_->descriptor, &info) != 0 || !S_ISREG(info.st_mode)) {
        unlock();
        return false;
    }
#endif
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
    for (;;) {
#if defined(_WIN32)
        if (LockFileEx(state_->handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                       &state_->overlap)) {
            state_->held = true;
            LARGE_INTEGER zero{};
            if (!SetFilePointerEx(state_->handle, zero, nullptr, FILE_BEGIN) ||
                !SetEndOfFile(state_->handle)) {
                unlock();
                return false;
            }
            return true;
        }
        if (GetLastError() != ERROR_LOCK_VIOLATION)
            break;
#else
        if (flock(state_->descriptor, LOCK_EX | LOCK_NB) == 0) {
            state_->held = true;
            if (ftruncate(state_->descriptor, 0) != 0 ||
                fchmod(state_->descriptor, S_IRUSR | S_IWUSR) != 0) {
                unlock();
                return false;
            }
            return true;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            break;
#endif
        if (std::chrono::steady_clock::now() >= deadline)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    unlock();
    return false;
}

void DiagnosticsFileLock::unlock() {
#if defined(_WIN32)
    if (state_->handle == INVALID_HANDLE_VALUE)
        return;
    if (state_->held)
        UnlockFileEx(state_->handle, 0, 1, 0, &state_->overlap);
    CloseHandle(state_->handle);
    state_->handle = INVALID_HANDLE_VALUE;
#else
    if (state_->descriptor < 0)
        return;
    if (state_->held)
        flock(state_->descriptor, LOCK_UN);
    ::close(state_->descriptor);
    state_->descriptor = -1;
#endif
    state_->held = false;
}
} // namespace choscordb
