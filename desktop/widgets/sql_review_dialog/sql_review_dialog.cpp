#include "widgets/sql_review_dialog/sql_review_dialog.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/fonts/fonts.h"
#include "design_system/metrics/metrics.h"
#include "design_system/modal_panel/modal_panel.h"
#include "design_system/text/text.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace choscordb {
bool confirmSqlReview(QWidget* parent, const SqlReview& review) {
    design::ModalDialog box(parent);
    box.setWindowTitle(review.title);
    auto* sections = box.createSections();
    auto* heading = new design::Text(review.title, sections);
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(review.dismiss);
    dismiss->setObjectName(review.objectPrefix + QStringLiteral("Dismiss"));
    QObject::connect(dismiss, &QPushButton::clicked, &box, &QDialog::reject);
    auto* layout = sections->bodyLayout();
    auto* description = new QLabel(review.description, sections);
    description->setProperty("designRole", "description");
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* preview = new QPlainTextEdit(review.sql, sections);
    preview->setObjectName(review.objectPrefix + QStringLiteral("Review"));
    preview->setReadOnly(true);
    preview->setProperty("designRole", "codePreview");
    preview->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    layout->addWidget(preview);
    auto* cancel = sections->addAction(design::DialogActionRole::Dismiss, QObject::tr("Cancel"));
    cancel->setObjectName(review.objectPrefix + QStringLiteral("Cancel"));
    auto* apply = sections->addAction(design::DialogActionRole::Primary, review.accept);
    apply->setObjectName(review.objectPrefix + QStringLiteral("Apply"));
    QObject::connect(cancel, &QPushButton::clicked, &box, &QDialog::reject);
    QObject::connect(apply, &QPushButton::clicked, &box, &QDialog::accept);
    box.resize(design::dialogInitialSize(design::DialogSize::Detail));
    return box.exec() == QDialog::Accepted;
}
}
