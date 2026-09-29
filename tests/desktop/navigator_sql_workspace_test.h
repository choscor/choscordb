#pragma once

#include <QObject>

class NavigatorSqlWorkspaceTest : public QObject {
    Q_OBJECT

  private slots:
    void sqlRowActionsLiveInResultContextMenu();
    void sqlRowJsonUsesClickedRowAndCopiesDisplayedDocument();
    void sqlCellJsonUsesClickedCellAndValidatesEligibility();
    void sqlTableJsonIncludesLoadedPageFromBlankSpace();
    void sqlCellAndTableJsonLoadFullDeferredText();
    void sqlMalformedRowJsonShowsErrorWithoutCopy();
    void sqlJsonViewsCloseAndDisableAfterDisconnect();
    void sqlTableJsonTracksRealPageControls();
    void sqlJsonActionsColorAndCopyInBothThemes();
    void sqlJsonActionsLeaveDatabaseAndSelectionIntact();
    void sqlRowJsonLoadsFullDeferredBinaryAndRejectsOversizedValue();
    void sqlRowJsonLoadsUtf8TextAndIgnoresReplacedResult();
    void sqlRowJsonDoesNotChangeDatabaseContents();
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
    void historySidebarFormatsSqlAndShowsEntryDetails();
    void historySidebarReusesRecordIdAndKeepsDistinctIdenticalSql();
    void historyNavigationStaysInSidebar();
    void recoveryActionsRemainInMenuWithoutToolButtons();
    void recoveryFailuresUsePersistentWindowToast();
    void recoveryToastDismissalKeepsActionsAndLaterFailure();
    void startNewWhileRestorePendingKeepsRecoveryBlocked();
    void startNewAfterRestoreFailureResolvesNotice();
    void rejectedRecoverySubmissionShowsBackendDetail();
    void objectTabsUseConnectionAndQualifiedIdentity();
    void erdKeyboardActivationOpensAndRefocusesRelatedTable();
    void sidebarViewsKeepTheirDefaultPane();
    void savedProfileIdCannotCollideWithSessionContext();
    void visibleConnectionsPreserveMetadataAndOrder();
    void searchLoadsCollapsedGroupsOnlyForSelectedConnection();
    void searchFailureKeepsRefineMessage();
    void selectingObjectLoadsMetadataAndSeparateDataThenReturnsToSql();
    void objectDataKeepsCancelPaneVisibleUntilAcknowledged();
    void documentsKeepTargetsAndImmutableResultOrigin();
    void activeExecutionKeepsDocumentAndCancelReachable();
    void disconnectMenuKeepsOtherSessionsAndDrafts();
    void generationOpensDraftOnExistingSavedConnectionWithoutExecuting();
    void objectActionsRenameAndDropThroughNavigator();
    void objectActionKeepsOtherSessionTabWithSameProfile();
};
