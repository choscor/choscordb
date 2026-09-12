#include "widgets/profile_dialog.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>
namespace choscordb {
ProfileDialog::ProfileDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter) {
    setObjectName("profileDialog");
    setWindowTitle(tr("Connection profiles"));
    setModal(false);
    resize(design::dialogInitialSize(design::DialogSize::Profiles));
    auto* outer = new QVBoxLayout(this);
    outer->addWidget(createDescription(
        tr("Save reusable SQLite or PostgreSQL connection details. Passwords use the operating "
           "system credential store."),
        this));
    auto* columns = new QHBoxLayout;
    outer->addLayout(columns);
    list_ = new QListWidget(this);
    list_->setObjectName("profileList");
    list_->setAccessibleName(tr("Saved connection profiles"));
    columns->addWidget(list_, 1);
    form_ = new QWidget(this);
    columns->addWidget(form_, 2);
    auto* formLayout = new QFormLayout(form_);
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
    formLayout->addRow(tr("&Name"), name_);
    driver_ = new QComboBox(form_);
    driver_->setObjectName("profileDriver");
    driver_->addItem(tr("SQLite"), "sqlite");
    driver_->addItem(tr("PostgreSQL"), "postgres");
    formLayout->addRow(tr("&Driver"), driver_);
    sqliteFields_ = new QWidget(form_);
    auto* sqlite = new QFormLayout(sqliteFields_);
    sqlite->setContentsMargins(0, 0, 0, 0);
    path_ = line("profilePath");
    path_->setAccessibleName(tr("Database path"));
    path_->setPlaceholderText(tr("Database path or :memory:"));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(path_);
    auto* browse = new QPushButton(tr("Browse…"), form_);
    pathRow->addWidget(browse);
    auto* pathLabel = new QLabel(tr("Database &path"), sqliteFields_);
    pathLabel->setBuddy(path_);
    sqlite->addRow(pathLabel, pathRow);
    readOnly_ = new QCheckBox(tr("Read only"), form_);
    readOnly_->setObjectName("profileReadOnly");
    sqlite->addRow(readOnly_);
    formLayout->addRow(sqliteFields_);
    postgresFields_ = new QWidget(form_);
    auto* pg = new QFormLayout(postgresFields_);
    pg->setContentsMargins(0, 0, 0, 0);
    host_ = line("profileHost");
    database_ = line("profileDatabase");
    user_ = line("profileUser");
    port_ = new QSpinBox(form_);
    port_->setObjectName("profilePort");
    port_->setRange(1, 65535);
    pg->addRow(tr("&Host"), host_);
    pg->addRow(tr("P&ort"), port_);
    pg->addRow(tr("Data&base"), database_);
    pg->addRow(tr("&User"), user_);
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
    pg->addRow(tr("&TLS"), tls_);
    rootCertificate_ = line("profileRootCertificate");
    pg->addRow(tr("Root &certificate"), rootCertificate_);
    formLayout->addRow(postgresFields_);
    status_ = createInlineStatus(this);
    status_->setObjectName("profileStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    outer->addWidget(status_);
    auto* buttons = new QHBoxLayout;
    outer->addLayout(buttons);
    auto button = [this, buttons](const QString& title, const char* object) {
        auto* result = new QPushButton(title, this);
        result->setObjectName(object);
        buttons->addWidget(result);
        actions_.append(result);
        return result;
    };
    auto* create = button(tr("New"), "profileNew");
    auto* save = button(tr("Save"), "profileSave");
    auto* duplicate = button(tr("Duplicate"), "profileDuplicate");
    auto* remove = button(tr("Delete"), "profileDelete");
    auto* test = button(tr("Test"), "profileTest");
    auto* open = button(tr("Connect"), "profileConnect");
    buttons->addStretch();
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addWidget(close);
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
    connect(create, &QPushButton::clicked, this, [this] {
        if (discardChanges()) {
            list_->setCurrentRow(-1);
            setDraft({});
        }
    });
    connect(save, &QPushButton::clicked, this, [this] { saveDraft(draft()); });
    connect(test, &QPushButton::clicked, this, [this] { testDraft(draft()); });
    connect(open, &QPushButton::clicked, this, [this] {
        if (busy_ || !adapter_)
            return;
        auto value = draft();
        if (value.name.trimmed().isEmpty()) {
            setBusy(false, tr("Enter a profile name."));
            name_->setFocus();
            return;
        }
        if (value.id.isEmpty())
            value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ++token_;
        setBusy(true, tr("Connecting…"));
        connectionSubmissionError_.clear();
        connecting_ = true;
        const bool hasPassword =
            value.driver == "postgres" && (password_->isModified() || !password_->text().isEmpty());
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
        bool savedProfile = false;
        for (const auto& profile : profiles_)
            savedProfile = savedProfile || profile.id == value.id;
        emit connectionSubmitted(value, *id, savedProfile);
    });
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
        QMessageBox confirmation(QMessageBox::Question, tr("Delete profile"),
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
        pendingConnection_.reset();
        if (kind == "connected") {
            setBusy(false, tr("Connected."));
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
    return value;
}
void ProfileDialog::setDraft(const SavedProfile& value) {
    filling_ = true;
    ++revision_;
    current_ = value;
    name_->setText(value.name);
    driver_->setCurrentIndex(value.driver == "postgres" ? 1 : 0);
    path_->setText(value.path);
    readOnly_->setChecked(value.readOnly);
    host_->setText(value.host.isEmpty() ? "localhost" : value.host);
    port_->setValue(value.port ? value.port : 5432);
    database_->setText(value.database);
    user_->setText(value.user);
    const auto tlsIndex = tls_->findData(value.tls);
    tls_->setCurrentIndex(tlsIndex < 0 ? 0 : tlsIndex);
    rootCertificate_->setText(value.rootCertificate);
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
    sqliteFields_->setVisible(sqlite);
    postgresFields_->setVisible(!sqlite);
}
void ProfileDialog::setBusy(bool busy, const QString& message) {
    busy_ = busy;
    list_->setEnabled(!busy);
    form_->setEnabled(!busy);
    for (auto* action : actions_)
        action->setEnabled(!busy && adapter_);
    status_->setText(message);
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
void ProfileDialog::saveDraft(const SavedProfile& profile) {
    if (busy_ || !adapter_)
        return;
    auto value = profile;
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
    if (value.driver == "postgres" && remember)
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
        setBusy(false, tr("Enter a profile name."));
        name_->setFocus();
        return;
    }
    if (value.id.isEmpty())
        value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setBusy(true, tr("Testing connection…"));
    const bool hasPassword =
        value.driver == "postgres" && (password_->isModified() || !password_->text().isEmpty());
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
    const auto response =
        QMessageBox::question(this, tr("Unsaved profile"), tr("Discard changes to this profile?"),
                              QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return response == QMessageBox::Discard && !busy_ && adapter_ && token_ == token &&
           revision_ == revision;
}
} // namespace choscordb
