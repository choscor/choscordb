#include "tools/preview/preview_dialogs.h"

#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/modal_panel/modal_panel.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace choscordb::design::preview_detail {
void applyHostTheme(QWidget& surface, const QWidget& host) {
    for (auto* ancestor = &host; ancestor; ancestor = ancestor->parentWidget()) {
        if (auto* manager =
                ancestor->findChild<ThemeManager*>(QString{}, Qt::FindDirectChildrenOnly)) {
            manager->applyTo(surface);
            return;
        }
    }
}

void populateDialogSections(QWidget* host, QVBoxLayout* layout) {
    layout->addWidget(new QLabel("Open the connection layout in either theme.", host));
    auto* open = new Button("Open sectioned modal", host);
    open->setObjectName("previewOpenDialogSections");
    layout->addWidget(open);
    auto* dialog = new ModalDialog(host);
    dialog->setObjectName("previewDialogSectionsModal");
    open->setProperty("previewSurface", QVariant::fromValue<QObject*>(dialog));
    applyHostTheme(*dialog, *host);
    dialog->resize(dialogInitialSize(DialogSize::Short));
    auto* sections = dialog->createSections();
    sections->setObjectName("previewDialogSections");
    auto* heading = new Text("New connection", sections);
    heading->setTextStyle(TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* close = sections->addDismissButton("Close sectioned modal");
    close->setObjectName("previewDialogSectionsDismiss");
    auto* description = new QLabel("Connect to a server or open a local database file.", sections);
    description->setProperty("designRole", "description");
    sections->bodyLayout()->addWidget(description);
    auto* form = sections->addForm();
    auto* hostField = new QLineEdit(sections);
    hostField->setObjectName("previewProfileHost");
    hostField->setText("db.example.com");
    sections->addFormRow(form, "Host", hostField);
    auto* port = new QSpinBox(sections);
    port->setObjectName("previewProfilePort");
    port->setRange(1, 65535);
    port->setValue(5432);
    port->setButtonSymbols(QAbstractSpinBox::NoButtons);
    sections->addFormRow(form, "Port", port);
    auto* database = new QLineEdit(sections);
    database->setObjectName("previewProfileDatabase");
    database->setText("app");
    sections->addFormRow(form, "Database", database, FieldNeed::Optional);
    auto* username = new QLineEdit(sections);
    username->setObjectName("previewProfileUsername");
    username->setText("operator");
    sections->addFormRow(form, "Username", username);
    auto* password = new QLineEdit(sections);
    password->setObjectName("previewProfilePassword");
    password->setEchoMode(QLineEdit::Password);
    sections->addFormRow(form, "Password", password, FieldNeed::Optional);
    auto* saveCredentials = new QCheckBox("Save credentials in OS credential store", sections);
    saveCredentials->setObjectName("previewProfileSaveCredentials");
    form->addRow(QString(), saveCredentials);
    sections->bodyLayout()->addStretch();
    auto* test = sections->addAction(DialogActionRole::Utility, "Test connection");
    test->setObjectName("previewProfileTest");
    auto* cancel = sections->addAction(DialogActionRole::Dismiss, "Cancel");
    auto* save = sections->addAction(DialogActionRole::Primary, "Save connection");
    QObject::connect(open, &QPushButton::clicked, dialog, &QDialog::open);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, dialog, &QDialog::accept);
    auto* openProgress = new Button("Open progress modal", host);
    openProgress->setObjectName("previewOpenDialogSectionsProgress");
    layout->addWidget(openProgress);
    auto* progressDialog = new ModalDialog(host);
    openProgress->setProperty("previewSurface", QVariant::fromValue<QObject*>(progressDialog));
    applyHostTheme(*progressDialog, *host);
    auto* progressSections = progressDialog->createSections();
    progressSections->setObjectName("previewDialogSectionsProgress");
    auto* progressHeading = new Text("Edit connection", progressSections);
    progressHeading->setTextStyle(TextStyle::Headline);
    progressSections->headerLayout()->addWidget(progressHeading);
    auto* progressForm = progressSections->addForm();
    auto* progressName = new QLineEdit("Analytics", progressSections);
    progressSections->addFormRow(progressForm, "Name", progressName);
    auto* progressStatus = progressSections->addStatusLine();
    progressStatus->setObjectName("previewDialogSectionsStatus");
    progressStatus->showProgress("Saving connection…");
    auto* progressCancel = progressSections->addAction(DialogActionRole::Dismiss, "Cancel");
    QObject::connect(progressCancel, &QPushButton::clicked, progressDialog, &QDialog::reject);
    QObject::connect(openProgress, &QPushButton::clicked, progressDialog, &QDialog::open);
    layout->addStretch();
}
}
