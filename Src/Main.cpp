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
#include    "IdLayersPalette.hpp"
#include    "SelectionDetailsPalette.hpp"
#include    "LicenseManager.hpp"
#include	"APICommon.h"

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

// Глобальная переменная для хранения состояния лицензии/демо
static bool g_isLicenseValid = false;
static bool g_isDemoExpired = false;

// Функции для проверки состояния лицензии из других модулей
extern "C" {
	bool IsLicenseValid() { return g_isLicenseValid; }
	bool IsDemoExpired() { return g_isDemoExpired; }
}

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
#ifdef DEBUG_UI_LOGS
	ACAPI_WriteReport("[Main] MenuCommandHandler: menuResID=%d, itemIndex=%d", false, 
		(int)menuParams->menuItemRef.menuResID, (int)menuParams->menuItemRef.itemIndex);
#endif
	
	// Проверяем лицензию/демо перед выполнением команд (кроме Support и Toolbar)
	// ВРЕМЕННО ОТКЛЮЧЕНО ДЛЯ РАЗРАБОТКИ
	#if 0
	if (!g_isLicenseValid && g_isDemoExpired) {
		short itemIndex = menuParams->menuItemRef.itemIndex;
		// Разрешаем только Support (2) и Toolbar (BrowserReplMenuItemIndex = 3)
		if (itemIndex != 2 && itemIndex != BrowserReplMenuItemIndex) {
			ACAPI_WriteReport("Demo period expired. Please purchase a license.", true);
			return NoError;
		}
	}
	#endif
	
	switch (menuParams->menuItemRef.menuResID) {
		case BrowserReplMenuResId:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:  // "Таблица выделенного" (Selection Details)
#ifdef DEBUG_UI_LOGS
					ACAPI_WriteReport("[Main] Handling Selection Details menu item", false);
#endif
					SelectionDetailsPalette::ShowPalette();
					break;
				case 2:  // "Поддержка" (Support)
					{
						GS::UniString url = LicenseManager::BuildLicenseUrl();
						
						// Логируем для отладки
						ACAPI_WriteReport("[Main] Support clicked, URL: ", false);
						ACAPI_WriteReport(url.ToCStr().Get(), false);
						
						HelpPalette::ShowWithURL(url);
					}
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
    
    bool isLicenseValid = (licenseStatus == LicenseManager::LicenseStatus::Valid);
    bool isDemoActive = false;
    bool isDemoExpired = false;
    
    if (!isLicenseValid) {
        // Если лицензии нет, проверяем демо-режим
        LicenseManager::DemoData demoData;
        isDemoActive = LicenseManager::CheckDemoPeriod(demoData);
        
        if (isDemoActive) {
            // Демо активен, обновляем счетчик запусков
            LicenseManager::UpdateDemoData();
        } else {
            // Демо истек
            isDemoExpired = true;
        }
    }
    
    // Сохраняем состояние для использования в MenuCommandHandler
    g_isLicenseValid = isLicenseValid;
    g_isDemoExpired = isDemoExpired;
    
    // Если ни лицензия, ни демо не активны - отключаем меню (кроме Support и Toolbar)
    // ВРЕМЕННО ОТКЛЮЧЕНО ДЛЯ РАЗРАБОТКИ
    #if 0
    if (!isLicenseValid && isDemoExpired) {
        // Отключаем Таблица выделенного (1)
        // Support (2) и Toolbar (3) остаются активными
        DisableEnableMenuItem(BrowserReplMenuResId, 1, true);   // Selection Details
        
        GS::UniString supportUrl = LicenseManager::BuildLicenseUrl();
        ACAPI_WriteReport("Demo period expired. Please purchase a license. Support: ", false);
        ACAPI_WriteReport(supportUrl.ToCStr().Get(), false);
    }
    #endif

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
    palErr |= IdLayersPalette::RegisterPaletteControlCallBack ();
	palErr |= SelectionDetailsPalette::RegisterPaletteControlCallBack ();

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
