#include "design_system/control_style.h"
#include "design_system/preview_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QTextStream>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QApplication::setApplicationName("ChoscorDB Component Gallery");
    QApplication::setStyle(new choscordb::design::ControlStyle);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Offline native design-system preview; no profile or database is opened.");
    parser.addHelpOption();
    parser.addOption(
        {"export", "Write a deterministic PNG and .json metadata, then exit.", "path"});
    parser.addOption({"section",
                      "Select Tokens, Typography, Icons, Components, Compositions, or Database UI.",
                      "name"});
    parser.addOption({"specimen", "Select a specimen ID (see --list).", "id"});
    parser.addOption(
        {"single", "Export only the Light specimen (640x900); default comparison is 1280x900."});
    parser.addOption({"list", "List specimen IDs and exit."});
    parser.process(application);
    choscordb::design::PreviewWindow window;
    if (parser.isSet("list")) {
        QTextStream(stdout) << window.specimenIds().join('\n') << '\n';
        return 0;
    }
    if (parser.isSet("section") && !window.selectSection(parser.value("section"))) {
        QTextStream(stderr) << "Unknown section: " << parser.value("section") << '\n';
        return 2;
    }
    if (parser.isSet("specimen") && !window.selectSpecimen(parser.value("specimen"))) {
        QTextStream(stderr) << "Unknown specimen: " << parser.value("specimen") << '\n';
        return 2;
    }
    if (parser.isSet("export")) {
        const bool success = window.exportCapture(parser.value("export"), !parser.isSet("single"));
        const auto* status = window.findChild<QLabel*>("previewExportStatus");
        QTextStream(success ? stdout : stderr) << status->text() << '\n';
        return success ? 0 : 1;
    }
    window.show();
    return application.exec();
}
