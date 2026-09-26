#include "tools/preview/preview_standard.h"

#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/dock/dock_style.h"
#include "design_system/field/field.h"
#include "design_system/icons.h"
#include "design_system/menu/menu.h"
#include "design_system/metrics/metrics.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/tabs/tab_add_corner.h"
#include "design_system/text_area/text_area_style.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "design_system/tree/navigation_tree_view.h"

#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHelpEvent>
#include <QKeySequenceEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace choscordb::design::preview_detail {
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
        select->addItems(
            {"First option", "Second option", "Long Unicode value · Việt Nam · 日本語"});
        select->setAccessibleName("Synthetic selection");
        layout->addWidget(select);
        auto* status = new QLabel(
            "The dropdown stays inside this window. Scroll over the field to keep its selection; "
            "open the list to choose another option.",
            host);
        status->setWordWrap(true);
        layout->addWidget(status);
        QObject::connect(select, &QComboBox::currentTextChanged, status,
                         [status](const QString& value) { status->setText("Selected: " + value); });
        auto* disabled = new QComboBox(host);
        disabled->addItem("Disabled select");
        disabled->setEnabled(false);
        layout->addWidget(disabled);
        auto* invalid = new QComboBox(host);
        invalid->setObjectName("select-invalid");
        invalid->addItems({"Choose an option", "Available option"});
        auto* validated = new FieldValidation(invalid, host);
        validated->setObjectName("select-validation");
        validated->setError("Select an option.");
        layout->addWidget(validated);
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
        auto* code = new QPlainTextEdit(host);
        code->setObjectName("previewCodePreview");
        code->setProperty("designRole", "codePreview");
        code->setPlainText("CREATE TABLE example (id INTEGER PRIMARY KEY);");
        code->setReadOnly(true);
        code->setFrameShape(QFrame::NoFrame);
        code->setFont(resolveTypography(TypographyRole::Monospace));
        layout->addWidget(code, 1);
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
        auto* tree = new NavigationTreeView(host);
        tree->setObjectName("previewNavigationTree");
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
                             popupContextMenu(*menu, tree->viewport()->mapToGlobal(point));
                         });
        tree->expandAll();
        tree->setCurrentIndex(model->index(0, 0));
        layout->addWidget(tree, 1);
    } else if (id == "navigation-profile-row") {
        auto* list = new QListWidget(host);
        list->setObjectName("previewNavigationProfiles");
        list->setAccessibleName("Saved database connections");
        list->setItemDelegate(new NavigationProfileDelegate(list));
        list->setSpacing(spacing(Spacing::Half));
        list->setProperty("designSurface", "sidebar");
        list->setMouseTracking(true);
        for (const auto& driver :
             {QStringLiteral("sqlite"), QStringLiteral("postgres"), QStringLiteral("mysql")}) {
            auto* item = new QListWidgetItem(driver == "sqlite"     ? "test sqlite\nSQLite"
                                             : driver == "postgres" ? "test postgres\nPostgreSQL"
                                                                    : "test mysql\nMySQL",
                                             list);
            item->setData(NavigationProfileDelegate::DriverRole, driver);
            if (driver == "mysql")
                item->setToolTip("MySQL · Dolphin icon");
        }
        list->setCurrentRow(1);
        list->setFixedHeight(138);
        layout->addWidget(list);
        layout->addStretch();
    } else if (id == "dock") {
        auto* dock = new QDockWidget("Dock title", host);
        dock->setWidget(new QLabel("Dock content", dock));
        styleDockWidget(*dock, resolvedThemeForWidget(*host));
        layout->addWidget(dock);
    } else if (id == "tabs") {
        auto* tabs = new QTabWidget(host);
        new TabAddCorner(tabs);
        tabs->tabBar()->setProperty("designTabVariant", "document");
        tabs->tabBar()->setElideMode(Qt::ElideRight);
        tabs->tabBar()->setExpanding(false);
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        for (int i = 0; i < 8; ++i) {
            tabs->addTab(new QLabel("Neutral document chrome", tabs),
                         themedIcon(Icon::Code, resolvedThemeForWidget(*host).colors.mutedText, 16),
                         i == 0 ? "abc.sql" : QString("History query %1 · 日本語").arg(i));
        }
        QObject::connect(tabs, &QTabWidget::tabCloseRequested, tabs, [tabs](int index) {
            auto* page = tabs->widget(index);
            tabs->removeTab(index);
            delete page;
        });
        layout->addWidget(tabs, 1);
        auto* objectTabs = new QTabBar(host);
        objectTabs->setObjectName("previewObjectTabs");
        for (const auto* label : {"Columns", "Indexes", "Keys", "DDL", "Data"})
            objectTabs->addTab(label);
        objectTabs->setExpanding(false);
        objectTabs->setCurrentIndex(3);
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
        auto* dockHost = new QMainWindow(host);
        dockHost->setObjectName("previewDockResizeHost");
        auto* dock = new QDockWidget("Connections", dockHost);
        dock->setObjectName("previewResizableSidebar");
        dock->setWidget(new QLabel("Navigator objects", dock));
        dockHost->addDockWidget(Qt::LeftDockWidgetArea, dock);
        dockHost->setCentralWidget(new QLabel("Drag the sidebar boundary", dockHost));
        layout->addWidget(dockHost, 1);
        auto* splitter = new QSplitter(Qt::Vertical, host);
        splitter->setObjectName("previewEditorResultsSplit");
        splitter->addWidget(new QPlainTextEdit("SELECT 1;", splitter));
        splitter->addWidget(new QLabel("Results · drag to resize", splitter));
        const DesignMetrics metrics;
        splitter->setSizes({metrics.initialEditorHeight, metrics.initialResultsHeight});
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
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
        overflow->setObjectName("previewMoreButton");
        overflow->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
        layout->addWidget(new QLabel(
            "Window notifications use a green success surface and a top-right dismiss button.",
            host));
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
            {"warning", "Warning",
             "Suggestions use loaded navigator objects. Expand nodes for more "
             "names; large catalogs may be limited.",
             choscordb::ToastVariant::Warning},
            {"danger", "Could not save", "Please try again.", choscordb::ToastVariant::Danger},
        };
        for (const auto& example : examples) {
            auto* button = new Button(QString("Show %1 toast").arg(example.name), host);
            button->setObjectName(QString("previewToast_%1").arg(example.name));
            QObject::connect(button, &QPushButton::clicked, toast, [toast, duration, example] {
                toast->showToast(example.title, example.body, example.variant,
                                 duration->value() * 1000);
            });
            actions->addWidget(button);
        }
        auto* progress = new Button("Show progress", host);
        progress->setObjectName("previewToast_progress");
        QObject::connect(progress, &QPushButton::clicked, viewport, [viewport] {
            progressToast(viewport)->showProgress("Exporting", "Writing rows…");
        });
        actions->addWidget(progress);
        auto* finish = new Button("Complete progress", host);
        finish->setObjectName("previewToast_progressDone");
        QObject::connect(finish, &QPushButton::clicked, viewport,
                         [viewport] { clearProgressToast(viewport); });
        actions->addWidget(finish);
        actions->addStretch();
        layout->addLayout(actions);
    } else if (id == "tooltip-popover") {
        layout->addWidget(
            new QLabel("Tooltips stay inside this window without taking focus.", host));
        auto* help = new Button("Show tooltip", host);
        help->setToolTip("Synthetic help text · Unicode Việt Nam · no external operation.");
        help->setObjectName("previewOpenTooltip");
        QObject::connect(help, &QPushButton::clicked, help, [help] {
            QHelpEvent event(QEvent::ToolTip, QPoint(0, help->height()),
                             help->mapToGlobal(QPoint(0, help->height())));
            QApplication::sendEvent(help, &event);
        });
        layout->addWidget(help);
        auto* items = new QListWidget(host);
        items->setObjectName("previewTooltipItems");
        auto* item = new QListWidgetItem("Example Postgres", items);
        item->setToolTip("Example Postgres");
        items->setFixedHeight(64);
        layout->addWidget(items);
    }
    layout->addStretch();
}
} // namespace choscordb::design::preview_detail
