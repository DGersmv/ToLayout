#ifndef LAYOUTHELPER_HPP
#define LAYOUTHELPER_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "GSRoot.hpp"

namespace LayoutHelper {

	struct LayoutItem {
		API_DatabaseUnId databaseUnId;
		GS::UniString name;
	};

	/** Шаблон макета (мастер-макет из папки Основные: Титульный лист, Обложка и т.п.) */
	struct MasterLayoutItem {
		API_DatabaseUnId databaseUnId;
		GS::UniString name;
	};

	/** Список макетов проекта (Layout databases) */
	GS::Array<LayoutItem> GetLayoutList ();

	/** Список мастер-макетов (шаблоны: Титульный лист, Обложка) */
	GS::Array<MasterLayoutItem> GetMasterLayoutList ();

	/** Текущий масштаб вида (100 = 1:100). Возвращает 100 при ошибке. */
	double GetCurrentDrawingScale ();

	/** Параметры размещения из палитры */
	struct PlaceParams {
		/** Точка привязки вида на макете: углы + центр */
		enum class Anchor {
			LeftBottom = 0,  // LB
			LeftTop,         // LT
			RightTop,        // RT
			RightBottom,     // RB
			Middle           // MM (центр)
		};
		Int32 masterLayoutIndex;    // индекс шаблона (0-based), -1 = использовать существующий layoutIndex
		Int32 layoutIndex;          // индекс существующего макета (если masterLayoutIndex < 0)
		GS::UniString layoutName;   // имя для нового макета
		double scale;               // 100 = 1:100
		GS::UniString drawingName;  // имя вида (заголовок Drawing)
		Anchor anchorPosition = Anchor::LeftBottom;
	};

	/**
	 * Разместить выделение в макет с заданными параметрами.
	 * Если masterLayoutIndex >= 0: создаёт новый макет из шаблона с именем layoutName.
	 * Иначе: использует существующий макет по layoutIndex.
	 */
	bool PlaceSelectionOnLayoutWithParams (const PlaceParams& params);

	/** Устаревший вызов — для совместимости */
	bool PlaceSelectionOnLayoutByIndex (Int32 layoutIndex);

} // namespace LayoutHelper

#endif // LAYOUTHELPER_HPP
