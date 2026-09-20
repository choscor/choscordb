#include "tools/preview/preview_window.h"

#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dock/dock_style.h"
#include "design_system/icons.h"
#include "design_system/menu/menu.h"
#include "design_system/metrics/metrics.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/modal_panel/modal_panel.h"
#include "design_system/text/text.h"
#include "design_system/text_area/text_area_style.h"
#include "design_system/theme_manager.h"
#include "design_system/table/table_style.h"
#include "design_system/toast_region/toast_region.h"

#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHelpEvent>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTextEdit>

#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSysInfo>
#include <QTableView>
#include <QTableWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace choscordb::design {
namespace {
struct Specimen {
    QString section;
    QString id;
    QString title;
    QString source;
};
QList<Specimen> specimens() {
    return {
        {"Tokens", "tokens", "Semantic colors and geometry",
         "desktop/design_system/tokens/tokens.cpp"},
        {"Typography", "typography", "Type roles and Unicode",
         "desktop/design_system/text/text.cpp"},
        {"Icons", "icons", "Lucide names, sizes and alignment", "desktop/design_system/icons.cpp"},
        {"Components", "buttons", "Buttons: variants, sizes and states",
         "desktop/design_system/button/button.cpp"},
        {"Components", "tool-buttons", "Tool buttons and dropdowns",
         "desktop/design_system/tool_button/tool_button_style.cpp"},
        {"Components", "button-groups", "Button groups",
         "desktop/design_system/button_group/button_group.cpp"},
        {"Components", "fields", "Text and password fields",
         "desktop/design_system/field/field_style.cpp"},
        {"Components", "numeric-fields", "Numeric fields",
         "desktop/design_system/spin_box/spin_box_style.cpp"},
        {"Components", "textareas", "Text areas and diagnostics",
         "desktop/design_system/text_area/text_area_style.cpp"},
        {"Components", "selects", "Selects and popup rows",
         "desktop/design_system/select/select_popup.cpp"},
        {"Components", "checks-toggles", "Checks and toggles",
         "desktop/design_system/checkbox/checkbox_indicator.cpp"},
        {"Components", "shortcuts", "Shortcut entry", "desktop/design_system/field/field_style.cpp"},
        {"Components", "lists-navigation", "Lists and navigation",
         "desktop/design_system/tree/tree_style.cpp"},
        {"Components", "navigation-profile-row", "Saved connection rows",
         "desktop/design_system/navigation_profile_row/navigation_profile_row.cpp"},
        {"Components", "dock", "Dock panel", "desktop/design_system/dock/dock_style.cpp"},
        {"Components", "tabs", "Tabs with close and overflow",
         "desktop/design_system/tabs/tabs_style.cpp"},
        {"Components", "scrolling", "Scroll areas and scrollbars",
         "desktop/design_system/scrollbar/scrollbar_style.cpp"},
        {"Components", "separators-splitters", "Separators and splitters",
         "desktop/design_system/splitter/splitter_style.cpp"},
        {"Components", "tables", "Table headers, cells and selection",
         "desktop/design_system/table/table_style.cpp"},
        {"Components", "tooltip-popover", "Tooltips and popovers",
         "desktop/design_system/tooltip/tooltip.cpp"},
        {"Components", "dialog-sections", "Dialog header, body and footer",
         "desktop/design_system/dialog_sections/dialog_sections.cpp"},
        {"Components", "dialogs", "Modal panel",
         "desktop/design_system/modal_panel/modal_panel.cpp"},
        {"Components", "nonmodal", "Nonmodal content",
         "desktop/design_system/dialog_shell/dialog_shell.cpp"},
        {"Components", "confirmations", "Destructive confirmations",
         "desktop/design_system/confirmation_dialog/confirmation_dialog.cpp"},
        {"Components", "menus", "Menus and submenus", "desktop/design_system/menu/menu.cpp"},
        {"Components", "feedback", "Toast",
         "desktop/design_system/toast_region/toast_region.cpp"},
    };
}
// Forced visual options live only in the developer host. Button's production
// renderer consumes the same options for real pointer and keyboard input.
class StateButton final : public Button {
  public:
    using Button::Button;
    QStyle::State forcedState;

  protected:
    QStyle::State visualState() const override { return Button::visualState() | forcedState; }
};
void applySpecimenTheme(QWidget& host) {
    if (auto* manager = host.findChild<ThemeManager*>(QString{}, Qt::FindDirectChildrenOnly))
        manager->applyTo(host);
    const auto palette = applicationPalette(resolvedThemeForWidget(host));
    for (auto* scroll : host.findChildren<QScrollArea*>()) {
        if (auto* paper = scroll->widget()) {
            paper->setPalette(palette);
            paper->setBackgroundRole(QPalette::Base);
        }
    }
}
void populateStandard(const QString& id, QWidget* host, QVBoxLayout* layout) {
    if (id == "numeric-fields") {
        auto* form = new QFormLayout;
        for (const auto& name : {"Minimum", "Maximum", "Disabled", "Invalid"}) {
            auto* number = new QSpinBox(host);
            number->setRange(0, 65535);
            number->setValue(QString(name) == "Maximum" ? 65535 : 0);
            number->setEnabled(QString(name) != "Disabled");
            number->setProperty("invalid", QString(name) == "Invalid");
            form->addRow(name, number);
        }
        auto* decimal = new QDoubleSpinBox(host);
        decimal->setDecimals(2);
        decimal->setValue(12.5);
        form->addRow("Decimal", decimal);
        layout->addLayout(form);
    } else if (id == "selects") {
        auto* select = new QComboBox(host);
        select->addItems({"First option", "Second option", "Long Unicode value · Việt Nam · 日本語"});
        select->setAccessibleName("Synthetic selection");
        layout->addWidget(select);
        auto* status = new QLabel(
            "Selection is local to this specimen; no preferences are read or saved.", host);
        status->setWordWrap(true);
        layout->addWidget(status);
        QObject::connect(select, &QComboBox::currentTextChanged, status,
                         [status](const QString& value) { status->setText("Selected: " + value); });
        auto* disabled = new QComboBox(host);
        disabled->addItem("Disabled select");
        disabled->setEnabled(false);
        layout->addWidget(disabled);
    } else if (id == "checks-toggles") {
        for (auto state : {Qt::Unchecked, Qt::PartiallyChecked, Qt::Checked}) {
            auto* check = new QCheckBox(state == Qt::Unchecked ? "Unchecked"
                                        : state == Qt::Checked ? "Checked"
                                                               : "Mixed",
                                        host);
            check->setTristate(state == Qt::PartiallyChecked);
            check->setCheckState(state);
            layout->addWidget(check);
        }
        auto* disabled = new QCheckBox("Disabled checked", host);
        disabled->setChecked(true);
        disabled->setEnabled(false);
        layout->addWidget(disabled);
        auto* lineNumbers = new QCheckBox("Line numbers", host);
        lineNumbers->setProperty("designRole", "switch");
        lineNumbers->setChecked(true);
        layout->addWidget(lineNumbers);
        auto* radio = new QRadioButton("Radio option", host);
        radio->setChecked(true);
        layout->addWidget(radio);
        auto* toggle = new Button("Toggle wrapping", host);
        toggle->setVariant(ButtonVariant::Outline);
        toggle->setCheckable(true);
        layout->addWidget(toggle);
    } else if (id == "textareas") {
        auto* text = new QPlainTextEdit(host);
        text->setPlaceholderText("Detailed diagnostics appear here…");
        text->setPlainText(
            "Read-only multiline text\n\nA short paragraph with Unicode: Việt Nam · 日本語.\n"
            "Longer content can be scrolled and selected.");
        text->setReadOnly(true);
        layout->addWidget(text, 1);
        auto* richText = new QTextEdit(host);
        configureRichTextArea(*richText);
        richText->setHtml("<p><b>Rich text</b> and editable content</p>");
        layout->addWidget(richText, 1);
    } else if (id == "shortcuts") {
        auto* form = new QFormLayout;
        auto* shortcut = new QKeySequenceEdit(QKeySequence("Ctrl+Return"), host);
        auto* empty = new QKeySequenceEdit(host);
        form->addRow("Primary action", shortcut);
        form->addRow("Unassigned", empty);
        layout->addLayout(form);
        auto* status =
            new QLabel("Click a field and press a shortcut. Changes remain local.", host);
        status->setWordWrap(true);
        layout->addWidget(status);
        QObject::connect(empty, &QKeySequenceEdit::keySequenceChanged, status,
                         [shortcut, status](const QKeySequence& sequence) {
                             status->setText(sequence == shortcut->keySequence()
                                                 ? "Conflict: already assigned to Primary action."
                                                 : "Shortcut available.");
                         });
    } else if (id == "lists-navigation") {
        auto* list = new QListWidget(host);
        list->addItems({"First item", "Second item", "Việt Nam · 日本語",
                        "Long navigation label that remains selectable in narrow layouts",
                        "Disabled item"});
        list->item(4)->setFlags(list->item(4)->flags() & ~Qt::ItemIsEnabled);
        list->setCurrentRow(1);
        layout->addWidget(list, 1);
        auto* tree = new QTreeView(host);
        auto* model = new QStandardItemModel(tree);
        auto* parent = new QStandardItem("Navigation group");
        auto* child = new QStandardItem("Nested item");
        child->setIcon(themedIcon(Icon::Table, resolvedThemeForWidget(*host).colors.mutedText, 14));
        parent->appendRow(child);
        model->appendRow(parent);
        tree->setModel(model);
        tree->setHeaderHidden(true);
        tree->setExpandsOnDoubleClick(false);
        tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        QObject::connect(tree, &QTreeView::clicked, tree, [tree](const QModelIndex& index) {
            if (tree->model()->hasChildren(index))
                tree->setExpanded(index, !tree->isExpanded(index));
        });
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(tree, &QTreeView::customContextMenuRequested, tree,
                         [tree](const QPoint& point) {
                             const QModelIndex index = tree->indexAt(point);
                             if (!index.isValid())
                                 return;
                             auto* menu = new QMenu(tree);
                             menu->setAttribute(Qt::WA_DeleteOnClose);
                             QObject::connect(menu->addAction("Rename"), &QAction::triggered, tree,
                                              [tree, index] { tree->edit(index); });
                             menu->popup(detail::contextMenuPosition(
                                 tree->viewport()->mapToGlobal(point)));
                         });
        tree->expandAll();
        layout->addWidget(tree, 1);
    } else if (id == "navigation-profile-row") {
        auto* list = new QListWidget(host);
        list->setObjectName("previewNavigationProfiles");
        list->setAccessibleName("Saved database connections");
        list->setItemDelegate(new NavigationProfileDelegate(list));
        list->setSpacing(spacing(Spacing::Half));
        list->setProperty("designSurface", "sidebar");
        list->setMouseTracking(true);
        for (const auto& name : {QStringLiteral("test sqlite"), QStringLiteral("SQLite")}) {
            auto* item = new QListWidgetItem(name + "\nSQLite", list);
            item->setData(NavigationProfileDelegate::DriverRole, "sqlite");
        }
        list->setCurrentRow(1);
        list->setFixedHeight(98);
        layout->addWidget(list);
        layout->addStretch();
    } else if (id == "dock") {
        auto* dock = new QDockWidget("Dock title", host);
        dock->setWidget(new QLabel("Dock content", dock));
        styleDockWidget(*dock, resolvedThemeForWidget(*host));
        layout->addWidget(dock);
    } else if (id == "tabs") {
        auto* tabs = new QTabWidget(host);
        tabs->tabBar()->setProperty("designTabVariant", "document");
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        for (int i = 0; i < 8; ++i) {
            tabs->addTab(new QLabel("Sample document content", tabs),
                         i == 0 ? "Document · modified" : QString("Long document %1 · 日本語").arg(i));
        }
        QObject::connect(tabs, &QTabWidget::tabCloseRequested, tabs, [tabs](int index) {
            auto* page = tabs->widget(index);
            tabs->removeTab(index);
            delete page;
        });
        layout->addWidget(tabs, 1);
        auto* objectTabs = new QTabBar(host);
        objectTabs->setObjectName("previewObjectTabs");
        for (const auto* label : {"Overview", "Details", "Settings", "Activity", "More"})
            objectTabs->addTab(label);
        objectTabs->setExpanding(false);
        layout->addWidget(objectTabs);
    } else if (id == "scrolling") {
        auto* scroll = new QScrollArea(host);
        auto* content = new QWidget(scroll);
        auto* rows = new QVBoxLayout(content);
        for (int i = 0; i < 40; ++i)
            rows->addWidget(new QLabel(
                QString("Synthetic row %1 · scroll vertically and horizontally").arg(i), content));
        content->setMinimumWidth(900);
        scroll->setWidget(content);
        scroll->setWidgetResizable(true);
        layout->addWidget(scroll, 1);
    } else if (id == "separators-splitters") {
        auto* splitter = new QSplitter(host);
        splitter->addWidget(new QLabel("Navigator side", splitter));
        splitter->addWidget(new QPlainTextEdit("Drag the shared splitter handle.", splitter));
        layout->addWidget(splitter, 1);
        auto* line = new QFrame(host);
        line->setFrameShape(QFrame::HLine);
        layout->addWidget(line);
        auto* vertical = new QFrame(host);
        vertical->setFrameShape(QFrame::VLine);
        vertical->setFixedHeight(48);
        layout->addWidget(vertical);
    } else if (id == "button-groups") {
        for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
            auto* group = new ButtonGroup(orientation, host);
            for (const auto& label : {"Previous", "1", "2", "Next"}) {
                auto* button = new Button(label, group);
                button->setVariant(ButtonVariant::Outline);
                group->addButton(button);
            }
            layout->addWidget(group);
        }
    } else if (id == "tool-buttons") {
        auto* bar = new QToolBar(host);
        bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        bar->addAction("Primary action");
        auto* disabled = bar->addAction("Disabled action");
        disabled->setEnabled(false);
        auto* overflow = new QToolButton(bar);
        overflow->setProperty("designRole", "menuButton");
        overflow->setText("More");
        auto* menu = new QMenu(overflow);
        menu->addAction("Another action");
        overflow->setMenu(menu);
        overflow->setPopupMode(QToolButton::InstantPopup);
        bar->addWidget(overflow);
        layout->addWidget(bar);
    } else if (id == "feedback") {
        auto* viewport = host->parentWidget();
        auto* toast = new choscordb::ToastRegion;
        toast->attachTo(viewport);
        auto* duration = new QSpinBox(host);
        duration->setObjectName("previewToastSeconds");
        duration->setRange(1, 30);
        duration->setValue(5);
        duration->setSuffix(" s");
        auto* durationRow = new QFormLayout;
        durationRow->addRow("Auto dismiss after", duration);
        layout->addLayout(durationRow);
        auto* actions = new QHBoxLayout;
        const struct {
            const char* name;
            const char* title;
            const char* body;
            choscordb::ToastVariant variant;
        } examples[] = {
            {"success", "Saved", "Your changes have been saved.", choscordb::ToastVariant::Success},
            {"warning", "Check your changes", "Some fields may need attention.",
             choscordb::ToastVariant::Warning},
            {"danger", "Could not save", "Please try again.", choscordb::ToastVariant::Danger},
        };
        for (const auto& example : examples) {
            auto* button = new Button(QString("Show %1 toast").arg(example.name), host);
            button->setObjectName(QString("previewToast_%1").arg(example.name));
            QObject::connect(button, &QPushButton::clicked, toast,
                             [toast, duration, example] {
                                 toast->showToast(example.title, example.body, example.variant,
                                                  duration->value() * 1000);
                             });
            actions->addWidget(button);
        }
        actions->addStretch();
        layout->addLayout(actions);
    } else if (id == "tooltip-popover") {
        auto* help = new Button("Show tooltip", host);
        help->setToolTip("Synthetic help text · Unicode Việt Nam · no external operation.");
        help->setObjectName("previewOpenTooltip");
        QObject::connect(help, &QPushButton::clicked, help, [help] {
            QHelpEvent event(QEvent::ToolTip, QPoint(0, help->height()),
                             help->mapToGlobal(QPoint(0, help->height())));
            QApplication::sendEvent(help, &event);
        });
        layout->addWidget(help);
    }
    layout->addStretch();
}
// Keep the specimen vector-backed through its actual display path. QLabel's
// stored one-times pixmap would otherwise be enlarged on Retina displays.
class IconDisplay final : public QLabel {
  public:
    IconDisplay(Icon role, int size, bool dimmed, QWidget* parent)
        : QLabel(parent), role_(role), size_(size), dimmed_(dimmed) {
        setMinimumSize(size, size);
    }
    QSize sizeHint() const override { return {size_, size_}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

  protected:
    void paintEvent(QPaintEvent*) override {
        const auto colors = resolvedThemeForWidget(*this).colors;
        const auto color = dimmed_ ? colors.disabled : colors.foreground;
        if (icon_.isNull() || color_ != color) {
            icon_ = themedIcon(role_, color, size_);
            color_ = color;
        }
        QPainter painter(this);
        icon_.paint(&painter, QRect(0, (height() - size_) / 2, size_, size_));
    }

  private:
    Icon role_;
    int size_;
    bool dimmed_;
    QIcon icon_;
    QColor color_;
};
void populateIcons(QWidget* host, QVBoxLayout* layout) {
    auto* grid = new QGridLayout;
    int row = 0;
    for (const auto& entry : iconCatalog()) {
        auto* name = new QLabel(entry.name, host);
        name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        name->setToolTip(entry.source);
        grid->addWidget(name, row, 0);
        int column = 1;
        for (int size : {12, 14, 16, 20, 24}) {
            auto* icon = new IconDisplay(entry.role, size, false, host);
            icon->setObjectName(QString("icon-%1-%2").arg(entry.name).arg(size));
            icon->setAccessibleName(QString("%1 · %2 pixels").arg(entry.name).arg(size));
            grid->addWidget(icon, row, column++);
        }
        auto* disabled = new IconDisplay(entry.role, 16, true, host);
        grid->addWidget(disabled, row++, column);
    }
    layout->addWidget(new QLabel("Name · 12 / 14 / 16 / 20 / 24 logical pixels · disabled", host));
    layout->addLayout(grid);
    layout->addStretch();
}
void populateTypography(QWidget* host, QVBoxLayout* layout) {
    for (const auto role : {TypographyRole::Heading, TypographyRole::DialogTitle,
                            TypographyRole::Base, TypographyRole::Ui, TypographyRole::Small,
                            TypographyRole::Field, TypographyRole::Monospace}) {
        const auto spec = typographySpec(role);
        auto* text = new Text("ChoscorDB · Truy vấn dữ liệu · 日本語 · Ελληνικά · 🙂", host);
        text->setTypographyRole(role);
        text->setWordWrap(true);
        layout->addWidget(text);
        layout->addWidget(new QLabel(QString("%1 · %2 px / %3 px line height · weight %4")
                                         .arg(spec.family)
                                         .arg(spec.pixelSize)
                                         .arg(spec.lineHeight)
                                         .arg(spec.weight),
                                     host));
    }
    for (const auto weight : {QFont::Normal, QFont::Medium, QFont::DemiBold, QFont::Bold}) {
        auto* weighted = new Text(
            QString("Platform weight %1 · Regular / Medium / Semibold / Bold").arg(weight), host);
        weighted->setWeight(weight);
        layout->addWidget(weighted);
    }
    auto* lineHeight =
        new Text("First body line\nSecond body line at the shared baseline interval", host);
    lineHeight->setWordWrap(true);
    layout->addWidget(lineHeight);
    auto* constrained = new Text("Long descriptions wrap within a constrained width. Unicode "
                                 "fallback follows the operating system for unsupported glyphs.",
                                 host);
    constrained->setWordWrap(true);
    constrained->setMaximumWidth(260);
    layout->addWidget(constrained);
    auto* keyboardHint = new QLabel("Ctrl / ⌘ + Enter", host);
    keyboardHint->setProperty("designRole", "kbd");
    layout->addWidget(keyboardHint);
    layout->addStretch();
}
void populateDialogSections(QWidget* host, QVBoxLayout* layout) {
    layout->addWidget(new QLabel("Open the real modal to inspect its compact action bars.", host));
    auto* open = new Button("Open sectioned modal", host);
    open->setObjectName("previewOpenDialogSections");
    layout->addWidget(open);
    auto* dialog = new ModalPanel(host);
    dialog->setEdgeToEdgeContent(true);
    dialog->setObjectName("previewDialogSectionsModal");
    dialog->setPalette(applicationPalette(resolvedThemeForWidget(*host)));
    dialog->resize(560, 360);
    auto* root = new QVBoxLayout(dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* sections = new DialogSections(dialog);
    sections->setObjectName("previewDialogSections");
    root->addWidget(sections);
    auto* heading = new Text("New connection", sections);
    heading->setTypographyRole(TypographyRole::DialogTitle);
    sections->headerLayout()->addWidget(heading);
    sections->headerLayout()->addStretch();
    auto* close = new Button({}, sections);
    close->setObjectName("previewDialogSectionsDismiss");
    close->setAccessibleName("Close sectioned modal");
    close->setVariant(ButtonVariant::Ghost);
    close->setButtonSize(ButtonSize::IconSmall);
    close->setDesignIcon(Icon::Close);
    sections->headerLayout()->addWidget(close);
    auto* description = new QLabel("Connect to a server or open a local database file.", sections);
    sections->bodyLayout()->addWidget(description);
    auto* name = new QLineEdit(sections);
    name->setPlaceholderText("Connection name");
    sections->bodyLayout()->addWidget(name);
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
    layout->addStretch();
}
void populateDialog(QWidget* host, QVBoxLayout* layout, bool modeless, bool destructive) {
    layout->addWidget(new QLabel(
        "Inspect the real window: Tab/Shift+Tab, Escape, backdrop and focus restoration.", host));
    auto* open = new Button(modeless ? "Open nonmodal window" : "Open modal panel", host);
    open->setObjectName("previewOpenDialog");
    layout->addWidget(open);
    auto* status = new QLabel("No action taken.", host);
    layout->addWidget(status);
    if (destructive) {
        status->setObjectName("previewConfirmationStatus");
        auto* confirmation = new choscordb::ConfirmationDialog(
            QMessageBox::Warning, "Delete synthetic record?",
            "This preview records your choice only. No data is changed.", QMessageBox::Cancel,
            host);
        confirmation->setObjectName("previewActualDialog");
        confirmation->setPalette(applicationPalette(resolvedThemeForWidget(*host)));
        auto* affirmative = confirmation->addButton("Delete", QMessageBox::DestructiveRole);
        confirmation->setDefaultButton(QMessageBox::Cancel);
        confirmation->setEscapeButton(QMessageBox::Cancel);
        QObject::connect(confirmation, &QDialog::finished, status,
                         [status, confirmation, affirmative](int) {
                             status->setText(confirmation->clickedButton() == affirmative
                                                 ? "Synthetic action accepted."
                                                 : "Cancelled.");
                         });
        QObject::connect(open, &QPushButton::clicked, confirmation,
                         [confirmation] { confirmation->open(); });
        return;
    }
    QDialog* dialog = modeless ? static_cast<QDialog*>(new choscordb::DialogShell(host))
                               : static_cast<QDialog*>(new ModalPanel(host));
    dialog->setAttribute(Qt::WA_WindowPropagation);
    dialog->setPalette(applicationPalette(resolvedThemeForWidget(*host)));
    dialog->setObjectName("previewActualDialog");
    dialog->setWindowTitle("Synthetic component preview");
    auto* content = new QVBoxLayout(dialog);
    auto* heading =
        new Text(destructive ? "Delete synthetic record?" : "Panel details", dialog);
    heading->setTypographyRole(TypographyRole::DialogTitle);
    content->addWidget(heading);
    auto* description =
        new Text(destructive ? "This preview records your choice only. No data is changed."
                             : "A reusable panel with a real keyboard focus boundary.",
                 dialog);
    description->setWordWrap(true);
    content->addWidget(description);
    auto* input = new QLineEdit(dialog);
    input->setPlaceholderText("Synthetic label");
    input->setAccessibleName("Synthetic label");
    content->addWidget(input);
    auto* actions = new QHBoxLayout;
    auto* cancel = new Button("Cancel", dialog);
    cancel->setVariant(ButtonVariant::Outline);
    cancel->setDefault(true);
    auto* confirm = new Button(destructive ? "Delete" : "Done", dialog);
    if (destructive)
        confirm->setVariant(ButtonVariant::Destructive);
    actions->addStretch();
    actions->addWidget(cancel);
    actions->addWidget(confirm);
    content->addLayout(actions);
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(confirm, &QPushButton::clicked, dialog, &QDialog::accept);
    QObject::connect(dialog, &QDialog::finished, status, [status](int result) {
        status->setText(result == QDialog::Accepted ? "Synthetic action accepted." : "Cancelled.");
    });
    QObject::connect(open, &QPushButton::clicked, dialog, [dialog, modeless] {
        if (modeless)
            dialog->show();
        else
            dialog->open();
    });
    layout->addStretch();
}
void populateMenu(QWidget* host, QVBoxLayout* layout) {
    auto* open = new Button("Open menu", host);
    open->setObjectName("previewOpenMenu");
    auto* menu = new QMenu(host);
    menu->setObjectName("previewActualMenu");
    menu->addAction(themedIcon(Icon::Run, resolvedThemeForWidget(*host).colors.foreground, 16),
                    "Primary action")
        ->setShortcut(QKeySequence("Ctrl+Return"));
    auto* toggle = menu->addAction("Wrap text");
    toggle->setCheckable(true);
    toggle->setChecked(true);
    menu->addAction("Unavailable action")->setEnabled(false);
    menu->addSeparator();
    menu->addMenu("Export format")
        ->addActions({new QAction("CSV", menu), new QAction("JSON", menu)});
    auto* status = new QLabel("No menu action selected.", host);
    QObject::connect(menu, &QMenu::triggered, status,
                     [status](QAction* action) { status->setText(action->text()); });
    QObject::connect(open, &QPushButton::clicked, menu,
                     [open, menu] { menu->popup(open->mapToGlobal(QPoint(0, open->height()))); });
    layout->addWidget(open);
    layout->addWidget(status);
    layout->addStretch();
}
class PasswordLineEdit final : public QLineEdit {
  public:
    using QLineEdit::QLineEdit;

    void centerTrailingAction() {
        if (auto* button = findChild<QToolButton*>())
            button->move(button->x(), (height() - button->height()) / 2);
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QLineEdit::resizeEvent(event);
        centerTrailingAction();
    }
};

void populateFields(QWidget* host, QVBoxLayout* layout) {
    auto* form = new QFormLayout;
    const QStringList states{"editable", "password", "selected", "disabled", "readonly", "invalid"};
    for (const auto& state : states) {
        QLineEdit* field = state == "password" ? new PasswordLineEdit(host) : new QLineEdit(host);
        field->setObjectName("field-" + state);
        field->setAccessibleName(state + " input");
        field->setPlaceholderText("Enter a value…");
        if (state == "password") {
            field->setEchoMode(QLineEdit::Password);
            field->setText("synthetic-password");
            const auto iconColor = resolvedThemeForWidget(*host).colors.foreground;
            auto* toggle = field->addAction(themedIcon(Icon::Eye, iconColor, 16),
                                            QLineEdit::TrailingPosition);
            toggle->setObjectName("field-password-toggle");
            toggle->setText("Show password");
            static_cast<PasswordLineEdit*>(field)->centerTrailingAction();
            QObject::connect(toggle, &QAction::triggered, field, [field, toggle, iconColor] {
                const bool show = field->echoMode() == QLineEdit::Password;
                field->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
                toggle->setIcon(themedIcon(show ? Icon::EyeOff : Icon::Eye, iconColor, 16));
                toggle->setText(show ? "Hide password" : "Show password");
            });
        }
        if (state == "selected") {
            field->setText("Selected text · Việt Nam");
            field->selectAll();
        }
        if (state == "disabled")
            field->setEnabled(false);
        if (state == "readonly") {
            field->setText("Read-only value");
            field->setReadOnly(true);
        }
        if (state == "invalid")
            field->setProperty("invalid", true);
        form->addRow(state, field);
    }
    auto* error = new QLabel("A value is required.", host);
    error->setObjectName("field-error");
    error->setProperty("state", "error");
    error->setProperty("designRole", "fieldError");
    form->addRow(QString{}, error);
    layout->addLayout(form);
    layout->addStretch();
}
void populateButtons(QWidget* host, QVBoxLayout* layout) {
    const QList<QPair<QString, ButtonVariant>> variants = {
        {"default", ButtonVariant::Default},         {"secondary", ButtonVariant::Secondary},
        {"outline", ButtonVariant::Outline},         {"ghost", ButtonVariant::Ghost},
        {"destructive", ButtonVariant::Destructive}, {"link", ButtonVariant::Link}};
    const QStringList states{"normal", "hover", "pressed", "focus", "disabled", "loading"};
    auto* referenceActions = new QHBoxLayout;
    auto* save = new Button("Save", host);
    save->setObjectName("previewReferenceSave");
    save->setVariant(ButtonVariant::Outline);
    save->setButtonSize(ButtonSize::Small);
    save->setButtonContext(ButtonContext::EditorAction);
    auto* run = new Button("Run", host);
    run->setObjectName("previewReferenceRun");
    run->setDesignIcon(Icon::Run);
    run->setButtonSize(ButtonSize::Small);
    run->setButtonContext(ButtonContext::EditorAction);
    run->setFixedWidth(75);
    referenceActions->addStretch();
    referenceActions->addWidget(save);
    referenceActions->addWidget(run);
    layout->addLayout(referenceActions);
    auto* grid = new QGridLayout;
    for (int row = 0; row < states.size(); ++row) {
        grid->addWidget(new QLabel(states[row], host), row * 3, 0, 1, 3);
        for (int column = 0; column < variants.size(); ++column) {
            const auto& variant = variants[column];
            auto* button = new StateButton(variant.first, host);
            button->setObjectName("button-" + states[row] + "-" + variant.first);
            button->setAccessibleName(states[row] + " " + variant.first);
            button->setVariant(variant.second);
            button->setButtonSize(ButtonSize::Default);
            if (states[row] == "hover")
                button->forcedState = QStyle::State_MouseOver;
            if (states[row] == "focus")
                button->forcedState = QStyle::State_HasFocus;
            if (states[row] == "pressed")
                button->setDown(true);
            if (states[row] == "disabled")
                button->setEnabled(false);
            if (states[row] == "loading")
                button->setLoading(true);
            grid->addWidget(button, row * 3 + 1 + column / 3, column % 3);
        }
    }
    layout->addLayout(grid);
    layout->addWidget(new QLabel("Reference text and icon sizes", host));
    const QList<QPair<QString, ButtonSize>> sizes = {{"xs", ButtonSize::ExtraSmall},
                                                     {"sm", ButtonSize::Small},
                                                     {"default", ButtonSize::Default},
                                                     {"lg", ButtonSize::Large},
                                                     {"icon-xs", ButtonSize::IconExtraSmall},
                                                     {"icon-sm", ButtonSize::IconSmall},
                                                     {"icon", ButtonSize::Icon},
                                                     {"icon-lg", ButtonSize::IconLarge}};
    auto* sizeGrid = new QGridLayout;
    for (int i = 0; i < sizes.size(); ++i) {
        const auto& size = sizes[i];
        auto* button = new Button(size.first.startsWith("icon") ? QString{} : size.first, host);
        button->setObjectName("button-size-" + size.first);
        button->setAccessibleName("Add · " + size.first);
        button->setIcon(
            themedIcon(Icon::Add, resolvedThemeForWidget(*host).colors.primaryForeground, 16));
        button->setButtonSize(size.second);
        sizeGrid->addWidget(button, i / 4, i % 4);
    }
    layout->addLayout(sizeGrid);
    auto* longLabel = new Button("Truy vấn dữ liệu · 日本語 · Long constrained label", host);
    longLabel->setVariant(ButtonVariant::Outline);
    longLabel->setMaximumWidth(250);
    layout->addWidget(longLabel);
    layout->addStretch();
}
void populateTokens(QWidget* host, QVBoxLayout* layout, ResolvedAppearance appearance) {
    auto* table = new QTableWidget(host);
    table->setObjectName("previewTokens");
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({"Token", "Value", "Source"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    const auto tokens = designTokens(appearance);
    table->setRowCount(static_cast<int>(tokens.size()));
    for (int row = 0; row < tokens.size(); ++row) {
        const auto& token = tokens[row];
        table->setItem(row, 0, new QTableWidgetItem(token.name));
        auto* value = new QTableWidgetItem(token.value);
        const QColor color(token.value);
        if (color.isValid()) {
            value->setData(Qt::DecorationRole, color);
        }
        table->setItem(row, 1, value);
        table->setItem(row, 2, new QTableWidgetItem(token.source));
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setCurrentCell(0, 0);
    layout->addWidget(table, 1);
    auto* copy = new QPushButton(QObject::tr("Copy selected token and source"), host);
    copy->setObjectName("previewCopyToken");
    QObject::connect(copy, &QPushButton::clicked, table, [table] {
        const int row = table->currentRow();
        if (row >= 0) {
            QApplication::clipboard()->setText(table->item(row, 0)->text() + " = " +
                                               table->item(row, 1)->text() + "\n" +
                                               table->item(row, 2)->text());
        }
    });
    layout->addWidget(copy);
}
} // namespace
PreviewWindow::PreviewWindow(QWidget* parent) : QMainWindow(parent) {
    auto* chrome = new ThemeManager(this);
    chrome->setMode(ThemeMode::Light);
    chrome->applyTo(*this);
    setWindowTitle(tr("ChoscorDB · Design system preview"));
    auto* body = new QWidget(this);
    auto* root = new QHBoxLayout(body);
    auto* controls = new QWidget(body);
    controls->setFixedWidth(190);
    auto* layout = new QVBoxLayout(controls);
    layout->setContentsMargins(0, 0, 0, 0);
    root->addWidget(controls);
    search_ = new QLineEdit(body);
    search_->setObjectName("previewSearch");
    search_->setPlaceholderText(tr("Search specimens"));
    search_->setAccessibleName(tr("Search specimens"));
    layout->addWidget(search_);
    navigation_ = new QListWidget(body);
    navigation_->setObjectName("previewNavigation");
    navigation_->addItems(
        {"Tokens", "Typography", "Icons", "Components"});
    navigation_->setMaximumHeight(170);
    layout->addWidget(navigation_);
    specimen_ = new QComboBox(body);
    specimen_->setObjectName("previewSpecimen");
    specimen_->setAccessibleName(tr("Specimen"));
    layout->addWidget(specimen_);
    source_ = new QLabel(body);
    source_->setObjectName("previewSource");
    source_->setWordWrap(true);
    source_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(source_);
    auto* copySource = new QPushButton(tr("Copy source location"), body);
    copySource->setObjectName("previewCopySource");
    connect(copySource, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(source_->text()); });
    layout->addWidget(copySource);
    comparison_ = new QWidget(body);
    auto* panes = new QHBoxLayout(comparison_);
    panes->setContentsMargins(0, 0, 0, 0);
    panes->setSpacing(0);
    light_ = new QWidget(comparison_);
    light_->setObjectName("previewLight");
    dark_ = new QWidget(comparison_);
    dark_->setObjectName("previewDark");
    for (auto* host : {light_, dark_}) {
        host->setAutoFillBackground(true);
        auto* theme = new ThemeManager(host);
        theme->setMode(host == light_ ? ThemeMode::Light : ThemeMode::Dark);
        theme->setReducedMotion(true);
        theme->applyTo(*host);
        host->setFont(resolveTypography(TypographyRole::Ui));
        (void)new QVBoxLayout(host);
        panes->addWidget(host);
    }
    root->addWidget(comparison_, 1);
    layout->addStretch();
    status_ = new QLabel(body);
    status_->setObjectName("previewExportStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* exportButton = new QPushButton(tr("Export comparison…"), body);
    exportButton->setObjectName("previewExport");
    layout->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("Export comparison"), {},
                                                       tr("PNG image (*.png)"));
        if (!path.isEmpty()) {
            (void)exportCapture(path);
        }
    });
    connect(specimen_, &QComboBox::currentIndexChanged, this, [this] { rebuildSpecimens(); });
    connect(navigation_, &QListWidget::currentTextChanged, this, [this](const QString& section) {
        const QSignalBlocker blocker(specimen_);
        specimen_->clear();
        for (const auto& entry : specimens()) {
            if (entry.section == section) {
                specimen_->addItem(entry.title, entry.id);
            }
        }
        rebuildSpecimens();
    });
    navigation_->setCurrentRow(0);
    resize(1280, 900);
    setCentralWidget(body);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& query) {
        for (int i = 0; i < navigation_->count(); ++i) {
            auto* item = navigation_->item(i);
            bool matches = item->text().contains(query, Qt::CaseInsensitive);
            for (const auto& entry : specimens()) {
                if (entry.section == item->text() &&
                    (entry.title + " " + entry.id).contains(query, Qt::CaseInsensitive)) {
                    matches = true;
                }
            }
            item->setHidden(!matches);
        }
    });
}
QStringList PreviewWindow::visibleSections() const {
    QStringList result;
    for (int i = 0; i < navigation_->count(); ++i) {
        if (!navigation_->item(i)->isHidden()) {
            result.append(navigation_->item(i)->text());
        }
    }
    return result;
}
bool PreviewWindow::selectSection(const QString& section) {
    for (int i = 0; i < navigation_->count(); ++i) {
        if (navigation_->item(i)->text() == section) {
            navigation_->setCurrentRow(i);
            return true;
        }
    }
    return false;
}
QStringList PreviewWindow::specimenIds() const {
    QStringList ids;
    for (const auto& entry : specimens()) {
        ids.append(entry.id);
    }
    return ids;
}
bool PreviewWindow::selectSpecimen(const QString& id) {
    for (const auto& entry : specimens()) {
        if (entry.id == id) {
            search_->clear();
            (void)selectSection(entry.section);
            specimen_->setCurrentIndex(specimen_->findData(id));
            return true;
        }
    }
    return false;
}
void PreviewWindow::rebuildSpecimens() {
    const auto id = specimen_->currentData().toString();
    for (const auto& entry : specimens()) {
        if (entry.id == id) {
            source_->setText(entry.source);
            break;
        }
    }
    for (auto* host : {light_, dark_}) {
        auto* layout = qobject_cast<QVBoxLayout*>(host->layout());
        while (auto* item = layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        layout->addWidget(new QLabel(host == light_ ? "Light" : "Dark", host));
        auto* title = new QLabel(specimen_->currentText(), host);
        title->setObjectName("previewSectionHeading");
        title->setWordWrap(true);
        layout->addWidget(title);
        auto* content = new QWidget(host);
        content->setObjectName("previewContent");
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(host);
        scroll->setObjectName("previewContentScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        layout->addWidget(scroll, 1);
        if (id == "tokens") {
            populateTokens(content, contentLayout,
                           host == light_ ? ResolvedAppearance::Light : ResolvedAppearance::Dark);
        } else if (id == "icons") {
            populateIcons(content, contentLayout);
        } else if (id == "typography") {
            populateTypography(content, contentLayout);
        } else if (id == "tables") {
            auto* table = new QTableWidget(content);
            table->setColumnCount(2);
            table->setHorizontalHeaderLabels({"Name", "Value"});
            table->setRowCount(2);
            table->setItem(0, 0, new QTableWidgetItem("First item"));
            table->setItem(0, 1, new QTableWidgetItem("Ready"));
            table->setItem(1, 0, new QTableWidgetItem("Second item"));
            table->setItem(1, 1, new QTableWidgetItem("Pending"));
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            contentLayout->addWidget(table, 1);
        } else if (id == "dialog-sections") {
            populateDialogSections(content, contentLayout);
        } else if (id == "dialogs" || id == "nonmodal" || id == "confirmations") {
            populateDialog(content, contentLayout, id == "nonmodal", id == "confirmations");
        } else if (id == "menus") {
            populateMenu(content, contentLayout);
        } else if (id == "fields") {
            populateFields(content, contentLayout);
        } else if (id == "buttons") {
            populateButtons(content, contentLayout);
        } else {
            populateStandard(id, content, contentLayout);
        }
        applySpecimenTheme(*host);
    }
}
bool PreviewWindow::exportCapture(const QString& path, bool comparison, QSize logicalSize,
                                  ResolvedAppearance appearance) {
    const auto fail = [this](const QString& reason) {
        status_->setText(tr("Capture failed: %1").arg(reason));
        return false;
    };

    if (logicalSize.isEmpty())
        logicalSize = QSize(comparison ? 1280 : 640, 900);
    if (logicalSize.width() < (comparison ? 640 : 320) || logicalSize.width() > 2560 ||
        logicalSize.height() < 320 || logicalSize.height() > 1800 ||
        (comparison && logicalSize.width() % 2 != 0))
        return fail(tr("Invalid capture dimensions."));
    const int paneWidth = logicalSize.width() / (comparison ? 2 : 1);
    const int paneHeight = logicalSize.height();

    // Recreate fixtures so editing, focus/caret blink, scroll position and pointer
    // location in the interactive preview cannot alter a reference capture.
    PreviewWindow fixture;
    (void)fixture.selectSpecimen(specimen_->currentData().toString());
    fixture.resize(1440, 1100);
    fixture.ensurePolished();
    fixture.layout()->activate();
    auto* singleHost = appearance == ResolvedAppearance::Dark ? fixture.dark_ : fixture.light_;
    auto* target = comparison ? fixture.comparison_ : singleHost;
    target->setFixedSize(logicalSize);
    target->ensurePolished();
    target->layout()->activate();
    // Scroll areas settle viewport/scrollbar geometry on show and the queued
    // layout pass. Rendering a hidden fixture immediately can clip the final
    // field edge or active tab indicator beneath stale scrollbars.
    const auto id = specimen_->currentData().toString();
    // These native popups require an active, focusable owner on Cocoa.
    fixture.setAttribute(Qt::WA_DontShowOnScreen, id != "selects");
    fixture.show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QImage image(logicalSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    target->render(&image);
    QString surface = "inline";
    if (id == "dialogs" || id == "confirmations" || id == "nonmodal" || id == "menus") {
        surface = id == "menus" ? "menu" : id == "nonmodal" ? "nonmodal" : "modal";
        QPainter painter(&image);
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{singleHost};
        for (int i = 0; i < hosts.size(); ++i) {
            auto* host = hosts[i];
            auto* previousParent = host->parentWidget();
            const auto previousGeometry = host->geometry();
            host->setParent(nullptr);
            host->setAttribute(Qt::WA_DontShowOnScreen);
            host->setFixedSize(paneWidth, paneHeight);
            // Reparenting can restore Qt's cached base palette. Keep detached
            // popup/backdrop captures scoped to their own Light/Dark specimen.
            applySpecimenTheme(*host);
            host->layout()->activate();
            const auto restoreHost = qScopeGuard([host, previousParent, previousGeometry] {
                host->setParent(previousParent);
                host->setGeometry(previousGeometry);
            });
            QWidget* actual =
                id == "menus"
                    ? static_cast<QWidget*>(host->findChild<QMenu*>("previewActualMenu"))
                    : static_cast<QWidget*>(host->findChild<QDialog*>("previewActualDialog"));
            if (!actual)
                return fail(tr("The selected surface is unavailable."));
            actual->setAttribute(Qt::WA_DontShowOnScreen);
            actual->ensurePolished();
            if (id == "menus") {
                actual->adjustSize();
            } else {
                actual->adjustSize();
                actual->show();
                if (surface == "modal") {
                    // Use the production backdrop's paint event, including its
                    // token opacity, instead of approximating the overlay here.
                    QWidget* backdrop = nullptr;
                    const auto candidates =
                        actual->parentWidget()->window()->findChildren<QWidget*>(
                            "modalBackdrop", Qt::FindDirectChildrenOnly);
                    for (auto* candidate : candidates) {
                        if (!candidate->isHidden()) {
                            backdrop = candidate;
                            break;
                        }
                    }
                    if (backdrop == nullptr)
                        return fail(tr("The modal backdrop is unavailable."));
                    {
                        backdrop->resize(paneWidth, paneHeight);
                        backdrop->render(&painter, QPoint(i * paneWidth, 0), {},
                                         QWidget::DrawChildren);
                    }
                }
            }
            if (actual->layout())
                actual->layout()->activate();
            if (id == "menus") {
                auto* menu = qobject_cast<QMenu*>(actual);
                menu->popup(QPoint(0, 0));
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                const auto snapshot = menu->grab();
                if (snapshot.isNull())
                    return fail(tr("The real menu could not be captured."));
                painter.drawPixmap(QPoint(i * paneWidth + (paneWidth - menu->width()) / 2,
                                          (paneHeight - menu->height()) / 2),
                                   snapshot);
            } else {
                actual->render(&painter, QPoint(i * paneWidth + (paneWidth - actual->width()) / 2,
                                                (paneHeight - actual->height()) / 2));
            }
            actual->hide();
        }
    }

    if (id == "selects") {
        surface = "selector-popup";
        fixture.show();
        fixture.activateWindow();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{singleHost};
        QPainter painter(&image);
        for (int i = 0; i < hosts.size(); ++i) {
            auto* select = hosts[i]->findChild<QComboBox*>();
            select->showPopup();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            auto* popup = select->view()->window();
            if (!popup->isVisible())
                return fail(tr("The real selector popup did not become ready."));
            painter.drawPixmap(QPoint(i * paneWidth + (paneWidth - popup->width()) / 2,
                                      (paneHeight - popup->height()) / 2),
                               popup->grab());
            select->hidePopup();
        }
    }
    if (id == "tooltip-popover") {
        surface = "tooltip";
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{singleHost};
        QPainter painter(&image);
        for (int i = 0; i < hosts.size(); ++i) {
            hosts[i]->findChild<QPushButton*>("previewOpenTooltip")->click();
            auto* tooltip = fixture.findChild<QWidget*>("designTooltip");
            if (!tooltip || !tooltip->isVisible()) {
                return fail(tr("The real tooltip did not become ready."));
            }
            tooltip->render(&painter, QPoint(i * paneWidth + (paneWidth - tooltip->width()) / 2,
                                             (paneHeight - tooltip->height()) / 2));
            tooltip->hide();
        }
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return fail(output.errorString());
    }
    if (!image.save(&output, "PNG") || !output.commit()) {
        return fail(output.errorString());
    }
    QJsonObject metadata{
        {"section", navigation_->currentItem()->text()},
        {"specimen", specimen_->currentData().toString()},
        {"source", source_->text()},
        {"surface", surface},
        {"logicalWidth", logicalSize.width()},
        {"logicalHeight", logicalSize.height()},
        {"scale", 1},
        {"sourceDeviceScale", target->devicePixelRatioF()},
        {"rendering", "QWidget logical-pixel render; native popup content; excludes OS shell"},
        {"themes", comparison                               ? "Light / Dark"
                   : appearance == ResolvedAppearance::Dark ? "Dark"
                                                            : "Light"},
        {"font", resolveTypography(TypographyRole::Ui).family()},
        {"qt", QT_VERSION_STR},
        {"platform", QGuiApplication::platformName()},
        {"os", QSysInfo::prettyProductName()},
        {"fixture", surface == "inline"
                        ? "synthetic; initial state; no focus; reduced motion"
                        : "synthetic; open real surface; no action dispatched; reduced motion"}};
    QJsonArray controls;
    for (auto* host : (comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                  : QList<QWidget*>{singleHost})) {
        for (auto* control : host->findChildren<QWidget*>()) {
            if (control->objectName().isEmpty() || control->isWindow())
                continue;
            const auto point = control->mapTo(target, QPoint());
            controls.append(QJsonObject{{"name", control->objectName()},
                                        {"theme", host == fixture.dark_ ? "Dark" : "Light"},
                                        {"x", point.x()},
                                        {"y", point.y()},
                                        {"width", control->width()},
                                        {"height", control->height()}});
        }
    }
    metadata.insert("controls", controls);
    QSaveFile manifest(path + ".json");
    const auto bytes = QJsonDocument(metadata).toJson();
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size() ||
        !manifest.commit()) {
        return fail(
            tr("PNG written, but metadata could not be saved: %1").arg(manifest.errorString()));
    }
    status_->setText(tr("Capture saved: %1").arg(path));
    return true;
}
} // namespace choscordb::design
