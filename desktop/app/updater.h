#pragma once
#include <QStringList>
namespace choscordb {
class MainWindow;
void installNativeUpdater(MainWindow& window, bool isolated);
#ifdef CHOSCORDB_CROSS_PLATFORM_UPDATER
int runNativeUpdateHelper(const QStringList& arguments);
#endif
#ifdef CHOSCORDB_TEST_UPDATER_MENU
void installNativeUpdaterForTest(MainWindow& window, bool downloadedReady = false);
#endif
}
