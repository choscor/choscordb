#include "design_system/theme_manager.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QApplication::setApplicationName("ChoscorDB Component Gallery");
    choscordb::design::ThemeManager theme;
    theme.installOn(&application);

    QMainWindow window;
    window.setWindowTitle(QObject::tr("ChoscorDB · Component gallery"));
    auto* body = new QWidget(&window);
    auto* layout = new QVBoxLayout(body);
    auto* selectors = new QFormLayout;
    auto* mode = new QComboBox(body);
    mode->addItems({QObject::tr("System"), QObject::tr("Light"), QObject::tr("Dark")});
    auto* density = new QComboBox(body);
    density->addItems({QObject::tr("Compact"), QObject::tr("Comfortable")});
    selectors->addRow(QObject::tr("Theme"), mode);
    selectors->addRow(QObject::tr("Density"), density);
    layout->addLayout(selectors);

    auto* controls = new QGroupBox(QObject::tr("Control states"), body);
    auto* controlLayout = new QFormLayout(controls);
    auto* normal = new QPushButton(QObject::tr("Primary action"), controls);
    normal->setProperty("primary", true);
    auto* disabled = new QPushButton(QObject::tr("Disabled action"), controls);
    disabled->setEnabled(false);
    auto* selectable = new QCheckBox(QObject::tr("Selected option"), controls);
    selectable->setChecked(true);
    auto* empty = new QLineEdit(controls);
    empty->setPlaceholderText(QObject::tr("Empty input"));
    auto* loading = new QProgressBar(controls);
    loading->setRange(0, 0);
    loading->setAccessibleName(QObject::tr("Loading"));
    auto* success = new QLabel(QObject::tr("✓ Completed"), controls);
    success->setProperty("state", "success");
    auto* warning = new QLabel(QObject::tr("! Review required"), controls);
    warning->setProperty("state", "warning");
    auto* error = new QLabel(QObject::tr("× Operation failed"), controls);
    error->setProperty("state", "error");
    controlLayout->addRow(QObject::tr("Normal"), normal);
    controlLayout->addRow(QObject::tr("Disabled"), disabled);
    controlLayout->addRow(QObject::tr("Selected"), selectable);
    controlLayout->addRow(QObject::tr("Empty"), empty);
    controlLayout->addRow(QObject::tr("Loading"), loading);
    controlLayout->addRow(QObject::tr("Success"), success);
    controlLayout->addRow(QObject::tr("Warning"), warning);
    controlLayout->addRow(QObject::tr("Error"), error);
    layout->addWidget(controls);
    layout->addWidget(new QLabel(
        QObject::tr("Use Tab and Space to inspect focus, hover, and pressed feedback."), body));

    auto* table = new QTableWidget(3, 3, body);
    table->setHorizontalHeaderLabels(
        {QObject::tr("id · int8"), QObject::tr("name · text"), QObject::tr("state")});
    table->setItem(0, 0, new QTableWidgetItem("1042"));
    table->setItem(0, 1, new QTableWidgetItem("Sample row"));
    table->setItem(0, 2, new QTableWidgetItem("ready"));
    table->setItem(1, 0, new QTableWidgetItem("1041"));
    table->setItem(1, 1, new QTableWidgetItem(""));
    table->setItem(1, 2, new QTableWidgetItem("NULL"));
    table->setItem(2, 0, new QTableWidgetItem("1040"));
    table->setItem(2, 1, new QTableWidgetItem("Selected row"));
    table->setItem(2, 2, new QTableWidgetItem("warning"));
    table->selectRow(2);
    layout->addWidget(table);
    window.setCentralWidget(body);

    QObject::connect(mode, &QComboBox::currentIndexChanged, &theme, [&theme](int index) {
        theme.setMode(index == 1   ? choscordb::design::ThemeMode::Light
                      : index == 2 ? choscordb::design::ThemeMode::Dark
                                   : choscordb::design::ThemeMode::System);
    });
    QObject::connect(density, &QComboBox::currentIndexChanged, &theme, [&theme](int index) {
        theme.setDensity(index == 0 ? choscordb::design::Density::Compact
                                    : choscordb::design::Density::Comfortable);
    });
    window.resize(theme.metrics().minimumWorkspaceWidth, theme.metrics().minimumWorkspaceHeight);
    window.show();
    return application.exec();
}
