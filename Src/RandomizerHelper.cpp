#include "RandomizerHelper.hpp"
#include "RotateHelper.hpp"
#include <random>

#ifdef _DEBUG
#include "APIdefs_Properties.h"
#endif

namespace RandomizerHelper {

	// Вспомогательная функция для генерации случайного значения в диапазоне [min, max]
	static double RandomValue(double min, double max)
	{
		static std::random_device rd;
		static std::mt19937 gen(rd());
		std::uniform_real_distribution<double> dist(min, max);
		return dist(gen);
	}

	// Универсальная функция поиска параметра по имени (независимо от локализации)
	// Ищет параметр по короткому имени (A, B, C) или по ключевым словам на разных языках
	static bool FindParameterByName(API_AddParType* params, GS::Int32 count, const char* shortName,
		const GS::Array<GS::UniString>& keywords, API_AddParType*& outParam)
	{
		if (params == nullptr || count == 0) return false;

		for (GS::Int32 i = 0; i < count; ++i) {
			API_AddParType& p = params[i];
			if (p.name != nullptr) {
				GS::UniString paramName(p.name);
				
				// Сначала проверяем точное совпадение с коротким именем (A, B, C)
				if (paramName == shortName) {
					outParam = &p;
					return true;
				}

				// Затем проверяем ключевые слова (регистронезависимо через Contains)
				// Contains в GS::UniString работает регистронезависимо для Unicode
				for (const GS::UniString& keyword : keywords) {
					if (paramName.Contains(keyword)) {
						outParam = &p;
						return true;
					}
				}
			}
		}
		return false;
	}

	// Рандомизировать ширину (A) выбранных элементов
	bool RandomizeWidth(double percent)
	{
		if (percent < 0.0 || percent > 100.0) {
			return false;
		}

		API_SelectionInfo selInfo = {};
		GS::Array<API_Neig> selNeigs;
		ACAPI_Selection_Get(&selInfo, &selNeigs, false, false);
		BMKillHandle((GSHandle*)&selInfo.marquee.coords);

		if (selNeigs.IsEmpty()) return false;

		const double factor = percent / 100.0;

		GSErrCode err = ACAPI_CallUndoableCommand("Randomize Width", [&]() -> GSErrCode {
			for (const API_Neig& n : selNeigs) {
				API_Element element = {};
				element.header.guid = n.guid;
				if (ACAPI_Element_Get(&element) != NoError) continue;

				API_Element mask; ACAPI_ELEMENT_MASK_CLEAR(mask);
				API_ElementMemo memo{};
				bool hasMemo = false;
				bool maskSet = false;
				bool needsMemo = false;
				bool changedAddPars = false;
				bool changedBeamSegments = false;
				bool changedColumnSegments = false;

				switch (element.header.type.typeID) {
				case API_BeamID: {
					// Ширина балки — nominalWidth в сегментах
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_BeamSegment) == NoError);
					if (hasMemo && memo.beamSegments != nullptr && element.beam.nSegments > 0) {
						for (USize idx = 0; idx < element.beam.nSegments; ++idx) {
							double currentWidth = memo.beamSegments[idx].assemblySegmentData.nominalWidth;
							if (currentWidth > 1e-6) {
								double minVal = currentWidth * (1.0 - factor);
								double maxVal = currentWidth * (1.0 + factor);
								memo.beamSegments[idx].assemblySegmentData.nominalWidth = RandomValue(minVal, maxVal);
								maskSet = true;
								needsMemo = true;
								changedBeamSegments = true;
							}
						}
					}
					break;
				}
				case API_ColumnID: {
					// Ширина колонны — nominalWidth в сегментах
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_ColumnSegment) == NoError);
					if (hasMemo && memo.columnSegments != nullptr && element.column.nSegments > 0) {
						for (USize idx = 0; idx < element.column.nSegments; ++idx) {
							double currentWidth = memo.columnSegments[idx].assemblySegmentData.nominalWidth;
							if (currentWidth > 1e-6) {
								double minVal = currentWidth * (1.0 - factor);
								double maxVal = currentWidth * (1.0 + factor);
								memo.columnSegments[idx].assemblySegmentData.nominalWidth = RandomValue(minVal, maxVal);
								maskSet = true;
								needsMemo = true;
								changedColumnSegments = true;
							}
						}
					}
					break;
				}
				case API_ObjectID: {
					// A (ширина/длина) — GDL-параметр для библиотечных элементов
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_AddPars) == NoError);
					if (hasMemo && memo.params != nullptr) {
						const GS::Int32 bytes = BMGetHandleSize((GSHandle)memo.params);
						const GS::Int32 count = (bytes > 0) ? (bytes / (GS::Int32)sizeof(API_AddParType)) : 0;
						API_AddParType* paramA = nullptr;
						GS::Array<GS::UniString> keywords;
						keywords.Push("Длина (A)");
						keywords.Push("Length (A)");
						keywords.Push("Length");
						keywords.Push("Длина");
						if (FindParameterByName((*memo.params), count, "A", keywords, paramA)) {
							if (paramA->typeID == APIParT_RealNum || paramA->typeID == APIParT_Length) {
								double currentA = paramA->value.real;
								if (currentA > 1e-6) {
									double minVal = currentA * (1.0 - factor);
									double maxVal = currentA * (1.0 + factor);
									paramA->value.real = RandomValue(minVal, maxVal);
									maskSet = true;
									needsMemo = true;
									changedAddPars = true;
								}
							}
						}
					}
					break;
				}
				default:
					break;
				}

				if (!maskSet)
					continue;

				// ---- Применение изменений ----
				if (needsMemo && hasMemo) {
					if (changedAddPars && memo.params != nullptr) {
						API_Guid guid = n.guid;
						(void)ACAPI_Element_ChangeMemo(guid, APIMemoMask_AddPars, &memo);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else if (changedBeamSegments) {
						(void)ACAPI_Element_Change(&element, &mask, &memo, APIMemoMask_BeamSegment, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else if (changedColumnSegments) {
						(void)ACAPI_Element_Change(&element, &mask, &memo, APIMemoMask_ColumnSegment, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else {
						(void)ACAPI_Element_Change(&element, &mask, &memo, 0, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					}
				} else {
					(void)ACAPI_Element_Change(&element, &mask, nullptr, 0, true);
				}
			}
			return NoError;
		});

		return err == NoError;
	}

	// Рандомизировать длину (B) выбранных элементов
	bool RandomizeLength(double percent)
	{
		if (percent < 0.0 || percent > 100.0) {
			return false;
		}

		API_SelectionInfo selInfo = {};
		GS::Array<API_Neig> selNeigs;
		ACAPI_Selection_Get(&selInfo, &selNeigs, false, false);
		BMKillHandle((GSHandle*)&selInfo.marquee.coords);

		if (selNeigs.IsEmpty()) return false;

		const double factor = percent / 100.0;

		GSErrCode err = ACAPI_CallUndoableCommand("Randomize Length", [&]() -> GSErrCode {
			for (const API_Neig& n : selNeigs) {
				API_Element element = {};
				element.header.guid = n.guid;
				if (ACAPI_Element_Get(&element) != NoError) continue;

				API_Element mask; ACAPI_ELEMENT_MASK_CLEAR(mask);
				API_ElementMemo memo{};
				bool hasMemo = false;
				bool maskSet = false;
				bool needsMemo = false;
				bool changedAddPars = false;
				bool changedBeamSegments = false;
				bool changedColumnSegments = false;

				switch (element.header.type.typeID) {
				case API_BeamID: {
					// Высота сечения балки (B) — nominalHeight
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_BeamSegment) == NoError);
					if (hasMemo && memo.beamSegments != nullptr && element.beam.nSegments > 0) {
						for (USize idx = 0; idx < element.beam.nSegments; ++idx) {
							double currentHeight = memo.beamSegments[idx].assemblySegmentData.nominalHeight;
							if (currentHeight > 1e-6) {
								double minVal = currentHeight * (1.0 - factor);
								double maxVal = currentHeight * (1.0 + factor);
								memo.beamSegments[idx].assemblySegmentData.nominalHeight = RandomValue(minVal, maxVal);
								maskSet = true;
								needsMemo = true;
								changedBeamSegments = true;
							}
						}
					}
					break;
				}
				case API_ColumnID: {
					// Высота/ширина сечения колонны (B) — nominalHeight
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_ColumnSegment) == NoError);
					if (hasMemo && memo.columnSegments != nullptr && element.column.nSegments > 0) {
						for (USize idx = 0; idx < element.column.nSegments; ++idx) {
							double currentHeight = memo.columnSegments[idx].assemblySegmentData.nominalHeight;
							if (currentHeight > 1e-6) {
								double minVal = currentHeight * (1.0 - factor);
								double maxVal = currentHeight * (1.0 + factor);
								memo.columnSegments[idx].assemblySegmentData.nominalHeight = RandomValue(minVal, maxVal);
								maskSet = true;
								needsMemo = true;
								changedColumnSegments = true;
							}
						}
					}
					break;
				}
				case API_ObjectID: {
					// B (ширина) — GDL-параметр для библиотечных элементов
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_AddPars) == NoError);
					if (hasMemo && memo.params != nullptr) {
						const GS::Int32 bytes = BMGetHandleSize((GSHandle)memo.params);
						const GS::Int32 count = (bytes > 0) ? (bytes / (GS::Int32)sizeof(API_AddParType)) : 0;
						API_AddParType* paramB = nullptr;
						GS::Array<GS::UniString> keywords;
						keywords.Push("Ширина Объекта (B)");
						keywords.Push("Width (B)");
						keywords.Push("Width");
						keywords.Push("Ширина");
						if (FindParameterByName((*memo.params), count, "B", keywords, paramB)) {
							if (paramB->typeID == APIParT_RealNum || paramB->typeID == APIParT_Length) {
								double currentB = paramB->value.real;
								if (currentB > 1e-6) {
									double minVal = currentB * (1.0 - factor);
									double maxVal = currentB * (1.0 + factor);
									paramB->value.real = RandomValue(minVal, maxVal);
									maskSet = true;
									needsMemo = true;
									changedAddPars = true;
								}
							}
						}
					}
					break;
				}
				default:
					break;
				}

				if (!maskSet)
					continue;

				// ---- Применение изменений ----
				if (needsMemo && hasMemo) {
					if (changedAddPars && memo.params != nullptr) {
						API_Guid guid = n.guid;
						(void)ACAPI_Element_ChangeMemo(guid, APIMemoMask_AddPars, &memo);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else if (changedBeamSegments) {
						(void)ACAPI_Element_Change(&element, &mask, &memo, APIMemoMask_BeamSegment, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else if (changedColumnSegments) {
						(void)ACAPI_Element_Change(&element, &mask, &memo, APIMemoMask_ColumnSegment, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else {
						(void)ACAPI_Element_Change(&element, &mask, &memo, 0, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					}
				} else {
					(void)ACAPI_Element_Change(&element, &mask, nullptr, 0, true);
				}
			}
			return NoError;
		});

		return err == NoError;
	}

	// Рандомизировать высоту (C) выбранных элементов
	bool RandomizeHeight(double percent)
	{
		if (percent < 0.0 || percent > 100.0) {
			return false;
		}

		API_SelectionInfo selInfo = {};
		GS::Array<API_Neig> selNeigs;
		ACAPI_Selection_Get(&selInfo, &selNeigs, false, false);
		BMKillHandle((GSHandle*)&selInfo.marquee.coords);

		if (selNeigs.IsEmpty()) return false;

		const double factor = percent / 100.0;

		GSErrCode err = ACAPI_CallUndoableCommand("Randomize Height", [&]() -> GSErrCode {
			for (const API_Neig& n : selNeigs) {
				API_Element element = {};
				element.header.guid = n.guid;
				if (ACAPI_Element_Get(&element) != NoError) continue;

				API_Element mask; ACAPI_ELEMENT_MASK_CLEAR(mask);
				API_ElementMemo memo{};
				bool hasMemo = false;
				bool maskSet = false;
				bool needsMemo = false;
				bool changedAddPars = false;
				bool changedBeamSegments = false;
				bool changedColumnSegments = false;

				switch (element.header.type.typeID) {
				case API_ObjectID:
				case API_LampID: {
					// Высота для библиотечных элементов и светильников - параметр "zzyzx"
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_AddPars) == NoError);
					if (hasMemo && memo.params != nullptr) {
						const GS::Int32 bytes = BMGetHandleSize((GSHandle)memo.params);
						const GS::Int32 count = (bytes > 0) ? (bytes / (GS::Int32)sizeof(API_AddParType)) : 0;
						
						// Ищем параметр "zzyzx" - стандартный параметр высоты в GDL
						for (GS::Int32 i = 0; i < count; ++i) {
							API_AddParType& p = (*memo.params)[i];
							if (p.name != nullptr) {
								GS::UniString paramName(p.name);
								// Проверяем точное совпадение с "zzyzx" (регистронезависимо)
								if (paramName == "zzyzx" || paramName == "ZZYZX") {
									if (p.typeID == APIParT_RealNum || p.typeID == APIParT_Length) {
										double currentHeight = p.value.real;
										if (currentHeight > 1e-6) {
											double minVal = currentHeight * (1.0 - factor);
											double maxVal = currentHeight * (1.0 + factor);
											p.value.real = RandomValue(minVal, maxVal);
											maskSet = true;
											needsMemo = true;
											changedAddPars = true;
											break;
										}
									}
								}
							}
						}
						
						// Если "zzyzx" не найден, пробуем найти по ключевым словам (C, Height, Высота)
						if (!changedAddPars) {
							API_AddParType* paramC = nullptr;
							GS::Array<GS::UniString> keywords;
							keywords.Push("Высота Объекта");
							keywords.Push("Height");
							keywords.Push("Размер Z");
							keywords.Push("Size Z");
							keywords.Push("C");
							if (FindParameterByName((*memo.params), count, "C", keywords, paramC)) {
								if (paramC->typeID == APIParT_RealNum || paramC->typeID == APIParT_Length) {
									double currentC = paramC->value.real;
									if (currentC > 1e-6) {
										double minVal = currentC * (1.0 - factor);
										double maxVal = currentC * (1.0 + factor);
										paramC->value.real = RandomValue(minVal, maxVal);
										maskSet = true;
										needsMemo = true;
										changedAddPars = true;
									}
								}
							}
						}
					}
					break;
				}
				case API_ColumnID: {
					// Для колонн пробуем найти параметр высоты в GDL-параметрах
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_AddPars) == NoError);
					if (hasMemo && memo.params != nullptr) {
						const GS::Int32 bytes = BMGetHandleSize((GSHandle)memo.params);
						const GS::Int32 count = (bytes > 0) ? (bytes / (GS::Int32)sizeof(API_AddParType)) : 0;
						
						// Ищем "zzyzx" или параметр C/Height
						bool found = false;
						for (GS::Int32 i = 0; i < count && !found; ++i) {
							API_AddParType& p = (*memo.params)[i];
							if (p.name != nullptr) {
								GS::UniString paramName(p.name);
								if (paramName == "zzyzx" || paramName == "ZZYZX") {
									if (p.typeID == APIParT_RealNum || p.typeID == APIParT_Length) {
										double currentHeight = p.value.real;
										if (currentHeight > 1e-6) {
											double minVal = currentHeight * (1.0 - factor);
											double maxVal = currentHeight * (1.0 + factor);
											p.value.real = RandomValue(minVal, maxVal);
											maskSet = true;
											needsMemo = true;
											changedAddPars = true;
											found = true;
										}
									}
								}
							}
						}
						
						// Если не найден "zzyzx", пробуем найти по ключевым словам
						if (!found) {
							API_AddParType* paramC = nullptr;
							GS::Array<GS::UniString> keywords;
							keywords.Push("Высота Колонны");
							keywords.Push("Column Height");
							keywords.Push("Height");
							keywords.Push("Высота");
							keywords.Push("C");
							if (FindParameterByName((*memo.params), count, "C", keywords, paramC)) {
								if (paramC->typeID == APIParT_RealNum || paramC->typeID == APIParT_Length) {
									double currentC = paramC->value.real;
									if (currentC > 1e-6) {
										double minVal = currentC * (1.0 - factor);
										double maxVal = currentC * (1.0 + factor);
										paramC->value.real = RandomValue(minVal, maxVal);
										maskSet = true;
										needsMemo = true;
										changedAddPars = true;
										found = true;
									}
								}
							}
						}
						
						// Если параметр не найден, работаем через topOffset
						if (!found) {
							const double currentHeight = element.column.topOffset - element.column.bottomOffset;
							if (currentHeight > 1e-6) {
								const double newHeight = currentHeight * (1.0 + RandomValue(-factor, factor));
								const double deltaHeight = newHeight - currentHeight;
								element.column.topOffset += deltaHeight;
								ACAPI_ELEMENT_MASK_SET(mask, API_ColumnType, topOffset);
								maskSet = true;
							}
						}
					} else {
						// Если memo не получен, работаем через topOffset
						const double currentHeight = element.column.topOffset - element.column.bottomOffset;
						if (currentHeight > 1e-6) {
							const double newHeight = currentHeight * (1.0 + RandomValue(-factor, factor));
							const double deltaHeight = newHeight - currentHeight;
							element.column.topOffset += deltaHeight;
							ACAPI_ELEMENT_MASK_SET(mask, API_ColumnType, topOffset);
							maskSet = true;
						}
					}
					break;
				}
				case API_BeamID: {
					// Для балок пробуем найти параметр "zzyzx" или C в GDL-параметрах
					hasMemo = (ACAPI_Element_GetMemo(n.guid, &memo, APIMemoMask_AddPars) == NoError);
					if (hasMemo && memo.params != nullptr) {
						const GS::Int32 bytes = BMGetHandleSize((GSHandle)memo.params);
						const GS::Int32 count = (bytes > 0) ? (bytes / (GS::Int32)sizeof(API_AddParType)) : 0;
						
						// Ищем "zzyzx" или параметр C
						for (GS::Int32 i = 0; i < count; ++i) {
							API_AddParType& p = (*memo.params)[i];
							if (p.name != nullptr) {
								GS::UniString paramName(p.name);
								if (paramName == "zzyzx" || paramName == "ZZYZX" || paramName == "C") {
									if (p.typeID == APIParT_RealNum || p.typeID == APIParT_Length) {
										double currentHeight = p.value.real;
										if (currentHeight > 1e-6) {
											double minVal = currentHeight * (1.0 - factor);
											double maxVal = currentHeight * (1.0 + factor);
											p.value.real = RandomValue(minVal, maxVal);
											maskSet = true;
											needsMemo = true;
											changedAddPars = true;
											break;
										}
									}
								}
							}
						}
					}
					break;
				}
				default:
					break;
				}

				if (!maskSet)
					continue;

				// ---- Применение изменений ----
				if (needsMemo && hasMemo) {
					if (changedAddPars && memo.params != nullptr) {
						API_Guid guid = n.guid;
						(void)ACAPI_Element_ChangeMemo(guid, APIMemoMask_AddPars, &memo);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else if (changedBeamSegments) {
						(void)ACAPI_Element_Change(&element, &mask, &memo, APIMemoMask_BeamSegment, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else if (changedColumnSegments) {
						(void)ACAPI_Element_Change(&element, &mask, &memo, APIMemoMask_ColumnSegment, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					} else {
						(void)ACAPI_Element_Change(&element, &mask, &memo, 0, true);
						ACAPI_DisposeElemMemoHdls(&memo);
					}
				} else {
					// Для колонн через topOffset (без memo)
					(void)ACAPI_Element_Change(&element, &mask, nullptr, 0, true);
				}
			}
			return NoError;
		});

		return err == NoError;
	}

	// Рандомизировать углы - используем функцию из RotateHelper
	bool RandomizeAngles()
	{
		return RotateHelper::RandomizeSelectedAngles();
	}

}
