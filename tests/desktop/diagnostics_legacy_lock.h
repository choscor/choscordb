#pragma once
#include <QString>
#include <memory>

namespace choscordb {
class DiagnosticsFileLock {
  public:
    explicit DiagnosticsFileLock(const QString& folder);
    ~DiagnosticsFileLock();
    DiagnosticsFileLock(const DiagnosticsFileLock&) = delete;
    DiagnosticsFileLock& operator=(const DiagnosticsFileLock&) = delete;
    bool lock(int timeoutMs);
    void unlock();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
}
