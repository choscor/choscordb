#pragma once

#include <QObject>

class WorkspaceTest : public QObject {
    Q_OBJECT

  private slots:
    void unsavedConnectionsRetainTheirDriver();
    void developmentUpdaterHasNoEnabledActions_data();
    void developmentUpdaterHasNoEnabledActions();
    void productionDataUsesOneIdentityDirectory();
    void updateAppearanceFailurePostponesInstallation();
    void updatePersistenceFailureKeepsWorkspaceUsable();
    void updateRestartWaitsForRecoveryAndDatabaseShutdown();
    void formErrorsAppearBelowTheRelevantFields();
    void newConnectionAfterSavingCreatesAnotherProfile();
    void cancellationRacingSuccessfulCompletionReleasesNavigation();
    void immediateCancellationDoesNotReenableCancelBeforeAcknowledgement();
    void connectionPanelRetainsFailedSaveConnectDraftAndRetries();
    void saveAndConnectPersistsProfileBeforeOpeningSession();
    void recoveryAdapterRejectsOversizeBeforeDispatch();
    void sqlStartedTransactionRequiresCloseConfirmation();
    void transactionCloseRequiresExplicitChoice();
    void mainWindowHistoryRecordsOpensDisablesAndFlushes();
    void cancelledUpdateDoesNotInstallOnLaterNormalClose();
    void closingActiveQueryFlushesDisconnectedHistory();
    void mainWindowSearchActionsTrackCurrentTabAndUndo();
    void mainWindowRunsRealQuery();
    void connectsExecutesPagesAndCopies();
    void revisitsPreviousPageWithoutReexecutingQuery();
    void byteLimitedPagesUseStoredRowOffsets();
    void transactionKeepsAlreadyStoredNextPageAccessible();
    void disconnectClearsPageNavigation();
    void visiblePageRemainsBudgetedAfterCoreQueryRelease();
    void repeatedPagingReleasesReplacedViews();
    void nestedEventLoopKeepsLiveTransferReserved();
    void previousPageUsesSharedHotCache();
    void deferredCellOpensBoundedDetailAndClosesOnNewQuery();
    void deferredTextRemainsInspectableAfterCommitAndDisconnectClosesDetail();
    void backwardTextWindowAlignsUtf8AndLongRowsScroll();
    void exportsMysqlInsertDialectThroughDialog();
    void exportsOriginalResultAfterBrowsing();
    void exportCancellationPreservesExistingDestination();
    void exportFailureLeavesResultUsable();
    void savedProfilesCreateDuplicateTestDeleteAndConnect();
    void profileAdapterPersistsAcrossRestart();
    void profileFailuresKeepDraftAndShowConnectionCodes();
    void passwordDraftSurvivesTestConnectAndFailedRemember();
    void sshSecretDraftSurvivesFailedSecureSave();
    void mysqlSelectedScriptNavigatesDifferentResultSchemas();
    void mysqlGridEditsReviewBoundValuesAndPersistChanges();
    void mysqlModeRefreshDoesNotExecuteChangedEditorInput();
    void mysqlSessionModeChangesSubsequentCursorExecution();
    void postgresProfileExecutesPagesAndCancels();
    void writesCommitAndRollbackComplete();
    void cancelsRunningQuery();
};
