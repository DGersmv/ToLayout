#ifndef RESOURCEIDS_HPP
#define RESOURCEIDS_HPP

#define BrowserReplResId        32500
#define BrowserReplMenuResId    32501
#define BrowserReplMenuItemIndex 3  // "Toolbar" item in menu (index 3 in STR# 32501)

// Icon button IDs for native toolbar (IDs start from 1 in .grc)
#define ToolbarButtonCloseId    1
#define ToolbarButtonTableId    2
#define ToolbarButtonLayersId   3
#define ToolbarButtonSupportId  4

// Icon resource IDs
#define IconTableResId          32100
#define IconLayersResId         32106
#define IconSupportResId        32110
#define IconCloseResId          32111

// Help Palette
constexpr short HelpPaletteResId = 32510;
constexpr short HelpBrowserCtrlId = 1;

// IdLayers Palette
constexpr short IdLayersPaletteResId = 32580;
constexpr short IdLayersBrowserCtrlId = 1;
constexpr short IdLayersHtmlResId = 170;

// SelectionDetails Palette
constexpr short SelectionDetailsPaletteResId = 32610;
constexpr short SelectionDetailsBrowserCtrlId = 1;
constexpr short SelectionDetailsHtmlResId = 200;

#endif
