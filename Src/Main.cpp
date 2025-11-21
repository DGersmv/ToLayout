// *****************************************************************************
// Source code for the Browser Control Test Add-On
// *****************************************************************************

// =============================================================================
//
// API Includes
//
// =============================================================================

#include	"APIEnvir.h"
#include	"ACAPinc.h"		// also includes APIdefs.h
#include	"ResourceIDs.hpp"
#include	"BrowserRepl.hpp"
#include    "HelpPalette.hpp"  
#include    "DistributionPalette.hpp"
#include    "OrientationPalette.hpp"
#include    "GroundPalette.hpp"
#include    "MarkupPalette.hpp"
#include    "ContourPalette.hpp"
#include    "MeshPalette.hpp"
#include    "IdLayersPalette.hpp"
#include    "AnglePalette.hpp"
#include    "SendXlsPalette.hpp"
#include    "SelectionDetailsPalette.hpp"
#include    "RandomizerPalette.hpp"

// -----------------------------------------------------------------------------
// Show or Hide Browser Palette
// -----------------------------------------------------------------------------

static void	ShowOrHideBrowserRepl ()
{
#ifdef DEBUG_UI_LOGS
	ACAPI_WriteReport("[Main] ShowOrHideBrowserRepl called", false);
#endif
	
	if (!BrowserRepl::HasInstance ()) {
#ifdef DEBUG_UI_LOGS
		ACAPI_WriteReport("[Main] Creating BrowserRepl instance", false);
#endif
		BrowserRepl::CreateInstance ();
	}
	
	if (BrowserRepl::GetInstance ().IsVisible ()) {
#ifdef DEBUG_UI_LOGS
		ACAPI_WriteReport("[Main] Hiding BrowserRepl", false);
#endif
		BrowserRepl::GetInstance ().Hide ();
	} else {
#ifdef DEBUG_UI_LOGS
		ACAPI_WriteReport("[Main] Showing BrowserRepl", false);
#endif
		BrowserRepl::GetInstance ().Show ();
	}
}

// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode __ACENV_CALL MenuCommandHandler (const API_MenuParams *menuParams)
{
#ifdef DEBUG_UI_LOGS
	ACAPI_WriteReport("[Main] MenuCommandHandler: menuResID=%d, itemIndex=%d", false, 
		(int)menuParams->menuItemRef.menuResID, (int)menuParams->menuItemRef.itemIndex);
#endif
	
	switch (menuParams->menuItemRef.menuResID) {
		case BrowserReplMenuResId:
			switch (menuParams->menuItemRef.itemIndex) {
				case BrowserReplMenuItemIndex:  // "Toolbar" - opens palette 32500
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Toolbar menu item", false);
#endif
					ShowOrHideBrowserRepl ();
					break;
				default:
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Unknown menu item index: %d", false, (int)menuParams->menuItemRef.itemIndex);
#endif
					break;
			}
			break;
		default:
#ifdef DEBUG_UI_LOGS
			ACAPI_WriteReport("[Main] Unknown menuResID: %d", false, (int)menuParams->menuItemRef.menuResID);
#endif
			break;
	}

	return NoError;
}


// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType	__ACDLL_CALL	CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode	__ACDLL_CALL	RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (BrowserReplMenuResId, 0, MenuCode_UserDef, MenuFlag_Default);
	if (DBERROR (err != NoError))
		return err;

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode __ACENV_CALL Initialize ()
{
    // 1) Меню
    GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (BrowserReplMenuResId, MenuCommandHandler);
    if (DBERROR (err != NoError))
        return err;

    // 2) Нотификация выбора - регистрируется внутри SelectionDetailsPalette при создании
    // (не нужно регистрировать здесь, так как SelectionDetailsPalette сам подписывается)

    // 3) Регистрация модельных окон (палитр) — аккумулируем ошибки
    GSErrCode palErr = NoError;
    palErr |= BrowserRepl::RegisterPaletteControlCallBack ();
    palErr |= HelpPalette::RegisterPaletteControlCallBack ();
    palErr |= DistributionPalette::RegisterPaletteControlCallBack ();
    palErr |= OrientationPalette::RegisterPaletteControlCallBack ();
    palErr |= GroundPalette::RegisterPaletteControlCallBack ();
    palErr |= MarkupPalette::RegisterPaletteControlCallBack ();
    palErr |= ContourPalette::RegisterPaletteControlCallBack ();
    palErr |= MeshPalette::RegisterPaletteControlCallBack ();
    palErr |= IdLayersPalette::RegisterPaletteControlCallBack ();
    palErr |= AnglePalette::RegisterPaletteControlCallBack ();
	palErr |= SendXlsPalette::RegisterPaletteControlCallBack ();
	palErr |= SelectionDetailsPalette::RegisterPaletteControlCallBack ();
	palErr |= RandomizerPalette::RegisterPaletteControlCallBack ();

    if (DBERROR (palErr != NoError))
        return palErr;

    return NoError;
}	// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode __ACENV_CALL	FreeData (void)
{
	return NoError;
}		// FreeData
