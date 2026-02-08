// *****************************************************************************
// LayoutHelper: размещение вида (плана/разреза/фасада) в макет как связанный Drawing
// *****************************************************************************

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "LayoutHelper.hpp"
#include "DGModule.hpp"
#include "DGDefs.h"
#include "GSGuid.hpp"
#include "HashSet.hpp"
#include "uchar_t.hpp"
#include <new>

namespace LayoutHelper {

// GUID для диалога выбора макета (уникальный)
static const GS::Guid layoutPickerDialogGuid("{5e6f7081-9234-1234-ef01-345678901234}");

// -----------------------------------------------------------------------------
// Диалог выбора макета (один столбец, OK/Cancel)
// -----------------------------------------------------------------------------
class LayoutPickerDialog : public DG::ModalDialog,
	public DG::ButtonItemObserver,
	public DG::CompoundItemObserver
{
public:
	LayoutPickerDialog (const GS::Array<LayoutItem>& items)
		: DG::ModalDialog (DG::NativePoint (), 320, 280, layoutPickerDialogGuid,
			DG::ModalDialog::NoGrow, DG::ModalDialog::TopCaption,
			DG::Dialog::NormalFrame),
		listBox (GetReference (), DG::Rect (10, 10, 310, 210),
			DG::ListBox::VScroll, DG::ListBox::PartialItems,
			DG::ListBox::NoHeader, 0, DG::ListBox::Frame),
		okButton (GetReference (), DG::Rect (130, 230, 210, 252)),
		cancelButton (GetReference (), DG::Rect (220, 230, 300, 252)),
		layoutItems (items),
		selectedIndex (-1)
	{
		AttachToAllItems (*this);
		okButton.SetText ("OK");
		cancelButton.SetText (GS::UniString ("Cancel"));
		listBox.SetTabFieldCount (1);
		listBox.SetTabFieldProperties (0, 0, 280, DG::ListBox::Left, DG::ListBox::NoTruncate);
		for (UIndex i = 0; i < items.GetSize (); ++i) {
			listBox.AppendItem ();
			listBox.SetTabItemText (static_cast<short> (i), 0, items[i].name);
		}
		if (listBox.GetItemCount () > 0)
			listBox.SelectItem (0);
		ShowItems ();
	}

	~LayoutPickerDialog ()
	{
		DetachFromAllItems (*this);
	}

	void ButtonClicked (const DG::ButtonClickEvent& ev) override
	{
		if (ev.GetSource () == &okButton) {
			selectedIndex = listBox.GetSelectedItem (DG::ListBox::TopItem);
			PostCloseRequest (DG::ModalDialog::Accept);
		} else if (ev.GetSource () == &cancelButton) {
			selectedIndex = -1;
			PostCloseRequest (DG::ModalDialog::Cancel);
		}
	}

	Int32 GetSelectedIndex () const { return selectedIndex; }

private:
	DG::SingleSelListBox listBox;
	DG::Button okButton;
	DG::Button cancelButton;
	const GS::Array<LayoutItem>& layoutItems;
	Int32 selectedIndex;
};

// -----------------------------------------------------------------------------
// GetLayoutList
// -----------------------------------------------------------------------------
GS::Array<LayoutItem> GetLayoutList ()
{
	GS::Array<LayoutItem> result;
	GS::Array<API_DatabaseUnId> dbIds;
	GSErrCode err = ACAPI_Database_GetLayoutDatabases (nullptr, &dbIds);
	if (err != NoError || dbIds.IsEmpty ())
		return result;
	for (const API_DatabaseUnId& id : dbIds) {
		API_DatabaseInfo dbInfo = {};
		dbInfo.databaseUnId = id;
		dbInfo.typeID = APIWind_LayoutID;
		if (ACAPI_Window_GetDatabaseInfo (&dbInfo) == NoError) {
			LayoutItem item;
			item.databaseUnId = id;
			item.name = GS::UniString (dbInfo.name);
			result.Push (item);
		} else {
			LayoutItem item;
			item.databaseUnId = id;
			item.name = GS::UniString ("Layout");
			result.Push (item);
		}
	}
	return result;
}

// -----------------------------------------------------------------------------
// GetMasterLayoutList — шаблоны из папки Основные (Титульный лист, Обложка)
// -----------------------------------------------------------------------------
GS::Array<MasterLayoutItem> GetMasterLayoutList ()
{
	GS::Array<MasterLayoutItem> result;
	GS::Array<API_DatabaseUnId> dbIds;
	GSErrCode err = ACAPI_Database_GetMasterLayoutDatabases (nullptr, &dbIds);
	if (err != NoError || dbIds.IsEmpty ())
		return result;
	for (const API_DatabaseUnId& id : dbIds) {
		API_DatabaseInfo dbInfo = {};
		dbInfo.databaseUnId = id;
		dbInfo.typeID = APIWind_MasterLayoutID;
		if (ACAPI_Window_GetDatabaseInfo (&dbInfo) == NoError) {
			MasterLayoutItem item;
			item.databaseUnId = id;
			item.name = GS::UniString (dbInfo.name);
			result.Push (item);
		} else {
			MasterLayoutItem item;
			item.databaseUnId = id;
			item.name = GS::UniString ("Master");
			result.Push (item);
		}
	}
	return result;
}

// -----------------------------------------------------------------------------
// GetCurrentDrawingScale
// -----------------------------------------------------------------------------
double GetCurrentDrawingScale ()
{
	double scale = 100.0;
	if (ACAPI_Drawing_GetDrawingScale (&scale) != NoError)
		return 100.0;
	return scale > 0 ? scale : 100.0;
}

// -----------------------------------------------------------------------------
// Получить Navigator item для текущей БД (план/разрез/фасад)
// -----------------------------------------------------------------------------
static bool GetCurrentViewNavigatorItem (API_Guid& outGuid)
{
	API_DatabaseInfo currentDb = {};
	if (ACAPI_Database_GetCurrentDatabase (&currentDb) != NoError)
		return false;
	API_NavigatorItemTypeID itemType = API_UndefinedNavItem;
	switch (currentDb.typeID) {
		case APIWind_FloorPlanID:      itemType = API_StoryNavItem; break;
		case APIWind_SectionID:        itemType = API_SectionNavItem; break;
		case APIWind_ElevationID:      itemType = API_ElevationNavItem; break;
		case APIWind_InteriorElevationID: itemType = API_InteriorElevationNavItem; break;
		case APIWind_DetailID:         itemType = API_DetailDrawingNavItem; break;
		case APIWind_WorksheetID:      itemType = API_WorksheetDrawingNavItem; break;
		default: return false;
	}
	API_NavigatorItem navItem = {};
	navItem.mapId = API_PublicViewMap;
	navItem.itemType = itemType;
	navItem.db = currentDb;
	GS::Array<API_NavigatorItem> items;
	if (ACAPI_Navigator_SearchNavigatorItem (&navItem, &items) != NoError || items.IsEmpty ())
		return false;
	for (UIndex i = 0; i < items.GetSize (); i++) {
		if (items[i].db.databaseUnId == currentDb.databaseUnId) {
			outGuid = items[i].guid;
			return true;
		}
	}
	if (!items.IsEmpty ())
		outGuid = items[0].guid;
	return !items.IsEmpty ();
}

// -----------------------------------------------------------------------------
// Собрать слои выделенных элементов
// -----------------------------------------------------------------------------
static GS::HashSet<API_AttributeIndex> GetLayersOfSelection ()
{
	GS::HashSet<API_AttributeIndex> layers;
	API_SelectionInfo selectionInfo = {};
	GS::Array<API_Neig> selNeigs;
	if (ACAPI_Selection_Get (&selectionInfo, &selNeigs, true) != NoError)
		return layers;
	BMKillHandle ((GSHandle*)&selectionInfo.marquee.coords);
	if (selectionInfo.typeID == API_SelEmpty || selNeigs.IsEmpty ())
		return layers;
	for (UIndex i = 0; i < selNeigs.GetSize () && i < static_cast<UIndex>(selectionInfo.sel_nElemEdit); i++) {
		API_Element elem = {};
		elem.header.guid = selNeigs[i].guid;
		if (ACAPI_Element_Get (&elem) == NoError)
			layers.Add (elem.header.layer);
	}
	return layers;
}

// -----------------------------------------------------------------------------
// Состояние слоёв вида для сохранения/восстановления
// -----------------------------------------------------------------------------
struct ViewLayerState {
	API_Guid viewGuid;
	GS::HashTable<API_AttributeIndex, API_LayerStat>* layerStatsCopy = nullptr;
	char layerCombination[API_AttrNameLen] = {};
	bool saveLaySet = false;
};

// -----------------------------------------------------------------------------
// Применить к виду: показывать ТОЛЬКО слои выделенных элементов
// -----------------------------------------------------------------------------
static bool ApplyLayersFilterToView (const API_Guid& viewGuid, const GS::HashSet<API_AttributeIndex>& selectedLayers)
{
	if (selectedLayers.IsEmpty ())
		return false;
	GS::Array<API_Attribute> layerCombs;
	if (ACAPI_Attribute_GetAttributesByType (API_LayerCombID, layerCombs) != NoError || layerCombs.IsEmpty ())
		return false;
	API_AttributeDef defs = {};
	if (ACAPI_Attribute_GetDef (API_LayerCombID, layerCombs[0].header.index, &defs) != NoError || defs.layer_statItems == nullptr)
		return false;
	GS::HashTable<API_AttributeIndex, API_LayerStat>* newStats = new GS::HashTable<API_AttributeIndex, API_LayerStat> ();
	for (auto it = defs.layer_statItems->BeginPairs (); it != nullptr; ++it) {
		API_LayerStat stat = *it->value;
		if (selectedLayers.Contains (*it->key))
			stat.lFlags &= static_cast<short>(~APILay_Hidden);
		else
			stat.lFlags |= APILay_Hidden;
		newStats->Add (*it->key, stat);
	}
	ACAPI_DisposeAttrDefsHdls (&defs);
	API_NavigatorItem navItem = {};
	navItem.guid = viewGuid;
	API_NavigatorView navView = {};
	if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) != NoError) {
		delete newStats;
		return false;
	}
	navView.layerStats = newStats;
	navView.layerCombination[0] = '\0';
	navView.saveLaySet = true;
	GSErrCode err = ACAPI_Navigator_ChangeNavigatorView (&navItem, &navView);
	delete newStats;
	return (err == NoError);
}

static void RestoreViewLayerState (ViewLayerState& state)
{
	API_NavigatorItem navItem = {};
	navItem.guid = state.viewGuid;
	API_NavigatorView navView = {};
	if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) != NoError)
		return;
	navView.layerStats = state.layerStatsCopy;
	CHCopyC (state.layerCombination, navView.layerCombination);
	navView.saveLaySet = state.saveLaySet;
	ACAPI_Navigator_ChangeNavigatorView (&navItem, &navView);
	if (state.layerStatsCopy != nullptr) {
		delete state.layerStatsCopy;
		state.layerStatsCopy = nullptr;
	}
}

// -----------------------------------------------------------------------------
// Размещение связанного Drawing (вид → макет) по выбранному макету
// -----------------------------------------------------------------------------
static bool DoPlaceLinkedDrawingOnLayout (API_DatabaseUnId chosenLayoutId, const GS::UniString& drawingName)
{
	API_Guid viewGuid = {};
	if (!GetCurrentViewNavigatorItem (viewGuid)) {
		ACAPI_WriteReport ("Otkroite plan, razrez ili fasad pered razmeshcheniem.", true);
		return false;
	}
	GS::HashSet<API_AttributeIndex> selectedLayers = GetLayersOfSelection ();
	ViewLayerState savedState = {};
	savedState.viewGuid = viewGuid;
	bool layersFilterApplied = false;
	if (!selectedLayers.IsEmpty ()) {
		API_NavigatorItem navItem = {};
		navItem.guid = viewGuid;
		API_NavigatorView navView = {};
		if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) == NoError) {
			CHCopyC (navView.layerCombination, savedState.layerCombination);
			savedState.saveLaySet = navView.saveLaySet;
			if (navView.layerStats != nullptr)
				savedState.layerStatsCopy = new GS::HashTable<API_AttributeIndex, API_LayerStat> (*navView.layerStats);
			layersFilterApplied = ApplyLayersFilterToView (viewGuid, selectedLayers);
		}
	}
	API_DatabaseInfo currentDb = {};
	if (ACAPI_Database_GetCurrentDatabase (&currentDb) != NoError) {
		if (layersFilterApplied)
			RestoreViewLayerState (savedState);
		return false;
	}
	API_Element element = {};
	element.header.type = API_DrawingID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		if (layersFilterApplied)
			RestoreViewLayerState (savedState);
		return false;
	}
	element.drawing.drawingGuid = viewGuid;
	element.drawing.nameType = APIName_CustomName;
	GS::UniString name = drawingName.IsEmpty () ? GS::UniString ("Vid v maket") : drawingName;
	CHCopyC (name.ToCStr (CC_UTF8).Get (), element.drawing.name);
	element.drawing.ratio = 1.0;
	element.drawing.anchorPoint = APIAnc_LB;
	element.drawing.pos.x = 0.0;
	element.drawing.pos.y = 0.0;
	element.drawing.isCutWithFrame = true;
	err = ACAPI_CallUndoableCommand ("Place view on layout", [&] () -> GSErrCode {
		API_DatabaseInfo layoutDb = {};
		layoutDb.databaseUnId = chosenLayoutId;
		layoutDb.typeID = APIWind_LayoutID;
		GSErrCode e = ACAPI_Database_ChangeCurrentDatabase (&layoutDb);
		if (e != NoError)
			return e;
		API_ElementMemo memo = {};
		GSErrCode createErr = ACAPI_Element_Create (&element, &memo);
		ACAPI_Database_ChangeCurrentDatabase (&currentDb);
		return createErr;
	});
	if (layersFilterApplied)
		RestoreViewLayerState (savedState);
	if (err != NoError) {
		ACAPI_WriteReport ("LayoutHelper: ne udalos sozdat Drawing na makete", true);
		return false;
	}
	ACAPI_WriteReport ("Vid razmeshchen v makete.", false);
	return true;
}

// -----------------------------------------------------------------------------
// PlaceSelectionOnLayoutWithParams — основной метод
// -----------------------------------------------------------------------------
bool PlaceSelectionOnLayoutWithParams (const PlaceParams& params)
{
	API_DatabaseUnId targetLayoutId = {};

	if (params.masterLayoutIndex >= 0) {
		// Создаём новый макет из шаблона
		GS::Array<MasterLayoutItem> masters = GetMasterLayoutList ();
		if (params.masterLayoutIndex >= (Int32)masters.GetSize ()) {
			ACAPI_WriteReport ("Неверный индекс шаблона макета.", true);
			return false;
		}
		// Список макетов до создания (для поиска нового)
		GS::Array<API_DatabaseUnId> layoutIdsBefore;
		{
			GS::Array<API_DatabaseUnId> dbIds;
			if (ACAPI_Database_GetLayoutDatabases (nullptr, &dbIds) == NoError)
				layoutIdsBefore = dbIds;
		}
		API_LayoutInfo layoutInfo = {};
		BNZeroMemory (&layoutInfo, sizeof (layoutInfo));
		API_DatabaseUnId masterId = masters[params.masterLayoutIndex].databaseUnId;
		if (ACAPI_Navigator_GetLayoutSets (&layoutInfo, &masterId) != NoError) {
			ACAPI_WriteReport ("LayoutHelper: GetLayoutSets (master) failed", true);
			return false;
		}
		GS::UniString layoutName = params.layoutName.IsEmpty () ? GS::UniString ("Noviy maket") : params.layoutName;
		BNZeroMemory (layoutInfo.layoutName, sizeof (layoutInfo.layoutName));
		GS::snuprintf (layoutInfo.layoutName, API_UniLongNameLen, "%s", layoutName.ToCStr (CC_UTF8).Get ());
		if (layoutInfo.customData != nullptr) {
			delete layoutInfo.customData;
			layoutInfo.customData = nullptr;
		}

		GSErrCode crErr = ACAPI_Navigator_CreateLayout (&layoutInfo, &masterId, nullptr);
		if (crErr != NoError) {
			ACAPI_WriteReport ("LayoutHelper: CreateLayout failed", true);
			return false;
		}
		// Ищем созданный макет: тот, которого не было в списке до создания
		GS::Array<API_DatabaseUnId> layoutIdsAfter;
		if (ACAPI_Database_GetLayoutDatabases (nullptr, &layoutIdsAfter) != NoError) {
			ACAPI_WriteReport ("LayoutHelper: не удалось получить список макетов", true);
			return false;
		}
		for (UIndex i = 0; i < layoutIdsAfter.GetSize (); i++) {
			bool foundBefore = false;
			for (UIndex j = 0; j < layoutIdsBefore.GetSize (); j++) {
				if (layoutIdsAfter[i] == layoutIdsBefore[j]) {
					foundBefore = true;
					break;
				}
			}
			if (!foundBefore) {
				targetLayoutId = layoutIdsAfter[i];
				break;
			}
		}
		if (targetLayoutId.elemSetId == APINULLGuid) {
			ACAPI_WriteReport ("LayoutHelper: не удалось найти созданный макет", true);
			return false;
		}
	} else {
		// Используем существующий макет
		GS::Array<LayoutItem> layouts = GetLayoutList ();
		if (layouts.IsEmpty ()) {
			ACAPI_WriteReport ("В проекте нет макетов.", true);
			return false;
		}
		if (params.layoutIndex < 0 || params.layoutIndex >= (Int32)layouts.GetSize ()) {
			ACAPI_WriteReport ("Неверный индекс макета.", true);
			return false;
		}
		targetLayoutId = layouts[params.layoutIndex].databaseUnId;
	}

	return DoPlaceLinkedDrawingOnLayout (targetLayoutId, params.drawingName);
}

// -----------------------------------------------------------------------------
// PlaceSelectionOnLayout — диалог выбора макета + размещение
// -----------------------------------------------------------------------------
bool PlaceSelectionOnLayout ()
{
	GS::Array<LayoutItem> layouts = GetLayoutList ();
	if (layouts.IsEmpty ()) {
		ACAPI_WriteReport ("В проекте нет макетов.", true);
		return false;
	}

	LayoutPickerDialog dlg (layouts);
	bool accepted = dlg.Invoke ();
	Int32 selIdx = dlg.GetSelectedIndex ();
	if (!accepted || selIdx < 0 || selIdx >= (Int32)layouts.GetSize ()) {
		return false;
	}

	PlaceParams p = {};
	p.masterLayoutIndex = -1;
	p.layoutIndex = selIdx;
	p.scale = 100.0;
	p.drawingName = GS::UniString ("Выделение в макет");
	return PlaceSelectionOnLayoutWithParams (p);
}

// -----------------------------------------------------------------------------
// PlaceSelectionOnLayoutByIndex — устаревший вызов для совместимости
// -----------------------------------------------------------------------------
bool PlaceSelectionOnLayoutByIndex (Int32 layoutIndex)
{
	PlaceParams p = {};
	p.masterLayoutIndex = -1;
	p.layoutIndex = layoutIndex;
	p.scale = 100.0;
	p.drawingName = GS::UniString ("Выделение в макет");
	return PlaceSelectionOnLayoutWithParams (p);
}

} // namespace LayoutHelper
