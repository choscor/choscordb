#include "app/main_window.h"
#include "app/update_metadata.h"
#include "app/update_readiness.h"
#include "app/updater.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/text/text.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <limits>
#include <memory>
#ifdef Q_OS_LINUX
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <linux/fs.h>
#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace choscordb {
namespace {
#ifdef Q_OS_WIN
constexpr auto platform = "windows";
constexpr auto architecture = "x64";
constexpr auto feedName = "windows-x64.json";
#else
constexpr auto platform = "linux";
constexpr auto architecture = "x86_64";
constexpr auto feedName = "linux-x86_64.json";
#endif

QByteArray updatePublicKey() {
    return QByteArray::fromBase64(QByteArray(CHOSCORDB_UPDATE_PUBLIC_KEY));
}

void report(QWidget* parent, const QString& title, const QString& detail) {
    ConfirmationDialog dialog(QMessageBox::Warning, title, detail, QMessageBox::Ok, parent);
    dialog.setTextFormat(Qt::PlainText);
    dialog.exec();
}

#ifdef Q_OS_LINUX
QString launchedAppImage() {
    const auto appImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    const auto invoked = QString::fromLocal8Bit(qgetenv("ARGV0"));
    if (appImage.isEmpty() || invoked.isEmpty())
        return {};
    const QFileInfo invocation(invoked);
    const QFileInfo target(appImage);
    const auto directPath = QDir::cleanPath(invocation.absoluteFilePath());
    const auto targetPath = QDir::cleanPath(target.absoluteFilePath());
    if (!target.isAbsolute() || directPath != targetPath || invocation.isSymLink() ||
        target.isSymLink() || !target.isFile() || !target.isExecutable() ||
        target.canonicalFilePath() != targetPath ||
        !QFileInfo(target.dir().absolutePath()).isWritable() ||
        !targetPath.endsWith(QStringLiteral(".AppImage")))
        return {};
    return targetPath;
}

bool exchangeFiles(const QString& first, const QString& second) {
    const auto firstBytes = QFile::encodeName(first);
    const auto secondBytes = QFile::encodeName(second);
    return syscall(SYS_renameat2, AT_FDCWD, firstBytes.constData(), AT_FDCWD,
                   secondBytes.constData(), RENAME_EXCHANGE) == 0;
}

class UpdateProcessHandle final {
  public:
    enum class State { Running, Exited, Unknown };
    explicit UpdateProcessHandle(qint64 pid) : pid_(pid) {
        if (pid <= 1 || pid > std::numeric_limits<pid_t>::max())
            return;
        descriptor_ = int(syscall(SYS_pidfd_open, pid_t(pid), 0));
        if (descriptor_ < 0)
            openError_ = errno;
    }
    ~UpdateProcessHandle() {
        if (descriptor_ >= 0)
            (void)::close(descriptor_);
    }
    UpdateProcessHandle(const UpdateProcessHandle&) = delete;
    UpdateProcessHandle& operator=(const UpdateProcessHandle&) = delete;

    bool valid() const { return descriptor_ >= 0; }
    State state() const {
        if (descriptor_ < 0)
            return openError_ == ESRCH ? State::Exited : State::Unknown;
        pollfd descriptor{descriptor_, POLLIN, 0};
        const int result = ::poll(&descriptor, 1, 0);
        if (result == 0)
            return State::Running;
        if (result == 1 && (descriptor.revents & POLLIN))
            return State::Exited;
        return State::Unknown;
    }
    bool hasReadinessEnvironment(const QString& path) const {
        if (state() != State::Running)
            return false;
        QFile environment(QStringLiteral("/proc/%1/environ").arg(pid_));
        if (!environment.open(QIODevice::ReadOnly))
            return false;
        const auto expected = QByteArray("CHOSCORDB_UPDATE_READY_FILE=") + QFile::encodeName(path);
        return environment.readAll().split('\0').contains(expected);
    }
    bool sendSignal(int signal) const {
        return descriptor_ >= 0 &&
               syscall(SYS_pidfd_send_signal, descriptor_, signal, nullptr, 0) == 0;
    }

  private:
    qint64 pid_ = 0;
    int descriptor_ = -1;
    int openError_ = 0;
};

bool stopUpdateProcess(UpdateProcessHandle& handle) {
    if (handle.state() == UpdateProcessHandle::State::Exited)
        return true;
    if (handle.state() != UpdateProcessHandle::State::Running)
        return false;
    if (!handle.sendSignal(SIGTERM))
        return handle.state() == UpdateProcessHandle::State::Exited;
    for (int count = 0; count < 20; ++count) {
        if (handle.state() == UpdateProcessHandle::State::Exited)
            return true;
        QThread::msleep(100);
    }
    if (!handle.sendSignal(SIGKILL))
        return handle.state() == UpdateProcessHandle::State::Exited;
    for (int count = 0; count < 20; ++count) {
        if (handle.state() == UpdateProcessHandle::State::Exited)
            return true;
        QThread::msleep(100);
    }
    return handle.state() == UpdateProcessHandle::State::Exited;
}

void offerManualUpdate(const UpdateRecord& record, const QString& detail) {
    const auto answer = ConfirmationDialog::question(
        nullptr, QObject::tr("Manual update available"),
        detail + QObject::tr(" Open the verified release download page?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        QDesktopServices::openUrl(record.url);
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
#ifdef Q_OS_WIN
        QTimer::singleShot(0, this, [this] {
            const auto marker =
                QDir(qEnvironmentVariable("LOCALAPPDATA"))
                    .filePath(QStringLiteral("ChoscorDB/update-install-failure.txt"));
            if (QFile::exists(marker)) {
                QFile::remove(marker);
                report(window_, tr("Update installation failed"),
                       tr("The installer could not complete the upgrade. Your previous version "
                          "remains available. Please try again or install manually."));
            }
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
        automatic->setObjectName(QStringLiteral("automaticUpdateChecks"));
        automatic->setCheckable(true);
        automatic->setChecked(
            QSettings().value(QStringLiteral("updates/backgroundConsent"), false).toBool());
        connect(automatic, &QAction::toggled, this, [this](bool enabled) {
            QSettings().setValue(QStringLiteral("updates/backgroundConsent"), enabled);
            if (enabled) {
                timer_.start();
                checkForUpdates(false);
            } else {
                timer_.stop();
            }
        });
        timer_.setInterval(24 * 60 * 60 * 1000);
        connect(&timer_, &QTimer::timeout, this, [this] { checkForUpdates(false); });
        if (QSettings().contains(QStringLiteral("updates/backgroundConsent"))) {
            if (automatic->isChecked()) {
                timer_.start();
                QTimer::singleShot(5000, this, [this] { checkForUpdates(false); });
            }
        } else {
            QTimer::singleShot(0, this, [this, automatic] {
                if (!window_)
                    return;
                const auto answer = ConfirmationDialog::question(
                    window_, tr("Automatic update checks"),
                    tr("Check for stable ChoscorDB updates in the background? You can change this "
                       "in the Updates menu."),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                automatic->setChecked(answer == QMessageBox::Yes);
                QSettings().setValue(QStringLiteral("updates/backgroundConsent"),
                                     answer == QMessageBox::Yes);
            });
        }
    }

#ifdef CHOSCORDB_TEST_UPDATER_MENU
    void markDownloadedReadyForTest() {
        readyToInstall_ = true;
        installAction_->setEnabled(true);
    }
#endif

  private:
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
        if (feedReply_ || packageReply_ || verifying_)
            return;
        manual_ = manual;
        feedBytes_.clear();
#ifdef CHOSCORDB_TEST_UPDATER_MENU
        const auto testUrl = qEnvironmentVariable("CHOSCORDB_TEST_UPDATE_FEED_URL");
        const auto feedUrl = testUrl.isEmpty()
                                 ? QUrl(QString::fromLatin1(CHOSCORDB_UPDATE_BASE_URL) +
                                        QLatin1Char('/') + QString::fromLatin1(feedName))
                                 : QUrl(testUrl);
#else
        const auto feedUrl = QUrl(QString::fromLatin1(CHOSCORDB_UPDATE_BASE_URL) +
                                  QLatin1Char('/') + QString::fromLatin1(feedName));
#endif
        QNetworkRequest request(feedUrl);
        request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        feedReply_ = network_.get(request);
        connect(feedReply_, &QNetworkReply::readyRead, this, [this] {
            feedBytes_ += feedReply_->readAll();
            if (feedBytes_.size() > 32768)
                feedReply_->abort();
        });
        connect(feedReply_, &QNetworkReply::finished, this, [this] {
            auto* reply = feedReply_.data();
            if (!reply)
                return;
            feedBytes_ += reply->readAll();
            const bool good =
                reply->error() == QNetworkReply::NoError &&
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                feedBytes_.size() <= 32768;
            reply->deleteLater();
            feedReply_ = nullptr;
            if (!good) {
                if (manual_)
                    report(window_, tr("Update check failed"),
                           tr("The update feed is unavailable. Please try again later."));
                return;
            }
            QString error;
            const auto baseline = readyToInstall_ && record_
                                      ? record_->version
                                      : QCoreApplication::applicationVersion();
            const auto record = parseSignedUpdateMetadata(
                feedBytes_, updatePublicKey(), baseline, QString::fromLatin1(platform),
                QString::fromLatin1(architecture), QStringLiteral(CHOSCORDB_UPDATE_REPOSITORY),
                &error);
            if (!error.isEmpty()) {
                if (manual_)
                    report(window_, tr("Update check failed"), error);
                return;
            }
            if (!record) {
                if (manual_) {
                    if (readyToInstall_ && record_)
                        report(window_, tr("Downloaded update is ready"),
                               tr("ChoscorDB %1 is downloaded. Choose Install Downloaded Update "
                                  "from the Updates menu when you are ready.")
                                   .arg(record_->version));
                    else
                        report(window_, tr("ChoscorDB is up to date"),
                               tr("You already have the latest stable version."));
                }
                return;
            }
            ConfirmationDialog offer(
                QMessageBox::Information, tr("ChoscorDB %1 is available").arg(record->version),
                tr("Release notes:\n%1\n\nDownload this update?").arg(record->notes),
                QMessageBox::Yes | QMessageBox::No, window_);
            offer.setTextFormat(Qt::PlainText);
            if (offer.exec() == QMessageBox::Yes) {
                if (readyToInstall_) {
                    staged_.reset();
                    readyToInstall_ = false;
                    installAction_->setEnabled(false);
                }
                record_ = *record;
                envelope_ = feedBytes_;
                download();
            }
        });
    }

    void download() {
        if (!record_ || !window_)
            return;
#ifdef Q_OS_LINUX
        const auto appImage = launchedAppImage();
        if (appImage.isEmpty()) {
            manualDownload();
            return;
        }
        const auto templatePath =
            QFileInfo(appImage).dir().filePath(QStringLiteral(".ChoscorDB-update-XXXXXX.AppImage"));
#else
        const auto templatePath =
            QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                .filePath(QStringLiteral("ChoscorDB-update-XXXXXX.exe"));
#endif
        staged_ = std::make_unique<QTemporaryFile>(templatePath);
        if (!staged_->open()) {
            manualDownload();
            return;
        }
        cancelled_ = false;
        auto* progress = new UpdateProgressDialog(window_);
        progress_ = progress;
        progress->open();
        connect(progress, &QDialog::rejected, this, [this] {
            cancelled_ = true;
            if (packageReply_)
                packageReply_->abort();
        });
        QNetworkRequest request(record_->url);
        request.setTransferTimeout(30000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        packageReply_ = network_.get(request);
        connect(packageReply_, &QNetworkReply::redirected, this, [this](const QUrl& next) {
            const auto host = next.host().toLower();
            if (next.scheme() != QStringLiteral("https") ||
                (host != QStringLiteral("github.com") &&
                 host != QStringLiteral("release-assets.githubusercontent.com")))
                packageReply_->abort();
        });
        connect(packageReply_, &QNetworkReply::readyRead, this, [this] {
            const auto bytes = packageReply_->readAll();
            if (!staged_ || staged_->write(bytes) != bytes.size() ||
                staged_->size() > record_->size)
                packageReply_->abort();
        });
        connect(packageReply_, &QNetworkReply::downloadProgress, this,
                [this](qint64 received, qint64) {
                    if (progress_ && record_ && record_->size > 0)
                        progress_->setProgress(
                            int(qMin(999.0, double(received) * 1000.0 / double(record_->size))));
                });
        connect(packageReply_, &QNetworkReply::finished, this, [this] {
            auto* reply = packageReply_.data();
            if (!reply)
                return;
            const auto bytes = reply->readAll();
            const bool good =
                reply->error() == QNetworkReply::NoError &&
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                staged_ && staged_->write(bytes) == bytes.size() && staged_->flush();
            reply->deleteLater();
            packageReply_ = nullptr;
            if (!good || cancelled_) {
                finishDownload(false,
                               cancelled_ ? QString() : tr("Download failed. Please try again."));
                return;
            }
            staged_->close();
            if (progress_)
                progress_->setVerifying();
            verifying_ = true;
            auto* watcher = new QFutureWatcher<bool>(this);
            const auto path = staged_->fileName();
            const auto record = *record_;
            connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
                verifying_ = false;
                const bool verified = watcher->result();
                watcher->deleteLater();
                if (cancelled_ || !verified) {
                    finishDownload(
                        false, cancelled_
                                   ? QString()
                                   : tr("The downloaded update failed integrity verification."));
                    return;
                }
                finishDownload(true, {});
                promptToInstall();
            });
            watcher->setFuture(QtConcurrent::run(
                [path, record] { return verifyUpdateFile(path, record, nullptr); }));
        });
    }

    void finishDownload(bool success, const QString& error) {
        if (progress_) {
            const QSignalBlocker blocker(progress_);
            progress_->close();
            progress_->deleteLater();
            progress_ = nullptr;
        }
        if (!success) {
            staged_.reset();
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
        if (!readyToInstall_ || !staged_ || !record_ || !window_)
            return;
        QPointer<MainWindow> window = window_;
        window_->requestUpdateRestart([this, window] {
            if (!window || !staged_)
                return;
#ifdef Q_OS_WIN
            if (!record_ || !verifyUpdateFile(staged_->fileName(), *record_, nullptr)) {
                staged_.reset();
                readyToInstall_ = false;
                installAction_->setEnabled(false);
                window->setEnabled(true);
                report(window, tr("Update could not be installed"),
                       tr("The downloaded installer failed integrity verification. Please download "
                          "the update again."));
                return;
            }
#endif
            staged_->setAutoRemove(false);
#ifdef Q_OS_WIN
            const auto executable = staged_->fileName();
            const QStringList arguments{
                QStringLiteral("/S"),
                QStringLiteral("/WAITPID=%1").arg(QCoreApplication::applicationPid())};
#elif defined(Q_OS_LINUX)
            const auto executable = launchedAppImage();
            const QStringList arguments{QStringLiteral("--apply-update"), staged_->fileName(),
                                        QString::fromLatin1(envelope_.toBase64()),
                                        QString::number(QCoreApplication::applicationPid())};
#else
            const QString executable;
            const QStringList arguments;
#endif
            if (executable.isEmpty() || !QProcess::startDetached(executable, arguments)) {
                staged_->setAutoRemove(true);
                window->setEnabled(true);
                report(
                    window, tr("Update could not be installed"),
                    tr("The installer could not start. Your current version is still available."));
                return;
            }
            qApp->quit();
        });
    }

    QPointer<MainWindow> window_;
    QPointer<QAction> installAction_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> feedReply_;
    QPointer<QNetworkReply> packageReply_;
    QPointer<UpdateProgressDialog> progress_;
    QByteArray feedBytes_;
    QByteArray envelope_;
    std::optional<UpdateRecord> record_;
    std::unique_ptr<QTemporaryFile> staged_;
    QTimer timer_;
    bool manual_ = false;
    bool cancelled_ = false;
    bool readyToInstall_ = false;
    bool verifying_ = false;
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
    if (arguments.size() != 5)
        return 2;
    const auto target = launchedAppImage();
    const auto staged = arguments.at(2);
    const QFileInfo stageInfo(staged);
    bool pidValid = false;
    const auto parentPid = arguments.at(4).toLongLong(&pidValid);
    QString error;
    const auto envelope = QByteArray::fromBase64(arguments.at(3).toLatin1());
    const auto record = parseSignedUpdateMetadata(
        envelope, updatePublicKey(), QCoreApplication::applicationVersion(),
        QStringLiteral("linux"), QStringLiteral("x86_64"),
        QStringLiteral(CHOSCORDB_UPDATE_REPOSITORY), &error);
    if (target.isEmpty() || !pidValid || parentPid <= 1 ||
        parentPid > std::numeric_limits<pid_t>::max() || !record || !stageInfo.isFile() ||
        stageInfo.isSymLink() ||
        stageInfo.dir().absolutePath() != QFileInfo(target).dir().absolutePath() ||
        !stageInfo.fileName().startsWith(QStringLiteral(".ChoscorDB-update-")) ||
        !verifyUpdateFile(staged, *record, &error)) {
        if (record)
            offerManualUpdate(*record, QObject::tr("The verified update is no longer safe to "
                                                   "install. Your previous version is unchanged."));
        else
            report(
                nullptr, QObject::tr("Update could not be installed"),
                QObject::tr(
                    "The update could not be authenticated. Your previous version is unchanged."));
        return 1;
    }
    UpdateProcessHandle pidfdSupport(getpid());
    UpdateProcessHandle parentProcess(parentPid);
    if (!pidfdSupport.valid() || !pidfdSupport.sendSignal(0) ||
        parentProcess.state() == UpdateProcessHandle::State::Unknown) {
        offerManualUpdate(*record, QObject::tr("Process state could not be checked safely. The "
                                               "previous AppImage remains usable."));
        return 1;
    }
    for (int i = 0; i < 900 && parentProcess.state() == UpdateProcessHandle::State::Running; ++i)
        QThread::msleep(100);
    QTemporaryFile readinessTemplate(
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("ChoscorDB-update-ready-XXXXXX.txt")));
    if (!readinessTemplate.open()) {
        offerManualUpdate(
            *record,
            QObject::tr(
                "Update startup could not be verified. The previous AppImage remains usable."));
        return 1;
    }
    const auto readyPath = readinessTemplate.fileName();
    readinessTemplate.close();
    if (!readinessTemplate.remove()) {
        offerManualUpdate(
            *record,
            QObject::tr(
                "Update startup could not be verified. The previous AppImage remains usable."));
        return 1;
    }
    if (parentProcess.state() != UpdateProcessHandle::State::Exited ||
        launchedAppImage() != target || !verifyUpdateFile(staged, *record, &error) ||
        !QFile::setPermissions(staged, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                           QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther |
                                           QFile::ExeOther) ||
        !exchangeFiles(staged, target)) {
        offerManualUpdate(
            *record,
            QObject::tr("The current AppImage could not be safely replaced. It remains usable."));
        return 1;
    }
    if (!verifyUpdateFile(target, *record, &error)) {
        (void)exchangeFiles(staged, target);
        offerManualUpdate(*record, QObject::tr("The replacement AppImage failed verification. The "
                                               "previous version was restored where possible."));
        return 1;
    }
    QProcess next;
    next.setProgram(target);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("CHOSCORDB_UPDATE_READY_FILE"), readyPath);
    next.setProcessEnvironment(environment);
    qint64 childPid = 0;
    const bool launched = next.startDetached(&childPid);
    UpdateProcessHandle childProcess(childPid);
    bool identified = false;
    if (launched && childProcess.valid()) {
        for (int attempt = 0;
             attempt < 50 && childProcess.state() == UpdateProcessHandle::State::Running;
             ++attempt) {
            if (childProcess.hasReadinessEnvironment(readyPath)) {
                identified = true;
                break;
            }
            QThread::msleep(100);
        }
    }
    const bool ready =
        identified && waitForUpdateReadiness(readyPath, 60000, 2000, [&childProcess] {
            return childProcess.state() == UpdateProcessHandle::State::Running;
        });
    if (!ready) {
        const bool stopped = !launched ||
                             childProcess.state() == UpdateProcessHandle::State::Exited ||
                             (identified && stopUpdateProcess(childProcess));
        QFile::remove(readyPath);
        const bool restored = stopped && exchangeFiles(staged, target);
        if (restored)
            QFile::remove(staged);
        offerManualUpdate(*record, restored
                                       ? QObject::tr("The previous AppImage was restored.")
                                       : QObject::tr("The new AppImage did not confirm startup. "
                                                     "Restore the previous version manually."));
        return 1;
    }
    QFile::remove(readyPath);
    QFile::remove(staged);
    return 0;
#else
    Q_UNUSED(arguments)
    return 2;
#endif
}
} // namespace choscordb
