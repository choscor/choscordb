#include "design_system/theme.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStandardItemModel>
namespace choscordb {
void ProfileDialog::createAuthenticationControls(QFormLayout* form) {
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    authentication_ = new QComboBox(postgresFields_);
    authentication_->setObjectName("profileAuthentication");
    authentication_->addItem(tr("Password / passwordless"), "password");
    authentication_->addItem(tr("PostgreSQL password file"), "pg_pass");
    authentication_->addItem(tr("Password command"), "command");
    authenticationFields_ = new QWidget(postgresFields_);
    auto* fields = new QFormLayout(authenticationFields_);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setRowWrapPolicy(QFormLayout::WrapAllRows);
    const auto changed = [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    };
    pgPassFile_ = new QLineEdit(authenticationFields_);
    pgPassFile_->setObjectName("profilePgPassFile");
    pgPassFile_->setPlaceholderText(
        tr("Optional — PGPASSFILE or the default PostgreSQL password file"));
    pgPassFile_->setMaxLength(16384);
    pgPassFile_->setToolTip(
        tr("Uses the first matching entry. On Unix, the file must be private (0600). "
           "An omitted PostgreSQL username can be resolved by the password file."));
    fields->addRow(tr("Password file"), pgPassFile_);
    connect(pgPassFile_, &QLineEdit::textEdited, this, changed);
    pgPassHostname_ = new QLineEdit(authenticationFields_);
    pgPassHostname_->setObjectName("profilePgPassHostname");
    pgPassHostname_->setMaxLength(16384);
    pgPassHostname_->setPlaceholderText(
        tr("Optional hostname override for password-file matching"));
    pgPassHostname_->setToolTip(
        tr("For a database on SSH localhost, the SSH host is tried before the database host."));
    fields->addRow(tr("Password-file hostname"), pgPassHostname_);
    connect(pgPassHostname_, &QLineEdit::textEdited, this, changed);
    passwordCommand_ = new QPlainTextEdit(authenticationFields_);
    passwordCommand_->setObjectName("profilePasswordCommand");
    passwordCommand_->setMaximumHeight(metrics.dataRowHeight * 3);
    passwordCommand_->setPlaceholderText(tr("Command returning one password on standard output"));
    fields->addRow(tr("Password command"), passwordCommand_);
    connect(passwordCommand_, &QPlainTextEdit::textChanged, this, changed);
    passwordCommandDirectory_ = new QLineEdit(authenticationFields_);
    passwordCommandDirectory_->setObjectName("profilePasswordCommandDirectory");
    passwordCommandDirectory_->setMaxLength(16384);
    passwordCommandDirectory_->setPlaceholderText(tr("Optional working directory"));
    fields->addRow(tr("Working directory"), passwordCommandDirectory_);
    connect(passwordCommandDirectory_, &QLineEdit::textEdited, this, changed);
    passwordCommandTimeout_ = new QSpinBox(authenticationFields_);
    passwordCommandTimeout_->setObjectName("profilePasswordCommandTimeout");
    passwordCommandTimeout_->setRange(1, 300);
    passwordCommandTimeout_->setValue(10);
    passwordCommandTimeout_->setSuffix(tr(" s"));
    fields->addRow(tr("Command timeout"), passwordCommandTimeout_);
    connect(passwordCommandTimeout_, &QSpinBox::valueChanged, this, changed);
    auto* help =
        new QLabel(tr("The command is saved with this profile and runs afresh on Test or Connect. "
                      "Its password output is not saved."),
                   authenticationFields_);
    help->setObjectName("profilePasswordCommandHelp");
    help->setWordWrap(true);
    help->setTextFormat(Qt::PlainText);
    fields->addRow(help);
    int row = 0;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(password_, &row, &role);
    form->insertRow(row, tr("Database authentication"), authentication_);
    form->insertRow(row + 1, authenticationFields_);
    connect(authentication_, &QComboBox::currentIndexChanged, this, [this, changed] {
        if (!filling_) {
            current_.credentialRef.clear();
            password_->clear();
            password_->setModified(false);
            password_->setPlaceholderText(
                tr("Optional — leave blank for passwordless authentication"));
            rememberPassword_->setChecked(false);
            changed();
        }
        updateAuthenticationControls();
    });
}
void ProfileDialog::updateAuthenticationControls() {
    const auto driver = driver_->currentData().toString();
    const auto method = authentication_->currentData().toString();
    const bool pgPass = method == "pg_pass";
    const bool command = method == "command";
    const bool password = method == "password" && driver != "sqlite";
    user_->setPlaceholderText(driver == "mysql" ? tr("Optional — anonymous authentication")
                              : pgPass          ? tr("Optional — resolved from the password file")
                                                : tr("Database username"));
    if (auto* model = qobject_cast<QStandardItemModel*>(authentication_->model()))
        if (auto* item = model->item(authentication_->findData("pg_pass")))
            item->setEnabled(driver == "postgres");
    password_->setEnabled(password);
    rememberPassword_->setEnabled(password);
    authenticationFields_->setVisible(driver != "sqlite" && (pgPass || command));
    auto* fields = qobject_cast<QFormLayout*>(authenticationFields_->layout());
    fields->setRowVisible(pgPassFile_, pgPass);
    fields->setRowVisible(pgPassHostname_, pgPass);
    fields->setRowVisible(passwordCommand_, command);
    fields->setRowVisible(passwordCommandDirectory_, command);
    fields->setRowVisible(passwordCommandTimeout_, command);
    fields->setRowVisible(authenticationFields_->findChild<QLabel*>("profilePasswordCommandHelp"),
                          command);
}
QString ProfileDialog::authenticationMethod(const SavedProfile& profile) {
    return QJsonDocument::fromJson(profile.authentication.toUtf8())
        .object()["method"]
        .toString("password");
}
void ProfileDialog::writeAuthenticationDraft(SavedProfile& profile) const {
    const auto method = profile.driver == "sqlite" ? QStringLiteral("password")
                                                   : authentication_->currentData().toString();
    QJsonObject settings{{"method", method}};
    const auto optional = [](const QString& value) {
        return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
    };
    if (method == "pg_pass") {
        settings.insert("path", optional(pgPassFile_->text()));
        settings.insert("hostname", optional(pgPassHostname_->text()));
    }
    if (method == "command") {
        settings.insert("command", passwordCommand_->toPlainText());
        settings.insert("working_directory", optional(passwordCommandDirectory_->text()));
        settings.insert("timeout_seconds", passwordCommandTimeout_->value());
    }
    if (method != "password")
        profile.credentialRef.clear();
    profile.authentication =
        QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact));
}
void ProfileDialog::setAuthenticationDraft(const SavedProfile& profile) {
    const auto settings = QJsonDocument::fromJson(profile.authentication.toUtf8()).object();
    const auto index = authentication_->findData(authenticationMethod(profile));
    authentication_->setCurrentIndex(index < 0 ? 0 : index);
    pgPassFile_->setText(settings["path"].toString());
    pgPassHostname_->setText(settings["hostname"].toString());
    passwordCommand_->setPlainText(settings["command"].toString());
    passwordCommandDirectory_->setText(settings["working_directory"].toString());
    passwordCommandTimeout_->setValue(settings["timeout_seconds"].toInt(10));
    if (authenticationMethod(profile) != "password") {
        password_->clear();
        password_->setModified(false);
        rememberPassword_->setChecked(false);
    }
    updateAuthenticationControls();
}
} // namespace choscordb
