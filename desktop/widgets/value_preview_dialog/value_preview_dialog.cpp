#include "widgets/value_preview_dialog/value_preview_dialog.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace choscordb {
ValuePreviewDialog::ValuePreviewDialog(const ValuePreview& preview, const QByteArray& bytes,
                                       QWidget* parent)
    : DialogShell(parent) {
    setObjectName("valuePreviewDialog");
    setWindowTitle(tr("Value preview"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(design::dialogInitialSize(design::DialogSize::Detail));
    auto* sections = createSections();
    auto* heading = new design::Text(windowTitle(), sections);
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close value preview"));
    connect(dismiss, &QPushButton::clicked, this, &ValuePreviewDialog::reject);
    auto* body = sections->bodyLayout();
    auto* summary = createDescription(preview.summary, sections);
    summary->setObjectName("valuePreviewSummary");
    summary->setWordWrap(true);
    body->addWidget(summary);
    const auto addText = [this, body, sections](const QString& text) {
        text_ = new QPlainTextEdit(text, sections);
        text_->setObjectName("valuePreviewText");
        text_->setReadOnly(true);
        text_->setProperty("designRole", "codePreview");
        text_->setFont(
            design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
        body->addWidget(text_, 1);
    };
    const auto addImage = [body, sections](const QImage& image, const QString& name) {
        auto* picture = new QLabel(sections);
        picture->setObjectName("valuePreviewImage");
        picture->setPixmap(QPixmap::fromImage(image));
        picture->setAccessibleName(name);
        auto* scroll = new QScrollArea(sections);
        scroll->setObjectName("valuePreviewImageScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setAlignment(Qt::AlignCenter);
        scroll->setWidget(picture);
        body->addWidget(scroll, 1);
    };
    if (preview.kind == QStringLiteral("json")) {
        auto* tree = new QTreeWidget(sections);
        tree->setObjectName("valuePreviewTree");
        tree->setHeaderLabels({tr("Key"), tr("Value"), tr("Type")});
        tree->setUniformRowHeights(true);
        QList<QTreeWidgetItem*> items;
        items.reserve(preview.nodes.size());
        for (const auto& node : preview.nodes) {
            auto* parentItem =
                node.parent >= 0 && node.parent < items.size() ? items.at(node.parent) : nullptr;
            auto* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree);
            item->setText(0, node.key);
            item->setText(1, node.value);
            item->setText(2, node.kind);
            items.append(item);
        }
        if (!items.isEmpty())
            items.first()->setExpanded(true);
        tree->header()->setSectionResizeMode(QHeaderView::Interactive);
        body->addWidget(tree, 1);
        addText(preview.pretty);
        auto* pretty = sections->addAction(design::DialogActionRole::Utility, tr("Pretty"));
        pretty->setObjectName("valuePreviewPretty");
        auto* minify = sections->addAction(design::DialogActionRole::Utility, tr("Minify"));
        minify->setObjectName("valuePreviewMinify");
        connect(pretty, &QPushButton::clicked, this,
                [this, text = preview.pretty] { text_->setPlainText(text); });
        connect(minify, &QPushButton::clicked, this,
                [this, text = preview.minified] { text_->setPlainText(text); });
    } else if (preview.kind == QStringLiteral("geometry")) {
        addText(preview.pretty);
    } else if (preview.kind == QStringLiteral("image")) {
        const auto image = QImage::fromData(bytes);
        if (image.isNull()) {
            summary->setText(tr("The %1 image data cannot be decoded.").arg(preview.imageFormat));
            summary->setProperty("designRole", "fieldError");
        } else {
            addImage(image, tr("%1 image, %2 × %3")
                                .arg(preview.imageFormat)
                                .arg(image.width())
                                .arg(image.height()));
        }
    } else if (preview.kind == QStringLiteral("pdf") && !preview.pageImage.isEmpty()) {
        const auto page = QImage::fromData(preview.pageImage, "PNG");
        addImage(page, tr("PDF page 1, %1 × %2").arg(page.width()).arg(page.height()));
    }
    auto* close = sections->addAction(design::DialogActionRole::Dismiss, tr("Close"));
    close->setObjectName("valuePreviewClose");
    connect(close, &QPushButton::clicked, this, &ValuePreviewDialog::reject);
}
}
