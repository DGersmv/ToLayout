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
#include <cstdio>

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
// Получить floorInd из выделенных элементов.
// Берём floorInd первого выделенного элемента — он определяет целевой этаж.
// -----------------------------------------------------------------------------
static bool GetSelectionFloorInd (short& outFloorInd)
{
	API_SelectionInfo selInfo = {};
	GS::Array<API_Neig> selNeigs;
	if (ACAPI_Selection_Get (&selInfo, &selNeigs, true) != NoError) {
		return false;
	}
	BMKillHandle ((GSHandle*) &selInfo.marquee.coords);
	if (selInfo.typeID == API_SelEmpty || selNeigs.IsEmpty ())
		return false;
	// Берём floorInd первого выделенного элемента
	for (UIndex i = 0; i < selNeigs.GetSize (); i++) {
		API_Elem_Head elemHead = {};
		elemHead.guid = selNeigs[i].guid;
		if (ACAPI_Element_GetHeader (&elemHead) == NoError) {
			outFloorInd = elemHead.floorInd;
			return true;
		}
	}
	return false;
}

// -----------------------------------------------------------------------------
// Распарсить индекс этажа из имени элемента навигатора.
// ArchiCAD формирует имена story-элементов в формате "N. Имя этажа",
// где N — целое число (индекс этажа), например: "0. Первый этаж", "-1. Подвал"
// -----------------------------------------------------------------------------
static bool ParseStoryIndexFromNavItemName (const GS::UniString& name, short& outIndex)
{
	if (name.IsEmpty ())
		return false;
	GS::UniString trimmed = name;
	trimmed.Trim ();
	int parsed = 0;
	if (std::sscanf (trimmed.ToCStr ().Get (), "%d", &parsed) == 1) {
		outIndex = static_cast<short> (parsed);
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Найти элемент навигатора (story) для заданного floorInd.
// Перебирает список story-элементов, для каждого получает полное имя (uName)
// и парсит из него индекс этажа. Сопоставляет с целевым floorInd.
// -----------------------------------------------------------------------------
static bool FindStoryNavItemByFloorInd (const GS::Array<API_NavigatorItem>& storyItems, short targetFloorInd, API_Guid& outGuid)
{
	if (storyItems.IsEmpty ())
		return false;

	// Стратегия: парсим индекс этажа из имени каждого story-элемента навигатора
	for (UIndex i = 0; i < storyItems.GetSize (); i++) {
		API_NavigatorItem fullItem = {};
		if (ACAPI_Navigator_GetNavigatorItem (&storyItems[i].guid, &fullItem) == NoError) {
			GS::UniString itemName (fullItem.uName);
			short parsedIndex = 0;
			if (ParseStoryIndexFromNavItemName (itemName, parsedIndex) && parsedIndex == targetFloorInd) {
				outGuid = storyItems[i].guid;
				return true;
			}
		}
	}
	return false;
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
	// Для планов этажей: все этажи имеют одинаковый databaseUnId,
	// поэтому определяем целевой этаж из выделенных элементов (floorInd)
	// и ищем соответствующий элемент навигатора по номеру этажа в имени
	if (currentDb.typeID == APIWind_FloorPlanID && items.GetSize () > 1) {
		short targetFloor = 0;
		if (GetSelectionFloorInd (targetFloor)) {
			if (FindStoryNavItemByFloorInd (items, targetFloor, outGuid))
				return true;
		}
	}
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
// Найти элемент в Project Map для клонирования (источник для View Map)
// -----------------------------------------------------------------------------
static bool GetProjectMapItemForCurrentView (API_Guid& outGuid)
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
	navItem.mapId = API_ProjectMap;
	navItem.itemType = itemType;
	navItem.db = currentDb;
	GS::Array<API_NavigatorItem> items;
	if (ACAPI_Navigator_SearchNavigatorItem (&navItem, &items) != NoError || items.IsEmpty ())
		return false;
	// Для планов этажей: все этажи имеют одинаковый databaseUnId,
	// поэтому определяем целевой этаж из выделенных элементов (floorInd)
	// и ищем соответствующий элемент навигатора по номеру этажа в имени
	if (currentDb.typeID == APIWind_FloorPlanID && items.GetSize () > 1) {
		short targetFloor = 0;
		if (GetSelectionFloorInd (targetFloor)) {
			if (FindStoryNavItemByFloorInd (items, targetFloor, outGuid))
				return true;
		}
	}
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
// Вычислить bounding box выделения + отступ 1 м во все стороны
// Возвращает true и заполняет outBox при наличии выделения, иначе false
// -----------------------------------------------------------------------------
static bool GetSelectionBoundsWithMargin (API_Box& outBox)
{
	API_SelectionInfo selectionInfo = {};
	GS::Array<API_Neig> selNeigs;
	if (ACAPI_Selection_Get (&selectionInfo, &selNeigs, true) != NoError)
		return false;
	BMKillHandle ((GSHandle*)&selectionInfo.marquee.coords);
	if (selectionInfo.typeID == API_SelEmpty || selNeigs.IsEmpty ())
		return false;
	bool first = true;
	double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
	for (UIndex i = 0; i < selNeigs.GetSize () && i < static_cast<UIndex>(selectionInfo.sel_nElemEdit); i++) {
		API_Elem_Head elemHead = {};
		elemHead.guid = selNeigs[i].guid;
		if (ACAPI_Element_GetHeader (&elemHead) != NoError)
			continue;
		API_Box3D bounds = {};
		if (ACAPI_Element_CalcBounds (&elemHead, &bounds) != NoError)
			continue;
		if (first) {
			xMin = bounds.xMin;
			yMin = bounds.yMin;
			xMax = bounds.xMax;
			yMax = bounds.yMax;
			first = false;
		} else {
			if (bounds.xMin < xMin) xMin = bounds.xMin;
			if (bounds.yMin < yMin) yMin = bounds.yMin;
			if (bounds.xMax > xMax) xMax = bounds.xMax;
			if (bounds.yMax > yMax) yMax = bounds.yMax;
		}
	}
	if (first)
		return false;
	const double margin = 1.0;
	outBox.xMin = xMin - margin;
	outBox.yMin = yMin - margin;
	outBox.xMax = xMax + margin;
	outBox.yMax = yMax + margin;
	return true;
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

// Имя временной комбинации слоёв для размещения
static const char* kTempLayerCombName = "ToLayout_Выделение";

// -----------------------------------------------------------------------------
// Создать новую комбинацию слоёв: только слои выделенных элементов видимы
// Остальные слои — скрыты. Возвращает true при успехе.
// -----------------------------------------------------------------------------
static bool CreateLayerCombForSelection (const GS::HashSet<API_AttributeIndex>& selectedLayers, API_Attr_Head& outAttrHead)
{
	if (selectedLayers.IsEmpty ())
		return false;
	API_Attr_Head existing = {};
	existing.typeID = API_LayerCombID;
	CHCopyC (kTempLayerCombName, existing.name);
	if (ACAPI_Attribute_Search (&existing) == NoError)
		ACAPI_Attribute_Delete (existing);
	GS::Array<API_Attribute> layerCombs;
	if (ACAPI_Attribute_GetAttributesByType (API_LayerCombID, layerCombs) != NoError || layerCombs.IsEmpty ())
		return false;
	API_AttributeDef defs = {};
	if (ACAPI_Attribute_GetDef (API_LayerCombID, layerCombs[0].header.index, &defs) != NoError || defs.layer_statItems == nullptr)
		return false;
	GS::HashTable<API_AttributeIndex, API_LayerStat>* newStats = new GS::HashTable<API_AttributeIndex, API_LayerStat> ();
	for (auto it = defs.layer_statItems->BeginPairs (); it != nullptr; ++it) {
		API_LayerStat stat = *it->value;
		const API_AttributeIndex layerIdx = *it->key;
		if (layerIdx == APIApplicationLayerAttributeIndex) {
			stat.lFlags &= static_cast<short>(~APILay_Hidden);
		} else if (selectedLayers.Contains (layerIdx)) {
			stat.lFlags &= static_cast<short>(~APILay_Hidden);
		} else {
			stat.lFlags |= APILay_Hidden;
		}
		newStats->Add (layerIdx, stat);
	}
	ACAPI_DisposeAttrDefsHdls (&defs);
	defs.layer_statItems = newStats;
	API_Attribute attrib = {};
	attrib.header.typeID = API_LayerCombID;
	strcpy (attrib.layerComb.head.name, kTempLayerCombName);
	attrib.layerComb.lNumb = static_cast<Int32> (newStats->GetSize ());
	GSErrCode err = ACAPI_Attribute_Create (&attrib, &defs);
	ACAPI_DisposeAttrDefsHdls (&defs);
	if (err != NoError)
		return false;
	outAttrHead = attrib.header;
	return true;
}

// -----------------------------------------------------------------------------
// Создать новый вид во вкладке «Виды»: клон текущего вида + фильтр слоёв
// customViewName — имя вида в Карте Видов (пустое = не менять)
// zoomBox — опционально: рамка обрезки вида (bounding box выделения + отступ)
// Возвращает guid созданного вида или APINULLGuid при ошибке
// -----------------------------------------------------------------------------
static API_Guid CloneViewToViewMapWithLayerFilter (const GS::HashSet<API_AttributeIndex>& selectedLayers, Int32 drawingScale, const GS::UniString& customViewName, const API_Box* zoomBox = nullptr)
{
	API_Guid sourceGuid = {};
	if (!GetProjectMapItemForCurrentView (sourceGuid))
		return APINULLGuid;
	API_NavigatorSet viewSet = {};
	viewSet.mapId = API_PublicViewMap;
	if (ACAPI_Navigator_GetNavigatorSet (&viewSet) != NoError)
		return APINULLGuid;
	API_NavigatorItem rootItem = {};
	rootItem.guid = viewSet.rootGuid;
	rootItem.mapId = API_PublicViewMap;
	GS::Array<API_NavigatorItem> rootChildren;
	if (ACAPI_Navigator_GetNavigatorChildrenItems (&rootItem, &rootChildren) != NoError || rootChildren.IsEmpty ())
		return APINULLGuid;
	API_Guid parentGuid = rootChildren[0].guid;
	API_Guid clonedGuid = {};
	if (ACAPI_Navigator_CloneProjectMapItemToViewMap (&sourceGuid, &parentGuid, &clonedGuid) != NoError || clonedGuid == APINULLGuid)
		return APINULLGuid;
	if (selectedLayers.IsEmpty ())
		return clonedGuid;
	API_Attr_Head tempLayerCombHead = {};
	if (!CreateLayerCombForSelection (selectedLayers, tempLayerCombHead))
		return clonedGuid;
	API_NavigatorItem clonedNavItem = {};
	clonedNavItem.guid = clonedGuid;
	clonedNavItem.mapId = API_PublicViewMap;
	API_NavigatorView navView = {};
	if (ACAPI_Navigator_GetNavigatorView (&clonedNavItem, &navView) != NoError) {
		if (navView.layerStats != nullptr) { delete navView.layerStats; navView.layerStats = nullptr; }
		return clonedGuid;
	}
	if (navView.layerStats != nullptr) {
		delete navView.layerStats;
		navView.layerStats = nullptr;
	}
	CHCopyC (kTempLayerCombName, navView.layerCombination);
	navView.saveLaySet = true;
	navView.drawingScale = drawingScale;
	navView.saveDScale = true;
	if (zoomBox != nullptr) {
		navView.zoom = *zoomBox;
		navView.saveZoom = true;
	}
	ACAPI_Navigator_ChangeNavigatorView (&clonedNavItem, &navView);
	if (!customViewName.IsEmpty ()) {
		if (ACAPI_Navigator_GetNavigatorItem (&clonedGuid, &clonedNavItem) == NoError) {
			clonedNavItem.customName = true;
			GS::ucscpy (clonedNavItem.uName, customViewName.ToUStr ());
			ACAPI_Navigator_ChangeNavigatorItem (&clonedNavItem);
		}
	}
	return clonedGuid;
}

// -----------------------------------------------------------------------------
// Применить к виду комбинацию слоёв по имени
// -----------------------------------------------------------------------------
static bool ApplyLayerCombToView (const API_Guid& viewGuid, const char* layerCombName)
{
	API_NavigatorItem navItem = {};
	navItem.guid = viewGuid;
	API_NavigatorView navView = {};
	if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) != NoError)
		return false;
	if (navView.layerStats != nullptr) {
		delete navView.layerStats;
		navView.layerStats = nullptr;
	}
	CHCopyC (layerCombName, navView.layerCombination);
	navView.saveLaySet = true;
	return (ACAPI_Navigator_ChangeNavigatorView (&navItem, &navView) == NoError);
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
// Вычислить позицию вида на макете по точке привязки
// layoutInfo — параметры макета (sizeX, sizeY, margins в мм)
// drawing.pos в layout БД использует внутренние единицы (метры), конвертируем мм → м
// -----------------------------------------------------------------------------
static void GetDrawingPositionForAnchor (const API_LayoutInfo& layoutInfo, PlaceParams::Anchor anchor, API_AnchorID& outAnchor, API_Coord& outPos)
{
	const double MM_TO_M = 1.0 / 1000.0;
	const double left = layoutInfo.leftMargin * MM_TO_M;
	const double right = (layoutInfo.sizeX - layoutInfo.rightMargin) * MM_TO_M;
	const double top = (layoutInfo.sizeY - layoutInfo.topMargin) * MM_TO_M;
	const double bottom = layoutInfo.bottomMargin * MM_TO_M;
	const double cx = left + (right - left) * 0.5;
	const double cy = bottom + (top - bottom) * 0.5;
	switch (anchor) {
		case PlaceParams::Anchor::LeftBottom:
			outAnchor = APIAnc_LB;
			outPos.x = left;
			outPos.y = bottom;
			break;
		case PlaceParams::Anchor::LeftTop:
			outAnchor = APIAnc_LT;
			outPos.x = left;
			outPos.y = top;
			break;
		case PlaceParams::Anchor::RightTop:
			outAnchor = APIAnc_RT;
			outPos.x = right;
			outPos.y = top;
			break;
		case PlaceParams::Anchor::RightBottom:
			outAnchor = APIAnc_RB;
			outPos.x = right;
			outPos.y = bottom;
			break;
		case PlaceParams::Anchor::Middle:
			outAnchor = APIAnc_MM;
			outPos.x = cx;
			outPos.y = cy;
			break;
		default:
			outAnchor = APIAnc_LB;
			outPos.x = left;
			outPos.y = bottom;
			break;
	}
}

// -----------------------------------------------------------------------------
// Размещение связанного Drawing (вид → макет) по выбранному макету
// -----------------------------------------------------------------------------
static bool DoPlaceLinkedDrawingOnLayout (API_DatabaseUnId chosenLayoutId, const PlaceParams& params)
{
	API_Guid dummyGuid = {};
	if (!GetCurrentViewNavigatorItem (dummyGuid)) {
		ACAPI_WriteReport ("Откройте план, разрез или фасад перед размещением.", true);
		return false;
	}
	const GS::UniString drawingName = params.drawingName.IsEmpty () ? GS::UniString ("Новый вид") : params.drawingName;
	double currentScale = GetCurrentDrawingScale ();
	GS::HashSet<API_AttributeIndex> selectedLayers = GetLayersOfSelection ();
	API_Box zoomBox = {};
	const bool hasZoomBox = GetSelectionBoundsWithMargin (zoomBox);
	API_Guid viewGuidForDrawing = APINULLGuid;
	if (!selectedLayers.IsEmpty ()) {
		viewGuidForDrawing = CloneViewToViewMapWithLayerFilter (
			selectedLayers, static_cast<Int32> (currentScale), drawingName,
			hasZoomBox ? &zoomBox : nullptr);
	}
	if (viewGuidForDrawing == APINULLGuid) {
		GetCurrentViewNavigatorItem (viewGuidForDrawing);
		if (viewGuidForDrawing == APINULLGuid) {
			ACAPI_WriteReport ("Не удалось получить вид для размещения.", true);
			return false;
		}
		API_NavigatorItem navItem = {};
		navItem.guid = viewGuidForDrawing;
		API_NavigatorView navView = {};
		if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) == NoError) {
			navView.drawingScale = static_cast<Int32> (currentScale);
			navView.saveDScale = true;
			ACAPI_Navigator_ChangeNavigatorView (&navItem, &navView);
			if (navView.layerStats != nullptr) {
				delete navView.layerStats;
				navView.layerStats = nullptr;
			}
		}
	}
	API_DatabaseInfo currentDb = {};
	if (ACAPI_Database_GetCurrentDatabase (&currentDb) != NoError)
		return false;
	API_LayoutInfo layoutInfo = {};
	BNZeroMemory (&layoutInfo, sizeof (layoutInfo));
	API_DatabaseUnId layoutDbId = chosenLayoutId;
	if (ACAPI_Navigator_GetLayoutSets (&layoutInfo, &layoutDbId) != NoError) {
		if (layoutInfo.customData != nullptr) {
			delete layoutInfo.customData;
			layoutInfo.customData = nullptr;
		}
	}
	API_AnchorID anchorId = APIAnc_LB;
	API_Coord drawPos = { 0.0, 0.0 };
	GetDrawingPositionForAnchor (layoutInfo, params.anchorPosition, anchorId, drawPos);
	if (layoutInfo.customData != nullptr) {
		delete layoutInfo.customData;
		layoutInfo.customData = nullptr;
	}

	API_Element element = {};
	element.header.type = API_DrawingID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError)
		return false;
	element.drawing.drawingGuid = viewGuidForDrawing;
	element.drawing.nameType = APIName_CustomName;
	CHCopyC (drawingName.ToCStr (CC_UTF8).Get (), element.drawing.name);
	element.drawing.ratio = 1.0;
	element.drawing.anchorPoint = anchorId;
	element.drawing.pos = drawPos;
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
	if (err != NoError) {
		ACAPI_WriteReport ("LayoutHelper: не удалось создать Drawing на макете", true);
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
		GS::UniString layoutName = params.layoutName.IsEmpty () ? GS::UniString ("Новый макет") : params.layoutName;
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

	return DoPlaceLinkedDrawingOnLayout (targetLayoutId, params);
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
