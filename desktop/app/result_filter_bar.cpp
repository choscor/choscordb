#include "app/result_filter_bar.h"
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
#include <QVBoxLayout>

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
    column_ = new QComboBox(this);
    column_->setObjectName("resultFilterColumn");
    column_->setAccessibleName(tr("Filter column"));
    operation_ = new QComboBox(this);
    operation_->setObjectName("resultFilterOperator");
    operation_->setAccessibleName(tr("Filter operator"));
    value_ = new QLineEdit(this);
    value_->setObjectName("resultFilterValue");
    value_->setAccessibleName(tr("Filter value"));
    value_->setPlaceholderText(tr("Value"));
    add_ = new QPushButton(tr("Add condition"), this);
    add_->setObjectName("resultFilterAdd");
    remove_ = new QPushButton(tr("Remove selected"), this);
    remove_->setObjectName("resultFilterRemove");
    apply_ = new QPushButton(tr("Apply filters"), this);
    apply_->setObjectName("resultFilterApply");
    clear_ = new QPushButton(tr("Clear filters"), this);
    clear_->setObjectName("resultFilterClear");
    const QList<QWidget*> controlWidgets = {column_, operation_, value_, add_,
                                            remove_, apply_,     clear_};
    for (auto* widget : controlWidgets)
        controls->addWidget(widget);
    root->addLayout(controls);
    list_ = new QListWidget(this);
    list_->setObjectName("resultFilterConditions");
    list_->setAccessibleName(tr("Filter conditions, all must match"));
    list_->setMaximumHeight(design::spacing(design::Spacing::Eight) * 2 +
                            design::spacing(design::Spacing::Two));
    list_->hide();
    root->addWidget(list_);
    error_ = new QLabel(this);
    error_->setObjectName("resultFilterError");
    error_->setAccessibleName(tr("Filter validation"));
    error_->hide();
    root->addWidget(error_);
    connect(column_, &QComboBox::currentIndexChanged, this, &ResultFilterBar::updateOperators);
    connect(operation_, &QComboBox::currentIndexChanged, this, &ResultFilterBar::updateValueState);
    connect(add_, &QPushButton::clicked, this, &ResultFilterBar::addCondition);
    connect(remove_, &QPushButton::clicked, this, &ResultFilterBar::removeCondition);
    connect(apply_, &QPushButton::clicked, this, [this] { emit applyRequested(conditions_); });
    connect(clear_, &QPushButton::clicked, this, [this] {
        conditions_.clear();
        refreshList();
        emit clearRequested();
    });
    setColumns({});
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
    column_->clear();
    for (int index = 0; index < static_cast<int>(columns_.size()); ++index)
        column_->addItem(columns_[index].name, index);
    const bool available = !columns_.empty();
    column_->setEnabled(available);
    operation_->setEnabled(available);
    value_->setEnabled(available);
    add_->setEnabled(available);
    apply_->setEnabled(available && !conditions_.isEmpty());
    updateOperators();
    refreshList();
}

QString ResultFilterBar::valueKind() const {
    const int index = column_->currentData().toInt();
    return index >= 0 && index < columnKinds_.size() ? columnKinds_[index] : QStringLiteral("text");
}

void ResultFilterBar::updateOperators() {
    const auto selected = operation_->currentData().toString();
    operation_->clear();
    const auto kind = valueKind();
    const bool supported = kind != "deferred";
    operation_->setEnabled(supported && column_->isEnabled());
    add_->setEnabled(supported && column_->isEnabled());
    const auto reason = supported ? QString{} : tr("Large deferred values cannot be filtered.");
    operation_->setToolTip(reason);
    value_->setToolTip(reason);
    add_->setToolTip(reason);
    if (!supported) {
        value_->setEnabled(false);
        return;
    }
    if (kind == "text")
        addOperator(operation_, tr("contains"), "contains");
    addOperator(operation_, tr("equals"), "equals");
    addOperator(operation_, tr("does not equal"), "not_equals");
    if (kind == "integer" || kind == "real" || kind == "decimal") {
        addOperator(operation_, tr("is less than"), "less_than");
        addOperator(operation_, tr("is at most"), "less_than_or_equal");
        addOperator(operation_, tr("is greater than"), "greater_than");
        addOperator(operation_, tr("is at least"), "greater_than_or_equal");
    }
    addOperator(operation_, tr("is NULL"), "is_null");
    addOperator(operation_, tr("is not NULL"), "is_not_null");
    const int restore = operation_->findData(selected);
    if (restore >= 0)
        operation_->setCurrentIndex(restore);
    updateValueState();
}

void ResultFilterBar::updateValueState() {
    const auto operation = operation_->currentData().toString();
    const bool needed = operation != "is_null" && operation != "is_not_null";
    value_->setVisible(needed);
    value_->setEnabled(needed && column_->isEnabled());
}

bool ResultFilterBar::validValue(QString* error) const {
    const auto operation = operation_->currentData().toString();
    if (operation == "is_null" || operation == "is_not_null")
        return true;
    const auto text = value_->text();
    const auto kind = valueKind();
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
    QString error;
    if (!validValue(&error)) {
        error_->setText(error);
        error_->show();
        return;
    }
    error_->hide();
    ResultFilterCondition condition;
    condition.column = column_->currentData().toUInt();
    condition.operation = operation_->currentData().toString();
    if (condition.operation != "is_null" && condition.operation != "is_not_null") {
        condition.valueKind = valueKind();
        condition.value = value_->text();
        if (condition.valueKind == "boolean")
            condition.value =
                condition.value == "1" || condition.value.compare("true", Qt::CaseInsensitive) == 0
                    ? QStringLiteral("true")
                    : QStringLiteral("false");
    }
    conditions_.append(condition);
    value_->clear();
    refreshList();
}

void ResultFilterBar::removeCondition() {
    const auto* item = list_->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid())
        return;
    const int row = item->data(Qt::UserRole).toInt();
    if (row < 0 || row >= conditions_.size())
        return;
    conditions_.removeAt(row);
    refreshList();
}

void ResultFilterBar::refreshList() {
    list_->clear();
    const auto addItem = [this](const ResultFilterCondition& condition, const QString& state) {
        const auto column = condition.column < columns_.size()
                                ? columns_[condition.column].name
                                : tr("Column %1").arg(condition.column + 1);
        const auto value = condition.value.isEmpty() ? QString{} : " " + condition.value;
        auto operation = condition.operation;
        operation.replace('_', ' ');
        auto* item = new QListWidgetItem(state + ": " + column + " " + operation + value, list_);
        return item;
    };
    const bool draft = !sameConditions(conditions_, applied_);
    for (int index = 0; index < applied_.size(); ++index) {
        auto* item = addItem(applied_[index], tr("Active"));
        if (!draft)
            item->setData(Qt::UserRole, index);
    }
    if (draft && conditions_.isEmpty())
        list_->addItem(tr("Pending: no filters"));
    else if (draft) {
        for (int index = 0; index < conditions_.size(); ++index)
            addItem(conditions_[index], tr("Pending"))->setData(Qt::UserRole, index);
    }
    list_->setVisible(!applied_.isEmpty() || draft);
    remove_->setEnabled(!conditions_.isEmpty());
    apply_->setEnabled(!columns_.empty() && !conditions_.isEmpty());
    clear_->setEnabled(!conditions_.isEmpty() || !applied_.isEmpty());
}

void ResultFilterBar::setBusy(bool busy) {
    const QList<QWidget*> controlWidgets = {column_, operation_, value_, add_,
                                            remove_, apply_,     clear_};
    for (auto* widget : controlWidgets)
        widget->setEnabled(!busy && widget->isEnabled());
    if (!busy) {
        column_->setEnabled(!columns_.empty());
        updateOperators();
        refreshList();
    }
}

bool ResultFilterBar::draftMatches(const QList<ResultFilterCondition>& conditions) const {
    return sameConditions(conditions_, conditions);
}

void ResultFilterBar::markApplied(const QList<ResultFilterCondition>& conditions) {
    applied_ = conditions;
    refreshList();
}

void ResultFilterBar::reset() {
    conditions_.clear();
    applied_.clear();
    columns_.clear();
    columnKinds_.clear();
    column_->clear();
    operation_->clear();
    value_->clear();
    error_->hide();
    refreshList();
    setColumns({});
}

void ResultFilterBar::restoreApplied() {
    conditions_ = applied_;
    refreshList();
}
} // namespace choscordb
