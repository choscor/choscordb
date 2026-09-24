#include "app/result_filter_bar.h"
#include "design_system/button/button.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace choscordb {
namespace {
QString kindForType(QString type) {
    type = type.toLower();
    if (type.contains("interval"))
        return "text";
    if (type == "tiny" || type == "short" || type == "long" || type == "longlong" ||
        type == "int24" || type == "year")
        return "integer";
    if (type == "float" || type == "double")
        return "real";
    if (type == "decimal" || type == "newdecimal")
        return "decimal";
    if (type == "bit")
        return "binary";
    if (type.contains("bool"))
        return "boolean";
    if (type.contains("int") || type == "serial" || type == "bigserial")
        return "integer";
    if (type.contains("numeric") || type.contains("decimal"))
        return "decimal";
    if (type.contains("real") || type.contains("float") || type.contains("double"))
        return "real";
    if (type.contains("timestamp") || type.contains("datetime"))
        return "timestamp";
    if (type == "date")
        return "date";
    if (type.startsWith("time"))
        return "time";
    if (type.contains("uuid"))
        return "uuid";
    if (type.contains("json"))
        return "json";
    if (type.contains("blob") || type.contains("binary") || type.contains("bytea"))
        return "binary";
    return "text";
}

void addOperator(QComboBox* box, const QString& label, const QString& id) {
    box->addItem(label, id);
}

bool sameConditions(const QList<ResultFilterCondition>& left,
                    const QList<ResultFilterCondition>& right) {
    if (left.size() != right.size())
        return false;
    for (int index = 0; index < left.size(); ++index) {
        if (left[index].column != right[index].column ||
            left[index].operation != right[index].operation ||
            left[index].valueKind != right[index].valueKind ||
            left[index].value != right[index].value)
            return false;
    }
    return true;
}
} // namespace

ResultFilterBar::ResultFilterBar(QWidget* parent) : QWidget(parent) {
    setObjectName("resultFilterBar");
    setAccessibleName(tr("Result filters"));
    auto* root = new QVBoxLayout(this);
    const auto horizontalInset = design::spacing(design::Spacing::Two);
    const auto verticalInset = design::spacing(design::Spacing::OneHalf);
    root->setContentsMargins(horizontalInset, verticalInset, horizontalInset, verticalInset);
    root->setSpacing(design::spacing(design::Spacing::One));
    auto* controls = new QHBoxLayout;
    const auto button = [this](const QString& name, const QString& label, design::Icon icon) {
        auto* result = new design::Button({}, this);
        result->setObjectName(name);
        result->setAccessibleName(label);
        result->setToolTip(label);
        result->setDesignIcon(icon);
        result->setButtonSize(design::ButtonSize::Icon);
        result->setVariant(design::ButtonVariant::Ghost);
        return result;
    };
    mode_ = button("resultFilterMode", tr("Switch to SQL filter"), design::Icon::Code);
    mode_->setCheckable(true);
    controls->addWidget(mode_, 0, Qt::AlignTop);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    manual_ = scroll;
    auto* rowsWidget = new QWidget(scroll);
    rowsLayout_ = new QVBoxLayout(rowsWidget);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(design::spacing(design::Spacing::One));
    rowsLayout_->setAlignment(Qt::AlignTop);
    scroll->setWidget(rowsWidget);
    controls->addWidget(manual_, 1);
    sql_ = new QLineEdit(this);
    sql_->setObjectName("resultFilterSql");
    sql_->setAccessibleName(tr("SQL filter expression"));
    sql_->setPlaceholderText(tr("SQL condition, e.g. id > 10 AND name LIKE 'A%'"));
    sql_->setToolTip(tr("SQLite WHERE condition over loaded results; omit WHERE. "
                        "Example: id IN (1,2) AND name LIKE 'A%'"));
    controls->addWidget(sql_, 1);
    add_ = button("resultFilterAdd", tr("Add condition"), design::Icon::Add);
    apply_ = button("resultFilterApply", tr("Apply filters"), design::Icon::Search);
    clear_ = button("resultFilterClear", tr("Clear filters"), design::Icon::Close);
    for (auto* action : {add_, apply_, clear_})
        controls->addWidget(action, 0, Qt::AlignTop);
    root->addLayout(controls);
    list_ = new QListWidget(this);
    list_->setObjectName("resultFilterConditions");
    list_->setAccessibleName(tr("Filter conditions, all must match"));
    list_->setMaximumHeight(design::spacing(design::Spacing::Eight) * 2 +
                            design::spacing(design::Spacing::Two));
    root->addWidget(list_);
    error_ = new QLabel(this);
    error_->setObjectName("resultFilterError");
    error_->setAccessibleName(tr("Filter validation"));
    error_->setWordWrap(true);
    error_->hide();
    root->addWidget(error_);
    connect(mode_, &QPushButton::clicked, this, [this] {
        sqlMode_ = mode_->isChecked();
        error_->hide();
        updateMode();
        refreshList();
    });
    connect(sql_, &QLineEdit::textChanged, this, [this] { refreshList(); });
    connect(sql_, &QLineEdit::returnPressed, this, &ResultFilterBar::submit);
    connect(add_, &QPushButton::clicked, this, &ResultFilterBar::addCondition);
    connect(apply_, &QPushButton::clicked, this, &ResultFilterBar::submit);
    connect(clear_, &QPushButton::clicked, this, [this] {
        loadDraft({});
        emit clearRequested();
    });
    addCondition();
    dirty_ = false;
    setColumns({});
}

ResultFilterBar::~ResultFilterBar() {
    qDeleteAll(rows_);
}

void ResultFilterBar::setColumns(const std::vector<ResultColumn>& columns,
                                 const std::vector<ResultTableModel::Row>& samples) {
    columns_ = columns;
    columnKinds_.clear();
    for (size_t column = 0; column < columns_.size(); ++column) {
        QString kind = columns_[column].databaseType.isEmpty()
                           ? QString{}
                           : kindForType(columns_[column].databaseType);
        if (kind.isEmpty()) {
            for (const auto& row : samples) {
                if (column >= row.size() || std::holds_alternative<std::monostate>(row[column]))
                    continue;
                kind = std::visit(
                    [](const auto& value) -> QString {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, bool>)
                            return "boolean";
                        if constexpr (std::is_same_v<T, qint64>)
                            return "integer";
                        if constexpr (std::is_same_v<T, double>)
                            return "real";
                        if constexpr (std::is_same_v<T, QByteArray>)
                            return "binary";
                        if constexpr (std::is_same_v<T, DeferredValue>)
                            return "deferred";
                        return "text";
                    },
                    row[column]);
                break;
            }
        }
        columnKinds_.append(kind.isEmpty() ? QStringLiteral("text") : kind);
    }
    updating_ = true;
    for (auto* row : rows_) {
        const QSignalBlocker columnSignals(row->column);
        const int selected = row->column->currentIndex();
        row->column->clear();
        for (int index = 0; index < static_cast<int>(columns_.size()); ++index)
            row->column->addItem(columns_[index].name, index);
        if (selected >= 0 && selected < row->column->count())
            row->column->setCurrentIndex(selected);
        updateOperators(row);
    }
    updating_ = false;
    updateMode();
    refreshList();
}

QString ResultFilterBar::valueKind(const Row* row) const {
    const int index = row->column->currentData().toInt();
    return index >= 0 && index < columnKinds_.size() ? columnKinds_[index] : QStringLiteral("text");
}

void ResultFilterBar::updateOperators(Row* row) {
    const auto selected = row->operation->currentData().toString();
    row->operation->clear();
    const auto kind = valueKind(row);
    const bool supported = kind != "deferred";
    row->operation->setEnabled(supported && !columns_.empty() && !busy_);
    const auto reason = supported ? QString{} : tr("Large deferred values cannot be filtered.");
    row->operation->setToolTip(reason);
    row->value->setToolTip(reason);
    if (!supported) {
        row->value->setEnabled(false);
        return;
    }
    if (kind == "text")
        addOperator(row->operation, tr("contains"), "contains");
    addOperator(row->operation, tr("equals"), "equals");
    addOperator(row->operation, tr("does not equal"), "not_equals");
    if (kind == "integer" || kind == "real" || kind == "decimal") {
        addOperator(row->operation, tr("is less than"), "less_than");
        addOperator(row->operation, tr("is at most"), "less_than_or_equal");
        addOperator(row->operation, tr("is greater than"), "greater_than");
        addOperator(row->operation, tr("is at least"), "greater_than_or_equal");
    }
    if (kind == "text") {
        addOperator(row->operation, tr("like"), "like");
        addOperator(row->operation, tr("not like"), "not_like");
    }
    addOperator(row->operation, tr("in"), "in");
    addOperator(row->operation, tr("is NULL"), "is_null");
    addOperator(row->operation, tr("is not NULL"), "is_not_null");
    const int restore = row->operation->findData(selected);
    if (restore >= 0)
        row->operation->setCurrentIndex(restore);
    updateValueState(row);
}

void ResultFilterBar::updateValueState(Row* row) {
    const auto operation = row->operation->currentData().toString();
    const bool needed = operation != "is_null" && operation != "is_not_null";
    row->value->setVisible(needed);
    row->value->setEnabled(needed && !columns_.empty() && !busy_);
}

bool ResultFilterBar::validValue(const Row* row, QString* error) const {
    const auto operation = row->operation->currentData().toString();
    if (operation == "is_null" || operation == "is_not_null")
        return true;
    const auto text = row->value->text();
    if (operation == "in") {
        if (text.trimmed().isEmpty()) {
            if (error)
                *error = tr("Enter a comma-separated SQL literal list.");
            return false;
        }
        return true; // The result engine validates the complete SQL literal list on submission.
    }
    const auto kind = valueKind(row);
    bool valid = kind == "text" || !text.isEmpty();
    if (kind == "boolean")
        valid = text.compare("true", Qt::CaseInsensitive) == 0 ||
                text.compare("false", Qt::CaseInsensitive) == 0 || text == "0" || text == "1";
    else if (kind == "integer")
        text.toLongLong(&valid);
    else if (kind == "real")
        text.toDouble(&valid);
    else if (kind == "decimal")
        valid = QRegularExpression(R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$)")
                    .match(text)
                    .hasMatch();
    else if (kind == "date")
        valid = QDate::fromString(text, Qt::ISODate).isValid();
    else if (kind == "time")
        valid = QRegularExpression(
                    QStringLiteral(R"(^[+-]?(?:\d{1,3}):[0-5]\d:[0-5]\d(?:\.\d{1,6})?$)"))
                    .match(text)
                    .hasMatch();
    else if (kind == "timestamp") {
        auto isoText = text;
        if (isoText.size() > 10 && isoText[10] == ' ')
            isoText[10] = 'T';
        valid = QDateTime::fromString(isoText, Qt::ISODateWithMs).isValid() ||
                QDateTime::fromString(isoText, Qt::ISODate).isValid();
    } else if (kind == "binary")
        valid = text.size() % 2 == 0 &&
                QRegularExpression(QStringLiteral("^[0-9A-Fa-f]*$")).match(text).hasMatch();
    if (!valid && error)
        *error = tr("Enter a valid %1 value.").arg(kind);
    return valid;
}

void ResultFilterBar::addCondition() {
    auto* row = new Row;
    row->widget = new QWidget(rowsLayout_->parentWidget());
    auto* layout = new QHBoxLayout(row->widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* conjunction = new QLabel(row->widget);
    conjunction->setObjectName("resultFilterConjunction");
    conjunction->setFixedWidth(conjunction->fontMetrics().horizontalAdvance(tr("AND")));
    layout->addWidget(conjunction);
    row->column = new QComboBox(row->widget);
    row->column->setObjectName("resultFilterColumn");
    row->column->setAccessibleName(tr("Filter column"));
    row->operation = new QComboBox(row->widget);
    row->operation->setObjectName("resultFilterOperator");
    row->operation->setAccessibleName(tr("Filter operator"));
    row->value = new QLineEdit(row->widget);
    row->value->setObjectName("resultFilterValue");
    row->value->setAccessibleName(tr("Filter value"));
    row->value->setPlaceholderText(tr("Enter value"));
    auto* remove = new design::Button({}, row->widget);
    row->remove = remove;
    remove->setObjectName("resultFilterRemove");
    remove->setAccessibleName(tr("Remove condition"));
    remove->setToolTip(tr("Remove condition"));
    remove->setDesignIcon(design::Icon::Close);
    remove->setButtonSize(design::ButtonSize::Icon);
    remove->setVariant(design::ButtonVariant::Ghost);
    layout->addWidget(row->column);
    layout->addWidget(row->operation);
    layout->addWidget(row->value, 1);
    layout->addWidget(row->remove);
    for (int index = 0; index < static_cast<int>(columns_.size()); ++index)
        row->column->addItem(columns_[index].name, index);
    rows_.append(row);
    rowsLayout_->addWidget(row->widget);
    updateOperators(row);
    connect(row->column, &QComboBox::currentIndexChanged, this, [this, row] {
        updateOperators(row);
        if (!updating_) {
            dirty_ = true;
            refreshList();
        }
    });
    connect(row->operation, &QComboBox::currentIndexChanged, this, [this, row] {
        updateValueState(row);
        if (!updating_) {
            dirty_ = true;
            refreshList();
        }
    });
    connect(row->value, &QLineEdit::textChanged, this, [this] {
        if (!updating_) {
            dirty_ = true;
            refreshList();
        }
    });
    connect(row->value, &QLineEdit::returnPressed, this, &ResultFilterBar::submit);
    connect(row->remove, &QPushButton::clicked, this, [this, row] { removeCondition(row); });
    dirty_ = true;
    error_->hide();
    refreshList();
}

void ResultFilterBar::removeCondition(Row* row) {
    if (rows_.size() == 1) {
        loadDraft({});
        return;
    }
    rows_.removeOne(row);
    delete row->widget;
    delete row;
    refreshList();
}

QList<ResultFilterCondition> ResultFilterBar::conditions() const {
    if (sqlMode_)
        return sql_->text().trimmed().isEmpty()
                   ? QList<ResultFilterCondition>{}
                   : QList<ResultFilterCondition>{{0, "sql", "text", sql_->text()}};
    QList<ResultFilterCondition> result;
    if (!dirty_ || columns_.empty())
        return result;
    for (const auto* row : rows_) {
        ResultFilterCondition condition;
        condition.column = row->column->currentData().toUInt();
        condition.operation = row->operation->currentData().toString();
        if (condition.operation != "is_null" && condition.operation != "is_not_null") {
            condition.valueKind = valueKind(row);
            condition.value = row->value->text();
            if (condition.valueKind == "boolean" && condition.operation != "in") {
                if (condition.value == "1" ||
                    condition.value.compare("true", Qt::CaseInsensitive) == 0)
                    condition.value = "true";
                else if (condition.value == "0" ||
                         condition.value.compare("false", Qt::CaseInsensitive) == 0)
                    condition.value = "false";
            }
        }
        result.append(condition);
    }
    return result;
}

void ResultFilterBar::submit() {
    if (busy_ || columns_.empty())
        return;
    QString error;
    if (sqlMode_) {
        if (sql_->text().trimmed().isEmpty())
            error = tr("Enter a SQL filter condition.");
    } else {
        for (const auto* row : rows_) {
            if (row->operation->currentIndex() < 0) {
                error = tr("Choose a supported filter column and operator.");
                break;
            }
            if (!validValue(row, &error))
                break;
        }
    }
    error_->setText(error);
    error_->setVisible(!error.isEmpty());
    if (!error.isEmpty())
        return;
    dirty_ = true;
    refreshList();
    emit applyRequested(conditions());
}

void ResultFilterBar::refreshList() {
    for (int index = 0; index < rows_.size(); ++index)
        rows_[index]
            ->widget->findChild<QLabel*>("resultFilterConjunction")
            ->setText(index == 0 ? QString{} : tr("AND"));
    const int rowHeight = design::dimension(design::Dimension::Input);
    manual_->setFixedHeight(std::min(rows_.size(), qsizetype(3)) *
                                (rowHeight + design::spacing(design::Spacing::One)) +
                            design::spacing(design::Spacing::One));
    list_->clear();
    const auto draftConditions = conditions();
    const auto addItem = [this](const ResultFilterCondition& condition, const QString& state) {
        const auto column = condition.operation == "sql" ? tr("SQL")
                            : condition.column < columns_.size()
                                ? columns_[condition.column].name
                                : tr("Column %1").arg(condition.column + 1);
        auto operation = condition.operation;
        operation.replace('_', ' ');
        list_->addItem(
            state + ": " + column + " " +
            (condition.operation == "sql" ? condition.value : operation + " " + condition.value));
    };
    for (const auto& condition : applied_)
        addItem(condition, tr("Active"));
    list_->setVisible(!applied_.isEmpty());
    apply_->setEnabled(!busy_ && !columns_.empty());
    clear_->setEnabled(!busy_ && (!draftConditions.isEmpty() || !applied_.isEmpty()));
}

void ResultFilterBar::updateMode() {
    mode_->setChecked(sqlMode_);
    const auto label = sqlMode_ ? tr("Switch to manual filters") : tr("Switch to SQL filter");
    mode_->setToolTip(label);
    mode_->setAccessibleName(label);
    manual_->setVisible(!sqlMode_);
    sql_->setVisible(sqlMode_);
    add_->setVisible(!sqlMode_);
    const bool available = !busy_ && !columns_.empty();
    for (auto* widget : QList<QWidget*>{mode_, manual_, sql_, add_})
        widget->setEnabled(available);
}

void ResultFilterBar::setBusy(bool busy) {
    busy_ = busy;
    updateMode();
    updating_ = true;
    for (auto* row : rows_)
        updateOperators(row);
    updating_ = false;
    refreshList();
}

bool ResultFilterBar::draftMatches(const QList<ResultFilterCondition>& filters) const {
    return sameConditions(conditions(), filters);
}

void ResultFilterBar::markApplied(const QList<ResultFilterCondition>& filters) {
    applied_ = filters;
    refreshList();
}

void ResultFilterBar::loadDraft(const QList<ResultFilterCondition>& filters) {
    updating_ = true;
    while (rows_.size() > 1) {
        auto* row = rows_.takeLast();
        delete row->widget;
        delete row;
    }
    sql_->clear();
    sqlMode_ = !filters.isEmpty() && filters.first().operation == "sql";
    if (sqlMode_)
        sql_->setText(filters.first().value);
    auto* first = rows_.first();
    first->column->setCurrentIndex(columns_.empty() ? -1 : 0);
    updateOperators(first);
    first->operation->setCurrentIndex(first->operation->findData("equals"));
    first->value->clear();
    if (!sqlMode_) {
        for (int index = 0; index < filters.size(); ++index) {
            if (index > 0)
                addCondition();
            auto* row = rows_[index];
            row->column->setCurrentIndex(static_cast<int>(filters[index].column));
            updateOperators(row);
            row->operation->setCurrentIndex(row->operation->findData(filters[index].operation));
            row->value->setText(filters[index].value);
        }
    }
    dirty_ = !filters.isEmpty();
    updating_ = false;
    error_->hide();
    updateMode();
    refreshList();
}

void ResultFilterBar::reset() {
    applied_.clear();
    loadDraft({});
    setColumns({});
}

void ResultFilterBar::showValidationError(const QString& error) {
    error_->setText(error);
    error_->setVisible(!error.isEmpty());
}

void ResultFilterBar::restoreApplied() {
    loadDraft(applied_);
}
} // namespace choscordb
