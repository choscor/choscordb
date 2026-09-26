#pragma once

#include <QObject>

class PreviewTest final : public QObject {
    Q_OBJECT

  private slots:
    void codePreviewTextAreaUsesSharedVariantInBothThemes();
    void navigationTreeSpecimenUsesRealTreeInBothThemes();
    void editorResultsSplitUsesEqualPanesInBothThemes();
    void documentTabSpecimenShowsFixedWidthTabsInBothThemes();
    void richTextParagraphsHaveCompactSpacing();
    void showToastOpensTransientToastAtViewportCorner();
    void toastPortalIsPresentInBothThemes();
    void progressToastHasPersistentIndicatorInBothThemes();
    void toastCanAttachAcrossWidgetTrees();
    void toastVariantsShowTitleBodyAndUseConfiguredTimeout();
    void nonmodalDialogSurfaceHasNoOutline();
    void nonmodalDialogGrowsWhenDescriptionWraps();
    void galleryOpenKeepsOverlayInsideWindow_data();
    void galleryOpenKeepsOverlayInsideWindow();
    void popupSpecimensStayInsideWindowInBothThemes();
    void contextMenuSpecimenUsesCursorInBothThemes();
    void moreButtonUsesThemeSurface();
    void scrollSpecimensKeepTheirIndependentThemePaper();
    void narrowGalleryKeepsNavigationAndActionsReachable();
    void standaloneCapturesRequestedThemeAndViewport_data();
    void standaloneCapturesRequestedThemeAndViewport();
    void confirmationSpecimenUsesProductionCancellationBoundary();
    void tableHoverPreservesBackgroundInBothThemes();
    void initTestCase();
    void exportedPopupContainsItsVisibleContent_data();
    void exportedPopupContainsItsVisibleContent();
    void tooltipUsesTheProductionSurfaceInBothThemes();
    void exportsRenderActualModalAndMenuSurfaces();
    void standaloneExportsWithoutAProfile();
    void displayedIconsRasterizeAtTargetScale_data();
    void displayedIconsRasterizeAtTargetScale();
    void iconsShowNamedProductionAssetsAtSupportedSizes();
    void examplesOpenActualDismissibleSurfaces();
    void fieldSpecimenRetainsEditingAndValidationStates();
    void sidebarTabSpecimenUsesProductionContextInBothThemes();
    void sidebarTabFocusRingSurvivesOnlyKeyboardActivation();
    void buttonsUseProductionVariantsAndStates();
    void individualSpecimensAreSelectableAndSearchable();
    void fieldValidationIsBelowInputAndSelectInBothThemes();
    void tokensExposeCopyableValuesAndSources();
    void exportsAreDeterministicAndFailuresVisible();
    void comparisonThemesAreIndependent();
    void navigationIsSearchable();
    void componentFamiliesAreRendered();
    void dialogSectionsPreviewUsesRealComponentInBothThemes();
    void navigationProfileRowsShowRegularAndSelectedStates();
    void dockSpecimenRendersThemedTitleAndButtons();
    void navigationTreeTogglesAndRenamesFromMenu();
};
