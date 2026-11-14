#include "tools/preview/preview_states.h"
#include "design_system/metrics/metrics.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QStyleOption>
#include <QTabBar>
#include <QTableView>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace choscordb::design::preview_detail {
namespace {
enum class Family { Field, Item, Tab, ToolButton, Check };

struct SampleState {
    const char* id;
    QStyle::State state;
};

class StateSample final : public QWidget {
  public:
    StateSample(Family family, QWidget* probe, QStyle::State state, QWidget* parent)
        : QWidget(parent), family_(family), probe_(probe), state_(state) {
        setFixedSize(dimension(Dimension::TableColumn),
                     family == Family::Tab ? dimension(Dimension::Tab) : dimension(Dimension::Row));
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        auto* style = probe_->style();
        const auto palette = applicationPalette(resolvedThemeForWidget(*this));
        const auto forced = [this, &palette](QStyleOption& option) {
            option.initFrom(probe_);
            option.rect = rect();
            option.state = state_;
            option.palette = palette;
        };
        if (family_ == Family::Field) {
            QStyleOptionFrame option;
            forced(option);
            option.lineWidth = style->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, probe_);
            style->drawPrimitive(QStyle::PE_PanelLineEdit, &option, &painter, probe_);
            style->drawItemText(&painter, option.rect.adjusted(spacing(Spacing::Two), 0, 0, 0),
                                Qt::AlignVCenter | Qt::AlignLeft, option.palette,
                                option.state.testFlag(QStyle::State_Enabled),
                                QStringLiteral("orders"), QPalette::Text);
        } else if (family_ == Family::ToolButton) {
            QStyleOptionToolButton option;
            forced(option);
            option.text = QStringLiteral("Refresh");
            option.toolButtonStyle = Qt::ToolButtonTextOnly;
            option.subControls = QStyle::SC_ToolButton;
            option.activeSubControls =
                state_.testFlag(QStyle::State_Sunken) ? QStyle::SC_ToolButton : QStyle::SC_None;
            option.state &= ~QStyle::State_HasFocus;
            style->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, probe_);
        } else if (family_ == Family::Check) {
            QStyleOptionButton option;
            forced(option);
            option.text = QStringLiteral("Read only");
            if (!option.state.testFlag(QStyle::State_On))
                option.state |= QStyle::State_Off;
            option.state &= ~QStyle::State_HasFocus;
            style->drawControl(QStyle::CE_CheckBox, &option, &painter, probe_);
        } else if (family_ == Family::Tab) {
            QStyleOptionTab option;
            forced(option);
            option.shape = static_cast<QTabBar*>(probe_)->shape();
            option.position = QStyleOptionTab::OnlyOneTab;
            option.text = QStringLiteral("Columns");
            style->drawControl(QStyle::CE_TabBarTab, &option, &painter, probe_);
        } else {
            QStyleOptionViewItem option;
            forced(option);
            option.text = QStringLiteral("orders");
            option.features = QStyleOptionViewItem::HasDisplay;
            option.showDecorationSelected = true;
            option.state &= ~QStyle::State_HasFocus;
            style->drawControl(QStyle::CE_ItemViewItem, &option, &painter, probe_);
        }
        if (state_.testFlag(QStyle::State_HasFocus)) {
            QStyleOptionFocusRect ring;
            forced(ring);
            ring.state |= QStyle::State_KeyboardFocusChange;
            style->drawPrimitive(QStyle::PE_FrameFocusRect, &ring, &painter, nullptr);
        }
    }

  private:
    Family family_;
    QWidget* probe_;
    QStyle::State state_;
};
}

void populateInteractionStates(QWidget* host, QVBoxLayout* layout) {
    constexpr QStyle::State enabled = QStyle::State_Enabled | QStyle::State_Active;
    const QList<SampleState> fieldStates{{"normal", enabled},
                                         {"hover", enabled | QStyle::State_MouseOver},
                                         {"focus", enabled | QStyle::State_HasFocus},
                                         {"disabled", QStyle::State_Active}};
    const QList<SampleState> itemStates{
        {"normal", enabled},
        {"hover", enabled | QStyle::State_MouseOver},
        {"selected", enabled | QStyle::State_Selected},
        {"selected-hover", enabled | QStyle::State_Selected | QStyle::State_MouseOver},
        {"focus", enabled | QStyle::State_HasFocus},
        {"disabled", QStyle::State_Active}};
    const QList<SampleState> tabStates{
        {"normal", enabled},
        {"hover", enabled | QStyle::State_MouseOver},
        {"pressed", enabled | QStyle::State_Sunken | QStyle::State_MouseOver},
        {"selected", enabled | QStyle::State_Selected},
        {"focus", enabled | QStyle::State_HasFocus},
        {"disabled", QStyle::State_Active}};
    const QList<SampleState> buttonStates{
        {"normal", enabled},
        {"hover", enabled | QStyle::State_MouseOver | QStyle::State_Raised},
        {"pressed", enabled | QStyle::State_MouseOver | QStyle::State_Sunken},
        {"focus", enabled | QStyle::State_HasFocus},
        {"disabled", QStyle::State_Active}};
    const QList<SampleState> checkStates{
        {"normal", enabled},
        {"hover", enabled | QStyle::State_MouseOver},
        {"pressed", enabled | QStyle::State_MouseOver | QStyle::State_Sunken},
        {"checked", enabled | QStyle::State_On},
        {"focus", enabled | QStyle::State_HasFocus},
        {"disabled", QStyle::State_Active}};
    struct Row {
        const char* id;
        const char* title;
        Family family;
        QWidget* probe;
        const QList<SampleState>* states;
    };
    const QList<Row> rows{
        {"field", "Field", Family::Field, new QLineEdit(host), &fieldStates},
        {"list-row", "List row", Family::Item, new QListView(host), &itemStates},
        {"table-cell", "Table cell", Family::Item, new QTableView(host), &itemStates},
        {"tree-row", "Tree row", Family::Item, new QTreeView(host), &itemStates},
        {"pane-tab", "Pane tab", Family::Tab, new QTabBar(host), &tabStates},
        {"tool-button", "Tool button", Family::ToolButton, new QToolButton(host), &buttonStates},
        {"check-box", "Check box", Family::Check, new QCheckBox(host), &checkStates}};
    constexpr int columns = 3;
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(spacing(Spacing::Three));
    grid->setVerticalSpacing(spacing(Spacing::Two));
    int gridRow = 0;
    for (const auto& entry : rows) {
        entry.probe->hide();
        auto* title = new Text(entry.title, host);
        title->setTextStyle(TextStyle::Headline);
        grid->addWidget(title, gridRow++, 0, 1, columns);
        for (int index = 0; index < entry.states->size(); ++index) {
            const auto& state = entry.states->at(index);
            auto* cell = new QVBoxLayout;
            auto* sample = new StateSample(entry.family, entry.probe, state.state, host);
            sample->setObjectName(QStringLiteral("state-%1-%2").arg(entry.id, state.id));
            sample->setAccessibleName(QStringLiteral("%1 %2").arg(entry.title, state.id));
            cell->addWidget(sample);
            auto* caption = new Text(state.id, host);
            caption->setTextStyle(TextStyle::Caption);
            caption->setProperty("designRole", "description");
            cell->addWidget(caption);
            grid->addLayout(cell, gridRow + index / columns, index % columns);
        }
        gridRow += (static_cast<int>(entry.states->size()) + columns - 1) / columns;
    }
    layout->addLayout(grid);
    layout->addStretch();
}
}
