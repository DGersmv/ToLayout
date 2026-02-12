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
#include "ToLayoutPalette.hpp"
#include "LayoutHelper.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

// --------------------- Palette GUID / Instance ---------------------
static const GS::Guid paletteGuid("{1a2b3c4d-5e6f-7890-abcd-ef1234567890}");
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

// --------------------- Layout working area helpers ---------------------
// Возвращают размеры рабочей области макета (с учётом полей) в миллиметрах.
static bool GetLayoutWorkingAreaByExistingIndex (Int32 layoutIndex, double& outWidthMm, double& outHeightMm)
{
	outWidthMm = outHeightMm = 0.0;

	const GS::Array<LayoutHelper::LayoutItem> layouts = LayoutHelper::GetLayoutList ();
	if (layoutIndex < 0 || layoutIndex >= static_cast<Int32> (layouts.GetSize ()))
		return false;

	API_LayoutInfo layoutInfo = {};
	BNZeroMemory (&layoutInfo, sizeof (layoutInfo));
	API_DatabaseUnId layoutDbId = layouts[layoutIndex].databaseUnId;
	if (ACAPI_Navigator_GetLayoutSets (&layoutInfo, &layoutDbId) != NoError) {
		if (layoutInfo.customData != nullptr) {
			delete layoutInfo.customData;
			layoutInfo.customData = nullptr;
		}
		return false;
	}

	const double availWmm = layoutInfo.sizeX - layoutInfo.leftMargin - layoutInfo.rightMargin;
	const double availHmm = layoutInfo.sizeY - layoutInfo.topMargin - layoutInfo.bottomMargin;

	if (layoutInfo.customData != nullptr) {
		delete layoutInfo.customData;
		layoutInfo.customData = nullptr;
	}

	if (availWmm <= 0.0 || availHmm <= 0.0)
		return false;

	outWidthMm = availWmm;
	outHeightMm = availHmm;
	return true;
}

static bool GetLayoutWorkingAreaByMasterIndex (Int32 masterIndex, double& outWidthMm, double& outHeightMm)
{
	outWidthMm = outHeightMm = 0.0;

	const GS::Array<LayoutHelper::MasterLayoutItem> masters = LayoutHelper::GetMasterLayoutList ();
	if (masterIndex < 0 || masterIndex >= static_cast<Int32> (masters.GetSize ()))
		return false;

	API_LayoutInfo layoutInfo = {};
	BNZeroMemory (&layoutInfo, sizeof (layoutInfo));
	API_DatabaseUnId masterId = masters[masterIndex].databaseUnId;
	if (ACAPI_Navigator_GetLayoutSets (&layoutInfo, &masterId) != NoError) {
		if (layoutInfo.customData != nullptr) {
			delete layoutInfo.customData;
			layoutInfo.customData = nullptr;
		}
		return false;
	}

	const double availWmm = layoutInfo.sizeX - layoutInfo.leftMargin - layoutInfo.rightMargin;
	const double availHmm = layoutInfo.sizeY - layoutInfo.topMargin - layoutInfo.bottomMargin;

	if (layoutInfo.customData != nullptr) {
		delete layoutInfo.customData;
		layoutInfo.customData = nullptr;
	}

	if (availWmm <= 0.0 || availHmm <= 0.0)
		return false;

	outWidthMm = availWmm;
	outHeightMm = availHmm;
	return true;
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
	buttonLayout(GetReference(), ToolbarButtonLayoutId),
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

	jsACAPI->AddItem(new JS::Function("SetElementsID", [](GS::Ref<JS::Base> param) {
		GS::Array<API_Guid> guids;
		GS::UniString newId;

		if (GS::Ref<JS::Array> params = GS::DynamicCast<JS::Array>(param)) {
			const GS::Array<GS::Ref<JS::Base>>& items = params->GetItemArray();
			if (items.GetSize() >= 2) {
				GS::Array<GS::UniString> guidStrings = GetStringArrayFromJavaScriptVariable(items[0]);
				newId = GetStringFromJavaScriptVariable(items[1]);
				newId.Trim();

				for (UIndex i = 0; i < guidStrings.GetSize(); ++i) {
					API_Guid guid = APIGuidFromString(guidStrings[i].ToCStr().Get());
					if (guid != APINULLGuid) {
						guids.Push(guid);
					}
				}
			}
		}

		SelectionHelper::UpdateElementsIdResult result = SelectionHelper::UpdateElementsID(guids, newId);
		GS::Ref<JS::Object> jsResult = new JS::Object();
		jsResult->AddItem("updated", ConvertToJavaScriptVariable((Int32)result.updated));
		jsResult->AddItem("requested", ConvertToJavaScriptVariable((Int32)result.requested));
		return jsResult;
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

	// --- Layout API (для палитры To Layout) ---
	// Простой список макетов (используется в старом UI и для совместимости)
	jsACAPI->AddItem(new JS::Function("GetLayouts", [](GS::Ref<JS::Base>) {
		const GS::Array<LayoutHelper::LayoutItem> layouts = LayoutHelper::GetLayoutList();
		GS::Ref<JS::Array> jsArr = new JS::Array();
		for (UIndex i = 0; i < layouts.GetSize(); ++i) {
			GS::Ref<JS::Object> obj = new JS::Object();
			obj->AddItem("index", new JS::Value(static_cast<Int32>(i)));
			obj->AddItem("name", new JS::Value(layouts[i].name));
			jsArr->AddItem(obj);
		}
		return jsArr;
		}));

	// Группированный список макетов: имитируем папки Навигатора
	// Формат: [{ groupName: string, layouts: [{ index: int, name: string }, ...] }, ...]
	jsACAPI->AddItem(new JS::Function("GetLayoutsTree", [](GS::Ref<JS::Base>) {
		const GS::Array<LayoutHelper::LayoutItem> layouts = LayoutHelper::GetLayoutList();

		// Карта: имя группы -> индексы макетов в исходном массиве
		GS::HashTable<GS::UniString, GS::Array<UIndex>> groups;
		GS::Array<GS::UniString> groupOrder;

		for (UIndex i = 0; i < layouts.GetSize(); ++i) {
			const GS::UniString& fullName = layouts[i].name;

			GS::UniString groupName;

			// Пробуем разделить по '/' как по разделителю папок
			USize slashPos = fullName.FindFirst (GS::UniChar ('/'));
			if (slashPos != MaxUSize) {
				groupName = fullName.GetSubstring (0, slashPos);
			} else {
				// Если нет разделителя — считаем, что макет без папки
				groupName = GS::UniString ("Без папки");
			}

			if (!groups.ContainsKey (groupName)) {
				groups.Add (groupName, GS::Array<UIndex> ());
				groupOrder.Push (groupName);
			}

			GS::Array<UIndex>* idxArray = groups.GetPtr (groupName);
			if (idxArray != nullptr) {
				idxArray->Push (i);
			}
		}

		GS::Ref<JS::Array> jsGroups = new JS::Array();

		for (UIndex gi = 0; gi < groupOrder.GetSize(); ++gi) {
			const GS::UniString& grpName = groupOrder[gi];
			const GS::Array<UIndex>& idxArray = groups[grpName];

			GS::Ref<JS::Object> grpObj = new JS::Object();
			grpObj->AddItem("groupName", new JS::Value(grpName));

			GS::Ref<JS::Array> jsLayouts = new JS::Array();
			for (UIndex k = 0; k < idxArray.GetSize(); ++k) {
				UIndex idx = idxArray[k];
				if (idx >= layouts.GetSize())
					continue;

				GS::Ref<JS::Object> lo = new JS::Object();
				lo->AddItem("index", new JS::Value(static_cast<Int32>(idx)));
				lo->AddItem("name", new JS::Value(layouts[idx].name));
				jsLayouts->AddItem(lo);
			}

			grpObj->AddItem("layouts", jsLayouts);
			jsGroups->AddItem(grpObj);
		}

		return jsGroups;
		}));

	jsACAPI->AddItem(new JS::Function("GetMasterLayouts", [](GS::Ref<JS::Base>) {
		const GS::Array<LayoutHelper::MasterLayoutItem> masters = LayoutHelper::GetMasterLayoutList();
		GS::Ref<JS::Array> jsArr = new JS::Array();
		for (UIndex i = 0; i < masters.GetSize(); ++i) {
			GS::Ref<JS::Object> obj = new JS::Object();
			obj->AddItem("index", new JS::Value(static_cast<Int32>(i)));
			obj->AddItem("name", new JS::Value(masters[i].name));
			jsArr->AddItem(obj);
		}
		return jsArr;
		}));

	// Рабочая область макета (с учётом полей) для ориентира сетки в палитре «Организация чертежей»
	// Вход: { mode: "existing"|"master", layoutIndex?: int, masterIndex?: int }
	// Выход: { ok: bool, widthMm: double, heightMm: double }
	jsACAPI->AddItem(new JS::Function("GetLayoutWorkingArea", [](GS::Ref<JS::Base> param) {
		GS::UniString mode ("existing");
		Int32 layoutIndex = -1;
		Int32 masterIndex = -1;

		if (GS::Ref<JS::Object> obj = GS::DynamicCast<JS::Object> (param)) {
			const GS::HashTable<GS::UniString, GS::Ref<JS::Base>>& tbl = obj->GetItemTable ();
			GS::Ref<JS::Base> item;
			if (tbl.Get ("mode", &item)) {
				GS::UniString m = GetStringFromJavaScriptVariable (item);
				if (!m.IsEmpty ())
					mode = m;
			}
			if (tbl.Get ("layoutIndex", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value> (item))
					layoutIndex = static_cast<Int32> (vv->GetInteger ());
			}
			if (tbl.Get ("masterIndex", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value> (item))
					masterIndex = static_cast<Int32> (vv->GetInteger ());
			}
		} else if (GS::Ref<JS::Value> v = GS::DynamicCast<JS::Value> (param)) {
			// Для совместимости: если передано одно число — считаем его индексом существующего макета
			if (v->GetType () == JS::Value::INTEGER)
				layoutIndex = static_cast<Int32> (v->GetInteger ());
		}

		double w = 0.0, h = 0.0;
		bool ok = false;
		if (mode == "master" || mode == "template") {
			ok = GetLayoutWorkingAreaByMasterIndex (masterIndex, w, h);
		} else {
			ok = GetLayoutWorkingAreaByExistingIndex (layoutIndex, w, h);
		}

		GS::Ref<JS::Object> result = new JS::Object ();
		result->AddItem ("ok", new JS::Value (ok));
		result->AddItem ("widthMm", new JS::Value (w));
		result->AddItem ("heightMm", new JS::Value (h));
		return result;
		}));

	jsACAPI->AddItem(new JS::Function("GetDrawingScale", [](GS::Ref<JS::Base>) {
		return new JS::Value(LayoutHelper::GetCurrentDrawingScale());
		}));

	jsACAPI->AddItem(new JS::Function("GetPlaceableViews", [](GS::Ref<JS::Base>) {
		const GS::Array<LayoutHelper::PlaceableViewItem> views = LayoutHelper::GetPlaceableViews();
		GS::Ref<JS::Array> jsArr = new JS::Array();
		for (UIndex i = 0; i < views.GetSize(); ++i) {
			GS::Ref<JS::Object> obj = new JS::Object();
			obj->AddItem("guid", new JS::Value(APIGuidToString(views[i].viewGuid)));
			obj->AddItem("name", new JS::Value(views[i].name));
			obj->AddItem("typeName", new JS::Value(views[i].typeName));
			jsArr->AddItem(obj);
		}
		return jsArr;
		}));

	jsACAPI->AddItem(new JS::Function("PlaceOnLayout", [](GS::Ref<JS::Base> param) {
		LayoutHelper::PlaceParams p = {};
		p.masterLayoutIndex = -1;
		p.layoutIndex = 0;
		p.scale = 100.0;
		if (GS::Ref<JS::Object> obj = GS::DynamicCast<JS::Object>(param)) {
			const GS::HashTable<GS::UniString, GS::Ref<JS::Base>>& tbl = obj->GetItemTable();
			GS::Ref<JS::Base> item;
			if (tbl.Get("masterLayoutIndex", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value>(item))
					p.masterLayoutIndex = static_cast<Int32>(vv->GetInteger());
			}
			if (tbl.Get("layoutIndex", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value>(item))
					p.layoutIndex = static_cast<Int32>(vv->GetInteger());
			}
			if (tbl.Get("scale", &item))
				p.scale = GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 100.0);
			if (tbl.Get("drawingName", &item))
				p.drawingName = GetStringFromJavaScriptVariable(item);
			if (tbl.Get("layoutName", &item))
				p.layoutName = GetStringFromJavaScriptVariable(item);

			// Точка привязки вида на макете (радиокнопки LB/LT/RT/RB/MM)
			// Читаем максимально устойчиво: сначала строку, при неудаче — возможное целочисленное значение.
			if (!tbl.Get("anchorPosition", &item)) {
				// fallback на ключ "anchor" на случай изменений в HTML
				tbl.Get("anchor", &item);
			}
			if (item != nullptr) {
				GS::UniString anchorStr = GetStringFromJavaScriptVariable(item);
				anchorStr.Trim ();
				if (anchorStr == "LT") {
					p.anchorPosition = LayoutHelper::PlaceParams::Anchor::LeftTop;
				} else if (anchorStr == "RT") {
					p.anchorPosition = LayoutHelper::PlaceParams::Anchor::RightTop;
				} else if (anchorStr == "RB") {
					p.anchorPosition = LayoutHelper::PlaceParams::Anchor::RightBottom;
				} else if (anchorStr == "MM") {
					p.anchorPosition = LayoutHelper::PlaceParams::Anchor::Middle;
				} else if (anchorStr == "LB") {
					p.anchorPosition = LayoutHelper::PlaceParams::Anchor::LeftBottom;
				} else if (anchorStr.IsEmpty ()) {
					// Возможен вариант, когда значение приходит как число (0..4)
					if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value> (item)) {
						if (vv->GetType () == JS::Value::INTEGER) {
							switch (vv->GetInteger ()) {
								case 1: p.anchorPosition = LayoutHelper::PlaceParams::Anchor::LeftTop;    break;
								case 2: p.anchorPosition = LayoutHelper::PlaceParams::Anchor::RightTop;   break;
								case 3: p.anchorPosition = LayoutHelper::PlaceParams::Anchor::RightBottom;break;
								case 4: p.anchorPosition = LayoutHelper::PlaceParams::Anchor::Middle;     break;
								default: p.anchorPosition = LayoutHelper::PlaceParams::Anchor::LeftBottom;break;
							}
						}
					}
				}
				// Если anchorStr не распознан и не число — остаётся значение по умолчанию (LeftBottom)
			}
			if (tbl.Get("fitScaleToLayout", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value>(item))
					p.fitScaleToLayout = vv->GetBool();
			}
			if (tbl.Get("useMarqueeAsBoundary", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value>(item))
					p.useMarqueeAsBoundary = vv->GetBool();
			}
			// Палитра «Организация чертежей»: вид по GUID и область сетки
			if (tbl.Get("placeViewGuid", &item)) {
				GS::UniString guidStr = GetStringFromJavaScriptVariable(item);
				if (!guidStr.IsEmpty())
					p.placeViewGuid = APIGuidFromString(guidStr.ToCStr().Get());
			}
			if (tbl.Get("useGridRegion", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value>(item))
					p.useGridRegion = vv->GetBool();
			}
			if (tbl.Get("gridRows", &item))
				p.gridRows = static_cast<Int32>(GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 1));
			if (tbl.Get("gridCols", &item))
				p.gridCols = static_cast<Int32>(GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 1));
			if (tbl.Get("gridGapMm", &item))
				p.gridGapMm = GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 0);
			if (tbl.Get("regionStartRow", &item))
				p.regionStartRow = static_cast<Int32>(GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 0));
			if (tbl.Get("regionStartCol", &item))
				p.regionStartCol = static_cast<Int32>(GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 0));
			if (tbl.Get("regionSpanRows", &item))
				p.regionSpanRows = static_cast<Int32>(GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 1));
			if (tbl.Get("regionSpanCols", &item))
				p.regionSpanCols = static_cast<Int32>(GetDoubleFromJs(GS::DynamicCast<JS::Value>(item), 1));
			if (tbl.Get("cloneViewForPlacement", &item)) {
				if (GS::Ref<JS::Value> vv = GS::DynamicCast<JS::Value>(item))
					p.cloneViewForPlacement = vv->GetBool();
			}
		} else if (GS::Ref<JS::Value> v = GS::DynamicCast<JS::Value>(param)) {
			p.layoutIndex = static_cast<Int32>(v->GetInteger());
		}
		const bool success = LayoutHelper::PlaceSelectionOnLayoutWithParams(p);
		return ConvertToJavaScriptVariable(success);
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

	jsACAPI->AddItem(new JS::Function("OpenToLayoutPalette", [](GS::Ref<JS::Base>) {
		ToLayoutPalette::ShowPalette();
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
				ACAPI_WriteReport("[JS] ", false);
				ACAPI_WriteReport(v->GetString().ToCStr().Get(), false);
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
		if (buttonId != ToolbarButtonCloseId && buttonId != ToolbarButtonSupportId && buttonId != ToolbarButtonLayoutId) {
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
		case ToolbarButtonLayoutId:
			ToLayoutPalette::ShowPalette();
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
