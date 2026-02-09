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
	if (ACAPI_Selection_Get (&selInfo, &selNeigs, true) != NoError)
		return false;
	BMKillHandle ((GSHandle*) &selInfo.marquee.coords);
	if (selInfo.typeID == API_SelEmpty || selNeigs.IsEmpty ())
		return false;
	for (UIndex i = 0; i < selNeigs.GetSize (); i++) {
		API_Element elem = {};
		elem.header.guid = selNeigs[i].guid;
		if (ACAPI_Element_Get (&elem) == NoError) {
			outFloorInd = elem.header.floorInd;
			return true;
		}
	}
	return false;
}

// -----------------------------------------------------------------------------
// Получить floorInd активного этажа на плане (не из выделения, а из текущего вида).
// Определяем этаж по имени текущего story-навигатора и story settings.
// -----------------------------------------------------------------------------
static bool GetActiveFloorInd (short& outFloorInd)
{
	// Получаем текущую базу данных
	API_DatabaseInfo currentDb = {};
	if (ACAPI_Database_GetCurrentDatabase (&currentDb) != NoError)
		return false;
	
	// Проверяем, что это план этажей
	if (currentDb.typeID != APIWind_FloorPlanID)
		return false;
	
	// Получаем story settings для сопоставления имён и floorInd
	API_StoryInfo storyInfo = {};
	if (ACAPI_ProjectSetting_GetStorySettings (&storyInfo) != NoError)
		return false;
	if (storyInfo.data == nullptr) {
		// Освобождаем memory handle даже если data == nullptr
		if (storyInfo.data != nullptr)
			BMKillHandle ((GSHandle*) &storyInfo.data);
		return false;
	}
	
	API_StoryType* stData = reinterpret_cast<API_StoryType*> (*storyInfo.data);
	short first  = storyInfo.firstStory;
	short last   = storyInfo.lastStory;
	
	// Ищем текущий story в навигаторе
	API_NavigatorItem navItem = {};
	navItem.mapId = API_PublicViewMap;
	navItem.itemType = API_StoryNavItem;
	navItem.db = currentDb;
	GS::Array<API_NavigatorItem> items;
	bool found = false;
	
	if (ACAPI_Navigator_SearchNavigatorItem (&navItem, &items) == NoError) {
		// Ищем элемент с совпадающим databaseUnId
		for (UIndex i = 0; i < items.GetSize (); i++) {
			if (items[i].db.databaseUnId == currentDb.databaseUnId) {
				// Получаем полную информацию о навигаторном элементе
				API_NavigatorItem fullItem = {};
				if (ACAPI_Navigator_GetNavigatorItem (&items[i].guid, &fullItem) == NoError) {
					GS::UniString currentStoryName (fullItem.uName);
					
					// Сначала пробуем точное совпадение
					for (short f = first; f <= last; f++) {
						short off = f - first;
						GS::UniString storyName (stData[off].uName);
						
						if (currentStoryName == storyName) {
							outFloorInd = f;
							found = true;
							
							char buf[512];
							std::snprintf (buf, sizeof buf,
								"[ToLayout] GetActiveFloorInd (exact): floor=%d name='%s'",
								(int)f, currentStoryName.ToCStr (CC_UTF8).Get ());
							ACAPI_WriteReport (buf, false);
							break;
						}
					}
					
					// Если точное совпадение не найдено, пробуем с префиксом (например "1. Этаж 1")
					if (!found) {
						for (short f = first; f <= last; f++) {
							short off = f - first;
							GS::UniString storyName (stData[off].uName);
							
							// Проверяем, что имя заканчивается на storyName (избегаем ложных совпадений)
							if (currentStoryName.EndsWith (storyName)) {
								outFloorInd = f;
								found = true;
								
								char buf[512];
								std::snprintf (buf, sizeof buf,
									"[ToLayout] GetActiveFloorInd (suffix): floor=%d name='%s'",
									(int)f, currentStoryName.ToCStr (CC_UTF8).Get ());
								ACAPI_WriteReport (buf, false);
								break;
							}
						}
					}
					
					if (found)
						break;
				}
			}
		}
	}
	
	BMKillHandle ((GSHandle*) &storyInfo.data);
	return found;
}

// -----------------------------------------------------------------------------
// Найти story-элемент навигатора для целевого этажа (floorInd).
//
// Стратегия: Получаем story settings через ACAPI_ProjectSetting_GetStorySettings,
// из них — имя и индекс каждого этажа, строим соответствие между floorInd
// и именем, затем ищем навигаторный элемент, чей uName содержит это имя.
//
// Альтернативная стратегия: обходим дерево Project Map / View Map, собираем
// story-элементы в порядке дерева, используем смещение от firstStory.
//
// Если ни одна стратегия не сработала — пишем в Report имена всех элементов
// для диагностики.
// -----------------------------------------------------------------------------
static bool FindStoryNavItemForFloor (const GS::Array<API_NavigatorItem>& storyItems,
	short targetFloorInd, API_Guid& outGuid)
{
	if (storyItems.IsEmpty ())
		return false;

	char buf[256];

	// ---------- Стратегия A: story settings ----------
	API_StoryInfo storyInfo = {};
	bool haveStoryInfo = (ACAPI_ProjectSetting_GetStorySettings (&storyInfo) == NoError);
	if (haveStoryInfo && storyInfo.data != nullptr) {
		API_StoryType* stData = reinterpret_cast<API_StoryType*> (*storyInfo.data);
		short first  = storyInfo.firstStory;
		short last   = storyInfo.lastStory;

		// Имя целевого этажа из настроек проекта
		GS::UniString targetStoryName;
		short off = targetFloorInd - first;
		if (off >= 0 && off <= last - first)
			targetStoryName = GS::UniString (stData[off].uName);

		BMKillHandle ((GSHandle*) &storyInfo.data);

		if (!targetStoryName.IsEmpty ()) {
			// Сначала ищем точное совпадение uName == storyName
			for (UIndex i = 0; i < storyItems.GetSize (); i++) {
				API_NavigatorItem fi = {};
				if (ACAPI_Navigator_GetNavigatorItem (&storyItems[i].guid, &fi) == NoError) {
					GS::UniString navName (fi.uName);
					if (navName == targetStoryName) {
						std::snprintf (buf, sizeof buf,
							"[ToLayout] StrategyA exact: floor=%d name='%s'",
							(int)targetFloorInd,
							targetStoryName.ToCStr (CC_UTF8).Get ());
						ACAPI_WriteReport (buf, false);
						outGuid = storyItems[i].guid;
						return true;
					}
				}
			}
			// Потом — «содержит» (навигатор может добавлять префикс вроде "1. ")
			for (UIndex i = 0; i < storyItems.GetSize (); i++) {
				API_NavigatorItem fi = {};
				if (ACAPI_Navigator_GetNavigatorItem (&storyItems[i].guid, &fi) == NoError) {
					GS::UniString navName (fi.uName);
					if (navName.Contains (targetStoryName)) {
						std::snprintf (buf, sizeof buf,
							"[ToLayout] StrategyA contains: floor=%d name='%s' nav='%s'",
							(int)targetFloorInd,
							targetStoryName.ToCStr (CC_UTF8).Get (),
							navName.ToCStr (CC_UTF8).Get ());
						ACAPI_WriteReport (buf, false);
						outGuid = storyItems[i].guid;
						return true;
					}
				}
			}
		}
	} else {
		if (storyInfo.data != nullptr) BMKillHandle ((GSHandle*) &storyInfo.data);
	}

	// ---------- Стратегия B: парсим число из начала uName ----------
	for (UIndex i = 0; i < storyItems.GetSize (); i++) {
		API_NavigatorItem fi = {};
		if (ACAPI_Navigator_GetNavigatorItem (&storyItems[i].guid, &fi) == NoError) {
			GS::UniString navName (fi.uName);
			int parsed = 0;
			if (std::sscanf (navName.ToCStr ().Get (), "%d", &parsed) == 1) {
				if (static_cast<short> (parsed) == targetFloorInd) {
					std::snprintf (buf, sizeof buf,
						"[ToLayout] StrategyB parse: floor=%d parsed=%d nav='%s'",
						(int)targetFloorInd, parsed,
						navName.ToCStr (CC_UTF8).Get ());
					ACAPI_WriteReport (buf, false);
					outGuid = storyItems[i].guid;
					return true;
				}
			}
		}
	}

	// ---------- Стратегия C: перебираем дерево Project Map, смещение ----------
	{
		API_NavigatorSet projSet = {};
		projSet.mapId = API_ProjectMap;
		if (ACAPI_Navigator_GetNavigatorSet (&projSet) == NoError) {
			API_NavigatorItem rootItem = {};
			rootItem.guid = projSet.rootGuid;
			rootItem.mapId = API_ProjectMap;
			GS::Array<API_NavigatorItem> rootChildren;
			if (ACAPI_Navigator_GetNavigatorChildrenItems (&rootItem, &rootChildren) == NoError) {
				// Собираем story-элементы в порядке дерева
				GS::Array<API_NavigatorItem> treeStories;
				for (UIndex c = 0; c < rootChildren.GetSize (); c++) {
					if (rootChildren[c].itemType == API_StoryNavItem)
						treeStories.Push (rootChildren[c]);
				}
				// Определяем firstStory: пробуем story settings или полагаем = 0
				short firstStory = 0;
				API_StoryInfo si2 = {};
				if (ACAPI_ProjectSetting_GetStorySettings (&si2) == NoError) {
					firstStory = si2.firstStory;
					if (si2.data != nullptr) BMKillHandle ((GSHandle*) &si2.data);
				} else {
					if (si2.data != nullptr) BMKillHandle ((GSHandle*) &si2.data);
				}
				short treeOff = targetFloorInd - firstStory;
				if (treeOff >= 0 && static_cast<UIndex> (treeOff) < treeStories.GetSize ()) {
					std::snprintf (buf, sizeof buf,
						"[ToLayout] StrategyC tree: floor=%d first=%d off=%d treeSize=%u",
						(int)targetFloorInd, (int)firstStory, (int)treeOff,
						(unsigned)treeStories.GetSize ());
					ACAPI_WriteReport (buf, false);
					outGuid = treeStories[static_cast<UIndex> (treeOff)].guid;
					return true;
				}
			}
		}
	}

	// ---------- Ничего не подошло — лог для диагностики ----------
	std::snprintf (buf, sizeof buf,
		"[ToLayout] FAIL: targetFloor=%d items=%u haveStoryInfo=%d",
		(int)targetFloorInd, (unsigned)storyItems.GetSize (), (int)haveStoryInfo);
	ACAPI_WriteReport (buf, false);

	for (UIndex i = 0; i < storyItems.GetSize (); i++) {
		API_NavigatorItem fi = {};
		if (ACAPI_Navigator_GetNavigatorItem (&storyItems[i].guid, &fi) == NoError) {
			GS::UniString navName (fi.uName);
			std::snprintf (buf, sizeof buf,
				"[ToLayout]   item[%u] uName='%s'",
				(unsigned)i, navName.ToCStr (CC_UTF8).Get ());
			ACAPI_WriteReport (buf, false);
		}
	}
	return false;
}

// -----------------------------------------------------------------------------
// Общая обёртка: искать story-элемент навигатора для активного этажа плана.
// Использует активный этаж из вида, а не из выделенных элементов.
// -----------------------------------------------------------------------------
static bool FindStoryForSelection (const GS::Array<API_NavigatorItem>& items,
	API_DatabaseTypeID dbTypeID, API_Guid& outGuid)
{
	if (dbTypeID != APIWind_FloorPlanID)
		return false;
	if (items.GetSize () <= 1)
		return false;
	short targetFloor = 0;
	// Сначала пытаемся получить активный этаж из плана
	if (GetActiveFloorInd (targetFloor)) {
		char b[128];
		std::snprintf (b, sizeof b, "[ToLayout] active floor floorInd = %d, items = %u",
			(int)targetFloor, (unsigned)items.GetSize ());
		ACAPI_WriteReport (b, false);
		return FindStoryNavItemForFloor (items, targetFloor, outGuid);
	}
	// Fallback: пробуем получить этаж из выделенных элементов
	if (GetSelectionFloorInd (targetFloor)) {
		char b[128];
		std::snprintf (b, sizeof b, "[ToLayout] selection floorInd = %d, items = %u",
			(int)targetFloor, (unsigned)items.GetSize ());
		ACAPI_WriteReport (b, false);
		return FindStoryNavItemForFloor (items, targetFloor, outGuid);
	}
	ACAPI_WriteReport ("[ToLayout] Failed to get floor from active view or selection", false);
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
	// Сначала ищем точное совпадение по databaseUnId (текущий активный вид)
	for (UIndex i = 0; i < items.GetSize (); i++) {
		if (items[i].db.databaseUnId == currentDb.databaseUnId) {
			outGuid = items[i].guid;
			return true;
		}
	}
	// Если не нашли по databaseUnId, пытаемся найти по этажу выделения
	if (FindStoryForSelection (items, currentDb.typeID, outGuid))
		return true;
	// В крайнем случае берём первый элемент
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
	// Сначала ищем точное совпадение по databaseUnId (текущий активный вид)
	for (UIndex i = 0; i < items.GetSize (); i++) {
		if (items[i].db.databaseUnId == currentDb.databaseUnId) {
			outGuid = items[i].guid;
			return true;
		}
	}
	// Если не нашли по databaseUnId, пытаемся найти по этажу выделения
	if (FindStoryForSelection (items, currentDb.typeID, outGuid))
		return true;
	// В крайнем случае берём первый элемент
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
