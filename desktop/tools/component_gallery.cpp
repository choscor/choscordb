#include "design_system/control_style.h"
#include "tools/preview/preview_window.h"

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
    parser.addOption({"section", "Select Tokens, Typography, Icons, or Components.", "name"});
    parser.addOption({"specimen", "Select a specimen ID (see --list).", "id"});
    parser.addOption(
        {"single", "Export only the Light specimen (640x900); default comparison is 1280x900."});
    parser.addOption({"theme", "Export one theme: light or dark.", "name"});
    parser.addOption({"width", "Capture width in logical pixels (320–2560).", "pixels"});
    parser.addOption({"height", "Capture height in logical pixels (320–1800).", "pixels"});
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
        const auto theme = parser.value("theme");
        const bool comparison = !parser.isSet("single") && !parser.isSet("theme");
        bool widthValid = true;
        bool heightValid = true;
        const int width = parser.isSet("width") ? parser.value("width").toInt(&widthValid)
                          : comparison          ? 1280
                                                : 640;
        const int height =
            parser.isSet("height") ? parser.value("height").toInt(&heightValid) : 900;
        if ((parser.isSet("theme") && theme != "light" && theme != "dark") || !widthValid ||
            !heightValid || width < 320 || width > 2560 || height < 320 || height > 1800 ||
            (comparison && (width < 640 || width % 2 != 0))) {
            QTextStream(stderr) << "Invalid capture theme or dimensions. See --help.\n";
            return 2;
        }
        const bool success =
            window.exportCapture(parser.value("export"), comparison, QSize(width, height),
                                 theme == "dark" ? choscordb::design::ResolvedAppearance::Dark
                                                 : choscordb::design::ResolvedAppearance::Light);
        const auto* status = window.findChild<QLabel*>("previewExportStatus");
        QTextStream(success ? stdout : stderr) << status->text() << '\n';
        return success ? 0 : 1;
    }
    window.show();
    return application.exec();
}
