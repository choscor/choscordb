#pragma once

#include <QObject>

class ModernUiTest final : public QObject {
    Q_OBJECT

  private slots:
    void queryToolbarShowsOnlyRequestedControls();
    void applicationMenusExposeHelpAndAbout();
    void applicationMenusProvideWindowControlsWithoutConnectionMenu();
    void quickSwitchOpensOneSearchOverlayFromViewAction();
    void quickSearchEmptyQueryShowsScreensAndOpenTabs();
    void quickSearchDistinguishesDuplicateTabTitles();
    void quickSearchRanksNamesAndActivatesAnOpenTab();
    void quickSearchFindsSqlInInactiveEditorsAndSelectsTheMatch();
    void quickSearchShowsLateEditorMatchesInTheirSnippet();
    void quickSearchClearsEditedSqlResultsBeforeActivation();
    void quickSearchClearsResultsWhenAnInactiveTabCloses();
    void quickSearchFindsSavedHistoryAndOpensItWithoutExecuting();
    void quickSearchShowsHistoryClearStartedBeforeFirstOpen();
    void quickSearchFindsCollapsedSelectedConnectionObjectBeforeSqlText();
    void quickSearchHidesRecentSystemObjectAfterPreferenceChanges();
    void quickSearchColumnResultOpensItsTablePane();
    void quickSearchRecoveryGuardKeepsTheCurrentWorkspace();
    void quickSearchActiveQueryGuardKeepsTheCurrentTab();
    void quickSearchUsesTheBrowsedConnectionWhenTwoAreVisible();
    void quickSearchDoesNotReuseAnObjectIdAfterMetadataRefresh();
    void resultActionsUseIconsInToolbarAndFootersOnlyContainPagination();
    void recoveryKeepsUnavailableToolbarActionsDisabled();
    void startUsesPanelAndMutedSupportingText();
    void emptyStatesUseOneTextWithOneLineBreak();
    void navigatorEmptyVariantsUseOneLineBreak();
    void savedAndHistoryEmptyStatesUseOneLineBreak();
    void freshSidebarUsesResponsiveReferenceWidthsAndSeamlessSections();
    void savedProfileBadgesDistinguishDriversInBothThemes();
    void completedResultsKeepContentWidthsAndDisableCancel();
    void findFromHistoryReturnsToExistingSqlAndDoesNotCreateClosedDocuments();
    void captureScreenFixtures();
    void newLayoutUsesReferenceProportionsAndPreservesSavedPlacement();
    void centralScreensPreserveDraftsAndLastCloseReturnsToStart();
    void startListsRealSavedProfilesAndConnectsWithoutExecuting();
    void savedProfileSelectionKeepsEditorTargetAndShowsMultipleTrees();
    void savedProfileSelectionSurvivesRefreshEditAndDeletionOnlyForExistingIds();
    void failedSidebarOpenShowsReasonAndClearsBrowseSelection();
    void failedSidebarOpenRestoresLastSuccessfulProfile();
    void rejectedSidebarOpenKeepsOtherRootsAndShowsOneReason();
    void removedPendingSidebarOpenCannotRestoreItsRoot();
    void savedConnectionMenuDuplicatesTheChosenProfileWithoutConnecting();
    void appearanceSaveDuringLayoutWriteReturnsRetryAndKeepsPreview();
    void activeWorkKeepsSqlVisibleAndRejectsPreferences();
    void sqlCompositionKeepsTabsFirstAndPinsResultActions();
    void workspaceHasNoOnboardingActionStrip();
    void liveAppearanceReachesAlreadyOpenConnectionPanelAndMenu();
    void minimumWorkspaceKeepsQueryControlsInsideTheWindow();
    void workspaceUsesApprovedButtonGeometryAndIcons();
    void developmentMenuOpensIndependentPreview();
    void workspaceProvidesDiscoverableModernControls();
    void navigatorContextActionsSupportKeyboardFocusAndMenus();
    void transactionControlsStayInToolbarAtBothWidths();
    void inactiveEditorCloseButtonAppearsOnHover();
    void addSqlTabButtonCreatesAndSelectsEditor();
    void paletteUpdatePreservesCompleteEditorState();
    void appearancePreviewsPersistAndRestoreAcrossRestart();
    void preferencesUseSectionNavigationAndCancelableLivePreview();
    void executionStripKeepsVisibleAndAccessibleTerminalState();
};
