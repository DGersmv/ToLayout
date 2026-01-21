#include "ACAPinc.h"
#include "APICommon.h"
#include "BrowserRepl.hpp"
#include "DGBrowser.hpp"
#include "LicenseManager.hpp"

#include <Windows.h>
#include "SelectionHelper.hpp"

// Внешние функции для проверки состояния лицензии
extern "C" {
	bool IsLicenseValid();
	bool IsDemoExpired();
}

#include "HelpPalette.hpp"
#include "LayerHelper.hpp"
#include "IdLayersPalette.hpp"
#include "SelectionPropertyHelper.hpp"
#include "SelectionMetricsHelper.hpp"
#include "SelectionDetailsPalette.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

// --------------------- Palette GUID / Instance ---------------------
static const GS::Guid paletteGuid("{b7e2a1c3-9d4f-5e6a-8b7c-0d1e2f3a4b5c}");
GS::Ref<BrowserRepl> BrowserRepl::instance;

// --- Extract double from JS::Base (supports 123 / "123.4" / "123,4") ---
static double GetDoubleFromJs(GS::Ref<JS::Base> p, double def = 0.0)
{
	if (p == nullptr) {
		return def;
	}

	if (GS::Ref<JS::Value> v = GS::DynamicCast<JS::Value>(p)) {
		const auto t = v->GetType();

		if (t == JS::Value::DOUBLE) {
			return v->GetDouble();
		}
		
		if (t == JS::Value::INTEGER) {
			return static_cast<double>(v->GetInteger());
		}

		if (t == JS::Value::STRING) {
			GS::UniString s = v->GetString();
			for (UIndex i = 0; i < s.GetLength(); ++i) if (s[i] == ',') s[i] = '.';
			double out = def;
			std::sscanf(s.ToCStr().Get(), "%lf", &out);
			return out;
		}
	}

	return def;
}

static GS::UniString GetStringFromJavaScriptVariable(GS::Ref<JS::Base> jsVariable)
{
	GS::Ref<JS::Value> jsValue = GS::DynamicCast<JS::Value>(jsVariable);
	if (DBVERIFY(jsValue != nullptr && jsValue->GetType() == JS::Value::STRING))
		return jsValue->GetString();
	return GS::EmptyUniString;
}

// --- Extract array of strings (GUIDs) from JS::Base ---
static GS::Array<GS::UniString> GetStringArrayFromJavaScriptVariable(GS::Ref<JS::Base> jsVariable)
{
	GS::Array<GS::UniString> result;
	GS::Ref<JS::Array> jsArray = GS::DynamicCast<JS::Array>(jsVariable);
	if (jsArray == nullptr)
		return result;
	
	const GS::Array<GS::Ref<JS::Base>>& items = jsArray->GetItemArray();
	for (UIndex i = 0; i < items.GetSize(); ++i) {
		GS::Ref<JS::Base> item = items[i];
		if (item != nullptr) {
			GS::Ref<JS::Value> jsValue = GS::DynamicCast<JS::Value>(item);
			if (jsValue != nullptr && jsValue->GetType() == JS::Value::STRING) {
				result.Push(jsValue->GetString());
			}
		}
	}
	return result;
}

template<class Type>
static GS::Ref<JS::Base> ConvertToJavaScriptVariable(const Type& cppVariable)
{
	return new JS::Value(cppVariable);
}

template<>
GS::Ref<JS::Base> ConvertToJavaScriptVariable(const SelectionHelper::ElementInfo& elemInfo)
{
	GS::Ref<JS::Array> js = new JS::Array();
	js->AddItem(ConvertToJavaScriptVariable(elemInfo.guidStr));
	js->AddItem(ConvertToJavaScriptVariable(elemInfo.typeName));
	js->AddItem(ConvertToJavaScriptVariable(elemInfo.elemID));
	js->AddItem(ConvertToJavaScriptVariable(elemInfo.layerName));
	return js;
}

template<>
GS::Ref<JS::Base> ConvertToJavaScriptVariable(const LayerHelper::LayerInfo& layerInfo)
{
	GS::Ref<JS::Array> js = new JS::Array();
	js->AddItem(ConvertToJavaScriptVariable(layerInfo.name));
	js->AddItem(ConvertToJavaScriptVariable(layerInfo.folder));
	return js;
}

template<class Type>
static GS::Ref<JS::Base> ConvertToJavaScriptVariable(const GS::Array<Type>& cppArray)
{
	GS::Ref<JS::Array> newArray = new JS::Array();
	for (const Type& item : cppArray) {
		newArray->AddItem(ConvertToJavaScriptVariable(item));
	}
	return newArray;
}

static void EnsureModelWindowIsActive()
{
	API_WindowInfo windowInfo = {};
	const GSErrCode getDbErr = ACAPI_Database_GetCurrentDatabase(&windowInfo);
	if (DBERROR(getDbErr != NoError))
		return;

	if (windowInfo.typeID == APIWind_MyDrawID || windowInfo.typeID == APIWind_MyTextID)
		return;

	const GSErrCode changeErr = ACAPI_Window_ChangeWindow(&windowInfo);
	(void)changeErr;
}

// --------------------- Project event handler ---------------------
static GSErrCode NotificationHandler(API_NotifyEventID notifID, Int32 /*param*/)
{
	if (notifID == APINotify_Quit) {
		BrowserRepl::DestroyInstance();
	}
	return NoError;
}

// --------------------- BrowserRepl impl ---------------------
BrowserRepl::BrowserRepl() :
	DG::Palette(ACAPI_GetOwnResModule(), BrowserReplResId, ACAPI_GetOwnResModule(), paletteGuid),
	buttonClose(GetReference(), ToolbarButtonCloseId),
	buttonTable(GetReference(), ToolbarButtonTableId),
	buttonLayers(GetReference(), ToolbarButtonLayersId),
	buttonSupport(GetReference(), ToolbarButtonSupportId)
{
	ACAPI_ProjectOperation_CatchProjectEvent(APINotify_Quit, NotificationHandler);

	Attach(*this);
	AttachToAllItems(*this);
	BeginEventProcessing();
}

BrowserRepl::~BrowserRepl()
{
	EndEventProcessing();
}

bool BrowserRepl::HasInstance() { return instance != nullptr; }

void BrowserRepl::CreateInstance()
{
	DBASSERT(!HasInstance());
	instance = new BrowserRepl();
	ACAPI_KeepInMemory(true);
}

BrowserRepl& BrowserRepl::GetInstance()
{
	DBASSERT(HasInstance());
	return *instance;
}

void BrowserRepl::DestroyInstance() { instance = nullptr; }

void BrowserRepl::Show()
{
	DG::Palette::Show();
	SetMenuItemCheckedState(true);
}

void BrowserRepl::Hide()
{
	DG::Palette::Hide();
	SetMenuItemCheckedState(false);
}

// JavaScript API registration - needed for IdLayersPalette and SelectionDetailsPalette
void BrowserRepl::RegisterACAPIJavaScriptObject(DG::Browser& targetBrowser)
{
	JS::Object* jsACAPI = new JS::Object("ACAPI");

	// --- Selection API ---
	jsACAPI->AddItem(new JS::Function("GetSelectedElements", [](GS::Ref<JS::Base>) {
		GS::Array<SelectionHelper::ElementInfo> elements = SelectionHelper::GetSelectedElements();
		return ConvertToJavaScriptVariable(elements);
		}));

	jsACAPI->AddItem(new JS::Function("AddElementToSelection", [](GS::Ref<JS::Base> param) {
		const GS::UniString id = GetStringFromJavaScriptVariable(param);
		SelectionHelper::ModifySelection(id, SelectionHelper::AddToSelection);
		return ConvertToJavaScriptVariable(true);
		}));

	jsACAPI->AddItem(new JS::Function("RemoveElementFromSelection", [](GS::Ref<JS::Base> param) {
		const GS::UniString id = GetStringFromJavaScriptVariable(param);
		SelectionHelper::ModifySelection(id, SelectionHelper::RemoveFromSelection);
		return ConvertToJavaScriptVariable(true);
		}));

	jsACAPI->AddItem(new JS::Function("ChangeSelectedElementsID", [](GS::Ref<JS::Base> param) {
		const GS::UniString baseID = GetStringFromJavaScriptVariable(param);
		const bool success = SelectionHelper::ChangeSelectedElementsID(baseID);
		return ConvertToJavaScriptVariable(success);
		}));

	jsACAPI->AddItem(new JS::Function("ApplyCheckedSelection", [](GS::Ref<JS::Base> param) {
		GS::Array<GS::UniString> guidStrings = GetStringArrayFromJavaScriptVariable(param);
		
		GS::Array<API_Guid> guids;
		for (UIndex i = 0; i < guidStrings.GetSize(); ++i) {
			API_Guid guid = APIGuidFromString(guidStrings[i].ToCStr().Get());
			if (guid != APINULLGuid) {
				guids.Push(guid);
			}
		}
		
		SelectionHelper::ApplyCheckedSelectionResult result = SelectionHelper::ApplyCheckedSelection(guids);
		
		GS::Ref<JS::Object> jsResult = new JS::Object();
		jsResult->AddItem("applied", ConvertToJavaScriptVariable((Int32)result.applied));
		jsResult->AddItem("requested", ConvertToJavaScriptVariable((Int32)result.requested));

		EnsureModelWindowIsActive();

		return jsResult;
		}));

	jsACAPI->AddItem(new JS::Function("GetSelectedProperties", [](GS::Ref<JS::Base> param) {
		API_Guid requestedGuid = APINULLGuid;
		if (param != nullptr) {
			GS::UniString guidStr = GetStringFromJavaScriptVariable(param);
			if (!guidStr.IsEmpty()) {
				requestedGuid = APIGuidFromString(guidStr.ToCStr().Get());
			}
		}

		GS::Array<SelectionPropertyHelper::PropertyInfo> props = (requestedGuid == APINULLGuid)
			? SelectionPropertyHelper::CollectForFirstSelected()
			: SelectionPropertyHelper::CollectForGuid(requestedGuid);
		GS::Ref<JS::Array> jsProps = new JS::Array();
		for (const auto& info : props) {
			GS::Ref<JS::Object> obj = new JS::Object();
			obj->AddItem("guid", new JS::Value(APIGuidToString(info.propertyGuid)));
			obj->AddItem("name", new JS::Value(info.propertyName));
			obj->AddItem("value", new JS::Value(info.valueString));
			jsProps->AddItem(obj);
		}
		return jsProps;
	}));

	jsACAPI->AddItem(new JS::Function("GetSelectionSeoMetrics", [](GS::Ref<JS::Base> param) {
		API_Guid requestedGuid = APINULLGuid;
		if (param != nullptr) {
			GS::UniString guidStr = GetStringFromJavaScriptVariable(param);
			if (!guidStr.IsEmpty()) {
				requestedGuid = APIGuidFromString(guidStr.ToCStr().Get());
			}
		}

		GS::Array<SelectionMetricsHelper::Metric> metrics = (requestedGuid == APINULLGuid)
			? SelectionMetricsHelper::CollectForFirstSelected()
			: SelectionMetricsHelper::CollectForGuid(requestedGuid);
		GS::Ref<JS::Array> jsMetrics = new JS::Array();
		for (const auto& metric : metrics) {
			GS::Ref<JS::Object> obj = new JS::Object();
			obj->AddItem("key", new JS::Value(metric.key));
			obj->AddItem("name", new JS::Value(metric.name));
			obj->AddItem("grossValue", new JS::Value(metric.grossValue));
			obj->AddItem("netValue", new JS::Value(metric.netValue));
			obj->AddItem("diffValue", new JS::Value(metric.diffValue));
			jsMetrics->AddItem(obj);
		}
		return jsMetrics;
	}));

	// --- Layers API ---
	jsACAPI->AddItem(new JS::Function("CreateLayerAndMoveElements", [](GS::Ref<JS::Base> param) {
		LayerHelper::LayerCreationParams params;
		
		GS::UniString jsonStr = GetStringFromJavaScriptVariable(param);
		
		GS::Array<GS::UniString> parts;
		jsonStr.Split(GS::UniString("|"), [&parts](const GS::UniString& part) {
			parts.Push(part);
		});
		
		if (parts.GetSize() >= 2) {
			params.folderPath = parts[0];
			params.layerName = parts[1];
			params.baseID = GS::UniString("");
			params.hideLayer = false;
			
			if (parts.GetSize() >= 3) {
				params.hideLayer = (parts[2] == "1" || parts[2] == "true" || parts[2] == "True");
			}
		}
		
		const bool success = LayerHelper::CreateLayerAndMoveElements(params);
		return ConvertToJavaScriptVariable(success);
		}));

	jsACAPI->AddItem(new JS::Function("GetLayersList", [](GS::Ref<JS::Base>) {
		const GS::Array<LayerHelper::LayerInfo> layers = LayerHelper::GetLayersList();
		return ConvertToJavaScriptVariable(layers);
		}));

	// --- Help / Palette control ---
	jsACAPI->AddItem(new JS::Function("OpenHelp", [](GS::Ref<JS::Base> param) {
		GS::UniString url;
		if (GS::Ref<JS::Value> v = GS::DynamicCast<JS::Value>(param)) {
			if (v->GetType() == JS::Value::STRING) url = v->GetString();
		}
		if (url.IsEmpty()) url = "https://landscape.227.info/help/start";
		HelpPalette::ShowWithURL(url);
		return new JS::Value(true);
		}));

	jsACAPI->AddItem(new JS::Function("OpenIdLayersPalette", [](GS::Ref<JS::Base>) {
		IdLayersPalette::ShowPalette();
		return new JS::Value(true);
		}));

	jsACAPI->AddItem(new JS::Function("OpenSelectionDetailsPalette", [](GS::Ref<JS::Base>) {
		SelectionDetailsPalette::ShowPalette();
		return new JS::Value(true);
		}));

	jsACAPI->AddItem(new JS::Function("ClosePalette", [](GS::Ref<JS::Base>) {
		if (BrowserRepl::HasInstance() && BrowserRepl::GetInstance().IsVisible())
			BrowserRepl::GetInstance().Hide();
		return new JS::Value(true);
		}));

	jsACAPI->AddItem(new JS::Function("LogMessage", [](GS::Ref<JS::Base> param) {
		if (GS::Ref<JS::Value> v = GS::DynamicCast<JS::Value>(param)) {
			if (v->GetType() == JS::Value::STRING) {
				// Log message (commented out for production)
			}
		}
		return new JS::Value(true);
		}));

	// --- Register object in the browser ---
	targetBrowser.RegisterAsynchJSObject(jsACAPI);
}

// ------------------- Palette and Events ----------------------

void BrowserRepl::ButtonClicked(const DG::ButtonClickEvent& ev)
{
	const DG::ButtonItem* clickedButton = ev.GetSource();
	if (clickedButton == nullptr) return;

	const short buttonId = clickedButton->GetId();

	// Проверяем лицензию/демо перед выполнением команд (кроме Close и Support)
	if (!IsLicenseValid() && IsDemoExpired()) {
		if (buttonId != ToolbarButtonCloseId && buttonId != ToolbarButtonSupportId) {
			ACAPI_WriteReport("Demo period expired. Please purchase a license.", true);
			return;
		}
	}

	switch (buttonId) {
		case ToolbarButtonCloseId:
			Hide();
			break;
		case ToolbarButtonTableId:
			SelectionDetailsPalette::ShowPalette();
			break;
		case ToolbarButtonLayersId:
			IdLayersPalette::ShowPalette();
			break;
		case ToolbarButtonSupportId:
			{
				GS::UniString url = LicenseManager::BuildLicenseUrl();
				HelpPalette::ShowWithURL(url);
			}
			break;
		default:
			break;
	}
}

void BrowserRepl::SetMenuItemCheckedState(bool isChecked)
{
	API_MenuItemRef itemRef = {};
	GSFlags itemFlags = {};

	itemRef.menuResID = BrowserReplMenuResId;
	itemRef.itemIndex = BrowserReplMenuItemIndex;

	ACAPI_MenuItem_GetMenuItemFlags(&itemRef, &itemFlags);
	if (isChecked) itemFlags |= API_MenuItemChecked;
	else           itemFlags &= ~API_MenuItemChecked;
	ACAPI_MenuItem_SetMenuItemFlags(&itemRef, &itemFlags);
}

void BrowserRepl::PanelResized(const DG::PanelResizeEvent& ev)
{
	(void)ev;
}

void BrowserRepl::PanelCloseRequested(const DG::PanelCloseRequestEvent&, bool* accepted)
{
	Hide();
	if (accepted) *accepted = true;
}

GSErrCode BrowserRepl::PaletteControlCallBack(Int32, API_PaletteMessageID messageID, GS::IntPtr param)
{
	switch (messageID) {
	case APIPalMsg_OpenPalette:
		if (!HasInstance()) CreateInstance();
		GetInstance().Show();
		break;
	case APIPalMsg_ClosePalette:
		if (!HasInstance()) break;
		GetInstance().Hide();
		break;
	case APIPalMsg_HidePalette_Begin:
		if (HasInstance() && GetInstance().IsVisible()) GetInstance().Hide();
		break;
	case APIPalMsg_HidePalette_End:
		if (HasInstance() && !GetInstance().IsVisible()) GetInstance().Show();
		break;
	case APIPalMsg_DisableItems_Begin:
		if (HasInstance() && GetInstance().IsVisible()) GetInstance().DisableItems();
		break;
	case APIPalMsg_DisableItems_End:
		if (HasInstance() && GetInstance().IsVisible()) GetInstance().EnableItems();
		break;
	case APIPalMsg_IsPaletteVisible:
		*(reinterpret_cast<bool*> (param)) = HasInstance() && GetInstance().IsVisible();
		break;
	default:
		break;
	}
	return NoError;
}

GSErrCode BrowserRepl::RegisterPaletteControlCallBack()
{
	return ACAPI_RegisterModelessWindow(
		GS::CalculateHashValue(paletteGuid),
		PaletteControlCallBack,
		API_PalEnabled_FloorPlan |
		API_PalEnabled_Section |
		API_PalEnabled_Elevation |
		API_PalEnabled_InteriorElevation |
		API_PalEnabled_3D |
		API_PalEnabled_Detail |
		API_PalEnabled_Worksheet |
		API_PalEnabled_Layout |
		API_PalEnabled_DocumentFrom3D,
		GSGuid2APIGuid(paletteGuid)
	);
}
