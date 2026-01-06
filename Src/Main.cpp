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
#include    "LicenseManager.hpp"

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

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
#ifdef DEBUG_UI_LOGS
	ACAPI_WriteReport("[Main] MenuCommandHandler: menuResID=%d, itemIndex=%d", false, 
		(int)menuParams->menuItemRef.menuResID, (int)menuParams->menuItemRef.itemIndex);
#endif
	
	switch (menuParams->menuItemRef.menuResID) {
		case BrowserReplMenuResId:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:  // "Selection Details"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Selection Details menu item", false);
#endif
					SelectionDetailsPalette::ShowPalette();
					break;
				case 2:  // "Distribution"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Distribution menu item", false);
#endif
					DistributionPalette::ShowPalette();
					break;
				case 3:  // "Orientation"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Orientation menu item", false);
#endif
					OrientationPalette::ShowPalette();
					break;
				case 4:  // "Angle"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Angle menu item", false);
#endif
					AnglePalette::ShowPalette();
					break;
				case 5:  // "Ground"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Ground menu item", false);
#endif
					GroundPalette::ShowPalette();
					break;
				case 6:  // "Markup"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Markup menu item", false);
#endif
					MarkupPalette::ShowPalette();
					break;
				case 7:  // "ID & Layers"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling ID & Layers menu item", false);
#endif
					IdLayersPalette::ShowPalette();
					break;
				case 8:  // "Contour"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Contour menu item", false);
#endif
					ContourPalette::ShowPalette();
					break;
				case 9:  // "Mesh"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Mesh menu item", false);
#endif
					MeshPalette::ShowPalette();
					break;
				case 10:  // "Send to Excel"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Send to Excel menu item", false);
#endif
					SendXlsPalette::ShowPalette();
					break;
				case 11:  // "Randomizer"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Randomizer menu item", false);
#endif
					RandomizerPalette::ShowPalette();
					break;
				case 12:  // "Support"
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Support menu item", false);
#endif
					HelpPalette::ShowWithURL("https://landscape.227.info/help");
					break;
				case BrowserReplMenuItemIndex:  // "Toolbar" - opens/closes palette 32500
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Toolbar menu item", false);
#endif
					ShowOrHideBrowserRepl();
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

API_AddonType	CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode	RegisterInterface (void)
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

GSErrCode Initialize ()
{
    // 0) Проверка лицензии
    LicenseManager::LicenseData licenseData;
    LicenseManager::LicenseStatus licenseStatus = LicenseManager::CheckLicense(licenseData);
    
    if (licenseStatus != LicenseManager::LicenseStatus::Valid) {
        GS::UniString errorMsg;
        switch (licenseStatus) {
            case LicenseManager::LicenseStatus::NotFound:
                errorMsg = "License file not found. Please place license.lic file next to the plugin.";
                break;
            case LicenseManager::LicenseStatus::Expired:
                errorMsg = "License expired. Valid until: " + licenseData.validUntil;
                break;
            case LicenseManager::LicenseStatus::ComputerMismatch:
                errorMsg = "License is not valid for this computer. Computer ID: " + LicenseManager::GetComputerId();
                break;
            case LicenseManager::LicenseStatus::PluginMismatch:
                errorMsg = "License is not valid for this plugin.";
                break;
            case LicenseManager::LicenseStatus::ParseError:
                errorMsg = "License file format error. Please check license.lic file.";
                break;
            default:
                errorMsg = "License validation failed.";
                break;
        }
        
        ACAPI_WriteReport(errorMsg.ToCStr().Get(), true);
        return APIERR_GENERAL;
    }

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

GSErrCode	FreeData (void)
{
	return NoError;
}		// FreeData
