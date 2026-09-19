# Keep source paths relative to the project root for the quality gate registry.
# Dependencies flow from application composition down to reusable Qt controls.
choscordb_add_library(choscordb-design-system
  desktop/resources/resources.qrc
  desktop/resources/styles.qrc
  desktop/design_system/style/style_resource.cpp
  desktop/design_system/badge/badge_style.cpp
  desktop/design_system/button/button.cpp
  desktop/design_system/button/button_style.cpp
  desktop/design_system/button_group/button_group.cpp
  desktop/design_system/checkbox/checkbox_indicator.cpp
  desktop/design_system/colors/colors.cpp
  desktop/design_system/confirmation_dialog/confirmation_dialog.cpp
  desktop/design_system/control_glyphs/arrow_indicator.cpp
  desktop/design_system/control_glyphs/control_glyphs.cpp
  desktop/design_system/control_style.cpp
  desktop/design_system/dialog_presentation/dialog_presentation.cpp
  desktop/design_system/dialog_shell/dialog_shell.cpp
  desktop/design_system/dialog_shell/dialog_shell_style.cpp
  desktop/design_system/dock/dock_style.cpp
  desktop/design_system/field/field.cpp
  desktop/design_system/field/field_style.cpp
  desktop/design_system/field/focus_indicator.cpp
  desktop/design_system/fonts/fonts.cpp
  desktop/design_system/header/header_style.cpp
  desktop/design_system/icons.cpp
  desktop/design_system/item_view/item_view_style.cpp
  desktop/design_system/kbd/kbd_style.cpp
  desktop/design_system/label/label_style.cpp
  desktop/design_system/list/list_style.cpp
  desktop/design_system/menu/menu.cpp
  desktop/design_system/menu/menu_indicator.cpp
  desktop/design_system/menu/menu_style.cpp
  desktop/design_system/metrics/metrics.cpp
  desktop/design_system/modal_panel/modal_panel.cpp
  desktop/design_system/platform_accessibility.cpp
  desktop/design_system/progress/progress_style.cpp
  desktop/design_system/scrollbar/scrollbar_style.cpp
  desktop/design_system/select/select_popup.cpp
  desktop/design_system/select/select_style.cpp
  desktop/design_system/separator/separator_style.cpp
  desktop/design_system/spin_box/spin_box_style.cpp
  desktop/design_system/splitter/splitter_style.cpp
  desktop/design_system/style/application_stylesheet.cpp
  desktop/design_system/style/control_stylesheet.cpp
  desktop/design_system/switch/switch_indicator.cpp
  desktop/design_system/table/table_style.cpp
  desktop/design_system/tabs/tab_indicator.cpp
  desktop/design_system/tabs/tabs_style.cpp
  desktop/design_system/text/text.cpp
  desktop/design_system/text_area/text_area_style.cpp
  desktop/design_system/theme.cpp
  desktop/design_system/theme_manager.cpp
  desktop/design_system/toast_region/toast_region.cpp
  desktop/design_system/toast_region/toast_region_style.cpp
  desktop/design_system/tokens/tokens.cpp
  desktop/design_system/tool_button/tool_button_style.cpp
  desktop/design_system/toolbar/toolbar_style.cpp
  desktop/design_system/tooltip/tooltip.cpp
  desktop/design_system/tooltip/tooltip_style.cpp
  desktop/design_system/tree/tree_indicator.cpp
  desktop/design_system/tree/tree_style.cpp
)
target_include_directories(choscordb-design-system PUBLIC desktop)
target_link_libraries(choscordb-design-system PUBLIC Qt6::Widgets Qt6::Svg)
if(UNIX AND NOT APPLE)
  find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
  target_link_libraries(choscordb-design-system PRIVATE Qt6::DBus)
endif()
if(APPLE)
  target_link_libraries(choscordb-design-system PRIVATE "-framework AppKit")
endif()

choscordb_add_library(choscordb-desktop-services
  desktop/app/appearance_controller.cpp
  desktop/bridge/template_service.cpp
  desktop/bridge/completion_service.cpp
  desktop/bridge/result_column_adapter.cpp
  desktop/bridge/engine_adapter.cpp
  desktop/models/shortcut_catalog.cpp
  desktop/models/history_model.cpp
  desktop/models/result_table_model.cpp
  desktop/models/navigator_model.cpp
  desktop/models/value_preview_model.cpp
)
target_include_directories(choscordb-desktop-services PUBLIC desktop)
target_link_libraries(choscordb-desktop-services PUBLIC
  choscordb-design-system Qt6::Concurrent choscordb-rust)
add_dependencies(choscordb-desktop-services cargo-build_choscordb_bridge)

choscordb_add_library(choscordb-widgets
  desktop/widgets/query_settings_dialog/query_settings_dialog.cpp
  desktop/widgets/editor_completion/editor_completion.cpp
  desktop/widgets/preferences_dialog/preferences_dialog.cpp
  desktop/widgets/search_panel/search_panel.cpp
  desktop/widgets/history_dock/history_dock.cpp
  desktop/widgets/sql_editor/sql_editor.cpp
  desktop/widgets/document_io/document_io.cpp
  desktop/widgets/value_detail_dialog/value_detail_dialog.cpp
  desktop/widgets/export_dialog/export_dialog.cpp
  desktop/widgets/profile_dialog/profile_dialog.cpp
)
target_include_directories(choscordb-widgets PUBLIC desktop)
target_include_directories(choscordb-widgets SYSTEM PUBLIC ${QSCINTILLA_INCLUDE_DIR})
target_link_libraries(choscordb-widgets PUBLIC
  choscordb-design-system choscordb-desktop-services ${QSCINTILLA_LIBRARY})
if(WIN32)
  target_compile_definitions(choscordb-widgets PUBLIC QSCINTILLA_DLL)
endif()

choscordb_add_library(choscordb-desktop
  desktop/app/query_settings.cpp
  desktop/app/editor_preferences.cpp
  desktop/app/main_window.cpp
  desktop/app/workspace_recovery.cpp
  desktop/app/query_workspace.cpp
  desktop/app/object_data_workspace.cpp
  desktop/app/object_explorer.cpp
  desktop/app/navigator_controller.cpp
)
target_link_libraries(choscordb-desktop PUBLIC choscordb-widgets)

if(BUILD_TESTING)
  choscordb_add_library(choscordb-preview desktop/tools/preview/preview_window.cpp)
  target_link_libraries(choscordb-preview PUBLIC choscordb-design-system)
  target_link_libraries(choscordb-desktop PUBLIC choscordb-preview)
  target_compile_definitions(choscordb-desktop PRIVATE CHOSCORDB_DEVELOPMENT_PREVIEW)
endif()
