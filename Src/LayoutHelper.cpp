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
#include <cmath>

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
// GetPlaceableViews — виды из View Map для палитры «Организация чертежей»
// -----------------------------------------------------------------------------
static const char* ViewTypeDisplayName (API_NavigatorItemTypeID itemType)
{
	switch (itemType) {
		case API_StoryNavItem:              return "План";
		case API_SectionNavItem:            return "Разрез";
		case API_ElevationNavItem:          return "Фасад";
		case API_InteriorElevationNavItem:  return "Внутренний фасад";
		case API_DetailDrawingNavItem:      return "Деталь";
		case API_WorksheetDrawingNavItem:   return "Рабочий лист";
		case API_DocumentFrom3DNavItem:     return "Документ из 3D";
		default:                            return "Вид";
	}
}

GS::Array<PlaceableViewItem> GetPlaceableViews ()
{
	GS::Array<PlaceableViewItem> result;
	const API_NavigatorItemTypeID types[] = {
		API_StoryNavItem,
		API_SectionNavItem,
		API_ElevationNavItem,
		API_InteriorElevationNavItem,
		API_DetailDrawingNavItem,
		API_WorksheetDrawingNavItem,
		API_DocumentFrom3DNavItem
	};
	for (API_NavigatorItemTypeID itemType : types) {
		API_NavigatorItem navItem = {};
		BNZeroMemory (&navItem, sizeof (navItem));
		navItem.mapId = API_PublicViewMap;
		navItem.itemType = itemType;
		GS::Array<API_NavigatorItem> items;
		if (ACAPI_Navigator_SearchNavigatorItem (&navItem, &items) != NoError)
			continue;
		for (UIndex i = 0; i < items.GetSize (); i++) {
			PlaceableViewItem pvi;
			pvi.viewGuid = items[i].guid;
			pvi.name = GS::UniString (items[i].uName);
			if (pvi.name.IsEmpty ())
				pvi.name = GS::UniString ("Без имени");
			pvi.typeName = GS::UniString (ViewTypeDisplayName (itemType));
			result.Push (pvi);
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
// Берём максимальный floorInd среди выделенных — для объектов на нескольких этажах
// это даёт верхний этаж, а не «базовый» (часто 1-й), чтобы вид соответствовал активному плану.
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
	short maxFloor = SHRT_MIN;
	bool any = false;
	for (UIndex i = 0; i < selNeigs.GetSize (); i++) {
		API_Element elem = {};
		elem.header.guid = selNeigs[i].guid;
		if (ACAPI_Element_Get (&elem) == NoError) {
			if (elem.header.floorInd > maxFloor)
				maxFloor = elem.header.floorInd;
			any = true;
		}
	}
	if (any) {
		outFloorInd = maxFloor;
		return true;
	}
	return false;
}

// -----------------------------------------------------------------------------
// Получить floorInd активного этажа (текущий открытый план).
// API: API_StoryInfo.actStory — "Actual (currently visible in 2D) story index".
// -----------------------------------------------------------------------------
static bool GetActiveFloorInd (short& outFloorInd)
{
	API_DatabaseInfo currentDb = {};
	if (ACAPI_Database_GetCurrentDatabase (&currentDb) != NoError)
		return false;
	if (currentDb.typeID != APIWind_FloorPlanID)
		return false;
	
	API_StoryInfo storyInfo = {};
	// APIElemMask_FromFloorplan — в контексте текущего плана этажа (actStory для активного окна)
	if (ACAPI_ProjectSetting_GetStorySettings (&storyInfo, APIElemMask_FromFloorplan) != NoError)
		return false;
	if (storyInfo.data == nullptr)
		return false;
	
	short first = storyInfo.firstStory;
	short last  = storyInfo.lastStory;
	// actStory = активный (сейчас видимый в 2D) этаж — из API
	if (storyInfo.actStory >= first && storyInfo.actStory <= last) {
		outFloorInd = storyInfo.actStory;
		BMKillHandle ((GSHandle*) &storyInfo.data);
		return true;
	}
	
	BMKillHandle ((GSHandle*) &storyInfo.data);
	return false;
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
// Собрать все элементы типа API_StoryNavItem из узла и (рекурсивно) из его детей.
static void CollectStoryItems (const API_NavigatorItem& node, GS::Array<API_NavigatorItem>& outStories)
{
	if (node.itemType == API_StoryNavItem) {
		outStories.Push (node);
		return;
	}
	GS::Array<API_NavigatorItem> children;
	if (ACAPI_Navigator_GetNavigatorChildrenItems (const_cast<API_NavigatorItem*> (&node), &children) != NoError)
		return;
	for (UIndex c = 0; c < children.GetSize (); c++)
		CollectStoryItems (children[c], outStories);
}

static bool FindStoryNavItemForFloor (const GS::Array<API_NavigatorItem>& storyItems,
	short targetFloorInd, API_Guid& outGuid)
{
	// ---------- Стратегия C первой: Project Map — единственный надёжный источник этажей (1-й, 2-й, 3-й) ----------
	// Не зависим от переданного storyItems (из View Map может быть "Новый вид"/"1-й этаж", из Project Map — иногда 1 элемент).
	// View Map даёт "Новый вид"/"1-й этаж" и т.п., без 2-го этажа; здесь берём реальный список этажей по порядку.
	{
		API_NavigatorSet projSet = {};
		projSet.mapId = API_ProjectMap;
		if (ACAPI_Navigator_GetNavigatorSet (&projSet) == NoError) {
			API_NavigatorItem rootItem = {};
			rootItem.guid = projSet.rootGuid;
			rootItem.mapId = API_ProjectMap;
			GS::Array<API_NavigatorItem> rootChildren;
			if (ACAPI_Navigator_GetNavigatorChildrenItems (&rootItem, &rootChildren) == NoError) {
				GS::Array<API_NavigatorItem> treeStories;
				for (UIndex c = 0; c < rootChildren.GetSize (); c++) {
					if (rootChildren[c].itemType == API_StoryNavItem)
						treeStories.Push (rootChildren[c]);
				}
				// Если у корня нет этажей — возможно они на уровень глубже (папка "Этажи" и т.п.)
				if (treeStories.IsEmpty ()) {
					for (UIndex c = 0; c < rootChildren.GetSize (); c++)
						CollectStoryItems (rootChildren[c], treeStories);
				}
				// Выбираем этаж по floorNum, а не по индексу в дереве — порядок узлов может не совпадать с 0,1,2
				for (UIndex k = 0; k < treeStories.GetSize (); k++) {
					if (treeStories[k].floorNum == targetFloorInd) {
						outGuid = treeStories[k].guid;
						return true;
					}
				}
				// Запас: сопоставление по имени этажа из Story Settings (если floorNum не заполнен в дереве)
				API_StoryInfo siName = {};
				if (ACAPI_ProjectSetting_GetStorySettings (&siName) == NoError && siName.data != nullptr) {
					API_StoryType* stData = reinterpret_cast<API_StoryType*> (*siName.data);
					short first = siName.firstStory, last = siName.lastStory;
					short off = targetFloorInd - first;
					if (off >= 0 && off <= last - first) {
						GS::UniString targetName (stData[off].uName);
						BMKillHandle ((GSHandle*) &siName.data);
						for (UIndex k = 0; k < treeStories.GetSize (); k++) {
							GS::UniString itemName (treeStories[k].uName);
							if (itemName == targetName || itemName.EndsWith (targetName)) {
								outGuid = treeStories[k].guid;
								return true;
							}
						}
					} else {
						if (siName.data != nullptr) BMKillHandle ((GSHandle*) &siName.data);
					}
				} else {
					if (siName.data != nullptr) BMKillHandle ((GSHandle*) &siName.data);
				}
				// Последний запас: индекс по firstStory
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
					outGuid = treeStories[static_cast<UIndex> (treeOff)].guid;
					return true;
				}
			}
		}
	}

	// ---------- Стратегия A: story settings + имя (если передан список из View Map — редко совпадает) ----------
	API_StoryInfo storyInfo = {};
	bool haveStoryInfo = (ACAPI_ProjectSetting_GetStorySettings (&storyInfo) == NoError);
	if (haveStoryInfo && storyInfo.data != nullptr) {
		API_StoryType* stData = reinterpret_cast<API_StoryType*> (*storyInfo.data);
		short first  = storyInfo.firstStory;
		short last   = storyInfo.lastStory;

		GS::UniString targetStoryName;
		short off = targetFloorInd - first;
		if (off >= 0 && off <= last - first)
			targetStoryName = GS::UniString (stData[off].uName);

		BMKillHandle ((GSHandle*) &storyInfo.data);

		if (!targetStoryName.IsEmpty ()) {
			for (UIndex i = 0; i < storyItems.GetSize (); i++) {
				API_NavigatorItem fi = {};
				if (ACAPI_Navigator_GetNavigatorItem (&storyItems[i].guid, &fi) == NoError) {
					GS::UniString navName (fi.uName);
					if (navName == targetStoryName) {
						outGuid = storyItems[i].guid;
						return true;
					}
				}
			}
			for (UIndex i = 0; i < storyItems.GetSize (); i++) {
				API_NavigatorItem fi = {};
				if (ACAPI_Navigator_GetNavigatorItem (&storyItems[i].guid, &fi) == NoError) {
					GS::UniString navName (fi.uName);
					if (navName.Contains (targetStoryName)) {
						outGuid = storyItems[i].guid;
						return true;
					}
				}
			}
		}
	} else {
		if (storyInfo.data != nullptr) BMKillHandle ((GSHandle*) &storyInfo.data);
	}

	// ---------- Стратегия B: по floorNum (актуально только если storyItems из Project Map) ----------
	for (UIndex i = 0; i < storyItems.GetSize (); i++) {
		if (storyItems[i].floorNum == targetFloorInd) {
			outGuid = storyItems[i].guid;
			return true;
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
	short targetFloor = 0;
	// Только активный этаж из API (actStory), не из выделения
	if (!GetActiveFloorInd (targetFloor))
		return false;
	return FindStoryNavItemForFloor (items, targetFloor, outGuid);
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
		case APIWind_DocumentFrom3DID: itemType = API_DocumentFrom3DNavItem; break;
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
		case APIWind_DocumentFrom3DID: return false;  // не клонируем Document from 3D из Project Map
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
	// Если не нашли по databaseUnId, пытаемся найти по этажу выделения/активному этажу (Strategy C по Project Map)
	if (FindStoryForSelection (items, currentDb.typeID, outGuid))
		return true;
	// Для плана этажа не подставляем items[0] — это может быть 1-й этаж при открытом 2-м/3-м
	if (currentDb.typeID == APIWind_FloorPlanID)
		return false;
	if (!items.IsEmpty ())
		outGuid = items[0].guid;
	return !items.IsEmpty ();
}

// -----------------------------------------------------------------------------
// Получить bounding box текущей рамки (Marquee). Работает на плане/разрезе при наличии рамки.
// Для фасада, 3D и т.д. при отсутствии marquee возвращает false — тогда используется fallback.
// -----------------------------------------------------------------------------
static bool GetMarqueeBounds (API_Box& outBox)
{
	API_SelectionInfo selectionInfo = {};
	GS::Array<API_Neig> selNeigs;
	if (ACAPI_Selection_Get (&selectionInfo, &selNeigs, true) != NoError)
		return false;
	const bool isMarquee = (selectionInfo.typeID == API_MarqueePoly ||
		selectionInfo.typeID == API_MarqueeHorBox ||
		selectionInfo.typeID == API_MarqueeRotBox);
	if (!isMarquee) {
		BMKillHandle ((GSHandle*)&selectionInfo.marquee.coords);
		return false;
	}
	const API_Region& marquee = selectionInfo.marquee;
	bool ok = false;
	if (marquee.coords != nullptr && marquee.nCoords > 0) {
		API_Coord* coords = *marquee.coords;
		double xMin = coords[0].x, yMin = coords[0].y, xMax = coords[0].x, yMax = coords[0].y;
		for (Int32 i = 1; i < marquee.nCoords; i++) {
			if (coords[i].x < xMin) xMin = coords[i].x;
			if (coords[i].y < yMin) yMin = coords[i].y;
			if (coords[i].x > xMax) xMax = coords[i].x;
			if (coords[i].y > yMax) yMax = coords[i].y;
		}
		outBox.xMin = xMin;
		outBox.yMin = yMin;
		outBox.xMax = xMax;
		outBox.yMax = yMax;
		ok = (xMax > xMin + 1e-10 && yMax > yMin + 1e-10);
	} else {
		const double cx = 0.5 * (marquee.box.xMin + marquee.box.xMax);
		const double cy = 0.5 * (marquee.box.yMin + marquee.box.yMax);
		const double degToRad = 3.14159265358979323846 / 180.0;
		const double a = marquee.boxRotAngle * degToRad;
		const double cosA = cos (a);
		const double sinA = sin (a);
		double xMin = 1e30, yMin = 1e30, xMax = -1e30, yMax = -1e30;
		for (int i = 0; i < 4; i++) {
			double x = (i == 0 || i == 3) ? marquee.box.xMin : marquee.box.xMax;
			double y = (i < 2) ? marquee.box.yMin : marquee.box.yMax;
			double dx = x - cx, dy = y - cy;
			double rx = cx + dx * cosA - dy * sinA;
			double ry = cy + dx * sinA + dy * cosA;
			if (rx < xMin) xMin = rx;
			if (ry < yMin) yMin = ry;
			if (rx > xMax) xMax = rx;
			if (ry > yMax) yMax = ry;
		}
		outBox.xMin = xMin;
		outBox.yMin = yMin;
		outBox.xMax = xMax;
		outBox.yMax = yMax;
		ok = (xMax > xMin + 1e-10 && yMax > yMin + 1e-10);
	}
	BMKillHandle ((GSHandle*)&selectionInfo.marquee.coords);
	return ok;
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
	if (selectedLayers.IsEmpty ()) {
		if (zoomBox != nullptr) {
			API_NavigatorItem clonedNavItem = {};
			clonedNavItem.guid = clonedGuid;
			clonedNavItem.mapId = API_PublicViewMap;
			API_NavigatorView navView = {};
			if (ACAPI_Navigator_GetNavigatorView (&clonedNavItem, &navView) == NoError) {
				if (navView.layerStats != nullptr) {
					delete navView.layerStats;
					navView.layerStats = nullptr;
				}
				navView.zoom = *zoomBox;
				navView.saveZoom = true;
				navView.drawingScale = drawingScale;
				navView.saveDScale = true;
				ACAPI_Navigator_ChangeNavigatorView (&clonedNavItem, &navView);
			}
		}
		return clonedGuid;
	}
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
// Вычислить прямоугольник области сетки в мм (left, bottom, width, height от левого нижнего угла листа).
// Макет: layoutInfo; сетка gridRows×gridCols, зазор gridGapMm; область startRow, startCol, spanRows, spanCols.
// -----------------------------------------------------------------------------
static void GetGridRegionRect (const API_LayoutInfo& layoutInfo,
	Int32 gridRows, Int32 gridCols, double gridGapMm,
	Int32 startRow, Int32 startCol, Int32 spanRows, Int32 spanCols,
	double& outLeftMm, double& outBottomMm, double& outWidthMm, double& outHeightMm)
{
	const double availW = layoutInfo.sizeX - layoutInfo.leftMargin - layoutInfo.rightMargin;
	const double availH = layoutInfo.sizeY - layoutInfo.topMargin - layoutInfo.bottomMargin;
	if (gridCols < 1) gridCols = 1;
	if (gridRows < 1) gridRows = 1;
	const double gap = gridGapMm > 0 ? gridGapMm : 0;
	const double cellW = (availW - (gridCols - 1) * gap) / gridCols;
	const double cellH = (availH - (gridRows - 1) * gap) / gridRows;
	outLeftMm   = layoutInfo.leftMargin + startCol * (cellW + gap);
	outBottomMm = layoutInfo.bottomMargin + startRow * (cellH + gap);
	outWidthMm  = spanCols * cellW + (spanCols > 1 ? (spanCols - 1) * gap : 0);
	outHeightMm = spanRows * cellH + (spanRows > 1 ? (spanRows - 1) * gap : 0);
}

// -----------------------------------------------------------------------------
// Вычислить позицию вида на макете по точке привязки (как было до экспериментов с pos).
// layoutInfo — параметры макета (sizeX, sizeY, margins в мм); pos в метрах.
// outAnchor и outPos — куда на макете привязать выбранный угол/центр чертежа.
// -----------------------------------------------------------------------------
static void GetDrawingPositionForAnchor (const API_LayoutInfo& layoutInfo, PlaceParams::Anchor anchor, API_AnchorID& outAnchor, API_Coord& outPos)
{
	const double MM_TO_M = 1.0 / 1000.0;
	const double left   = layoutInfo.leftMargin * MM_TO_M;
	const double right  = (layoutInfo.sizeX - layoutInfo.rightMargin) * MM_TO_M;
	const double top    = (layoutInfo.sizeY - layoutInfo.topMargin) * MM_TO_M;
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
	API_DatabaseInfo currentDb = {};
	if (ACAPI_Database_GetCurrentDatabase (&currentDb) != NoError)
		return false;
	if (currentDb.typeID == APIWind_3DModelID) {
		ACAPI_WriteReport ("Чтобы разместить вид из 3D, создайте Документ из 3D (меню Archicad), откройте его и нажмите Разместить в макете.", true);
		return false;
	}
	const bool placeByGuid = (params.placeViewGuid != APINULLGuid);
	if (!placeByGuid) {
		API_Guid dummyGuid = {};
		if (!GetCurrentViewNavigatorItem (dummyGuid)) {
			ACAPI_WriteReport ("Откройте план, разрез, фасад или Документ из 3D перед размещением.", true);
			return false;
		}
	}
	// Параметры макета (размер листа, поля) — всегда для выбранного макета chosenLayoutId
	API_LayoutInfo layoutInfo = {};
	BNZeroMemory (&layoutInfo, sizeof (layoutInfo));
	API_DatabaseUnId layoutDbId = chosenLayoutId;
	if (ACAPI_Navigator_GetLayoutSets (&layoutInfo, &layoutDbId) != NoError) {
		if (layoutInfo.customData != nullptr) {
			delete layoutInfo.customData;
			layoutInfo.customData = nullptr;
		}
	}
	if (params.useGridRegion && (layoutInfo.sizeX < 1.0 || layoutInfo.sizeY < 1.0)) {
		ACAPI_WriteReport ("LayoutHelper: не удалось получить размер выбранного макета (GetLayoutSets).", true);
		return false;
	}
	// При размещении по GUID вида — сразу получаем zoom и масштаб из этого вида
	API_Box zoomBox = {};
	bool hasZoomBox = false;
	if (placeByGuid) {
		API_NavigatorItem navItem = {};
		navItem.guid = params.placeViewGuid;
		navItem.mapId = API_PublicViewMap;
		API_NavigatorView navView = {};
		if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) == NoError) {
			zoomBox = navView.zoom;
			hasZoomBox = (navView.zoom.xMax > navView.zoom.xMin + 1e-6 && navView.zoom.yMax > navView.zoom.yMin + 1e-6);
			if (navView.layerStats != nullptr) {
				delete navView.layerStats;
				navView.layerStats = nullptr;
			}
		}
	}
	// Рамка контента вида: при «Выбрать по рамке» — из Marquee; иначе выделение+отступ или zoom текущего вида
	if (!placeByGuid && params.useMarqueeAsBoundary)
		hasZoomBox = GetMarqueeBounds (zoomBox);
	if (!placeByGuid && !hasZoomBox)
		hasZoomBox = GetSelectionBoundsWithMargin (zoomBox);
	if (!placeByGuid && !hasZoomBox) {
		API_Guid currentViewGuid = {};
		if (GetCurrentViewNavigatorItem (currentViewGuid)) {
			API_NavigatorItem navItem = {};
			navItem.guid = currentViewGuid;
			navItem.mapId = API_PublicViewMap;
			API_NavigatorView navView = {};
			if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) == NoError) {
				zoomBox = navView.zoom;
				hasZoomBox = (navView.zoom.xMax > navView.zoom.xMin + 1e-6 && navView.zoom.yMax > navView.zoom.yMin + 1e-6);
				if (navView.layerStats != nullptr) {
					delete navView.layerStats;
					navView.layerStats = nullptr;
				}
			}
		}
	}
	double currentScale = GetCurrentDrawingScale ();
	double viewScaleBeforeFit = currentScale;  // масштаб вида до подгонки (для восстановления при placeByGuid)
	if (placeByGuid) {
		API_NavigatorItem navItem = {};
		navItem.guid = params.placeViewGuid;
		navItem.mapId = API_PublicViewMap;
		API_NavigatorView navView = {};
		if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) == NoError) {
			if (navView.saveDScale)
				currentScale = static_cast<double> (navView.drawingScale);
			viewScaleBeforeFit = currentScale;
			if (navView.layerStats != nullptr) {
				delete navView.layerStats;
				navView.layerStats = nullptr;
			}
		}
	}
	double regionWmm = 0, regionHmm = 0;
	double regionLeftMm = 0, regionBottomMm = 0;
	if (params.useGridRegion && params.gridRows > 0 && params.gridCols > 0) {
		GetGridRegionRect (layoutInfo,
			params.gridRows, params.gridCols, params.gridGapMm,
			params.regionStartRow, params.regionStartCol, params.regionSpanRows, params.regionSpanCols,
			regionLeftMm, regionBottomMm, regionWmm, regionHmm);
	}
	const bool fitToRegion = params.useGridRegion && regionWmm > 1.0 && regionHmm > 1.0;
	// При размещении в сектор — тот же принцип, что «Подогнать масштаб» в палитре «Расположить в макете»: подгонка по области (здесь область = сектор), размещение по точке (LB сектора, якорь LB).
	if ((params.fitScaleToLayout || fitToRegion) && hasZoomBox) {
		double extentW = zoomBox.xMax - zoomBox.xMin;
		double extentH = zoomBox.yMax - zoomBox.yMin;
		if (extentW > 1e-6 && extentH > 1e-6) {
			double availWmm = layoutInfo.sizeX - layoutInfo.leftMargin - layoutInfo.rightMargin;
			double availHmm = layoutInfo.sizeY - layoutInfo.topMargin - layoutInfo.bottomMargin;
			if (fitToRegion) {
				availWmm = regionWmm;
				availHmm = regionHmm;
			}
			if (availWmm > 1.0 && availHmm > 1.0) {
				double scaleW = extentW * 1000.0 / availWmm;
				double scaleH = extentH * 1000.0 / availHmm;
				// При размещении в сектор сетки — вписать вид целиком (min); иначе — заполнить область (max)
				double fitScale = fitToRegion
					? ((scaleW < scaleH) ? scaleW : scaleH)
					: ((scaleW > scaleH) ? scaleW : scaleH);
				if (fitScale < 1.0) fitScale = 1.0;
				if (fitScale > 10000.0) fitScale = 10000.0;
				currentScale = fitScale;
			}
		}
	}
	GS::UniString drawingName = params.drawingName.IsEmpty () ? GS::UniString ("Новый вид") : params.drawingName;
	API_Guid viewGuidForDrawing = APINULLGuid;
	if (placeByGuid) {
		viewGuidForDrawing = params.placeViewGuid;
		if (drawingName == GS::UniString ("Новый вид")) {
			API_NavigatorItem navItem = {};
			navItem.guid = params.placeViewGuid;
			navItem.mapId = API_PublicViewMap;
			if (ACAPI_Navigator_GetNavigatorItem (&params.placeViewGuid, &navItem) == NoError)
				drawingName = GS::UniString (navItem.uName);
		}
	} else {
		// В режиме «Выбрать по рамке» — вид как есть, без фильтра слоёв; обрезка по рамке через клон (не трогаем текущий вид)
		GS::HashSet<API_AttributeIndex> selectedLayers;
		if (!params.useMarqueeAsBoundary)
			selectedLayers = GetLayersOfSelection ();
		if (!selectedLayers.IsEmpty ()) {
			viewGuidForDrawing = CloneViewToViewMapWithLayerFilter (
				selectedLayers, static_cast<Int32> (currentScale), drawingName,
				hasZoomBox ? &zoomBox : nullptr);
		} else if (params.useMarqueeAsBoundary && hasZoomBox) {
			viewGuidForDrawing = CloneViewToViewMapWithLayerFilter (
				selectedLayers, static_cast<Int32> (currentScale), drawingName, &zoomBox);
		}
		if (viewGuidForDrawing == APINULLGuid) {
			GetCurrentViewNavigatorItem (viewGuidForDrawing);
			if (viewGuidForDrawing == APINULLGuid) {
				ACAPI_WriteReport ("Не удалось получить вид для размещения.", true);
				return false;
			}
			API_NavigatorItem navItem = {};
			navItem.guid = viewGuidForDrawing;
			navItem.mapId = API_PublicViewMap;
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
	}
	// Если extent нулевой (например при RT/RB после смены вида) — берём zoom из размещаемого вида
	double extentW = zoomBox.xMax - zoomBox.xMin;
	double extentH = zoomBox.yMax - zoomBox.yMin;
	if (extentW < 1e-6 || extentH < 1e-6) {
		API_NavigatorItem navItem = {};
		navItem.guid = viewGuidForDrawing;
		navItem.mapId = API_PublicViewMap;
		API_NavigatorView navView = {};
		if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) == NoError) {
			zoomBox = navView.zoom;
			extentW = zoomBox.xMax - zoomBox.xMin;
			extentH = zoomBox.yMax - zoomBox.yMin;
			if (navView.layerStats != nullptr) {
				delete navView.layerStats;
				navView.layerStats = nullptr;
			}
		}
	}
	double sizeMmW = (currentScale > 1e-6) ? (extentW * 1000.0 / currentScale) : 0.0;
	double sizeMmH = (currentScale > 1e-6) ? (extentH * 1000.0 / currentScale) : 0.0;
	const double sizeW_m = sizeMmW * 0.001;
	const double sizeH_m = sizeMmH * 0.001;

	API_AnchorID anchorId = APIAnc_LB;
	API_Coord drawPos = { 0.0, 0.0 };
	if (params.useGridRegion && params.gridRows > 0 && params.gridCols > 0 && regionWmm > 0 && regionHmm > 0) {
		// Позиция = левый нижний угол сектора (тот же принцип, что якорь на листе в «Расположить в макете»)
		drawPos.x = regionLeftMm * 0.001;
		drawPos.y = regionBottomMm * 0.001;
		anchorId = APIAnc_LB;
	} else {
		GetDrawingPositionForAnchor (layoutInfo, params.anchorPosition, anchorId, drawPos);
		switch (params.anchorPosition) {
			case PlaceParams::Anchor::LeftTop:
				drawPos.y -= sizeH_m;
				anchorId = APIAnc_LB;
				break;
			case PlaceParams::Anchor::Middle:
				drawPos.x -= sizeW_m * 0.5;
				drawPos.y -= sizeH_m * 0.5;
				anchorId = APIAnc_LB;
				break;
			case PlaceParams::Anchor::RightTop:
				drawPos.x -= sizeW_m;
				drawPos.y -= sizeH_m;
				anchorId = APIAnc_LB;
				break;
			case PlaceParams::Anchor::RightBottom:
				drawPos.x -= sizeW_m;
				anchorId = APIAnc_LB;
				break;
			default:
				break;
		}
	}
	if (layoutInfo.customData != nullptr) {
		delete layoutInfo.customData;
		layoutInfo.customData = nullptr;
	}

	// Система координат макета: начало в левом нижнем углу листа, pos в метрах
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
	element.drawing.useOwnOrigoAsAnchor = false;
	element.drawing.pos = drawPos;
	element.drawing.isCutWithFrame = true;

	// При размещении по GUID размер чертежа на макете берётся из масштаба вида. Временно выставляем масштаб вида в currentScale, чтобы Drawing получил нужный размер; после размещения восстанавливаем.
	const bool needTempViewScale = placeByGuid && (currentScale > 0) && (fabs (currentScale - viewScaleBeforeFit) > 0.01);
	if (needTempViewScale) {
		API_NavigatorItem navItem = {};
		navItem.guid = params.placeViewGuid;
		navItem.mapId = API_PublicViewMap;
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

	err = ACAPI_CallUndoableCommand ("Place view on layout", [&] () -> GSErrCode {
		API_DatabaseInfo layoutDb = {};
		layoutDb.databaseUnId = chosenLayoutId;
		layoutDb.typeID = APIWind_LayoutID;
		GSErrCode e = ACAPI_Database_ChangeCurrentDatabase (&layoutDb);
		if (e != NoError)
			return e;
		API_ElementMemo memo = {};
		GSErrCode createErr = ACAPI_Element_Create (&element, &memo);
		if (createErr != NoError) {
			ACAPI_Database_ChangeCurrentDatabase (&currentDb);
			return createErr;
		}
		ACAPI_Database_ChangeCurrentDatabase (&currentDb);
		return createErr;
	});

	if (needTempViewScale) {
		API_NavigatorItem navItem = {};
		navItem.guid = params.placeViewGuid;
		navItem.mapId = API_PublicViewMap;
		API_NavigatorView navView = {};
		if (ACAPI_Navigator_GetNavigatorView (&navItem, &navView) == NoError) {
			navView.drawingScale = static_cast<Int32> (viewScaleBeforeFit);
			navView.saveDScale = true;
			ACAPI_Navigator_ChangeNavigatorView (&navItem, &navView);
			if (navView.layerStats != nullptr) {
				delete navView.layerStats;
				navView.layerStats = nullptr;
			}
		}
	}

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
