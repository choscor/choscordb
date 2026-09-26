#include "app/result_filter_bar.h"
#include "design_system/button/button.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
namespace {
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
    root->setContentsMargins(
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::OneHalf),
        design::spacing(design::Spacing::Two), design::spacing(design::Spacing::OneHalf));
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
    sql_ = new QLineEdit(this);
    sql_->setObjectName("resultFilterSql");
    sql_->setAccessibleName(tr("SQL filter expression"));
    sql_->setPlaceholderText(tr("SQL condition, e.g. id > 10 AND name LIKE 'A%'"));
    sql_->setToolTip(tr("SQLite WHERE condition over loaded results; omit WHERE. "
                        "Example: id IN (1,2) AND name LIKE 'A%'"));
    controls->addWidget(sql_, 1);
    apply_ = button("resultFilterApply", tr("Apply filters"), design::Icon::Search);
    clear_ = button("resultFilterClear", tr("Clear filters"), design::Icon::Close);
    controls->addWidget(apply_, 0, Qt::AlignTop);
    controls->addWidget(clear_, 0, Qt::AlignTop);
    root->addLayout(controls);
    error_ = new QLabel(this);
    error_->setObjectName("resultFilterError");
    error_->setAccessibleName(tr("Filter validation"));
    error_->setWordWrap(true);
    error_->hide();
    root->addWidget(error_);
    connect(sql_, &QLineEdit::textChanged, this, &ResultFilterBar::refreshActions);
    connect(sql_, &QLineEdit::returnPressed, this, &ResultFilterBar::submit);
    connect(apply_, &QPushButton::clicked, this, &ResultFilterBar::submit);
    connect(clear_, &QPushButton::clicked, this, [this] {
        loadDraft({});
        emit clearRequested();
    });
    refreshActions();
}

void ResultFilterBar::setColumns(const std::vector<ResultColumn>& columns,
                                 const std::vector<ResultTableModel::Row>&) {
    hasColumns_ = !columns.empty();
    refreshActions();
}

QList<ResultFilterCondition> ResultFilterBar::conditions() const {
    return sql_->text().trimmed().isEmpty()
               ? QList<ResultFilterCondition>{}
               : QList<ResultFilterCondition>{{0, "sql", "text", sql_->text()}};
}

void ResultFilterBar::submit() {
    if (busy_ || !hasColumns_)
        return;
    const auto error =
        sql_->text().trimmed().isEmpty() ? tr("Enter a SQL filter condition.") : QString{};
    showValidationError(error);
    if (!error.isEmpty())
        return;
    emit applyRequested(conditions());
}

void ResultFilterBar::refreshActions() {
    sql_->setEnabled(!busy_ && hasColumns_);
    apply_->setEnabled(!busy_ && hasColumns_);
    clear_->setEnabled(!busy_ && (!conditions().isEmpty() || !applied_.isEmpty()));
}

void ResultFilterBar::setBusy(bool busy) {
    busy_ = busy;
    refreshActions();
}

bool ResultFilterBar::draftMatches(const QList<ResultFilterCondition>& filters) const {
    return sameConditions(conditions(), filters);
}

void ResultFilterBar::markApplied(const QList<ResultFilterCondition>& filters) {
    applied_ = filters;
    refreshActions();
}

void ResultFilterBar::loadDraft(const QList<ResultFilterCondition>& filters) {
    sql_->setText(!filters.isEmpty() && filters.first().operation == "sql" ? filters.first().value
                                                                           : QString{});
    error_->hide();
    refreshActions();
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
