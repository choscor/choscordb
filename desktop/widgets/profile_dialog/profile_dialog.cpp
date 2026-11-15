#include "widgets/profile_dialog/profile_dialog.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/screen_state/screen_state.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
namespace choscordb {

ProfileDialog::ProfileDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter) {
    setObjectName("profileDialog");
    setWindowTitle(tr("New connection"));
    setAppModal();
    const auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
    const auto margin = design::spacing(design::Spacing::Four);
    resize(design::dialogInitialSize(design::DialogSize::Profiles)
               .boundedTo(available - QSize(margin * 2, margin * 2)));
    sections_ = createSections();
    auto* sections = sections_;
    sections->setObjectName("profileSections");
    heading_ = new design::Text(tr("New connection"), sections);
    heading_->setObjectName("profileHeading");
    heading_->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading_);
    auto* dismiss = sections->addDismissButton(tr("Close connection dialog"));
    dismiss->setObjectName("profileDismiss");
    connect(dismiss, &QPushButton::clicked, this, &QDialog::reject);
    list_ = new QListWidget(this);
    list_->setObjectName("profileList");
    list_->hide();
    auto* formScroll = new QScrollArea(this);
    formScroll->setObjectName("profileFormScroll");
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setWidgetResizable(true);
    formScroll->viewport()->setAutoFillBackground(false);
    form_ = new QWidget(formScroll);
    formScroll->setWidget(form_);
    form_->setAutoFillBackground(false);
    const auto bodyMargins = sections->bodyLayout()->contentsMargins();
    sections->bodyLayout()->setContentsMargins(bodyMargins.left(), 0, 0, 0);
    sections->bodyLayout()->addWidget(formScroll);
    formLayout_ = new QFormLayout(form_);
    design::DialogSections::configureForm(formLayout_);
    formLayout_->setContentsMargins(0, bodyMargins.top(), design::spacing(design::Spacing::Six),
                                    bodyMargins.bottom());
    formLayout_->addRow(
        createDescription(tr("Connect to a server or open a local database file."), form_));
    driver_ = new QComboBox(form_);
    driver_->setObjectName("profileDriver");
    driver_->addItem(tr("SQLite"), "sqlite");
    driver_->addItem(tr("PostgreSQL"), "postgres");
    driver_->addItem(tr("MySQL"), "mysql");
    driver_->addItem(tr("MariaDB"), "mariadb");
    driver_->hide();
    auto* driverRow = new QWidget(form_);
    driverRow->setObjectName("profileDriverChoices");
    auto* drivers = new QHBoxLayout(driverRow);
    drivers->setContentsMargins(0, 0, 0, 0);
    drivers->setSpacing(design::spacing(design::Spacing::Two));
    driverChoices_ = new QButtonGroup(this);
    const auto choice = [&](const QString& title, const char* object, int index) {
        auto* button = new design::Button({}, driverRow);
        button->setObjectName(object);
        button->setToolTip(title);
        button->setAccessibleName(title);
        button->setVariant(design::ButtonVariant::Outline);
        button->setButtonContext(design::ButtonContext::Choice);
        button->setDesignIcon(design::driverIcon(driver_->itemData(index).toString()));
        button->setCheckable(true);
        driverChoices_->addButton(button, index);
        drivers->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, index] { driver_->setCurrentIndex(index); });
    };
    choice(tr("PostgreSQL"), "profileDriverPostgres", 1);
    choice(tr("MySQL"), "profileDriverMysql", 2);
    choice(tr("MariaDB"), "profileDriverMariadb", 3);
    choice(tr("SQLite"), "profileDriverSqlite", 0);
    drivers->addStretch(1);
    sections->addFormRow(formLayout_, tr("Type"), driverRow);
    auto line = [this](const char* object) {
        auto* edit = new QLineEdit(form_);
        edit->setObjectName(object);
        connect(edit, &QLineEdit::textEdited, this, [this] {
            dirty_ = true;
            ++revision_;
        });
        return edit;
    };
    const auto port = [this](const char* object) {
        auto* spin = new QSpinBox(form_);
        spin->setObjectName(object);
        spin->setRange(1, 65535);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return spin;
    };
    name_ = line("profileName");
    nameValidation_ = new design::FieldValidation(name_, form_);
    sections->addFormRow(formLayout_, tr("Name"), nameValidation_);
    connect(name_, &QLineEdit::textChanged, this, [this] { nameValidation_->setError({}); });
    createTagControls();
    path_ = line("profilePath");
    path_->setAccessibleName(tr("Database file"));
    path_->setPlaceholderText(tr("/path/to/database.sqlite"));
    auto* pathRow = new QWidget(form_);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(design::spacing(design::Spacing::Two));
    pathLayout->addWidget(validated(path_));
    auto* browse = new design::Button(tr("Browse…"), pathRow);
    browse->setObjectName("profileBrowse");
    browse->setVariant(design::ButtonVariant::Outline);
    pathLayout->addWidget(browse, 0, Qt::AlignTop);
    sections->addFormRow(formLayout_, tr("Database file"), pathRow);
    pathRow->setFocusProxy(path_);
    readOnly_ = new QCheckBox(tr("Open read-only"), form_);
    readOnly_->setProperty("designRole", "switch");
    readOnly_->setObjectName("profileReadOnly");
    formLayout_->addRow(QString(), readOnly_);
    auto* readOnlyHelp = createDescription(tr("Browse without changes to the file."), form_);
    formLayout_->addRow(QString(), readOnlyHelp);
    sqliteRows_ = {pathRow, readOnly_, readOnlyHelp};
    host_ = line("profileHost");
    host_->setPlaceholderText(tr("localhost"));
    host_->setToolTip(
        tr("TCP hostname/IP address, or Unix socket path (PostgreSQL directory; MySQL file). "
           "Unix sockets use a local connection without TLS or SSH. Enter TCP ports separately. "
           "With SSH enabled, this address is reached from the SSH server; "
           "localhost refers to that server."));
    database_ = line("profileDatabase");
    user_ = line("profileUser");
    port_ = port("profilePort");
    password_ = line("profilePassword");
    password_->setEchoMode(QLineEdit::Password);
    password_->setMaxLength(EngineAdapter::profileSecretMaxBytes());
    auto* hostField = validated(host_);
    auto* userField = validated(user_);
    auto* passwordField = validated(password_);
    sections->addFormRow(formLayout_, tr("Host"), hostField);
    sections->addFormRow(formLayout_, tr("Port"), port_);
    sections->addFormRow(formLayout_, tr("Database"), database_, design::FieldNeed::Optional);
    databaseHelp_ = createDescription({}, form_);
    databaseHelp_->setObjectName("profileDatabaseHelp");
    formLayout_->addRow(QString(), databaseHelp_);
    sections->addFormRow(formLayout_, tr("Username"), userField);
    sections->addFormRow(formLayout_, tr("Password"), passwordField, design::FieldNeed::Optional);
    saveCredentials_ = new QCheckBox(tr("Save credentials in OS credential store"), form_);
    saveCredentials_->setObjectName("profileSaveCredentials");
    formLayout_->addRow(QString(), saveCredentials_);
    tlsSecret_ = line("profileTlsSecret");
    tlsSecret_->setEchoMode(QLineEdit::Password);
    tlsSecret_->setToolTip(
        tr("Password for a client identity from an earlier version of this connection"));
    tlsSecretField_ = validated(tlsSecret_);
    sections->addFormRow(formLayout_, tr("Client identity password"), tlsSecretField_);
    connect(saveCredentials_, &QCheckBox::toggled, this, [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    sshEnabled_ = new QCheckBox(tr("Connect through SSH tunnel"), form_);
    sshEnabled_->setObjectName("profileSshEnabled");
    sshEnabled_->setProperty("designRole", "switch");
    auto* sshEnabledField = validated(sshEnabled_);
    formLayout_->addRow(QString(), sshEnabledField);
    serverRows_ = {hostField, port_,         database_,        databaseHelp_,
                   userField, passwordField, saveCredentials_, sshEnabledField};
    connect(host_, &QLineEdit::textChanged, this,
            [this] { validationFor(sshEnabled_)->setError({}); });
    sshHost_ = line("profileSshHost");
    sshHost_->setPlaceholderText(tr("bastion.example.com"));
    sshUser_ = line("profileSshUser");
    sshAuthentication_ = new QComboBox(form_);
    sshAuthentication_->setObjectName("profileSshAuthentication");
    sshAuthentication_->addItem(tr("SSH agent"), "agent");
    sshAuthentication_->addItem(tr("Public key"), "public_key");
    sshAuthentication_->addItem(tr("Password"), "password");
    sshIdentityFile_ = line("profileSshIdentityFile");
    sshIdentityFile_->setPlaceholderText(tr("~/.ssh/id_ed25519"));
    sshSecret_ = line("profileSshSecret");
    sshSecret_->setEchoMode(QLineEdit::Password);
    sshSecret_->setMaxLength(EngineAdapter::profileSecretMaxBytes());
    sshPort_ = port("profileSshPort");
    auto* sshHostField = validated(sshHost_);
    auto* sshUserField = validated(sshUser_);
    sections->addFormRow(formLayout_, tr("SSH host"), sshHostField);
    auto* sshHostHelp = createDescription(
        tr("The SSH server that this computer can reach. Enter its port in SSH port."), form_);
    sshHostHelp->setObjectName("profileSshHostHelp");
    formLayout_->addRow(QString(), sshHostHelp);
    sections->addFormRow(formLayout_, tr("SSH port"), sshPort_);
    sections->addFormRow(formLayout_, tr("SSH username"), sshUserField);
    sections->addFormRow(formLayout_, tr("Authentication"), sshAuthentication_);
    sshRows_ = {sshHostField, sshHostHelp, sshPort_, sshUserField, sshAuthentication_};
    createPrivateKeyControls();
    sections->addFormRow(formLayout_, tr("SSH password"), validated(sshSecret_));
    createTrustControls();
    sshRows_.append(inspectSshKeys_);
    connect(sshEnabled_, &QCheckBox::toggled, this, [this] {
        updateRowVisibility();
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    connect(sshPort_, &QSpinBox::valueChanged, this, [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    const auto updateSshAuthentication = [this] {
        const bool publicKey = sshAuthentication_->currentData().toString() == "public_key";
        sections_->relabelFormRow(formLayout_, validationFor(sshSecret_),
                                  publicKey ? tr("Key passphrase") : tr("SSH password"),
                                  publicKey ? design::FieldNeed::Optional
                                            : design::FieldNeed::Required);
        updatePrivateKeyControls();
    };
    connect(sshAuthentication_, &QComboBox::currentIndexChanged, this,
            [this, updateSshAuthentication] {
                if (!filling_) {
                    sshSecret_->clear();
                    sshSecret_->setModified(false);
                    if (sshPrivateKey_)
                        sshPrivateKey_->setDraft({});
                }
                updateSshAuthentication();
                if (!filling_) {
                    dirty_ = true;
                    ++revision_;
                }
            });
    updateSshAuthentication();
    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("profileStatusLine");
    auto* status = statusLine_->findChild<QLabel*>("statusMessage");
    status->setObjectName("profileStatus");
    status->setTextFormat(Qt::PlainText);
    feedbackToast_ = windowToast(this);
    auto hidden = [this](const QString& title, const char* object) {
        auto* result = new design::Button(title, this);
        result->setObjectName(object);
        result->hide();
        actions_.append(result);
        return result;
    };
    auto* create = hidden(tr("New"), "profileNew");
    auto* duplicate = hidden(tr("Duplicate"), "profileDuplicate");
    auto* remove = hidden(tr("Delete"), "profileDelete");
    remove->setVariant(design::ButtonVariant::Destructive);
    auto* open = hidden(tr("Connect"), "profileConnect");
    auto action = [this](design::DialogActionRole role, const QString& title, const char* object) {
        auto* result = sections_->addAction(role, title);
        result->setObjectName(object);
        actions_.append(result);
        return result;
    };
    auto* test = action(design::DialogActionRole::Utility, tr("Test connection"), "profileTest");
    auto* save = action(design::DialogActionRole::Utility, tr("Save connection"), "profileSave");
    auto* close = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    close->setObjectName("profileCancel");
    auto* saveConnect =
        action(design::DialogActionRole::Primary, tr("Save && connect"), "profileSaveConnect");
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto revision = revision_;
        const auto token = token_;
        const auto path = QFileDialog::getOpenFileName(this, tr("SQLite database"), path_->text());
        if (!path.isEmpty() && !busy_ && revision_ == revision && token_ == token) {
            path_->setText(path);
            dirty_ = true;
            ++revision_;
        }
    });
    connect(driver_, &QComboBox::currentIndexChanged, this, [this] {
        if (!filling_)
            port_->setValue(EngineAdapter::profilePortForDriver(
                static_cast<quint16>(port_->value()), driver_->currentData().toString()));
        updateDriver();
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    connect(readOnly_, &QCheckBox::toggled, this, [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    connect(port_, &QSpinBox::valueChanged, this, [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    connect(create, &QPushButton::clicked, this, &ProfileDialog::newProfile);
    connect(save, &QPushButton::clicked, this, [this] { saveDraft(draft()); });
    connect(saveConnect, &QPushButton::clicked, this, [this] {
        if (busy_ || !adapter_)
            return;
        connectAfterSave_ = true;
        saveDraft(draft());
        if (!busy_)
            connectAfterSave_ = false;
    });
    connect(test, &QPushButton::clicked, this, [this] { testDraft(draft()); });
    connect(open, &QPushButton::clicked, this, [this] { connectDraft(false); });
    connect(duplicate, &QPushButton::clicked, this, [this] {
        if (busy_ || !adapter_ || current_.id.isEmpty())
            return;
        present(ProfileOperation::Duplicating);
        adapter_->duplicateProfile(current_.id, name_->text(), ++token_);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (busy_ || !adapter_ || current_.id.isEmpty())
            return;
        const auto id = current_.id;
        const auto token = token_;
        const auto revision = revision_;
        if (!ConfirmationDialog::confirm(this, tr("Delete connection"),
                                         tr("Delete the saved connection “%1”?").arg(current_.name),
                                         tr("Delete"),
                                         ConfirmationDialog::Consequence::Destructive))
            return;
        if (busy_ || !adapter_ || current_.id != id || token_ != token || revision_ != revision)
            return;
        present(ProfileOperation::Deleting);
        adapter_->deleteProfile(id, ++token_);
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (filling_ || row < 0 || row >= profiles_.size())
            return;
        const auto selected = profiles_[row];
        if (discardChanges())
            setDraft(selected);
        else {
            QSignalBlocker blocker(list_);
            list_->setCurrentRow(-1);
            for (qsizetype index = 0; index < profiles_.size(); ++index) {
                if (profiles_[index].id == current_.id) {
                    list_->setCurrentRow(static_cast<int>(index));
                    break;
                }
            }
        }
    });
    connect(
        adapter, &EngineAdapter::profilesReady, this,
        [this](quint64 token, const QList<SavedProfile>& profiles) {
            if (token != token_)
                return;
            profiles_ = profiles;
            QSignalBlocker blocker(list_);
            list_->clear();
            for (const auto& profile : profiles_)
                list_->addItem(profile.name);
            present(refreshNotice_
                        ? *refreshNotice_
                        : EngineAdapter::profileOperationPresentation(ProfileOperation::Listed));
            if (isVisible() && focusWidget() == findChild<QPushButton*>("profileDismiss"))
                name_->setFocus(Qt::OtherFocusReason);
            refreshNotice_.reset();
            const auto sessionPassword = preservePasswordOnRefresh_ ? password_->text() : QString();
            const bool passwordModified = password_->isModified();
            const auto sessionSshSecret =
                preserveSshSecretOnRefresh_ ? sshSecret_->text() : QString();
            const bool sshSecretModified = sshSecret_->isModified();
            const auto sessionTlsSecret =
                preserveTlsSecretOnRefresh_ ? tlsSecret_->text() : QString();
            const bool tlsSecretModified = tlsSecret_->isModified();
            if (!pendingSelection_.isEmpty()) {
                const auto id = pendingSelection_;
                pendingSelection_.clear();
                selectProfile(id);
            }
            if (preservePasswordOnRefresh_) {
                password_->setText(sessionPassword);
                password_->setModified(passwordModified);
                preservePasswordOnRefresh_ = false;
            }
            if (preserveSshSecretOnRefresh_) {
                sshSecret_->setText(sessionSshSecret);
                sshSecret_->setModified(sshSecretModified);
                preserveSshSecretOnRefresh_ = false;
            }
            if (preserveTlsSecretOnRefresh_) {
                tlsSecret_->setText(sessionTlsSecret);
                tlsSecret_->setModified(tlsSecretModified);
                preserveTlsSecretOnRefresh_ = false;
            }
            if (pendingPrivateKey_) {
                sshPrivateKey_->setDraft(*pendingPrivateKey_);
                pendingPrivateKey_.reset();
            }
            if (connectAfterSave_) {
                connectAfterSave_ = false;
                connectDraft(true);
            }
            dispatchProfileAction();
        });
    connect(adapter, &EngineAdapter::profileSaved, this,
            [this](quint64 token, const SavedProfile& profile, const QString& warning) {
                if (token != token_)
                    return;
                pendingPrivateKey_ =
                    savingDraft_ && profile.sshEnabled && profile.sshIdentitySource == "inline" &&
                            !saveCredentials_->isChecked()
                        ? std::optional<SshPrivateKeyDraft>(sshPrivateKey_->draft())
                        : std::nullopt;
                preserveTlsSecretOnRefresh_ = savingDraft_ && !saveCredentials_->isChecked() &&
                                              !profile.tlsClientIdentity.isEmpty();
                preservePasswordOnRefresh_ = savingDraft_ && !saveCredentials_->isChecked();
                preserveSshSecretOnRefresh_ = savingDraft_ && !saveCredentials_->isChecked() &&
                                              sshAuthentication_->currentData() != "agent";
                const auto sessionPassword =
                    preservePasswordOnRefresh_ ? password_->text() : QString();
                const bool passwordModified = password_->isModified();
                const auto sessionSshSecret =
                    preserveSshSecretOnRefresh_ ? sshSecret_->text() : QString();
                const bool sshSecretModified = sshSecret_->isModified();
                const auto sessionTlsSecret =
                    preserveTlsSecretOnRefresh_ ? tlsSecret_->text() : QString();
                const bool tlsSecretModified = tlsSecret_->isModified();
                savingDraft_ = false;
                setDraft(profile);
                if (pendingPrivateKey_)
                    sshPrivateKey_->setDraft(*pendingPrivateKey_);
                if (preservePasswordOnRefresh_) {
                    password_->setText(sessionPassword);
                    password_->setModified(passwordModified);
                }
                if (preserveSshSecretOnRefresh_) {
                    sshSecret_->setText(sessionSshSecret);
                    sshSecret_->setModified(sshSecretModified);
                }
                if (preserveTlsSecretOnRefresh_) {
                    tlsSecret_->setText(sessionTlsSecret);
                    tlsSecret_->setModified(tlsSecretModified);
                }
                refreshNotice_ =
                    EngineAdapter::profileOperationPresentation(ProfileOperation::Saved, warning);
                pendingSelection_ = profile.id;
                refresh();
            });
    connect(adapter, &EngineAdapter::profileDeleted, this,
            [this](quint64 token, const QString&, const QString& warning) {
                if (token != token_)
                    return;
                setDraft({});
                refreshNotice_ =
                    EngineAdapter::profileOperationPresentation(ProfileOperation::Deleted, warning);
                refresh();
            });
    connect(adapter, &EngineAdapter::profileFailed, this,
            [this](quint64 token, const QString& error) {
                if (token == token_) {
                    savingDraft_ = false;
                    pendingPrivateKey_.reset();
                    preservePasswordOnRefresh_ = false;
                    preserveTlsSecretOnRefresh_ = false;
                    connectAfterSave_ = false;
                    if (std::exchange(testToken_, 0) == token) {
                        showTest(ProfileTestState::Failed, error);
                        return;
                    }
                    present(refreshNotice_ ? ProfileOperation::ListRefreshFailed
                                           : ProfileOperation::Failed,
                            error);
                    refreshNotice_.reset();
                }
            });
    connect(adapter, &EngineAdapter::profileTested, this, [this](quint64 token) {
        if (token == token_ && std::exchange(testToken_, 0) == token)
            showTest(ProfileTestState::Succeeded);
    });
    connect(adapter, &EngineAdapter::profileConnectFailed, this, [this](const QString& error) {
        if (connecting_)
            connectionSubmissionError_ = error;
    });
    connect(adapter, &EngineAdapter::eventReady, this, [this](const BridgeEvent& event) {
        if (!pendingConnection_ || event.id != *pendingConnection_)
            return;
        const auto kind = bridge_detail::fromRust(event.kind);
        if (kind != "connected" && kind != "connection_failed" && kind != "disconnected")
            return;
        const auto connection = *pendingConnection_;
        pendingConnection_.reset();
        if (kind == "connected") {
            if (openQueryAfterConnect_) {
                present(ProfileOperation::Connected);
                openQueryAfterConnect_ = false;
                emit openQueryRequested(connection);
                accept();
            } else
                present(ProfileOperation::Connected);
        } else if (kind == "disconnected") {
            present(ProfileOperation::ConnectionClosed);
        } else {
            auto message = bridge_detail::fromRust(event.error);
            if (!event.vendor_code.empty()) {
                message += tr(" [Code: %1]").arg(bridge_detail::fromRust(event.vendor_code));
            }
            present(ProfileOperation::Failed, message);
        }
    });
    setDraft({});
    refresh();
}
bool ProfileDialog::hasDraft() const {
    return dirty_ && !busy_;
}

void ProfileDialog::updateRowVisibility() {
    const auto form = EngineAdapter::profileDriverForm(driver_->currentData().toString());
    const bool ssh = form.server && sshEnabled_->isChecked();
    const auto authentication = sshAuthentication_->currentData().toString();
    const bool key = authentication == "public_key";
    const bool inlineKey = key && sshIdentitySource_->currentData() == "inline";
    for (auto* row : sqliteRows_)
        formLayout_->setRowVisible(row, !form.server);
    for (auto* row : serverRows_)
        formLayout_->setRowVisible(row, form.server);
    formLayout_->setRowVisible(tlsSecretField_, form.server && current_.tls != "disable" &&
                                                    !current_.tlsClientIdentity.isEmpty());
    for (auto* row : sshRows_)
        formLayout_->setRowVisible(row, ssh);
    formLayout_->setRowVisible(sshIdentitySource_, ssh && key);
    formLayout_->setRowVisible(validationFor(sshIdentityFile_), ssh && key && !inlineKey);
    formLayout_->setRowVisible(sshPrivateKey_, ssh && inlineKey);
    formLayout_->setRowVisible(sshPrivateKeyHelp_, ssh && inlineKey);
    formLayout_->setRowVisible(validationFor(sshSecret_), ssh && authentication != "agent");
}

QWidget* ProfileDialog::validated(QWidget* field) {
    auto* validation = new design::FieldValidation(field, form_);
    validations_.insert(field, validation);
    if (auto* line = qobject_cast<QLineEdit*>(field))
        connect(line, &QLineEdit::textChanged, validation,
                [validation] { validation->setError({}); });
    if (auto* select = qobject_cast<QComboBox*>(field))
        connect(select, &QComboBox::currentIndexChanged, validation,
                [validation] { validation->setError({}); });
    if (auto* check = qobject_cast<QCheckBox*>(field))
        connect(check, &QCheckBox::toggled, validation, [validation] { validation->setError({}); });
    return validation;
}

design::FieldValidation* ProfileDialog::validationFor(QWidget* field) const {
    return validations_.value(field, nullptr);
}

void ProfileDialog::showFieldError(QWidget* field, const QString& message) {
    if (auto* validation = validationFor(field))
        validation->setError(message);
    else if (field == name_)
        nameValidation_->setError(message);
    field->setFocus();
    if (auto* scroll = findChild<QScrollArea*>("profileFormScroll"))
        scroll->ensureWidgetVisible(field);
}

void ProfileDialog::showTest(ProfileTestState state, const QString& detail) {
    present(EngineAdapter::profileTestPresentation(state, detail));
}
void ProfileDialog::present(ProfileOperation operation, const QString& detail) {
    present(EngineAdapter::profileOperationPresentation(operation, detail));
}
void ProfileDialog::present(const Presentation& shown) {
    busy_ = shown.busy;
    list_->setEnabled(!busy_);
    form_->setEnabled(!busy_);
    for (auto* action : actions_)
        action->setEnabled(!busy_ && adapter_);
    updateTrustControls();
    if (shown.toast && isVisible()) {
        feedbackToast_ = windowToast(this);
        if (feedbackToast_) {
            statusLine_->clearReport();
            showPresentationToast(this, shown);
            return;
        }
    }
    if (shown.message.isEmpty()) {
        statusLine_->clearReport();
        return;
    }
    statusLine_->setMessage(shown.message);
    statusLine_->setBusy(shown.busy);
    statusLine_->setState(statusLineState(shown.tone));
    statusLine_->show();
}
void ProfileDialog::refresh() {
    if (!adapter_)
        return;
    present(ProfileOperation::Loading);
    adapter_->listProfiles(++token_);
}
void ProfileDialog::selectProfile(const QString& id) {
    if (busy_) {
        pendingSelection_ = id;
        return;
    }
    for (qsizetype row = 0; row < profiles_.size(); ++row) {
        if (profiles_[row].id == id) {
            QSignalBlocker blocker(list_);
            list_->setCurrentRow(static_cast<int>(row));
            setDraft(profiles_[row]);
            return;
        }
    }
}
void ProfileDialog::manageProfile(const QString& id, const QString& action) {
    if (action != "edit" && action != "test" && action != "duplicate" && action != "delete")
        return;
    managedProfile_ = id;
    managedAction_ = action;
    selectProfile(id);
    dispatchProfileAction();
}
void ProfileDialog::dispatchProfileAction() {
    if (busy_ || managedAction_.isEmpty())
        return;
    const auto action = managedAction_;
    managedAction_.clear();
    if (current_.id != managedProfile_) {
        present(ProfileOperation::ProfileMissing);
        return;
    }
    if (action == "edit")
        return;
    const auto id = current_.id;
    QTimer::singleShot(0, this, [this, id, action] {
        if (busy_ || current_.id != id)
            return;
        const auto object = action == "test"        ? "profileTest"
                            : action == "duplicate" ? "profileDuplicate"
                                                    : "profileDelete";
        if (auto* button = findChild<QPushButton*>(object))
            button->click();
    });
}
void ProfileDialog::newProfile() {
    if (discardChanges()) {
        pendingSelection_.clear();
        managedProfile_.clear();
        managedAction_.clear();
        list_->setCurrentRow(-1);
        setDraft({});
    }
}

bool ProfileDialog::discardChanges() {
    if (busy_ || !adapter_)
        return false;
    if (!dirty_)
        return true;
    const auto token = token_;
    const auto revision = revision_;
    if (!confirmDiscardDraft())
        return false;
    return !busy_ && adapter_ && token_ == token && revision_ == revision;
}
}
