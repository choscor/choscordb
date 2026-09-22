#pragma once

#include <QObject>

class ModernUiTest final : public QObject {
    Q_OBJECT

  private slots:
    void queryToolbarShowsOnlyRequestedControls();
    void applicationMenusExposeHelpAndAbout();
    void resultActionsUseIconsInToolbarAndFootersOnlyContainPagination();
    void recoveryKeepsUnavailableToolbarActionsDisabled();
    void startUsesPanelAndMutedSupportingText();
    void freshSidebarUsesResponsiveReferenceWidthsAndSeamlessSections();
    void savedProfileBadgesDistinguishDriversInBothThemes();
    void completedResultsKeepContentWidthsAndDisableCancel();
    void findFromHistoryReturnsToExistingSqlAndDoesNotCreateClosedDocuments();
    void captureScreenFixtures();
    void newLayoutUsesReferenceProportionsAndPreservesSavedPlacement();
    void centralScreensPreserveDraftsAndLastCloseReturnsToStart();
    void startListsRealSavedProfilesAndConnectsWithoutExecuting();
    void savedProfileSelectionKeepsEditorTargetAndShowsOneTree();
    void failedSidebarOpenShowsReasonAndClearsBrowseSelection();
    void failedSidebarOpenRestoresLastSuccessfulProfile();
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
    void transactionControlsStayInMoreMenuAtBothWidths();
    void inactiveEditorCloseButtonAppearsOnHover();
    void paletteUpdatePreservesCompleteEditorState();
    void appearancePreviewsPersistAndRestoreAcrossRestart();
    void preferencesUseSectionNavigationAndCancelableLivePreview();
    void executionStripKeepsVisibleAndAccessibleTerminalState();
};
