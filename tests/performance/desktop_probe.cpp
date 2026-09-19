#include "app/main_window.h"
#include "app/query_settings.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "process_memory.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPushButton>
#include <QScreen>
#include <QSysInfo>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

class MeasuredApplication : public QApplication {
  public:
    using QApplication::QApplication;
    bool notify(QObject* receiver, QEvent* event) override {
        if (QThread::currentThread() != thread())
            return QApplication::notify(receiver, event);
        const bool outer = depth_++ == 0;
        const bool readyAtDispatch = workspaceReady;
        QElapsedTimer timer;
        if (outer)
            timer.start();
        const auto type = event->type();
        const auto receiverClass = receiver->metaObject()->className();
        const bool result = QApplication::notify(receiver, event);
        --depth_;
        if (outer) {
            const double ms = timer.nsecsElapsed() / 1e6;
            if (ms > maximumDispatchMs) {
                maximumDispatchMs = ms;
                maximumDispatchType = int(type);
                maximumDispatchClass = QString::fromLatin1(receiverClass);
            }
            ++dispatchCount;
            if (ms > 16)
                ++dispatchOver16;
            if (readyAtDispatch) {
                if (ms > steadyMaximumDispatchMs) {
                    steadyMaximumDispatchMs = ms;
                    steadyMaximumDispatchType = int(type);
                    steadyMaximumDispatchClass = QString::fromLatin1(receiverClass);
                    steadyMaximumDispatchPhase = measurementPhase;
                }
                ++steadyDispatchCount;
                if (ms > 16)
                    ++steadyDispatchOver16;
                if (measurementPhase != "shutdown") {
                    if (ms > interactiveMaximumDispatchMs) {
                        interactiveMaximumDispatchMs = ms;
                        interactiveMaximumDispatchType = int(type);
                        interactiveMaximumDispatchClass = QString::fromLatin1(receiverClass);
                        interactiveMaximumDispatchPhase = measurementPhase;
                    }
                    ++interactiveDispatchCount;
                    if (ms > 16)
                        ++interactiveDispatchOver16;
                }
            }
        }
        if (afterEvent)
            afterEvent(receiver, type);
        return result;
    }
    std::function<void(QObject*, QEvent::Type)> afterEvent;
    double maximumDispatchMs = 0;
    int maximumDispatchType = 0;
    QString maximumDispatchClass;
    quint64 dispatchCount = 0, dispatchOver16 = 0;
    bool workspaceReady = false;
    QString measurementPhase;
    double steadyMaximumDispatchMs = 0;
    int steadyMaximumDispatchType = 0;
    QString steadyMaximumDispatchClass, steadyMaximumDispatchPhase;
    quint64 steadyDispatchCount = 0, steadyDispatchOver16 = 0;
    double interactiveMaximumDispatchMs = 0;
    int interactiveMaximumDispatchType = 0;
    QString interactiveMaximumDispatchClass, interactiveMaximumDispatchPhase;
    quint64 interactiveDispatchCount = 0, interactiveDispatchOver16 = 0;

  private:
    int depth_ = 0;
};

class Probe : public QObject {
  public:
    Probe(MeasuredApplication& app, QElapsedTimer& clock, const QString& path,
          const QJsonObject& startupMemory)
        : app_(app), clock_(clock), path_(path),
          window_(nullptr, directory_.filePath("metadata.sqlite")) {
        report_["startup_memory"] = startupMemory;
        const auto constructedMemory = currentResidentBytes();
        report_["constructed_window_rss_bytes"] = constructedMemory
                                                      ? QJsonValue(double(*constructedMemory))
                                                      : QJsonValue(QJsonValue::Null);
        if (!constructedMemory)
            QTimer::singleShot(
                0, this, [this] { finish("Current resident memory sampling is unavailable"); });
        if (const auto footprint = currentPhysicalFootprintBytes())
            report_["constructed_window_physical_footprint_bytes"] = double(*footprint);
        workspace_ = window_.findChild<choscordb::QueryWorkspace*>();
        grid_ = window_.findChild<QTableView*>("queryResults");
        tabs_ = window_.findChild<QTabWidget*>("editorTabs");
        run_ = window_.findChild<QAction*>("runStatement");
        next_ = window_.findChild<QPushButton*>("nextPage");
        app_.setQuitOnLastWindowClosed(false);
        app_.afterEvent = [this](QObject* receiver, QEvent::Type type) {
            if (receiver == &window_ && type == QEvent::Paint && firstPaint_ < 0)
                firstPaint_ = now();
            if (editor_ && receiver == editor_->viewport() && type == QEvent::Paint &&
                keyPending_ && textChanged_) {
                latencies_.append(now() - keyStart_);
                keyPending_ = false;
                QTimer::singleShot(0, this, [this] { typeNext(); });
            }
        };
        connect(&app_, &QApplication::lastWindowClosed, this, [this] {
            if (closing_) {
                report_["close_request_to_window_closed_ms"] = now() - closeStart_;
                // Let the closing dispatch finish before serializing its timing.
                QTimer::singleShot(0, this, [this] { finish({}); });
            }
        });
        connect(
            workspace_->adapter(), &choscordb::EngineAdapter::eventReady, this,
            [this](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "query_state" &&
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size())) ==
                        "queued")
                    ++executions_;
                if (kind == "query_failed") {
                    const auto errorKind = QString::fromUtf8(event.error_kind.data(),
                                                             qsizetype(event.error_kind.size()));
                    if (closing_ && errorKind == "Disconnected")
                        return;
                    finish("Query failed during measurement");
                } else if (kind == "stored_page" && browsing_) {
                    query_ = event.id;
                    const auto index = event.page_index;
                    const auto rows = event.row_count;
                    QTimer::singleShot(0, this, [this, index, rows] { page(index, rows); });
                }
            },
            Qt::DirectConnection);
        auto* recovery = window_.findChild<choscordb::WorkspaceRecoveryController*>();
        auto* settings = window_.findChild<choscordb::QuerySettingsController*>();
        connect(recovery, &choscordb::WorkspaceRecoveryController::mutationEnabled, this,
                [this](bool) { checkReady(); });
        connect(settings, &choscordb::QuerySettingsController::readyChanged, this,
                [this](bool) { checkReady(); });
        connect(run_, &QAction::changed, this, [this] {
            if (waitingConnection_)
                waitForConnection();
        });
        QTimer::singleShot(0, this, [this] { checkReady(); });
        QTimer::singleShot(240000, this, [this] { finish("Measurement deadline exceeded"); });
        // Keep the benchmark viewport reproducible despite asynchronous layout
        // restoration and the native window manager's initial placement limits.
        window_.setFixedSize(1280, 800);
        window_.show();
    }
    ~Probe() override { app_.afterEvent = {}; }

  private:
    double now() const { return clock_.nsecsElapsed() / 1e6; }
    bool rss(const QString& key) {
        const auto value = currentResidentBytes();
        report_[key] = value ? QJsonValue(double(*value)) : QJsonValue(QJsonValue::Null);
        if (!value) {
            finish("Current resident memory sampling is unavailable");
            return false;
        }
        const auto footprint = currentPhysicalFootprintBytes();
#ifdef Q_OS_MACOS
        if (!footprint) {
            finish("Current physical-footprint sampling is unavailable on macOS");
            return false;
        }
#endif
        if (footprint) {
            auto footprintKey = key;
            footprintKey.replace("_rss_bytes", "_physical_footprint_bytes");
            report_[footprintKey] = double(*footprint);
        }
        return true;
    }
    void checkReady() {
        if (ready_ || finished_)
            return;
        auto* recovery = window_.findChild<choscordb::WorkspaceRecoveryController*>();
        auto* settings = window_.findChild<choscordb::QuerySettingsController*>();
        if (!recovery || !recovery->isReady() || !settings || !settings->isReady() ||
            !tabs_->isEnabled())
            return;
        ready_ = true;
        app_.workspaceReady = true;
        app_.measurementPhase = "ready-idle";
        report_["workspace_ready_ms"] = now();
        std::puts("{\"event\":\"ready\"}");
        std::fflush(stdout);
        QTimer::singleShot(1000, this, [this] {
            if (!rss("idle_rss_bytes"))
                return;
            // The MVP starts on Start; measure connection/input on the same
            // visible SQL workspace used by the original probe.
            if (!window_.showScreen(choscordb::MainWindow::Screen::Sql)) {
                finish("Could not open SQL workspace for measurement");
                return;
            }
            waitingConnection_ = true;
            app_.measurementPhase = "connecting";
            workspace_->connectSqlite(":memory:");
            if (waitingConnection_)
                waitForConnection();
        });
    }
    void waitForConnection() {
        if (!run_->isEnabled() || !waitingConnection_)
            return;
        waitingConnection_ = false;
        QTimer::singleShot(1000, this, [this] {
            if (!rss("connected_sqlite_rss_bytes"))
                return;
            editor_ = qobject_cast<choscordb::SqlEditor*>(tabs_->currentWidget());
            app_.measurementPhase = "editor-input";
            editor_->setText("-- input latency sample\n");
            editor_->SendScintilla(QsciScintilla::SCI_GOTOPOS,
                                   static_cast<unsigned long>(editor_->text().toUtf8().size()));
            editor_->setFocus();
            connect(editor_, &choscordb::SqlEditor::textChanged, this, [this] {
                if (keyPending_)
                    textChanged_ = true;
            });
            report_["editor_initial_document_bytes"] = editor_->text().toUtf8().size();
            QTimer::singleShot(100, this, [this] { typeNext(); });
        });
    }
    void typeNext() {
        if (finished_)
            return;
        if (latencies_.size() == 200) {
            auto sorted = latencies_;
            std::sort(sorted.begin(), sorted.end());
            report_["editor_key_to_paint_p95_ms"] =
                sorted.at(std::size_t(std::ceil(sorted.size() * 0.95)) - 1);
            report_["editor_input_samples"] = sorted.size();
            report_["editor_final_document_bytes"] = editor_->text().toUtf8().size();
            editor_->setText("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE "
                             "x<1000000) SELECT x, printf('%080d',x) FROM n");
            editor_->SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
            browsing_ = true;
            app_.measurementPhase = "million-row-browse";
            browseStart_ = now();
            run_->trigger();
            return;
        }
        keyPending_ = true;
        textChanged_ = false;
        keyStart_ = now();
        QCoreApplication::postEvent(
            editor_, new QKeyEvent(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x"));
        QCoreApplication::postEvent(
            editor_, new QKeyEvent(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier, "x"));
    }
    void page(quint64 index, quint32 rows) {
        if (finished_)
            return;
        const quint64 expected = revisiting_ ? 0 : pages_;
        if (index != expected || rows != 1000 || grid_->model()->rowCount() != 1000 ||
            grid_->model()->data(grid_->model()->index(0, 0)).toLongLong() !=
                qint64(index * 1000 + 1) ||
            grid_->model()->data(grid_->model()->index(999, 0)).toLongLong() !=
                qint64((index + 1) * 1000)) {
            finish("Grid did not contain the expected contiguous page");
            return;
        }
        if (const auto resident = currentResidentBytes()) {
            rssPeak_ = std::max(rssPeak_, *resident);
            ++rssSamples_;
        } else {
            finish("Current resident memory sampling failed while browsing");
            return;
        }
        const auto footprint = currentPhysicalFootprintBytes();
#ifdef Q_OS_MACOS
        if (!footprint) {
            finish("Physical-footprint sampling failed while browsing");
            return;
        }
#endif
        if (footprint)
            footprintPeak_ = std::max(footprintPeak_, *footprint);
        const auto memory = workspace_->adapter()->memoryUsage();
        reservationPeak_ = std::max(reservationPeak_, memory.peak);
        cachePeak_ = std::max(cachePeak_, workspace_->adapter()->cacheUsage().residentBytes);
        if (revisiting_) {
            if (workspace_->adapter()->cacheUsage().misses <= missesBefore_) {
                finish("Archived reread did not verify a cache miss after eviction");
                return;
            }
            report_["archived_first_page_verified"] = true;
            report_["archived_reread_cache_miss"] =
                workspace_->adapter()->cacheUsage().misses > missesBefore_;
            report_["million_row_browse_ms"] = now() - browseStart_;
            report_["traversed_rows"] = double(pages_ * 1000);
            report_["query_submissions"] = executions_;
            report_["sampled_browse_peak_rss_bytes"] =
                rssSamples_ ? QJsonValue(double(rssPeak_)) : QJsonValue(QJsonValue::Null);
            report_["sampled_browse_peak_physical_footprint_bytes"] =
                footprintPeak_ ? QJsonValue(double(footprintPeak_)) : QJsonValue(QJsonValue::Null);
            report_["browse_rss_samples"] = double(rssSamples_);
            report_["core_reservation_peak_bytes"] = double(reservationPeak_);
            report_["hot_cache_sampled_peak_bytes"] = double(cachePeak_);
            if (executions_ != 1) {
                finish("Browsing submitted SQL more than once");
                return;
            }
            browsing_ = false;
            closing_ = true;
            app_.measurementPhase = "shutdown";
            closeStart_ = now();
            std::puts("{\"event\":\"shutdown_started\"}");
            std::fflush(stdout);
            window_.close();
        } else if (++pages_ == 1000) {
            revisiting_ = true;
            missesBefore_ = workspace_->adapter()->cacheUsage().misses;
            workspace_->adapter()->fetchPageAt(query_, 0);
        } else {
            if (!next_->isEnabled()) {
                finish("Next page unexpectedly disabled");
                return;
            }
            next_->click();
        }
    }
    void finish(const QString& error) {
        if (finished_)
            return;
        finished_ = true;
        app_.afterEvent = {};
        report_["status"] = error.isEmpty() ? "measured" : "failed";
        if (!error.isEmpty())
            report_["error"] = error;
        report_["first_window_paint_ms"] =
            firstPaint_ >= 0 ? QJsonValue(firstPaint_) : QJsonValue(QJsonValue::Null);
        report_["platform"] = QSysInfo::prettyProductName();
        report_["cpu_architecture"] = QSysInfo::currentCpuArchitecture();
        report_["qt_version"] = qVersion();
        report_["rendering_backend"] = QGuiApplication::platformName();
        report_["build_mode"] = "Release";
        const auto geometry = [](const QRect& rect) {
            return QJsonObject{{"x", rect.x()},
                               {"y", rect.y()},
                               {"width", rect.width()},
                               {"height", rect.height()}};
        };
        QJsonObject display{{"window_geometry", geometry(window_.geometry())},
                            {"window_device_pixel_ratio", window_.devicePixelRatioF()}};
        if (const auto* screen = window_.screen()) {
            display["screen_geometry"] = geometry(screen->geometry());
            display["screen_device_pixel_ratio"] = screen->devicePixelRatio();
            display["screen_logical_dpi"] = screen->logicalDotsPerInch();
        }
        if (editor_) {
            display["editor_viewport_geometry"] = geometry(editor_->viewport()->geometry());
            display["editor_device_pixel_ratio"] = editor_->viewport()->devicePixelRatioF();
        }
        if (grid_) {
            display["grid_viewport_geometry"] = geometry(grid_->viewport()->geometry());
            display["grid_device_pixel_ratio"] = grid_->viewport()->devicePixelRatioF();
        }
        report_["display"] = display;

        report_["outer_notify_max_ms"] = app_.maximumDispatchMs;
        report_["outer_notify_max_event_type"] = app_.maximumDispatchType;
        report_["outer_notify_max_receiver_class"] = app_.maximumDispatchClass;
        report_["outer_notify_count"] = double(app_.dispatchCount);
        report_["outer_notify_over_16ms"] = double(app_.dispatchOver16);
        report_["ready_workspace_notify_max_ms"] = app_.steadyMaximumDispatchMs;
        report_["ready_workspace_notify_max_event_type"] = app_.steadyMaximumDispatchType;
        report_["ready_workspace_notify_max_receiver_class"] = app_.steadyMaximumDispatchClass;
        report_["ready_workspace_notify_max_phase"] = app_.steadyMaximumDispatchPhase;
        report_["ready_workspace_notify_count"] = double(app_.steadyDispatchCount);
        report_["ready_workspace_notify_over_16ms"] = double(app_.steadyDispatchOver16);
        report_["interactive_notify_max_ms"] = app_.interactiveMaximumDispatchMs;
        report_["interactive_notify_max_event_type"] = app_.interactiveMaximumDispatchType;
        report_["interactive_notify_max_receiver_class"] = app_.interactiveMaximumDispatchClass;
        report_["interactive_notify_max_phase"] = app_.interactiveMaximumDispatchPhase;
        report_["interactive_notify_count"] = double(app_.interactiveDispatchCount);
        report_["interactive_notify_over_16ms"] = double(app_.interactiveDispatchOver16);
        report_["limitations"] = QJsonArray{
            "Query submissions count core queued events; driver SQL replay is covered by separate "
            "write-side-effect regressions, not observed by this probe.",
            "In-process startup begins before QApplication; excludes process creation and loader "
            "time. Fresh profile is not cold OS cache.",
            "Paint completion measures Qt viewport handling, not compositor presentation; "
            "offscreen/minimal rendering cannot verify displayed latency.",
            "Current RSS samples can miss transient peaks. SQLite session only; PostgreSQL is "
            "unmeasured.",
            "Notify durations include harness timer work and synchronous nested work in outer "
            "dispatch; nested dispatches "
            "are not separate samples. Measurement callbacks are excluded.",
            "Shutdown ends at lastWindowClosed; external launcher must measure process exit. CPU "
            "model and reference-hardware qualification require external metadata.",
            "Single run is evidence only, not full release acceptance; repeat runs and "
            "supported-platform measurements remain required."};
        QFile output(path_);
        const bool saved = output.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                           output.write(QJsonDocument(report_).toJson()) > 0;
        output.close();
        app_.exit(error.isEmpty() && saved ? 0 : 1);
    }
    MeasuredApplication& app_;
    QElapsedTimer& clock_;
    QString path_;
    QTemporaryDir directory_;
    choscordb::MainWindow window_;
    choscordb::QueryWorkspace* workspace_ = nullptr;
    choscordb::SqlEditor* editor_ = nullptr;
    QTableView* grid_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QAction* run_ = nullptr;
    QPushButton* next_ = nullptr;
    QJsonObject report_;
    QList<double> latencies_;
    double firstPaint_ = -1, keyStart_ = 0, browseStart_ = 0, closeStart_ = 0;
    quint64 query_ = 0, pages_ = 0, missesBefore_ = 0, reservationPeak_ = 0, cachePeak_ = 0;
    std::uint64_t rssPeak_ = 0, footprintPeak_ = 0, rssSamples_ = 0;
    int executions_ = 0;
    bool keyPending_ = false, textChanged_ = false, browsing_ = false, revisiting_ = false;
    bool closing_ = false, finished_ = false, ready_ = false, waitingConnection_ = false;
};

int main(int argc, char** argv) {
    QElapsedTimer clock;
    clock.start();
    const auto beforeApplication = currentResidentBytes();
    const auto beforeApplicationFootprint = currentPhysicalFootprintBytes();
    MeasuredApplication app(argc, argv);
    const auto afterApplication = currentResidentBytes();
    const auto afterApplicationFootprint = currentPhysicalFootprintBytes();
    app.setApplicationName("ChoscorDB Performance Probe");
    app.setOrganizationName("ChoscorDB");
    if (argc != 2)
        return 2;
#ifndef NDEBUG
    QFile report(QString::fromLocal8Bit(argv[1]));
    if (report.open(QIODevice::WriteOnly))
        report.write("{\"status\":\"unverified\",\"build_mode\":\"Debug\",\"error\":\"Release "
                     "build required\"}\n");
    return 2;
#else
    const auto memoryValue = [](auto value) {
        return value ? QJsonValue(double(*value)) : QJsonValue(QJsonValue::Null);
    };
    Probe probe(
        app, clock, QString::fromLocal8Bit(argv[1]),
        {{"before_qapplication_rss_bytes", memoryValue(beforeApplication)},
         {"after_qapplication_rss_bytes", memoryValue(afterApplication)},
         {"before_qapplication_physical_footprint_bytes", memoryValue(beforeApplicationFootprint)},
         {"after_qapplication_physical_footprint_bytes", memoryValue(afterApplicationFootprint)}});
    return app.exec();
#endif
}
