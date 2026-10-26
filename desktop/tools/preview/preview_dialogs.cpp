#include "tools/preview/preview_dialogs.h"

#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/modal_panel/modal_panel.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
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
    dialog->resize(560, 440);
    auto* sections = dialog->createSections();
    sections->setObjectName("previewDialogSections");
    auto* heading = new Text("New connection", sections);
    heading->setTextStyle(TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* close = sections->addDismissButton("Close sectioned modal");
    close->setObjectName("previewDialogSectionsDismiss");
    auto* description = new QLabel("Connect to a server or open a local database file.", sections);
    sections->bodyLayout()->addWidget(description);
    auto* fields = new QGridLayout;
    fields->setColumnStretch(0, 5);
    fields->setColumnStretch(1, 1);
    fields->setColumnStretch(2, 5);
    const auto field = [sections, fields](const QString& title, QWidget* control, int row,
                                          int column, int span = 1) {
        auto* group = new QVBoxLayout;
        auto* label = new QLabel(title, sections);
        label->setBuddy(control);
        group->addWidget(label);
        group->addWidget(control);
        fields->addLayout(group, row, column, 1, span);
    };
    auto* hostField = new QLineEdit(sections);
    hostField->setObjectName("previewProfileHost");
    hostField->setText("db.example.com");
    field("Host", hostField, 0, 0);
    auto* port = new QSpinBox(sections);
    port->setObjectName("previewProfilePort");
    port->setRange(1, 65535);
    port->setValue(5432);
    field("Port", port, 0, 1);
    auto* database = new QLineEdit(sections);
    database->setObjectName("previewProfileDatabase");
    database->setText("app");
    field("Database", database, 0, 2);
    auto* username = new QLineEdit(sections);
    username->setObjectName("previewProfileUsername");
    username->setText("operator");
    field("Username", username, 1, 0);
    auto* password = new QLineEdit(sections);
    password->setObjectName("previewProfilePassword");
    password->setEchoMode(QLineEdit::Password);
    field("Password", password, 1, 1, 2);
    sections->bodyLayout()->addLayout(fields);
    auto* saveCredentials = new QCheckBox("Save credentials in OS credential store", sections);
    saveCredentials->setObjectName("previewProfileSaveCredentials");
    sections->bodyLayout()->addWidget(saveCredentials);
    sections->bodyLayout()->addStretch();
    sections->footerLayout()->addStretch();
    auto* cancel = new Button("Cancel", sections);
    cancel->setVariant(ButtonVariant::Outline);
    sections->footerLayout()->addWidget(cancel);
    auto* save = new Button("Save profile", sections);
    sections->footerLayout()->addWidget(save);
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
    auto* progressHeading = new Text("Profiles", progressSections);
    progressHeading->setTextStyle(TextStyle::Headline);
    progressSections->headerLayout()->addWidget(progressHeading);
    progressSections->bodyLayout()->setSpacing(spacing(Spacing::Two));
    progressSections->bodyLayout()->addWidget(
        new QLabel("Saving connection profile…", progressSections));
    auto* progress = new QProgressBar(progressSections);
    progress->setRange(0, 0);
    progress->setTextVisible(false);
    progressSections->bodyLayout()->addWidget(progress);
    QObject::connect(openProgress, &QPushButton::clicked, progressDialog, &QDialog::open);
    layout->addStretch();
}
} // namespace choscordb::design::preview_detail
