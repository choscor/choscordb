#include "widgets/profile_dialog/profile_dialog.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
namespace choscordb {
ProfileDialog::ProfileDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter) {
    setObjectName("profileDialog");
    setWindowTitle(tr("New connection"));
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Profiles));
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* outer = new QVBoxLayout(this);
    auto* sections = new design::DialogSections(this);
    sections->setObjectName("profileSections");
    outer->addWidget(sections);
    auto* headerLayout = sections->headerLayout();
    auto* header = sections;
    auto* heading = new design::Text(tr("New connection"), header);
    heading->setObjectName("profileHeading");
    heading->setTypographyRole(design::TypographyRole::DialogTitle);
    headerLayout->addWidget(heading);
    headerLayout->addStretch();
    auto* dismiss = new design::Button({}, header);
    dismiss->setObjectName("profileDismiss");
    dismiss->setAccessibleName(tr("Close connection dialog"));
    dismiss->setVariant(design::ButtonVariant::Ghost);
    dismiss->setButtonSize(design::ButtonSize::IconSmall);
    dismiss->setDesignIcon(design::Icon::Close);
    connect(dismiss, &QPushButton::clicked, this, &QDialog::close);
    headerLayout->addWidget(dismiss);
    // Saved profiles are managed by the sidebar. Retain the internal selection
    // model for the asynchronous profile-management service contract.
    list_ = new QListWidget(this);
    list_->setObjectName("profileList");
    list_->hide();
    auto* formScroll = new QScrollArea(this);
    formScroll->setObjectName("profileFormScroll");
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setWidgetResizable(true);
    formScroll->viewport()->setBackgroundRole(QPalette::Base);
    formScroll->viewport()->setProperty("designSurface", "panel");
    form_ = new QWidget(formScroll);
    // Scroll-area content auto-fills its background. Use the themed panel
    // rather than Window/canvas so open dialogs also follow live previews.
    form_->setBackgroundRole(QPalette::Base);
    form_->setProperty("designSurface", "panel");
    form_->setAttribute(Qt::WA_StyledBackground);
    formScroll->setWidget(form_);
    sections->bodyLayout()->addWidget(formScroll);
    auto* formLayout = new QFormLayout(form_);
    formLayout->setContentsMargins(0, 0, 0, metrics.spacingMedium);
    formLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
    formLayout->addRow(
        createDescription(tr("Connect to a server or open a local database file."), form_));
    driver_ = new QComboBox(form_);
    driver_->setObjectName("profileDriver");
    driver_->addItem(tr("SQLite"), "sqlite");
    driver_->addItem(tr("PostgreSQL"), "postgres");
    driver_->addItem(tr("MySQL"), "mysql");
    driver_->hide();
    auto* drivers = new QHBoxLayout;
    auto* driverGroup = new QButtonGroup(this);
    const auto choice = [&](const QString& title, const char* object, int index) {
        auto* button = new design::Button(title, form_);
        button->setObjectName(object);
        button->setVariant(design::ButtonVariant::Outline);
        button->setButtonContext(design::ButtonContext::Choice);
        button->setDesignIcon(index == 0   ? design::Icon::SQLite
                              : index == 1 ? design::Icon::PostgreSQL
                                           : design::Icon::MySQL);
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        driverGroup->addButton(button, index);
        drivers->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, index] { driver_->setCurrentIndex(index); });
        return button;
    };
    postgresChoice_ = choice(tr("PostgreSQL"), "profileDriverPostgres", 1);
    mysqlChoice_ = choice(tr("MySQL"), "profileDriverMysql", 2);
    sqliteChoice_ = choice(tr("SQLite"), "profileDriverSqlite", 0);
    formLayout->addRow(drivers);
    auto line = [this](const char* object) {
        auto* edit = new QLineEdit(form_);
        edit->setObjectName(object);
        connect(edit, &QLineEdit::textEdited, this, [this] {
            dirty_ = true;
            ++revision_;
        });
        return edit;
    };
    name_ = line("profileName");
    name_->setPlaceholderText(tr("Connection name"));
    nameValidation_ = new design::FieldValidation(name_, form_);
    formLayout->addRow(tr("Connection &name"), nameValidation_);
    connect(name_, &QLineEdit::textChanged, this, [this] { nameValidation_->setError({}); });
    sqliteFields_ = new QWidget(form_);
    auto* sqlite = new QFormLayout(sqliteFields_);
    sqlite->setContentsMargins(0, 0, 0, 0);
    sqlite->setRowWrapPolicy(QFormLayout::WrapAllRows);
    path_ = line("profilePath");
    path_->setAccessibleName(tr("Database path"));
    path_->setPlaceholderText(tr("Database path or :memory:"));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(path_);
    auto* browse = new design::Button(tr("Browse…"), form_);
    browse->setVariant(design::ButtonVariant::Outline);
    pathRow->addWidget(browse);
    auto* pathLabel = new QLabel(tr("Database &file"), sqliteFields_);
    pathLabel->setBuddy(path_);
    sqlite->addRow(pathLabel, pathRow);
    readOnly_ = new QCheckBox(tr("Open read-only"), form_);
    readOnly_->setProperty("designRole", "switch");
    readOnly_->setObjectName("profileReadOnly");
    sqlite->addRow(readOnly_);
    sqlite->addRow(createDescription(tr("Browse safely without modifying the file."), form_));
    formLayout->addRow(sqliteFields_);
    postgresFields_ = new QWidget(form_);
    auto* pg = new QFormLayout(postgresFields_);
    pg->setContentsMargins(0, 0, 0, 0);
    pg->setRowWrapPolicy(QFormLayout::WrapAllRows);
    host_ = line("profileHost");
    database_ = line("profileDatabase");
    user_ = line("profileUser");
    port_ = new QSpinBox(form_);
    port_->setObjectName("profilePort");
    port_->setRange(1, 65535);
    auto* serverFields = new QGridLayout;
    const auto serverField = [&](const QString& title, QWidget* widget, int row, int column) {
        auto* field = new QVBoxLayout;
        auto* label = new QLabel(title, postgresFields_);
        label->setBuddy(widget);
        field->addWidget(label);
        field->addWidget(widget);
        serverFields->addLayout(field, row, column);
    };
    serverField(tr("&Host"), host_, 0, 0);
    serverField(tr("P&ort"), port_, 0, 1);
    serverField(tr("Data&base"), database_, 1, 0);
    serverField(tr("&Username"), user_, 1, 1);
    pg->addRow(serverFields);
    password_ = line("profilePassword");
    password_->setEchoMode(QLineEdit::Password);
    password_->setMaxLength(16384);
    pg->addRow(tr("&Password"), password_);
    rememberPassword_ = new QCheckBox(tr("Save password in OS credential store"), form_);
    rememberPassword_->setObjectName("profileRememberPassword");
    pg->addRow(rememberPassword_);
    connect(rememberPassword_, &QCheckBox::toggled, this, [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    tls_ = new QComboBox(form_);
    tls_->setObjectName("profileTls");
    tls_->addItem(tr("Verify server identity"), "verify_full");
    tls_->addItem(tr("Disable TLS"), "disable");
    auto* security = new QWidget(postgresFields_);
    auto* securityLayout = new QFormLayout(security);
    securityLayout->setContentsMargins(0, 0, 0, 0);
    securityLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
    securityLayout->addRow(tr("&TLS"), tls_);
    rootCertificate_ = line("profileRootCertificate");
    securityLayout->addRow(tr("Root &certificate"), rootCertificate_);
    auto* securityToggle = new design::Button(tr("TLS && security"), postgresFields_);
    securityToggle->setObjectName("profileSecurity");
    securityToggle->setVariant(design::ButtonVariant::Ghost);
    securityToggle->setCheckable(true);
    securityToggle->setDesignIcon(design::Icon::ChevronRight);
    connect(securityToggle, &QPushButton::toggled, security, &QWidget::setVisible);
    connect(securityToggle, &QPushButton::toggled, this, [securityToggle](bool expanded) {
        securityToggle->setDesignIcon(expanded ? design::Icon::ChevronDown
                                               : design::Icon::ChevronRight);
    });
    pg->addRow(securityToggle);
    pg->addRow(security);
    security->hide();
    sshEnabled_ = new QCheckBox(tr("Connect through SSH tunnel"), postgresFields_);
    sshEnabled_->setObjectName("profileSshEnabled");
    sshEnabled_->setProperty("designRole", "switch");
    pg->addRow(sshEnabled_);
    auto* sshFields = new QWidget(postgresFields_);
    sshFields_ = sshFields;
    auto* sshLayout = new QFormLayout(sshFields);
    sshLayout->setContentsMargins(0, 0, 0, 0);
    sshLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
    sshHost_ = line("profileSshHost");
    sshUser_ = line("profileSshUser");
    sshIdentityFile_ = line("profileSshIdentityFile");
    sshIdentityFile_->setPlaceholderText(tr("Optional — use SSH agent or default keys"));
    sshPort_ = new QSpinBox(sshFields);
    sshPort_->setObjectName("profileSshPort");
    sshPort_->setRange(1, 65535);
    sshLayout->addRow(tr("SSH host"), sshHost_);
    sshLayout->addRow(tr("SSH port"), sshPort_);
    sshLayout->addRow(tr("SSH username"), sshUser_);
    sshLayout->addRow(tr("SSH private key file"), sshIdentityFile_);
    sshLayout->addRow(createDescription(
        tr("Uses system OpenSSH with your SSH agent or key file. The server must already be "
           "trusted in SSH known hosts. Unlock encrypted keys in your agent first. "
           "The database host and port are reached from the SSH server."),
        sshFields));
    pg->addRow(sshFields);
    connect(sshEnabled_, &QCheckBox::toggled, sshFields, &QWidget::setVisible);
    connect(sshEnabled_, &QCheckBox::toggled, this, [this] {
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
    sshFields->hide();
    formLayout->addRow(postgresFields_);
    status_ = createInlineStatus(this);
    status_->setObjectName("profileStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    status_->hide();
    formLayout->addRow(status_);
    auto* buttons = sections->footerLayout();
    auto* footer = sections;
    auto button = [this](const QString& title, const char* object) {
        auto* result = new design::Button(title, this);
        result->setObjectName(object);
        result->setVariant(design::ButtonVariant::Outline);
        actions_.append(result);
        return result;
    };
    auto* create = button(tr("New"), "profileNew");
    auto* save = button(tr("Save profile"), "profileSave");
    auto* duplicate = button(tr("Duplicate"), "profileDuplicate");
    auto* remove = button(tr("Delete"), "profileDelete");
    auto* test = button(tr("Test connection"), "profileTest");
    auto* open = button(tr("Connect"), "profileConnect");
    auto* saveConnect = button(tr("Save && connect"), "profileSaveConnect");
    for (auto* action : {create, duplicate, remove, open})
        action->hide();
    save->setVariant(design::ButtonVariant::Outline);
    remove->setVariant(design::ButtonVariant::Destructive);
    saveConnect->setVariant(design::ButtonVariant::Default);
    buttons->addWidget(test);
    buttons->addStretch();
    auto* close = new design::Button(tr("Cancel"), footer);
    close->setObjectName("profileCancel");
    close->setVariant(design::ButtonVariant::Outline);
    buttons->addWidget(close);
    buttons->addWidget(save);
    buttons->addWidget(saveConnect);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
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
        if (!filling_ && driver_->currentData() != "sqlite" &&
            (port_->value() == 5432 || port_->value() == 3306))
            port_->setValue(driver_->currentData() == "mysql" ? 3306 : 5432);
        updateDriver();
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
    connect(tls_, &QComboBox::currentIndexChanged, this, [this] {
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
    });
    connect(test, &QPushButton::clicked, this, [this] { testDraft(draft()); });
    connect(open, &QPushButton::clicked, this, [this] { connectDraft(false); });
    connect(duplicate, &QPushButton::clicked, this, [this] {
        if (busy_ || !adapter_ || current_.id.isEmpty())
            return;
        setBusy(true, tr("Duplicating profile…"));
        adapter_->duplicateProfile(current_.id, QUuid::createUuid().toString(QUuid::WithoutBraces),
                                   name_->text() + tr(" copy"), ++token_);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (busy_ || !adapter_ || current_.id.isEmpty())
            return;
        const auto id = current_.id;
        const auto token = token_;
        const auto revision = revision_;
        ConfirmationDialog confirmation(QMessageBox::Question, tr("Delete profile"),
                                        tr("Delete saved profile “%1”?").arg(current_.name),
                                        QMessageBox::Yes | QMessageBox::Cancel, this);
        confirmation.setTextFormat(Qt::PlainText);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Yes)
            return;
        if (busy_ || !adapter_ || current_.id != id || token_ != token || revision_ != revision)
            return;
        setBusy(true, tr("Deleting profile…"));
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
    connect(adapter, &EngineAdapter::profilesReady, this,
            [this](quint64 token, const QList<SavedProfile>& profiles) {
                if (token != token_)
                    return;
                profiles_ = profiles;
                QSignalBlocker blocker(list_);
                list_->clear();
                for (const auto& profile : profiles_)
                    list_->addItem(profile.name);
                setBusy(false, refreshNotice_);
                if (isVisible() && focusWidget() == findChild<QPushButton*>("profileDismiss"))
                    name_->setFocus(Qt::OtherFocusReason);
                refreshNotice_.clear();
                const auto sessionPassword =
                    preservePasswordOnRefresh_ ? password_->text() : QString();
                const bool passwordModified = password_->isModified();
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
                preservePasswordOnRefresh_ = savingDraft_ && !rememberPassword_->isChecked();
                const auto sessionPassword =
                    preservePasswordOnRefresh_ ? password_->text() : QString();
                const bool passwordModified = password_->isModified();
                savingDraft_ = false;
                setDraft(profile);
                if (preservePasswordOnRefresh_) {
                    password_->setText(sessionPassword);
                    password_->setModified(passwordModified);
                }
                refreshNotice_ =
                    warning.isEmpty() ? tr("Profile saved.") : tr("Profile saved. %1").arg(warning);
                pendingSelection_ = profile.id;
                refresh();
            });
    connect(adapter, &EngineAdapter::profileDeleted, this,
            [this](quint64 token, const QString&, const QString& warning) {
                if (token != token_)
                    return;
                setDraft({});
                refreshNotice_ = warning.isEmpty() ? tr("Profile deleted.")
                                                   : tr("Profile deleted. %1").arg(warning);
                refresh();
            });
    connect(
        adapter, &EngineAdapter::profileFailed, this, [this](quint64 token, const QString& error) {
            if (token == token_) {
                savingDraft_ = false;
                preservePasswordOnRefresh_ = false;
                connectAfterSave_ = false;
                setBusy(false, refreshNotice_.isEmpty()
                                   ? error
                                   : tr("%1 List refresh failed: %2").arg(refreshNotice_, error));
                refreshNotice_.clear();
            }
        });
    connect(adapter, &EngineAdapter::profileTested, this, [this](quint64 token) {
        if (token == token_)
            setBusy(false, tr("Connection test succeeded."));
    });
    connect(adapter, &EngineAdapter::profileConnectFailed, this, [this](const QString& error) {
        if (connecting_)
            connectionSubmissionError_ = error;
    });
    connect(adapter, &EngineAdapter::eventReady, this, [this](const BridgeEvent& event) {
        if (!pendingConnection_ || event.id != *pendingConnection_)
            return;
        const auto kind =
            QString::fromUtf8(event.kind.data(), static_cast<qsizetype>(event.kind.size()));
        if (kind != "connected" && kind != "connection_failed" && kind != "disconnected")
            return;
        const auto connection = *pendingConnection_;
        pendingConnection_.reset();
        if (kind == "connected") {
            setBusy(false, tr("Connected."));
            if (openQueryAfterConnect_) {
                openQueryAfterConnect_ = false;
                emit openQueryRequested(connection);
                accept();
            }
        } else if (kind == "disconnected") {
            setBusy(false, tr("Connection closed."));
        } else {
            auto message =
                QString::fromUtf8(event.error.data(), static_cast<qsizetype>(event.error.size()));
            if (!event.vendor_code.empty()) {
                message +=
                    tr(" [Code: %1]")
                        .arg(QString::fromUtf8(event.vendor_code.data(),
                                               static_cast<qsizetype>(event.vendor_code.size())));
            }
            setBusy(false, message);
        }
    });
    setDraft({});
    refresh();
}
void ProfileDialog::showEvent(QShowEvent* event) {
    DialogShell::showEvent(event);
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSpacing(0);
    findChild<design::DialogSections*>("profileSections")->applyCompactSpacing();
}
void ProfileDialog::connectDraft(bool openQuery) {
    if (busy_ || !adapter_)
        return;
    auto value = draft();
    if (value.name.trimmed().isEmpty()) {
        nameValidation_->setError(tr("Enter a profile name."));
        name_->setFocus();
        return;
    }
    if (value.id.isEmpty())
        value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ++token_;
    setBusy(true, tr("Connecting…"));
    connectionSubmissionError_.clear();
    connecting_ = true;
    const bool hasPassword = (value.driver == "postgres" || value.driver == "mysql") &&
                             (password_->isModified() || !password_->text().isEmpty());
    const auto id = adapter_->connectProfileWithPassword(
        value, hasPassword ? password_->text() : QString(), hasPassword);
    connecting_ = false;
    if (!id) {
        setBusy(false, connectionSubmissionError_.isEmpty()
                           ? tr("Connection could not be submitted.")
                           : connectionSubmissionError_);
        return;
    }
    pendingConnection_ = id;
    openQueryAfterConnect_ = openQuery;
    bool savedProfile = false;
    for (const auto& profile : profiles_)
        savedProfile = savedProfile || profile.id == value.id;
    emit connectionSubmitted(value, *id, savedProfile);
}
SavedProfile ProfileDialog::draft() const {
    auto value = current_;
    value.name = name_->text();
    value.driver = driver_->currentData().toString();
    value.path = path_->text();
    value.readOnly = readOnly_->isChecked();
    value.host = host_->text();
    value.port = static_cast<quint16>(port_->value());
    value.database = database_->text();
    value.user = user_->text();
    value.tls = tls_->currentData().toString();
    value.rootCertificate = rootCertificate_->text();
    value.sshEnabled =
        (value.driver == "postgres" || value.driver == "mysql") && sshEnabled_->isChecked();
    value.sshHost = sshHost_->text();
    value.sshPort = static_cast<quint16>(sshPort_->value());
    value.sshUser = sshUser_->text();
    value.sshIdentityFile = sshIdentityFile_->text();
    return value;
}
void ProfileDialog::setDraft(const SavedProfile& value) {
    filling_ = true;
    ++revision_;
    current_ = value;
    name_->setText(value.name);
    driver_->setCurrentIndex(driver_->findData(value.driver));
    path_->setText(value.path);
    readOnly_->setChecked(value.readOnly);
    host_->setText(value.host.isEmpty() ? "localhost" : value.host);
    port_->setValue(value.port ? value.port : value.driver == "mysql" ? 3306 : 5432);
    database_->setText(value.database);
    user_->setText(value.user);
    const auto tlsIndex = tls_->findData(value.tls);
    tls_->setCurrentIndex(tlsIndex < 0 ? 0 : tlsIndex);
    rootCertificate_->setText(value.rootCertificate);
    sshEnabled_->setChecked(value.sshEnabled);
    sshHost_->setText(value.sshHost);
    sshPort_->setValue(value.sshPort ? value.sshPort : 22);
    sshUser_->setText(value.sshUser);
    sshIdentityFile_->setText(value.sshIdentityFile);
    password_->clear();
    password_->setModified(false);
    password_->setPlaceholderText(value.credentialRef.isEmpty()
                                      ? tr("Session password")
                                      : tr("Saved password — leave unchanged to keep"));
    rememberPassword_->setChecked(!value.credentialRef.isEmpty());
    filling_ = false;
    dirty_ = false;
    updateDriver();
}
void ProfileDialog::updateDriver() {
    const bool sqlite = driver_->currentData().toString() == "sqlite";
    sqliteChoice_->setChecked(sqlite);
    const bool mysql = driver_->currentData().toString() == "mysql";
    postgresChoice_->setChecked(!sqlite && !mysql);
    mysqlChoice_->setChecked(mysql);
    sshEnabled_->setVisible(!sqlite);
    sshFields_->setVisible(!sqlite && sshEnabled_->isChecked());

    sqliteFields_->setVisible(sqlite);
    postgresFields_->setVisible(!sqlite);
}
void ProfileDialog::setBusy(bool busy, const QString& message) {
    if (busy)
        progressToast(this)->showProgress(tr("Profiles"), message);
    else
        clearProgressToast(this);
    busy_ = busy;
    list_->setEnabled(!busy);
    form_->setEnabled(!busy);
    for (auto* action : actions_)
        action->setEnabled(!busy && adapter_);
    status_->setText(busy ? QString() : message);
    status_->setVisible(!busy && !message.isEmpty());
}
void ProfileDialog::refresh() {
    if (!adapter_)
        return;
    setBusy(true, tr("Loading profiles…"));
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
        setBusy(false, tr("The saved profile is no longer available. Refresh the profile list."));
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
void ProfileDialog::saveDraft(const SavedProfile& profile) {
    if (busy_ || !adapter_)
        return;
    auto value = profile;
    if (value.name.trimmed().isEmpty()) {
        nameValidation_->setError(tr("Enter a profile name."));
        name_->setFocus();
        return;
    }
    if (value.id.isEmpty())
        value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto password = password_->text();
    const bool modified = password_->isModified();
    const bool remember = rememberPassword_->isChecked();
    setDraft(value);
    password_->setText(password);
    password_->setModified(modified);
    rememberPassword_->setChecked(remember);
    dirty_ = true;
    ++revision_;
    QString action = "clear";
    if ((value.driver == "postgres" || value.driver == "mysql") && remember)
        action =
            modified || !password.isEmpty() || value.credentialRef.isEmpty() ? "replace" : "keep";
    savingDraft_ = true;
    setBusy(true, tr("Saving profile…"));
    adapter_->saveProfileWithPassword(value, action == "replace" ? password : QString(), action,
                                      ++token_);
}
void ProfileDialog::testDraft(const SavedProfile& profile) {
    if (busy_ || !adapter_)
        return;
    auto value = profile;
    if (value.name.trimmed().isEmpty()) {
        nameValidation_->setError(tr("Enter a profile name."));
        name_->setFocus();
        return;
    }
    if (value.id.isEmpty())
        value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setBusy(true, tr("Testing connection…"));
    const bool hasPassword = (value.driver == "postgres" || value.driver == "mysql") &&
                             (password_->isModified() || !password_->text().isEmpty());
    adapter_->testProfileWithPassword(value, hasPassword ? password_->text() : QString(),
                                      hasPassword, ++token_);
}
bool ProfileDialog::discardChanges() {
    if (busy_ || !adapter_)
        return false;
    if (!dirty_)
        return true;
    const auto token = token_;
    const auto revision = revision_;
    const auto response = ConfirmationDialog::question(
        this, tr("Unsaved profile"), tr("Discard changes to this profile?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return response == QMessageBox::Discard && !busy_ && adapter_ && token_ == token &&
           revision_ == revision;
}
} // namespace choscordb
