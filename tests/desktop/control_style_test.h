#pragma once

#include <QObject>

class ControlStyleTest final : public QObject {
    Q_OBJECT

  private slots:
    void popupStaysInsideOwner_data();
    void popupStaysInsideOwner();
    void embeddedPopupInputAndLifetime();
    void contextMenuUsesCursorAndDismissesOutside();
    void contextMenuShadowClickDismisses();
    void standardTextContextMenusUseCursorAndDismissOutside();
    void scrolledTextContextMenuCopiesTheClickedLink();
    void nativeToolbarContextMenuUsesCursor();
    void fontComboResizePreservesOwnerGeometry();
    void menuCheckmarkUsesTheSharedVectorPath();
    void treePointerSelectionDoesNotFrameTheEntireViewport();
    void comboPopupUsesOneBorderAndFilledSelection();
    void comboPopupDelegatePaintsNormalWeight();
    void appMenusSuppressDuplicateNativeShadows_data();
    void appMenusSuppressDuplicateNativeShadows();
    void selectArrowUsesForegroundInkAndPreservesPopupInput();
    void menuPanelHasReferenceMinimumWidthOutsideItsShadow();
    void paneTabsPaintReferenceInsetsWithoutMovingDocumentTabs();
    void documentOverflowDoesNotPaintTornEdges();
    void closableDocumentTabsUseCompactHeight();
    void inputTrackingResetsBodyTrackingAndRemainsEditable();
    void referenceSwitchMovesItsThumbAndKeepsKeyboardSemantics();
    void tabCloseUsesNeutralGlyphAndStillDispatches();
    void standardConfirmationVariantsKeepSafeDefaultDistinct();
    void scopedSelectAndSpinRenderArrows_data();
    void scopedSelectAndSpinRenderArrows();
    void spinBoxesIgnoreWheelButKeepButtonStepping();
    void scopedCheckboxUsesSemanticFill_data();
    void scopedCheckboxUsesSemanticFill();
    void scopedDarkButtonsAndTextSelectionUseSemanticColors();
    void navigationRowsUseCompactGeometry();
    void treeHoverFillsSquareRowCorners();
    void listSelectionFillsSquareRowCorners();
    void validationStateUpdatesAnAlreadyVisibleField();
    void tableRowsHonorReferenceLineBoxAndPadding();
    void unusedHeaderGutterUsesThemeSurface_data();
    void unusedHeaderGutterUsesThemeSurface();
    void tableHeaderColumnsHaveSeparators();
    void rowNumberColumnHasVerticalSeparator();
    void tableHeaderAndTabTooltipsUseSharedSurface();
    void itemTooltipsUseSharedSurface();
    void unbrokenTooltipWrapsAndOwnerDestructionDismissesIt();
    void actualTooltipHasArrowAndDismissesOnOwnerLeave();
    void menuHasRenderedTranslucentElevationOutsidePanel();
    void treeBranchesUseHollowChevronsAndNoLeafDecoration();
    void fieldKeyboardFocusPaintsOutsideWithoutMovingText();
    void keyboardFocusPrimitiveUsesContrastSafeContinuousOutline();
    void badgeAndProgressUseCompactReferenceGeometry();
    void applicationInstallationUsesProductionControlStyle();
    void directionalPrimitiveUsesHollowLucideChevron();
    void initTestCase();
    void tabsToolsAndTableHeadersUseCompactPaneGeometry();
    void toolButtonMenuPanelOpensBesideItsButton();
    void menusTabsAndScrollbarsUseSharedSurfacesAndRemainInteractive();
    void fieldsExposeInvalidBorderAndPreserveReadOnlyAndPopupInput();
    void checkedAndMixedIndicatorsRenderSemanticFill();
    void checkedRadioUsesCheckboxAccentAndSize();
    void disabledCheckedIndicatorUsesMutedGreenOutlineAndCheck();
    void checkboxUsesReferenceGeometryAndKeyboardMixedState();
};
