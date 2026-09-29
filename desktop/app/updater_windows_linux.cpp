#include "app/application_data.h"
#include "app/main_window.h"
#include "app/update_metadata.h"
#include "app/updater.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/text/text.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFutureWatcher>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyle>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace choscordb {
namespace {
#ifdef Q_OS_WIN
constexpr auto platform = "windows";
constexpr auto architecture = "x64";
#else
constexpr auto platform = "linux";
constexpr auto architecture = "x86_64";
#endif

rust::Str utf8(const QByteArray& bytes) {
    return {bytes.constData(), size_t(bytes.size())};
}

rust::Slice<const uint8_t> bytes(const QByteArray& value) {
    return {reinterpret_cast<const uint8_t*>(value.constData()), size_t(value.size())};
}

QString text(const rust::String& value) {
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}

UpdateRecord recordFromDto(const UpdateRecordDto& dto) {
    return {
        text(dto.version), QUrl(text(dto.url)), qint64(dto.size),
        QByteArray(reinterpret_cast<const char*>(dto.sha256.data()), qsizetype(dto.sha256.size())),
        text(dto.notes)};
}

QByteArray updatePublicKey() {
    return QByteArray::fromBase64(QByteArray(CHOSCORDB_UPDATE_PUBLIC_KEY));
}

void report(QWidget* parent, const QString& title, const QString& detail) {
    ConfirmationDialog dialog(QMessageBox::Warning, title, detail, QMessageBox::Ok, parent);
    dialog.setTextFormat(Qt::PlainText);
    dialog.exec();
}

#ifdef Q_OS_LINUX
void offerManualUpdate(const QUrl& url, const QString& detail) {
    const auto answer = ConfirmationDialog::question(
        nullptr, QObject::tr("Manual update available"),
        detail + QObject::tr(" Open the verified release download page?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        QDesktopServices::openUrl(url);
}
#endif

class UpdateProgressDialog final : public DialogShell {
  public:
    explicit UpdateProgressDialog(QWidget* parent) : DialogShell(parent) {
        setObjectName(QStringLiteral("updateDownloadProgress"));
        setWindowTitle(tr("ChoscorDB update"));
        auto* layout = new QVBoxLayout(this);
        auto* title = new design::Text(tr("Updating ChoscorDB"), this);
        title->setTypographyRole(design::TypographyRole::DialogTitle);
        layout->addWidget(title);
        message_ = createDescription(tr("Downloading update…"), this);
        layout->addWidget(message_);
        progress_ = new QProgressBar(this);
        progress_->setObjectName(QStringLiteral("updateDownloadProgressBar"));
        progress_->setRange(0, 1000);
        progress_->setValue(0);
        progress_->setTextVisible(false);
        layout->addWidget(progress_);
        auto* cancel = new design::Button(tr("Cancel"), this);
        cancel->setObjectName(QStringLiteral("cancelUpdateDownload"));
        cancel->setVariant(design::ButtonVariant::Outline);
        layout->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        setAppModal();
    }
    void setProgress(int value) { progress_->setValue(value); }
    void setVerifying() {
        message_->setText(tr("Verifying downloaded update…"));
        progress_->setRange(0, 0);
    }

  private:
    QLabel* message_ = nullptr;
    QProgressBar* progress_ = nullptr;
};

class NativeUpdater final : public QObject {
  public:
    explicit NativeUpdater(MainWindow& window) : QObject(&window), window_(&window) {
        const auto feedBase = QByteArray(CHOSCORDB_UPDATE_BASE_URL);
        const auto repository = QByteArray(CHOSCORDB_UPDATE_REPOSITORY);
        const auto key = updatePublicKey();
        session_ = std::make_shared<rust::Box<RustUpdateSession>>(update_session_new(
            utf8(feedBase), bytes(key), platform, architecture, utf8(repository)));
        consentDirectory_ = applicationDataDirectory();
#ifdef CHOSCORDB_TEST_UPDATER_MENU
        const auto testDataDirectory = qEnvironmentVariable("CHOSCORDB_TEST_UPDATE_DATA_DIR");
        if (!testDataDirectory.isEmpty())
            consentDirectory_ = testDataDirectory;
#endif
        consentPool_.setMaxThreadCount(1);
#ifdef Q_OS_WIN
        QTimer::singleShot(0, this, [this] {
            auto* watcher = new QFutureWatcher<bool>(this);
            connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
                const bool failed = watcher->result();
                watcher->deleteLater();
                if (failed && window_)
                    report(window_, tr("Update installation failed"),
                           tr("The installer could not complete the upgrade. Your previous version "
                              "remains available. Please try again or install manually."));
            });
            watcher->setFuture(
                QtConcurrent::run([] { return update_take_windows_failure_marker(); }));
        });
#endif
        auto* menu = window.menuBar()->addMenu(tr("Updates"));
        auto* check = menu->addAction(tr("Check for Updates…"));
        check->setObjectName(QStringLiteral("checkForUpdates"));
        check->setMenuRole(QAction::ApplicationSpecificRole);
        check->setIcon(window.style()->standardIcon(QStyle::SP_BrowserReload));
        check->setIconVisibleInMenu(true);
        connect(check, &QAction::triggered, this, [this] { checkForUpdates(true); });
        installAction_ = menu->addAction(tr("Install Downloaded Update…"));
        installAction_->setObjectName(QStringLiteral("installDownloadedUpdate"));
        installAction_->setMenuRole(QAction::ApplicationSpecificRole);
        installAction_->setEnabled(false);
        connect(installAction_, &QAction::triggered, this, [this] { promptToInstall(); });
        auto* automatic = menu->addAction(tr("Check Automatically"));
        automaticAction_ = automatic;
        automatic->setObjectName(QStringLiteral("automaticUpdateChecks"));
        automatic->setCheckable(true);
        automatic->setEnabled(false);
        connect(automatic, &QAction::toggled, this, [this](bool enabled) {
            persistConsent(enabled);
            if (enabled) {
                timer_.start();
                checkForUpdates(false);
            } else {
                timer_.stop();
            }
        });
        timer_.setInterval(24 * 60 * 60 * 1000);
        connect(&timer_, &QTimer::timeout, this, [this] { checkForUpdates(false); });
        progressTimer_.setInterval(50);
        connect(&progressTimer_, &QTimer::timeout, this, [this] {
            if (!progress_)
                return;
            const auto progress = update_session_progress(**session_);
            if (progress.verifying) {
                if (!showingVerification_) {
                    showingVerification_ = true;
                    progress_->setVerifying();
                }
            } else if (progress.total > 0 && !showingVerification_) {
                progress_->setProgress(
                    int(std::min<uint64_t>(999, progress.received * 1000 / progress.total)));
            }
        });
        loadConsent();
    }

    ~NativeUpdater() override {
        auto session = std::move(session_);
        update_session_cancel(**session);
        // A staged TempPath can unlink a file when the session is dropped.
        (void)QtConcurrent::run(&consentPool_, [session = std::move(session)] { (void)session; });
    }

#ifdef CHOSCORDB_TEST_UPDATER_MENU
    void markDownloadedReadyForTest() {
        readyToInstall_ = true;
        installAction_->setEnabled(true);
    }
#endif

  private:
    void loadConsent() {
        const auto directory = consentDirectory_;
        auto* watcher = new QFutureWatcher<UpdateConsentDto>(this);
        connect(watcher, &QFutureWatcher<UpdateConsentDto>::finished, this, [this, watcher] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (!window_ || !automaticAction_)
                return;
            automaticAction_->setEnabled(true);
            if (!result.error.empty())
                report(window_, tr("Automatic update checks"), text(result.error));
            if (result.has_value) {
                persistedConsent_ = result.value;
                const QSignalBlocker blocker(automaticAction_);
                automaticAction_->setChecked(result.value);
                if (result.value) {
                    timer_.start();
                    QTimer::singleShot(5000, this, [this] { checkForUpdates(false); });
                }
                return;
            }
            QTimer::singleShot(0, this, [this] {
                if (!window_ || !automaticAction_)
                    return;
                const auto answer = ConfirmationDialog::question(
                    window_, tr("Automatic update checks"),
                    tr("Check for stable ChoscorDB updates in the background? You can change this "
                       "in the Updates menu."),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                const bool enabled = answer == QMessageBox::Yes;
                if (automaticAction_->isChecked() != enabled)
                    automaticAction_->setChecked(enabled);
                else
                    persistConsent(enabled);
            });
        });
        watcher->setFuture(QtConcurrent::run(&consentPool_, [directory] {
            const auto path = directory.toUtf8();
#ifdef CHOSCORDB_TEST_UPDATER_MENU
            const auto legacyIni =
                qEnvironmentVariable("CHOSCORDB_TEST_UPDATE_LEGACY_INI").toUtf8();
            if (!legacyIni.isEmpty()) {
                rust::Vec<rust::String> legacyPaths;
                legacyPaths.push_back(
                    rust::String(legacyIni.constData(), size_t(legacyIni.size())));
                return update_consent_load_legacy_ini(utf8(path), std::move(legacyPaths));
            }
#endif
            return update_consent_load_native(utf8(path));
        }));
    }

    void persistConsent(bool enabled) {
        const auto generation = ++consentGeneration_;
        const auto directory = consentDirectory_;
        auto* watcher = new QFutureWatcher<rust::String>(this);
        connect(watcher, &QFutureWatcher<rust::String>::finished, this,
                [this, watcher, generation, enabled] {
                    const auto error = watcher->result();
                    watcher->deleteLater();
                    if (error.empty()) {
                        if (generation > persistedConsentGeneration_) {
                            persistedConsentGeneration_ = generation;
                            persistedConsent_ = enabled;
                        }
                    } else if (generation == consentGeneration_ && automaticAction_) {
                        const QSignalBlocker blocker(automaticAction_);
                        automaticAction_->setChecked(persistedConsent_);
                        if (persistedConsent_)
                            timer_.start();
                        else
                            timer_.stop();
                        if (window_)
                            report(window_, tr("Automatic update checks"), text(error));
                    }
                });
        watcher->setFuture(QtConcurrent::run(&consentPool_, [directory, enabled] {
            const auto path = directory.toUtf8();
            return update_consent_save(utf8(path), enabled);
        }));
    }

    void promptToInstall() {
        if (!readyToInstall_ || !window_)
            return;
        const auto message = record_ ? tr("Install ChoscorDB %1 and restart now? Open work will be "
                                          "saved or confirmed before installation.")
                                           .arg(record_->version)
                                     : tr("Install the downloaded update and restart now? Open "
                                          "work will be saved or confirmed before installation.");
        ConfirmationDialog prompt(QMessageBox::Question, tr("Install update"), message,
                                  QMessageBox::Yes | QMessageBox::No, window_);
        prompt.setTextFormat(Qt::PlainText);
        if (prompt.exec() == QMessageBox::Yes)
            requestInstall();
    }

    void checkForUpdates(bool manual) {
        if (checking_ || downloading_ || installing_)
            return;
        manual_ = manual;
        checking_ = true;
#ifdef CHOSCORDB_TEST_UPDATER_MENU
        const auto testUrl = qEnvironmentVariable("CHOSCORDB_TEST_UPDATE_FEED_URL");
        if (!testUrl.isEmpty()) {
            const auto url = testUrl.toUtf8();
            session_ = std::make_shared<rust::Box<RustUpdateSession>>(
                update_session_new_failure_fixture(utf8(url)));
        }
#endif
        const auto session = session_;
        update_session_begin_check(**session);
        const auto baseline =
            readyToInstall_ && record_ ? record_->version : QCoreApplication::applicationVersion();
        auto* watcher = new QFutureWatcher<UpdateCheckDto>(this);
        connect(watcher, &QFutureWatcher<UpdateCheckDto>::finished, this, [this, watcher] {
            checking_ = false;
            const auto result = watcher->result();
            watcher->deleteLater();
            if (!window_)
                return;
            if (!result.error.empty()) {
                if (manual_)
                    report(window_, tr("Update check failed"), text(result.error));
                return;
            }
            if (!result.found) {
                if (manual_) {
                    if (readyToInstall_ && record_)
                        report(window_, tr("Downloaded update is ready"),
                               tr("ChoscorDB %1 is downloaded. Choose Install Downloaded "
                                  "Update from the Updates menu when you are ready.")
                                   .arg(record_->version));
                    else
                        report(window_, tr("ChoscorDB is up to date"),
                               tr("You already have the latest stable version."));
                }
                return;
            }
            const auto offered = recordFromDto(result.record);
            ConfirmationDialog offer(
                QMessageBox::Information, tr("ChoscorDB %1 is available").arg(offered.version),
                tr("Release notes:\n%1\n\nDownload this update?").arg(offered.notes),
                QMessageBox::Yes | QMessageBox::No, window_);
            offer.setTextFormat(Qt::PlainText);
            if (offer.exec() == QMessageBox::Yes) {
                if (readyToInstall_) {
                    readyToInstall_ = false;
                    installAction_->setEnabled(false);
                }
                record_ = offered;
                download();
            }
        });
        watcher->setFuture(QtConcurrent::run([session, baseline] {
            const auto version = baseline.toUtf8();
            return update_session_check(**session, utf8(version));
        }));
    }

    void download() {
        if (!record_ || !window_)
            return;
        downloading_ = true;
        cancelled_ = false;
        showingVerification_ = false;
        auto* progress = new UpdateProgressDialog(window_);
        progress_ = progress;
        progress->open();
        connect(progress, &QDialog::rejected, this, [this, session = session_] {
            cancelled_ = true;
            update_session_cancel(**session);
        });
        progressTimer_.start();
        const auto session = session_;
        const auto appimage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
        const auto invoked = QString::fromLocal8Bit(qgetenv("ARGV0"));
        auto* watcher = new QFutureWatcher<UpdateDownloadDto>(this);
        connect(watcher, &QFutureWatcher<UpdateDownloadDto>::finished, this, [this, watcher] {
            progressTimer_.stop();
            const auto result = watcher->result();
            watcher->deleteLater();
            if (!window_)
                return;
            if (cancelled_ && result.success) {
                const auto session = session_;
                auto* cleanup = new QFutureWatcher<void>(this);
                connect(cleanup, &QFutureWatcher<void>::finished, this, [this, cleanup] {
                    cleanup->deleteLater();
                    downloading_ = false;
                    finishDownload(false, {});
                });
                cleanup->setFuture(
                    QtConcurrent::run([session] { update_session_discard_staged(**session); }));
                return;
            }
            downloading_ = false;
            if (!result.success) {
                finishDownload(false, cancelled_ || result.cancelled || result.staging_unavailable
                                          ? QString()
                                          : text(result.error));
                if (result.staging_unavailable && !cancelled_)
                    manualDownload();
                return;
            }
            finishDownload(true, {});
            promptToInstall();
        });
        watcher->setFuture(QtConcurrent::run([session, appimage, invoked] {
            const auto target = appimage.toUtf8(), invocation = invoked.toUtf8();
            return update_session_download(**session, utf8(target), utf8(invocation));
        }));
    }

    void finishDownload(bool success, const QString& error) {
        if (progress_) {
            const QSignalBlocker blocker(progress_);
            progress_->close();
            progress_->deleteLater();
            progress_ = nullptr;
        }
        if (!success) {
            if (!error.isEmpty())
                report(window_, tr("Update could not be downloaded"), error);
        } else {
            readyToInstall_ = true;
            installAction_->setEnabled(true);
        }
    }

    void manualDownload() {
        if (!record_)
            return;
        const auto answer = ConfirmationDialog::question(
            window_, tr("Manual update required"),
#ifdef Q_OS_LINUX
            tr("This AppImage cannot be safely replaced at its current path. Open the verified "
               "release download page?"),
#else
            tr("This computer cannot safely stage the installer. Open the verified release "
               "download page?"),
#endif
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            QDesktopServices::openUrl(record_->url);
    }

    void requestInstall() {
        if (!readyToInstall_ || !record_ || !window_ || installing_)
            return;
        QPointer<MainWindow> window = window_;
        window_->requestUpdateRestart([this, window] {
            if (!window)
                return;
            installing_ = true;
            const auto session = session_;
            const auto appimage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
            const auto invoked = QString::fromLocal8Bit(qgetenv("ARGV0"));
            const auto parentPid = uint32_t(QCoreApplication::applicationPid());
            auto* watcher = new QFutureWatcher<UpdateInstallDto>(this);
            connect(watcher, &QFutureWatcher<UpdateInstallDto>::finished, this,
                    [this, watcher, window] {
                        installing_ = false;
                        const auto result = watcher->result();
                        watcher->deleteLater();
                        if (!window)
                            return;
                        if (result.success) {
                            qApp->quit();
                            return;
                        }
                        if (result.invalid_package) {
                            readyToInstall_ = false;
                            installAction_->setEnabled(false);
                        }
                        window->setEnabled(true);
                        report(window, tr("Update could not be installed"), text(result.error));
                    });
            watcher->setFuture(QtConcurrent::run([session, appimage, invoked, parentPid] {
                const auto target = appimage.toUtf8(), invocation = invoked.toUtf8();
                return update_session_install(**session, utf8(target), utf8(invocation), parentPid);
            }));
        });
    }

    QPointer<MainWindow> window_;
    QPointer<QAction> installAction_;
    QPointer<QAction> automaticAction_;
    QPointer<UpdateProgressDialog> progress_;
    std::shared_ptr<rust::Box<RustUpdateSession>> session_;
    QThreadPool consentPool_;
    QString consentDirectory_;
    QTimer timer_;
    QTimer progressTimer_;
    std::optional<UpdateRecord> record_;
    bool manual_ = false;
    bool readyToInstall_ = false;
    bool checking_ = false;
    bool downloading_ = false;
    bool cancelled_ = false;
    bool installing_ = false;
    bool showingVerification_ = false;
    bool persistedConsent_ = false;
    quint64 consentGeneration_ = 0;
    quint64 persistedConsentGeneration_ = 0;
};
} // namespace

void installNativeUpdater(MainWindow& window, bool isolated) {
    if (!isolated && !QStandardPaths::isTestModeEnabled() &&
        qEnvironmentVariable("QT_QPA_PLATFORM") != QStringLiteral("offscreen"))
        new NativeUpdater(window);
}

#ifdef CHOSCORDB_TEST_UPDATER_MENU
void installNativeUpdaterForTest(MainWindow& window, bool downloadedReady) {
    auto* updater = new NativeUpdater(window);
    if (downloadedReady)
        updater->markDownloadedReadyForTest();
}
#endif

int runNativeUpdateHelper(const QStringList& arguments) {
#ifdef Q_OS_LINUX
    rust::Vec<rust::String> values;
    values.reserve(size_t(arguments.size()));
    for (const auto& argument : arguments) {
        const auto value = argument.toUtf8();
        values.push_back(rust::String(value.constData(), size_t(value.size())));
    }
    const auto key = updatePublicKey();
    const auto version = QCoreApplication::applicationVersion().toUtf8();
    const auto repo = QByteArray(CHOSCORDB_UPDATE_REPOSITORY);
    const auto target = QString::fromLocal8Bit(qgetenv("APPIMAGE")).toUtf8();
    const auto invoked = QString::fromLocal8Bit(qgetenv("ARGV0")).toUtf8();
    const auto result = update_run_linux_helper(std::move(values), bytes(key), utf8(version),
                                                utf8(repo), utf8(target), utf8(invoked));
    if (result.success)
        return 0;
    if (!result.manual_url.empty())
        offerManualUpdate(QUrl(text(result.manual_url)), text(result.error));
    else
        report(nullptr, QObject::tr("Update could not be installed"), text(result.error));
    return 1;
#else
    Q_UNUSED(arguments)
    return 2;
#endif
}
} // namespace choscordb
