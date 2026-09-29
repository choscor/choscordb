#include "app/diagnostics_service.h"
#include <QMetaObject>
#include <QObject>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace choscordb {
struct DiagnosticsWatchdog::State {
    QObject* mainContext;
    DiagnosticsService* service;
    std::atomic_bool running = false;
    std::atomic_bool beatPending = false;
    std::mutex beatMutex;
    std::chrono::steady_clock::time_point lastAck;
    std::optional<std::chrono::steady_clock::time_point> hangStart;
    std::thread worker;
};
DiagnosticsWatchdog::DiagnosticsWatchdog(QObject* mainContext, DiagnosticsService* service)
    : state_(std::make_shared<State>(mainContext, service)) {}
DiagnosticsWatchdog::~DiagnosticsWatchdog() {
    stop();
}
void DiagnosticsWatchdog::start() {
    if (!state_->mainContext || !state_->service || state_->running.exchange(true))
        return;
    {
        std::lock_guard lock(state_->beatMutex);
        state_->lastAck = std::chrono::steady_clock::now();
        state_->hangStart.reset();
    }
    state_->beatPending = false;
    const auto state = state_;
    try {
        state_->worker = std::thread([state] {
            using namespace std::chrono_literals;
            while (state->running) {
                std::this_thread::sleep_for(250ms);
                if (!state->running)
                    break;
                if (!state->beatPending.exchange(true)) {
                    if (!QMetaObject::invokeMethod(
                            state->mainContext,
                            [state] {
                                if (!state->running)
                                    return;
                                const auto now = std::chrono::steady_clock::now();
                                std::lock_guard lock(state->beatMutex);
                                if (state->hangStart) {
                                    const auto duration =
                                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                            now - *state->hangStart)
                                            .count();
                                    state->service->record(
                                        {.event = DiagnosticEvent::UiHangEnd,
                                         .durationMs = int(
                                             std::clamp<decltype(duration)>(duration, 0, 600000))});
                                    state->hangStart.reset();
                                }
                                state->lastAck = now;
                                state->beatPending = false;
                            },
                            Qt::QueuedConnection))
                        state->beatPending = false;
                }
                std::lock_guard lock(state->beatMutex);
                const auto now = std::chrono::steady_clock::now();
                if (!state->hangStart && now - state->lastAck >= 2s) {
                    state->hangStart = state->lastAck;
                    state->service->record({.event = DiagnosticEvent::UiHangStart});
                }
            }
        });
    } catch (...) {
        state_->running = false;
    }
}
void DiagnosticsWatchdog::stop() {
    if (!state_->running.exchange(false))
        return;
    if (state_->worker.joinable())
        state_->worker.join();
}
} // namespace choscordb
