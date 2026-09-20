#pragma once

#include <QObject>

class NavigatorSqlWorkspaceTest : public QObject {
    Q_OBJECT

  private slots:
    void sqlRowActionsLiveInResultContextMenu();
    void sqlOpensWithEqualEditorAndResults();
    void sqlToolbarInsetsControls();
    void tabContextMenuFollowsCursor();
    void tabContextMenuClosesRequestedDocuments_data();
    void tabContextMenuClosesRequestedDocuments();
    void tabContextMenuDisablesEmptyGroupsAndIgnoresEmptySpace();
    void tabContextMenuStopsBulkCloseWhenDiscardIsCancelled();
    void unusedObjectInspectorDoesNotCoverSidebar();
    void resultsAutomaticallyShowGridOrFailureMessage();
    void capturesSqlAndObjectTabsAtBothWorkspaceWidths();
    void sqlHeaderSitsBetweenWorkspaceTabsAndEditor();
    void workspaceTabsUseContentWidth();
    void sidebarPanelsSwitchWithoutChangingTheSqlTarget();
    void savedPanelFiltersFolderTreeAndReusesEditedTab();
    void historySearchAppliesToRefreshedFullSql();
    void historySidebarReusesRecordIdAndKeepsDistinctIdenticalSql();
    void historyNavigationStaysInSidebar();
    void recoveryActionsRemainInMenuWithoutToolButtons();
    void objectTabsUseConnectionAndQualifiedIdentity();
    void sidebarViewsKeepTheirDefaultPane();
    void savedProfileIdCannotCollideWithSessionContext();
    void selectedConnectionShowsOnlyItsTree();
    void searchLoadsCollapsedGroupsOnlyForSelectedConnection();
    void searchFailureKeepsRefineMessage();
    void selectingObjectLoadsMetadataAndSeparateDataThenReturnsToSql();
    void objectDataKeepsCancelPaneVisibleUntilAcknowledged();
    void documentsKeepTargetsAndImmutableResultOrigin();
    void activeExecutionKeepsDocumentAndCancelReachable();
    void disconnectMenuKeepsOtherSessionsAndDrafts();
    void generationOpensDraftOnExistingSavedConnectionWithoutExecuting();
};
