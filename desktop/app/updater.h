#pragma once
namespace choscordb {
class MainWindow;
// Production-only native backend. Isolated automation never starts an updater.
void installNativeUpdater(MainWindow& window, bool isolated);
} // namespace choscordb
