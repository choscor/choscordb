#include "design_system/preview_window.h"

#include "design_system/components.h"
#include "design_system/icons.h"
#include "design_system/modal_panel.h"
#include "design_system/theme_manager.h"
#include "design_system/typography.h"
#include "models/history_model.h"
#include "models/navigator_model.h"
#include "models/result_table_model.h"
#include "widgets/confirmation_dialog.h"
#include "widgets/dialog_shell.h"
#include "widgets/editor_completion.h"
#include "widgets/search_panel.h"
#include "widgets/sql_editor.h"
#include "widgets/toast_region.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontComboBox>
#include <QFrame>
#include <QHelpEvent>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>

#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSysInfo>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
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
        {"Tokens", "tokens", "Semantic colors and geometry", "desktop/design_system/theme.cpp"},
        {"Typography", "typography", "Type roles and Unicode",
         "desktop/design_system/typography.cpp"},
        {"Icons", "icons", "Lucide names, sizes and alignment", "desktop/design_system/icons.cpp"},
        {"Components", "buttons", "Buttons: variants, sizes and states",
         "desktop/design_system/components.cpp"},
        {"Components", "tool-buttons", "Tool buttons and dropdowns",
         "desktop/design_system/control_style.cpp"},
        {"Components", "button-groups", "Button groups", "desktop/design_system/components.cpp"},
        {"Components", "fields", "Text and password fields",
         "desktop/design_system/control_style.cpp"},
        {"Components", "numeric-fields", "Numeric fields",
         "desktop/design_system/control_style.cpp"},
        {"Components", "textareas", "Text areas and diagnostics",
         "desktop/design_system/control_style.cpp"},
        {"Components", "selects", "Selects and popup rows",
         "desktop/design_system/control_style.cpp"},
        {"Components", "checks-toggles", "Checks and toggles",
         "desktop/design_system/control_style.cpp"},
        {"Components", "editor-preferences", "Editor font preferences",
         "desktop/app/editor_preferences.cpp"},
        {"Components", "shortcuts", "Shortcut entry", "desktop/models/shortcut_catalog.cpp"},
        {"Components", "lists-navigation", "Lists and navigation",
         "desktop/design_system/control_style.cpp"},
        {"Components", "tabs", "Tabs with close and overflow",
         "desktop/design_system/control_style.cpp"},
        {"Components", "scrolling", "Scroll areas and scrollbars",
         "desktop/design_system/control_style.cpp"},
        {"Components", "separators-splitters", "Separators and splitters",
         "desktop/design_system/control_style.cpp"},
        {"Components", "tables", "Table headers, cells and selection",
         "desktop/models/result_table_model.cpp"},
        {"Components", "tooltip-popover", "Tooltips and popovers",
         "desktop/design_system/theme.cpp"},
        {"Compositions", "dialogs", "Modal panel", "desktop/design_system/modal_panel.cpp"},
        {"Compositions", "nonmodal", "Nonmodal content", "desktop/widgets/dialog_shell.cpp"},
        {"Compositions", "confirmations", "Destructive confirmations",
         "desktop/design_system/modal_panel.cpp"},
        {"Compositions", "menus", "Menus and submenus", "desktop/design_system/control_style.cpp"},
        {"Compositions", "feedback", "Feedback and toast states",
         "desktop/widgets/toast_region.cpp"},
        {"Compositions", "connection-form", "Synthetic connection form and password",
         "desktop/design_system/control_style.cpp"},
        {"Compositions", "appearance-form", "Synthetic System Light Dark selection",
         "desktop/design_system/theme_manager.cpp"},
        {"Compositions", "native-exceptions", "Native shell exceptions",
         "desktop/design_system/preview_window.cpp"},
        {"Database UI", "sidebar-tree", "Navigator connection and object rows",
         "desktop/models/navigator_model.cpp"},
        {"Database UI", "paging", "Result paging", "desktop/models/result_table_model.cpp"},
        {"Database UI", "completion", "SQL completion popup",
         "desktop/widgets/editor_completion.cpp"},
        {"Database UI", "sql-editor", "SQL editor and find/replace",
         "desktop/widgets/sql_editor.cpp"},
        {"Database UI", "query-controls", "Query and transaction controls",
         "desktop/design_system/components.cpp"},
        {"Database UI", "results", "Typed results, NULL, empty and large values",
         "desktop/models/result_table_model.cpp"},
        {"Database UI", "messages-summary", "Messages and execution summary",
         "desktop/design_system/control_style.cpp"},
        {"Database UI", "history", "Synthetic history records", "desktop/models/history_model.cpp"},
        {"Database UI", "recovery", "Recovery retry and reset feedback",
         "desktop/widgets/dialog_shell.cpp"},
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
void populateNavigator(QWidget* host, QVBoxLayout* layout) {
    auto* filter = new QLineEdit(host);
    filter->setPlaceholderText("Filter synthetic objects");
    layout->addWidget(filter);
    auto* tree = new QTreeView(host);
    auto* model = new choscordb::NavigatorModel(tree);
    QObject::connect(
        model, &choscordb::NavigatorModel::childrenRequested, model,
        [model](quint64 connection, const QString& object, quint64 token) {
            if (connection == 2) {
                (void)model->failChildren(connection, object, token,
                                          "Synthetic disconnected state");
            } else {
                (void)model->applyChildren(
                    connection, object, token,
                    {{"customers", "customers · synthetic", "public.customers", "table", false},
                     {"names", "Việt Nam · 日本語", "public.names", "table", false}});
            }
        });
    (void)model->addConnection(1, "Synthetic SQLite");
    (void)model->addConnection(2, "Synthetic disconnected PostgreSQL");
    model->fetchMore(model->index(0, 0));
    model->fetchMore(model->index(1, 0));
    tree->setModel(model);
    tree->setHeaderHidden(true);
    tree->expand(model->index(0, 0));
    QObject::connect(filter, &QLineEdit::textChanged, tree, [tree, model](const QString& value) {
        for (int row = 0; row < model->rowCount(); ++row) {
            tree->setRowHidden(
                row, {},
                !model->data(model->index(row, 0)).toString().contains(value, Qt::CaseInsensitive));
        }
    });
    layout->addWidget(tree, 1);
}
void populateHistory(QWidget* host, QVBoxLayout* layout) {
    layout->addWidget(new QCheckBox("Record synthetic history (local specimen only)", host));
    auto* table = new QTableView(host);
    auto* model = new choscordb::HistoryModel(table);
    model->setEntries({{"sample-1", "", "SELECT 'synthetic';", 0, 12, 4, "completed", true},
                       {"sample-2", "", "SELECT missing FROM synthetic;", 0, 2, 0, "failed", false},
                       {"sample-3", "", "SELECT * FROM synthetic;", 0, 0, 0, "cancelled", false}});
    table->setModel(model);
    // Production timestamps are local-time values; hide them in the deterministic
    // fixture so timezone differences do not masquerade as visual regressions.
    table->hideColumn(0);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);
    auto* preview = new QPlainTextEdit(host);
    preview->setReadOnly(true);
    preview->setPlainText("Select a history row to inspect its bounded SQL excerpt.");
    QObject::connect(table, &QTableView::clicked, preview,
                     [model, preview](const QModelIndex& index) {
                         if (const auto* entry = model->entry(index.row()))
                             preview->setPlainText(entry->sql);
                     });
    layout->addWidget(preview);
    layout->addWidget(new QLabel("Synthetic history · 3 records · no storage service", host));
}
void populateCompletion(QWidget* host, QVBoxLayout* layout) {
    auto* editor = new choscordb::SqlEditor(host);
    // QScintilla consumes QPalette::Base/Text rather than QSS background rules.
    editor->setPalette(applicationPalette(resolvedThemeForWidget(*host)));
    editor->setText("syn");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 3);
    editor->SendScintilla(QsciScintillaBase::SCI_SETCARETPERIOD, 0UL);
    auto* controller = new choscordb::EditorCompletionController(host);
    controller->setCatalog(
        choscordb::CompletionService({{"synthetic_customers", "synthetic_customers", "table"},
                                      {"synthetic_orders", "synthetic_orders", "table"}}));
    controller->setEditor(editor);
    auto* popup = controller->findChild<QCompleter*>()->popup();
    popup->setPalette(applicationPalette(resolvedThemeForWidget(*host)));
    popup->setFont(host->font());
    const auto appearance = resolvedThemeForWidget(*host).appearance;
    ThemeManager theme;
    theme.setMode(appearance == ResolvedAppearance::Dark ? ThemeMode::Dark : ThemeMode::Light);
    theme.applyTo(*popup);
    auto* open = new Button("Open real SQL completion", host);
    open->setObjectName("previewOpenCompletion");
    QObject::connect(open, &QPushButton::clicked, editor, [editor, controller] {
        editor->window()->activateWindow();
        editor->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(0, controller, [controller] { controller->requestCompletion(true); });
    });
    layout->addWidget(open);
    layout->addWidget(editor, 1);
    layout->addWidget(
        new QLabel("Immutable synthetic catalog; no engine or connection is accessed.", host));
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
        layout->addLayout(form);
    } else if (id == "selects" || id == "appearance-form") {
        auto* select = new QComboBox(host);
        select->addItems(
            id == "appearance-form"
                ? QStringList{"System", "Light", "Dark"}
                : QStringList{"SQLite", "PostgreSQL", "Long Unicode value · Việt Nam · 日本語"});
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
        auto* toggle = new Button("Toggle wrapping", host);
        toggle->setVariant(ButtonVariant::Outline);
        toggle->setCheckable(true);
        layout->addWidget(toggle);
    } else if (id == "textareas" || id == "messages-summary") {
        auto* text = new QPlainTextEdit(host);
        text->setPlaceholderText("Detailed diagnostics appear here…");
        text->setPlainText(
            "Synthetic execution summary\n4 rows · 12 ms\n\nSuccess: query completed.\nWarning: "
            "result truncated.\nError: synthetic syntax error at line 2.\nCancelled: no further "
            "rows will arrive.\nDisconnected: reconnect before running.\nLoading: row count and "
            "duration are not yet known.");
        text->setReadOnly(id == "messages-summary");
        layout->addWidget(text, 1);
    } else if (id == "editor-preferences") {
        auto* form = new QFormLayout;
        auto* family = new QFontComboBox(host);
        family->setFontFilters(QFontComboBox::MonospacedFonts);
        auto* size = new QSpinBox(host);
        size->setRange(8, 36);
        size->setValue(13);
        form->addRow("Editor font", family);
        form->addRow("Editor size", size);
        layout->addLayout(form);
        auto* system = new QCheckBox("Use system monospace", host);
        system->setChecked(true);
        layout->addWidget(system);
        auto* editor = new choscordb::SqlEditor(host);
        // QScintilla consumes QPalette::Base/Text rather than QSS background rules.
        editor->setPalette(applicationPalette(resolvedThemeForWidget(*host)));
        editor->setText("-- synthetic editor font preview\nSELECT 'Việt Nam';");
        editor->SendScintilla(QsciScintillaBase::SCI_SETCARETPERIOD, 0UL);
        layout->addWidget(editor, 1);
        QObject::connect(family, &QFontComboBox::currentFontChanged, editor,
                         [editor, size](QFont font) {
                             font.setPointSize(size->value());
                             editor->setEditorFont(font);
                         });
        QObject::connect(size, &QSpinBox::valueChanged, editor, [editor, family](int value) {
            auto font = family->currentFont();
            font.setPointSize(value);
            editor->setEditorFont(font);
        });
        QObject::connect(system, &QCheckBox::toggled, editor, [editor, family, size](bool checked) {
            family->setEnabled(!checked);
            auto font =
                checked ? resolveTypography(TypographyRole::Monospace) : family->currentFont();
            font.setPointSize(size->value());
            editor->setEditorFont(font);
        });
        family->setEnabled(false);
    } else if (id == "shortcuts") {
        auto* form = new QFormLayout;
        auto* shortcut = new QKeySequenceEdit(QKeySequence("Ctrl+Return"), host);
        auto* empty = new QKeySequenceEdit(host);
        form->addRow("Run query", shortcut);
        form->addRow("Unassigned", empty);
        layout->addLayout(form);
        auto* status =
            new QLabel("Click a field and press a shortcut. Changes remain local.", host);
        status->setWordWrap(true);
        layout->addWidget(status);
        QObject::connect(empty, &QKeySequenceEdit::keySequenceChanged, status,
                         [shortcut, status](const QKeySequence& sequence) {
                             status->setText(sequence == shortcut->keySequence()
                                                 ? "Conflict: already assigned to Run query."
                                                 : "Shortcut available.");
                         });
    } else if (id == "lists-navigation") {
        auto* list = new QListWidget(host);
        list->addItems({"Connections", "Query history", "Việt Nam · 日本語",
                        "Long navigation label that remains selectable in narrow layouts",
                        "Disabled item"});
        list->item(4)->setFlags(list->item(4)->flags() & ~Qt::ItemIsEnabled);
        list->setCurrentRow(1);
        layout->addWidget(list, 1);
    } else if (id == "tabs") {
        auto* tabs = new QTabWidget(host);
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        for (int i = 0; i < 8; ++i) {
            tabs->addTab(new QLabel("Synthetic document content", tabs),
                         i == 0 ? "Query · modified" : QString("Long query %1 · 日本語").arg(i));
        }
        QObject::connect(tabs, &QTabWidget::tabCloseRequested, tabs, [tabs](int index) {
            auto* page = tabs->widget(index);
            tabs->removeTab(index);
            delete page;
        });
        layout->addWidget(tabs, 1);
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
    } else if (id == "paging" || id == "query-controls" || id == "tool-buttons") {
        auto* bar = new QToolBar(host);
        bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        auto* run = bar->addAction(
            themedIcon(Icon::Run, resolvedThemeForWidget(*host).colors.foreground, 16), "Run");
        auto* stop = bar->addAction(
            themedIcon(Icon::Cancel, resolvedThemeForWidget(*host).colors.foreground, 16),
            "Cancel");
        stop->setEnabled(false);
        bar->addSeparator();
        auto* toggle = bar->addAction("Autocommit");
        toggle->setCheckable(true);
        toggle->setChecked(true);
        auto* overflow = new QToolButton(bar);
        overflow->setText("More");
        auto* menu = new QMenu(overflow);
        menu->addAction("Commit");
        menu->addAction("Rollback");
        overflow->setMenu(menu);
        overflow->setPopupMode(QToolButton::InstantPopup);
        bar->addWidget(overflow);
        layout->addWidget(bar);
        auto* status = new QLabel("Ready · synthetic controls do not dispatch queries", host);
        status->setWordWrap(true);
        QObject::connect(run, &QAction::triggered, status, [status, stop, run] {
            status->setText("Running synthetic fixture · total unknown");
            stop->setEnabled(true);
            run->setEnabled(false);
        });
        QObject::connect(stop, &QAction::triggered, status, [status, stop, run] {
            status->setText("Cancelled synthetic fixture");
            stop->setEnabled(false);
            run->setEnabled(true);
        });
        layout->addWidget(status);
        auto* actions = new QHBoxLayout;
        auto* previous = new Button("Previous", host);
        previous->setVariant(ButtonVariant::Outline);
        auto* next = new Button("Next", host);
        next->setVariant(ButtonVariant::Outline);
        auto* range = new QLabel("1–4 of 8", host);
        previous->setEnabled(false);
        QObject::connect(next, &QPushButton::clicked, range, [previous, next, range] {
            previous->setEnabled(true);
            next->setEnabled(false);
            range->setText("5–8 of 8");
        });
        QObject::connect(previous, &QPushButton::clicked, range, [previous, next, range] {
            previous->setEnabled(false);
            next->setEnabled(true);
            range->setText("1–4 of 8");
        });
        actions->addWidget(previous);
        actions->addWidget(range);
        actions->addWidget(next);
        layout->addLayout(actions);
    } else if (id == "feedback" || id == "recovery") {
        auto* badges = new QHBoxLayout;
        for (const auto& variant : {"default", "secondary", "outline", "destructive"}) {
            auto* badge = new QLabel(variant, host);
            badge->setProperty("designRole", "badge");
            badge->setProperty("variant", variant);
            badges->addWidget(badge);
        }
        layout->addLayout(badges);
        auto* toast = new choscordb::ToastRegion(host);
        toast->showPersistent("Synthetic notification · no timers in captures");
        layout->addWidget(toast);
        for (const auto& name :
             {"Empty", "Loading", "Success", "Warning", "Error", "Cancelled", "Disconnected"}) {
            auto* label = new QLabel(QString(name) + " · synthetic state", host);
            label->setProperty("state", QString(name).toLower());
            layout->addWidget(label);
        }
        auto* progress = new QProgressBar(host);
        progress->setValue(40);
        layout->addWidget(progress);
        auto* retry = new Button("Retry synthetic operation", host);
        QObject::connect(retry, &QPushButton::clicked, toast,
                         [toast] { toast->showPersistent("Synthetic retry completed."); });
        layout->addWidget(retry);
    } else if (id == "connection-form") {
        auto* form = new QFormLayout;
        auto* engine = new QComboBox(host);
        engine->addItems({"SQLite", "PostgreSQL"});
        form->addRow("Database type", engine);
        for (const auto& name : {"Name", "Host", "Database", "Username", "Password"}) {
            auto* input = new QLineEdit(host);
            input->setPlaceholderText(QString("Synthetic %1").arg(name));
            if (QString(name) == "Password")
                input->setEchoMode(QLineEdit::Password);
            form->addRow(name, input);
        }
        auto* port = new QSpinBox(host);
        port->setRange(1, 65535);
        port->setValue(5432);
        form->addRow("Port", port);
        auto* tls = new QComboBox(host);
        tls->addItems({"Prefer", "Require", "Verify full"});
        form->addRow("TLS", tls);
        layout->addLayout(form);
        layout->addWidget(new QCheckBox("Remember password (synthetic; never stored)", host));
        auto* status = new QLabel("No connection will be opened.", host);
        auto* test = new Button("Simulate validation", host);
        QObject::connect(test, &QPushButton::clicked, status, [status] {
            status->setText("Name is required. No connection attempted.");
            status->setProperty("state", "error");
        });
        layout->addWidget(test);
        layout->addWidget(status);
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
    } else if (id == "native-exceptions") {
        auto* note = new QLabel(
            "Native title bars, OS menu bars and system file pickers retain operating-system "
            "rendering. Application menus, forms and dialogs use the shared components. Native "
            "accessibility and placement require real-display review.",
            host);
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    layout->addStretch();
}
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
            auto* icon = new QLabel(host);
            icon->setObjectName(QString("icon-%1-%2").arg(entry.name).arg(size));
            icon->setAccessibleName(QString("%1 · %2 pixels").arg(entry.name).arg(size));
            icon->setPixmap(
                themedIcon(entry.role, resolvedThemeForWidget(*host).colors.foreground, size)
                    .pixmap(QSize(size, size), 1.0));
            grid->addWidget(icon, row, column++);
        }
        auto* disabled = new QLabel(host);
        disabled->setPixmap(
            themedIcon(entry.role, resolvedThemeForWidget(*host).colors.disabled, 16)
                .pixmap(QSize(16, 16), 1.0));
        grid->addWidget(disabled, row++, column);
    }
    layout->addWidget(new QLabel("Name · 12 / 14 / 16 / 20 / 24 logical pixels · disabled", host));
    layout->addLayout(grid);
    layout->addStretch();
}
void populateTypography(QWidget* host, QVBoxLayout* layout) {
    for (const auto role :
         {TypographyRole::Heading, TypographyRole::DialogTitle, TypographyRole::Base,
          TypographyRole::Ui, TypographyRole::Small, TypographyRole::Monospace}) {
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
            QString("Geist weight %1 · Regular / Medium / Semibold / Bold").arg(weight), host);
        weighted->setWeight(weight);
        layout->addWidget(weighted);
    }
    auto* lineHeight =
        new Text("First body line\nSecond body line at the shared 20 px baseline interval", host);
    lineHeight->setWordWrap(true);
    layout->addWidget(lineHeight);
    auto* constrained = new Text("Long descriptions wrap within a constrained width. Unicode "
                                 "fallback follows the operating system for glyphs outside Geist.",
                                 host);
    constrained->setWordWrap(true);
    constrained->setMaximumWidth(260);
    layout->addWidget(constrained);
    auto* keyboardHint = new QLabel("Ctrl / ⌘ + Enter", host);
    keyboardHint->setProperty("designRole", "kbd");
    layout->addWidget(keyboardHint);
    layout->addStretch();
}
void populateResults(QWidget* host, QVBoxLayout* layout) {
    layout->addWidget(new QLabel("Synthetic results · no database connection", host));
    auto* table = new QTableView(host);
    table->setObjectName("previewResults");
    auto* model = new choscordb::ResultTableModel(table);
    std::vector<choscordb::ResultColumn> columns(3);
    columns[0].name = "id";
    columns[0].databaseType = "int8";
    columns[1].name = "value";
    columns[1].databaseType = "text";
    columns[2].name = "active";
    columns[2].databaseType = "boolean";
    const bool loaded =
        model->setPage(std::move(columns),
                       {{qint64(1), std::monostate{}, true},
                        {qint64(2), QString(""), false},
                        {qint64(3), choscordb::DeferredValue{1, 1048576, "text"}, true},
                        {qint64(4), QString("Việt Nam · 日本語 · 🙂"), false}},
                       0);
    if (!loaded)
        layout->addWidget(new QLabel("Synthetic fixture failed to load.", host));
    table->setModel(model);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSortingEnabled(false);
    table->selectRow(3);
    layout->addWidget(table, 1);
    layout->addWidget(
        new QLabel("4 rows · first page · NULL and empty strings remain distinct", host));
}
void populateEditor(QWidget* host, QVBoxLayout* layout) {
    layout->addWidget(new QLabel("Synthetic SQL · edits stay in this specimen", host));
    auto* editor = new choscordb::SqlEditor(host);
    // QScintilla consumes QPalette::Base/Text rather than QSS background rules.
    editor->setPalette(applicationPalette(resolvedThemeForWidget(*host)));
    editor->setObjectName("previewSqlEditor");
    editor->setText("-- synthetic offline fixture\nSELECT id, name, NULL AS missing\nFROM "
                    "synthetic_customers\nWHERE active = true;\n");
    editor->setEditorFont(resolveTypography(TypographyRole::Monospace));
    // A deterministic export has no blinking insertion caret.
    editor->SendScintilla(QsciScintillaBase::SCI_SETCARETPERIOD, 0UL);
    auto* search = new choscordb::SearchPanel([editor] { return editor; }, host);
    layout->addWidget(search);
    layout->addWidget(editor, 1);
    search->showReplace();
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
    dialog->setWindowTitle("Synthetic preview · no database operations");
    auto* content = new QVBoxLayout(dialog);
    auto* heading =
        new Text(destructive ? "Delete synthetic record?" : "Connection details", dialog);
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
                    "Run query")
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
void populateFields(QWidget* host, QVBoxLayout* layout) {
    auto* form = new QFormLayout;
    const QStringList states{"editable", "password", "selected", "disabled", "readonly", "invalid"};
    for (const auto& state : states) {
        auto* field = new QLineEdit(host);
        field->setObjectName("field-" + state);
        field->setAccessibleName(state + " input");
        field->setPlaceholderText("Enter a value…");
        if (state == "password") {
            field->setEchoMode(QLineEdit::Password);
            field->setText("synthetic-password");
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
    auto* layout = new QVBoxLayout(body);
    search_ = new QLineEdit(body);
    search_->setObjectName("previewSearch");
    search_->setPlaceholderText(tr("Search specimens"));
    search_->setAccessibleName(tr("Search specimens"));
    layout->addWidget(search_);
    navigation_ = new QListWidget(body);
    navigation_->setObjectName("previewNavigation");
    navigation_->addItems(
        {"Tokens", "Typography", "Icons", "Components", "Compositions", "Database UI"});
    navigation_->setMaximumHeight(110);
    layout->addWidget(navigation_);
    specimen_ = new QComboBox(body);
    specimen_->setObjectName("previewSpecimen");
    specimen_->setAccessibleName(tr("Specimen"));
    layout->addWidget(specimen_);
    source_ = new QLabel(body);
    source_->setObjectName("previewSource");
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
    layout->addWidget(comparison_);
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
    resize(1440, 1100);
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
        layout->addWidget(content, 1);
        if (id == "tokens") {
            populateTokens(content, contentLayout,
                           host == light_ ? ResolvedAppearance::Light : ResolvedAppearance::Dark);
        } else if (id == "icons") {
            populateIcons(content, contentLayout);
        } else if (id == "typography") {
            populateTypography(content, contentLayout);
        } else if (id == "sidebar-tree") {
            populateNavigator(content, contentLayout);
        } else if (id == "history") {
            populateHistory(content, contentLayout);
        } else if (id == "completion") {
            populateCompletion(content, contentLayout);
        } else if (id == "results" || id == "tables") {
            populateResults(content, contentLayout);
        } else if (id == "sql-editor") {
            populateEditor(content, contentLayout);
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
    }
}
bool PreviewWindow::exportCapture(const QString& path, bool comparison) {
    const auto fail = [this](const QString& reason) {
        status_->setText(tr("Capture failed: %1").arg(reason));
        return false;
    };

    // Recreate fixtures so editing, focus/caret blink, scroll position and pointer
    // location in the interactive preview cannot alter a reference capture.
    PreviewWindow fixture;
    (void)fixture.selectSpecimen(specimen_->currentData().toString());
    fixture.resize(1440, 1100);
    fixture.ensurePolished();
    fixture.layout()->activate();
    auto* target = comparison ? fixture.comparison_ : fixture.light_;
    const QSize logicalSize(comparison ? 1280 : 640, 900);
    target->setFixedSize(logicalSize);
    target->ensurePolished();
    target->layout()->activate();
    QImage image(logicalSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    target->render(&image);
    QString surface = "inline";
    const auto id = specimen_->currentData().toString();
    if (id == "dialogs" || id == "confirmations" || id == "nonmodal" || id == "menus") {
        surface = id == "menus" ? "menu" : id == "nonmodal" ? "nonmodal" : "modal";
        QPainter painter(&image);
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{fixture.light_};
        for (int i = 0; i < hosts.size(); ++i) {
            auto* host = hosts[i];
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
                        backdrop->resize(640, 900);
                        backdrop->render(&painter, QPoint(i * 640, 0), {}, QWidget::DrawChildren);
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
                painter.drawPixmap(
                    QPoint(i * 640 + (640 - menu->width()) / 2, (900 - menu->height()) / 2),
                    snapshot);
            } else {
                actual->render(&painter, QPoint(i * 640 + (640 - actual->width()) / 2,
                                                (900 - actual->height()) / 2));
            }
            actual->hide();
        }
    }

    if (id == "tooltip-popover") {
        surface = "tooltip";
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{fixture.light_};
        QPainter painter(&image);
        for (int i = 0; i < hosts.size(); ++i) {
            hosts[i]->findChild<QPushButton*>("previewOpenTooltip")->click();
            auto* tooltip = fixture.findChild<QWidget*>("designTooltip");
            if (!tooltip || !tooltip->isVisible()) {
                return fail(tr("The real tooltip did not become ready."));
            }
            tooltip->render(&painter, QPoint(i * 640 + (640 - tooltip->width()) / 2,
                                             (900 - tooltip->height()) / 2));
            tooltip->hide();
        }
    }
    if (id == "completion") {
        surface = "completion-popup";
        fixture.show();
        fixture.activateWindow();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const QList<QWidget*> hosts = comparison ? QList<QWidget*>{fixture.light_, fixture.dark_}
                                                 : QList<QWidget*>{fixture.light_};
        QPainter painter(&image);
        for (int i = 0; i < hosts.size(); ++i) {
            auto* host = hosts[i];
            auto* completer = host->findChild<QCompleter*>();
            auto* popup = completer->popup();
            popup->setAttribute(Qt::WA_DontShowOnScreen);
            host->findChild<QPushButton*>("previewOpenCompletion")->click();
            QElapsedTimer deadline;
            deadline.start();
            while (!popup->isVisible() && deadline.elapsed() < 2000) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
            }
            if (!popup->isVisible() || popup->model()->rowCount() == 0) {
                return fail(tr("The real completion popup did not become ready."));
            }
            popup->render(&painter, QPoint(i * 640 + 40, 180));
            popup->hide();
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
        {"themes", comparison ? "Light / Dark" : "Light"},
        {"font", resolveTypography(TypographyRole::Ui).family()},
        {"qt", QT_VERSION_STR},
        {"platform", QGuiApplication::platformName()},
        {"os", QSysInfo::prettyProductName()},
        {"fixture", surface == "completion-popup"
                        ? "synthetic; real completion; first candidate selected; reduced motion"
                    : surface == "inline"
                        ? "synthetic; initial state; no focus; reduced motion"
                        : "synthetic; open real surface; no action dispatched; reduced motion"}};
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
