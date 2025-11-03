#include "LayerHelper.hpp"
#include "APICommon.h"

namespace LayerHelper {

// ---------------- Удалить префикс "Слои/" или "Layers/" из пути ---------------- 
static GS::UniString RemoveRootFolderPrefix(const GS::UniString& path)
{
    if (path.IsEmpty()) {
        return path;
    }
    
    // Проверяем, начинается ли путь с "Слои/" или "Layers/"
    GS::UniString prefix1 = "Слои/";
    GS::UniString prefix2 = "Layers/";
    
    if (path.GetLength() >= prefix1.GetLength() && 
        path.GetSubstring(0, prefix1.GetLength()) == prefix1) {
        return path.GetSubstring(prefix1.GetLength(), path.GetLength() - prefix1.GetLength());
    }
    
    if (path.GetLength() >= prefix2.GetLength() && 
        path.GetSubstring(0, prefix2.GetLength()) == prefix2) {
        return path.GetSubstring(prefix2.GetLength(), path.GetLength() - prefix2.GetLength());
    }
    
    // Также проверяем без слэша в конце
    GS::UniString prefix1NoSlash = "Слои";
    GS::UniString prefix2NoSlash = "Layers";
    
    if (path == prefix1NoSlash || path == prefix2NoSlash) {
        return GS::UniString("");
    }
    
    return path;
}

// ---------------- Удалить первый элемент "Слои" или "Layers" из массива пути ---------------- 
static GS::Array<GS::UniString> RemoveRootFolderFromPath(const GS::Array<GS::UniString>& pathParts)
{
    GS::Array<GS::UniString> result;
    
    if (pathParts.GetSize() == 0) {
        return result;
    }
    
    // Проверяем первый элемент
    GS::UniString firstPart = pathParts[0];
    
    // Если первый элемент - "Слои" или "Layers" (регистронезависимо), пропускаем его
    if (firstPart == "Слои" || firstPart == "Layers" || firstPart == "layers" || firstPart == "LAYERS") {
        // Копируем остальные элементы
        for (UIndex i = 1; i < pathParts.GetSize(); ++i) {
            result.Push(pathParts[i]);
        }
    } else {
        // Копируем все элементы
        result = pathParts;
    }
    
    return result;
}

// ---------------- Разбить путь к папке на массив ---------------- 
GS::Array<GS::UniString> ParseFolderPath(const GS::UniString& folderPath)
{
    GS::Array<GS::UniString> pathParts;
    
    if (folderPath.IsEmpty()) {
        return pathParts;
    }

    // Разбиваем строку по символу "/"
    GS::UniString currentPath = folderPath;
    GS::UniString separator = "/";
    
    while (!currentPath.IsEmpty()) {
        Int32 separatorPos = currentPath.FindFirst(separator);
        if (separatorPos == -1) {
            // Последняя часть пути
            if (!currentPath.IsEmpty()) {
                pathParts.Push(currentPath);
            }
            break;
        } else {
            // Добавляем часть до разделителя
            GS::UniString part = currentPath.GetSubstring(0, separatorPos);
            if (!part.IsEmpty()) {
                pathParts.Push(part);
            }
            // Убираем обработанную часть и разделитель
            currentPath = currentPath.GetSubstring(separatorPos + 1, currentPath.GetLength() - separatorPos - 1);
        }
    }

    return pathParts;
}

// ---------------- Создать папку для слоев ---------------- 
bool CreateLayerFolder(const GS::UniString& folderPath, GS::Guid& folderGuid)
{
    if (folderPath.IsEmpty()) {
        folderGuid = GS::Guid(); // Пустой GUID для корневой папки
        return true; // Корневая папка уже существует
    }

    // Удаляем префикс "Слои/" или "Layers/" из пути
    GS::UniString cleanFolderPath = RemoveRootFolderPrefix(folderPath);
    
    GS::Array<GS::UniString> pathParts = ParseFolderPath(cleanFolderPath);
    if (pathParts.IsEmpty()) {
        folderGuid = GS::Guid();
        return true;
    }

    // Удаляем первый элемент "Слои" или "Layers", если он есть
    pathParts = RemoveRootFolderFromPath(pathParts);
    if (pathParts.IsEmpty()) {
        folderGuid = GS::Guid();
        return true;
    }

    // Проверяем на пустые части пути и дубликаты
    GS::Array<GS::UniString> cleanPathParts;
    for (UIndex i = 0; i < pathParts.GetSize(); ++i) {
        GS::UniString part = pathParts[i];
        part.Trim();
        if (!part.IsEmpty()) {
            // Проверяем, нет ли дубликатов подряд
            if (cleanPathParts.IsEmpty() || cleanPathParts[cleanPathParts.GetSize() - 1] != part) {
                cleanPathParts.Push(part);
            }
        }
    }

    if (cleanPathParts.IsEmpty()) {
        folderGuid = GS::Guid();
        return true;
    }

    // Создаем папки пошагово
    GS::Array<GS::UniString> currentPath;
    
    for (UIndex i = 0; i < cleanPathParts.GetSize(); ++i) {
        currentPath.Push(cleanPathParts[i]);
        
        // Формируем строку пути для логирования
        GS::UniString currentPathStr;
        for (UIndex j = 0; j < currentPath.GetSize(); ++j) {
            if (j > 0) currentPathStr += "/";
            currentPathStr += currentPath[j];
        }
        
        // Проверяем, существует ли папка
        API_AttributeFolder existingFolder = {};
        existingFolder.typeID = API_LayerID;
        existingFolder.path = currentPath;
        GSErrCode err = ACAPI_Attribute_GetFolder(existingFolder);
        
        if (err != NoError) {
            // Папка не существует, создаем её
            API_AttributeFolder folder = {};
            folder.typeID = API_LayerID;
            folder.path = currentPath;
            
            err = ACAPI_Attribute_CreateFolder(folder);
            if (err != NoError) {
#ifdef DEBUG_UI_LOGS
                ACAPI_WriteReport("[LayerHelper] Ошибка создания папки '%s' (код: %d)", true, 
                    currentPathStr.ToCStr().Get(), err);
#endif
                return false;
            }
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Создана папка: %s", false, currentPathStr.ToCStr().Get());
#endif
            
            // Используем GUID созданной папки
            folderGuid = folder.guid;
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] GUID созданной папки получен", false);
            ACAPI_WriteReport("[LayerHelper] Папка создана успешно, GUID не пустой: %s", false, 
                (folderGuid != GS::Guid()) ? "да" : "нет");
#endif
        } else {
            // Папка существует, используем её GUID
            folderGuid = existingFolder.guid;
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Папка уже существует: %s", false, currentPathStr.ToCStr().Get());
#endif
        }
    }

    return true;
}

// ---------------- Найти слой по имени, вернуть его индекс (0 если не найден) ----------------
static API_AttributeIndex FindLayerByName(const GS::UniString& layerName)
{
    API_AttributeIndex foundIndex = APIInvalidAttributeIndex;

    // Получаем количество слоев
    GS::UInt32 layerCount = 0;
    if (ACAPI_Attribute_GetNum(API_LayerID, layerCount) != NoError || layerCount == 0)
        return foundIndex;

    // Перебираем все слои и ищем совпадение по имени
    for (Int32 i = 1; i <= static_cast<Int32>(layerCount); ++i) {
        API_Attribute attr = {};
        attr.header.typeID = API_LayerID;
        attr.header.index = ACAPI_CreateAttributeIndex(i);
        if (ACAPI_Attribute_Get(&attr) != NoError)
            continue;

        if (GS::UniString(attr.header.name) == layerName) {
            foundIndex = ACAPI_CreateAttributeIndex(i);
            break;
        }
    }

    return foundIndex;
}

// ---------------- Создать слой в указанной папке ---------------- 
bool CreateLayer(const GS::UniString& folderPath, const GS::UniString& layerName, API_AttributeIndex& layerIndex)
{
    // Если имя слоя совпадает с именем папки, или слой с таким именем уже существует —
    // не создаём новый слой, а только переносим существующий в указанную папку
    if (!layerName.IsEmpty() && (layerName == folderPath)) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Имя слоя совпадает с именем папки: '%s' — пропускаем создание", false, layerName.ToCStr().Get());
#endif
        API_AttributeIndex existingIdx = FindLayerByName(layerName);
        if (existingIdx.IsPositive()) {
            layerIndex = existingIdx;
            if (!folderPath.IsEmpty()) {
#ifdef DEBUG_UI_LOGS
                ACAPI_WriteReport("[LayerHelper] Переносим существующий слой '%s' в папку '%s'", false, layerName.ToCStr().Get(), folderPath.ToCStr().Get());
#endif
                MoveLayerToFolder(layerIndex, folderPath);
            }
            return true;
        }
        // Если слоя с таким именем не нашли — продолжим обычное создание ниже
    } else {
        // Даже если имена не совпадают, стоит проверить существование слоя с таким именем,
        // чтобы избежать ошибки создания дубликата
        API_AttributeIndex existingIdx = FindLayerByName(layerName);
        if (existingIdx.IsPositive()) {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Слой '%s' уже существует — используем его и переносим при необходимости", false, layerName.ToCStr().Get());
#endif
            layerIndex = existingIdx;
            if (!folderPath.IsEmpty()) {
                MoveLayerToFolder(layerIndex, folderPath);
            }
            return true;
        }
    }

    // Сначала создаем папку, если нужно
    GS::Guid folderGuid;
    if (!CreateLayerFolder(folderPath, folderGuid)) {
        return false;
    }

    // Создаем слой
    API_Attribute layer = {};
    layer.header.typeID = API_LayerID;
    strcpy(layer.header.name, layerName.ToCStr().Get());
    
    // Устанавливаем свойства слоя (только основные поля)
    layer.layer.conClassId = 1; // Класс соединения по умолчанию

    // Создаем слой в корне (папка будет назначена позже через MoveLayerToFolder)
#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] Создаем слой в корне", false);
#endif

    // Создаем слой
    GSErrCode err = ACAPI_Attribute_Create(&layer, nullptr);
    if (err != NoError) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Ошибка создания слоя: %s", true, layerName.ToCStr().Get());
#endif
        return false;
    }

    layerIndex = layer.header.index;
    
    // Перемещаем слой в папку, если папка указана
    if (!folderPath.IsEmpty()) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Пытаемся переместить слой в папку: %s", false, folderPath.ToCStr().Get());
#endif
        if (!MoveLayerToFolder(layerIndex, folderPath)) {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Предупреждение: не удалось переместить слой в папку", false);
#endif
        }
    }
    
#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] Создан слой: %s в папке: %s", false, layerName.ToCStr().Get(), folderPath.ToCStr().Get());
#endif
    return true;
}

// ---------------- Переместить выделенные элементы в указанный слой ----------------
bool MoveSelectedElementsToLayer(API_AttributeIndex layerIndex)
{
    // Получаем выделенные элементы
    API_SelectionInfo selectionInfo = {};
    GS::Array<API_Neig> selNeigs;
    ACAPI_Selection_Get(&selectionInfo, &selNeigs, false, false);
    BMKillHandle((GSHandle*)&selectionInfo.marquee.coords);

    if (selNeigs.IsEmpty()) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Нет выделенных элементов", false);
#endif
        return false;
    }

#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] Перемещаем %d элементов в слой %s", false, (int)selNeigs.GetSize(), layerIndex.ToUniString().ToCStr().Get());
#endif

    // Перемещаем каждый элемент
    for (const API_Neig& neig : selNeigs) {
        API_Element element = {};
        element.header.guid = neig.guid;
        
        GSErrCode err = ACAPI_Element_Get(&element);
        if (err != NoError) {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Ошибка получения элемента: %s", true, APIGuidToString(neig.guid).ToCStr().Get());
#endif
            continue;
        }

        // Изменяем слой элемента
        API_Element mask = {};
        ACAPI_ELEMENT_MASK_CLEAR(mask);
        
        element.header.layer = layerIndex;
        ACAPI_ELEMENT_MASK_SET(mask, API_Elem_Head, layer);

        err = ACAPI_Element_Change(&element, &mask, nullptr, 0, true);
        if (err != NoError) {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Ошибка изменения слоя элемента: %s", true, APIGuidToString(neig.guid).ToCStr().Get());
#endif
        } else {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Элемент перемещен в слой: %s", false, APIGuidToString(neig.guid).ToCStr().Get());
#endif
        }
    }

    return true;
}

// ---------------- Изменить ID всех выделенных элементов ----------------
bool ChangeSelectedElementsID(const GS::UniString& baseID)
{
    if (baseID.IsEmpty()) return false;

    API_SelectionInfo selectionInfo = {};
    GS::Array<API_Neig> selNeigs;
    ACAPI_Selection_Get(&selectionInfo, &selNeigs, false, false);
    BMKillHandle((GSHandle*)&selectionInfo.marquee.coords);

    if (selNeigs.IsEmpty()) return false;

#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] Изменяем ID %d элементов с базовым названием: %s", false, (int)selNeigs.GetSize(), baseID.ToCStr().Get());
#endif

    // Используем Undo-группу для возможности отмены
    GSErrCode err = ACAPI_CallUndoableCommand("Change Elements ID", [&]() -> GSErrCode {
        for (UIndex i = 0; i < selNeigs.GetSize(); ++i) {
            // Создаем новый ID: baseID-01, baseID-02, etc.
            GS::UniString newID = baseID;
            if (selNeigs.GetSize() > 1) {
                newID += GS::UniString::Printf("-%02d", (int)(i + 1));
            }

            // Изменяем ID элемента
            if (ACAPI_Element_ChangeElementInfoString(&selNeigs[i].guid, &newID) != NoError) {
#ifdef DEBUG_UI_LOGS
                ACAPI_WriteReport("[LayerHelper] Ошибка изменения ID элемента: %s", true, APIGuidToString(selNeigs[i].guid).ToCStr().Get());
#endif
                continue;
            } else {
#ifdef DEBUG_UI_LOGS
                ACAPI_WriteReport("[LayerHelper] ID изменен: %s", false, newID.ToCStr().Get());
#endif
            }
        }
        return NoError;
    });

    return err == NoError;
}

// ---------------- Основная функция: создать папку, слой и переместить элементы ----------------
bool CreateLayerAndMoveElements(const LayerCreationParams& params)
{
#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] Начинаем создание папки, слоя и перемещение элементов", false);
    ACAPI_WriteReport("[LayerHelper] Папка: %s, Слой: %s, ID: %s", false, 
        params.folderPath.ToCStr().Get(), 
        params.layerName.ToCStr().Get(), 
        params.baseID.ToCStr().Get());
#endif

    // Используем Undo-группу для возможности отмены всей операции
    GSErrCode err = ACAPI_CallUndoableCommand("Create Layer and Move Elements", [&]() -> GSErrCode {
        // 1. Создаем слой
        API_AttributeIndex layerIndex;
        if (!CreateLayer(params.folderPath, params.layerName, layerIndex)) {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Ошибка создания слоя", true);
#endif
            return APIERR_GENERAL;
        }

        // 2. Перемещаем элементы в новый слой
        if (!MoveSelectedElementsToLayer(layerIndex)) {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] Ошибка перемещения элементов", true);
#endif
            return APIERR_GENERAL;
        }

        // 3. Изменяем ID элементов (только если baseID не пустой)
        if (!params.baseID.IsEmpty()) {
            if (!ChangeSelectedElementsID(params.baseID)) {
#ifdef DEBUG_UI_LOGS
                ACAPI_WriteReport("[LayerHelper] Ошибка изменения ID элементов", true);
#endif
                return APIERR_GENERAL;
            }
        } else {
#ifdef DEBUG_UI_LOGS
            ACAPI_WriteReport("[LayerHelper] ID элементов не изменяются (baseID пустой)", false);
#endif
        }

        // 4. Скрываем слой, если требуется
        if (params.hideLayer) {
            if (!SetLayerVisibility(layerIndex, true)) {
#ifdef DEBUG_UI_LOGS
                ACAPI_WriteReport("[LayerHelper] Ошибка скрытия слоя", true);
#endif
                return APIERR_GENERAL;
            }
        }

#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Операция завершена успешно", false);
#endif
        return NoError;
    });

    return err == NoError;
}

// ---------------- Переместить слой в папку ---------------- 
bool MoveLayerToFolder(API_AttributeIndex layerIndex, const GS::UniString& folderPath)
{
    if (folderPath.IsEmpty()) {
        return true; // Корневая папка
    }

    // Гарантируем существование папки и получаем её GUID
    GS::Guid folderGuid;
    if (!CreateLayerFolder(folderPath, folderGuid)) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Не удалось подготовить папку для слоя: %s", true, folderPath.ToCStr().Get());
#endif
        return false;
    }

    // Если GUID пустой — это корень, перемещать не нужно
    if (folderGuid == GS::Guid()) {
        return true;
    }

    // Пытаемся переместить слой в папку через изменение атрибута
    API_Attribute layer = {};
    layer.header.typeID = API_LayerID;
    layer.header.index = layerIndex;
    
    GSErrCode err = ACAPI_Attribute_Get(&layer);
    if (err != NoError) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Не удалось получить информацию о слое (код: %d)", true, err);
        ACAPI_WriteReport("[LayerHelper] Слой остался в корне, но папка создана: %s", false, folderPath.ToCStr().Get());
#endif
        return true;
    }
    
    // Пытаемся переместить слой в папку через ACAPI_Attribute_Move
    // Сначала получаем GUID слоя
    API_Attribute currentLayer = {};
    currentLayer.header.typeID = API_LayerID;
    currentLayer.header.index = layerIndex;
    
    err = ACAPI_Attribute_Get(&currentLayer);
    if (err != NoError) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Ошибка получения слоя для перемещения (код: %d)", true, err);
#endif
        return false;
    }
    
    // Создаем массив GUID атрибутов для перемещения (слой)
    GS::Array<GS::Guid> attributesToMove;
    // Попробуем конвертировать API_Guid в GS::Guid
    GS::UniString guidStr = APIGuidToString(currentLayer.header.guid);
    GS::Guid layerGuid(guidStr);
    attributesToMove.Push(layerGuid);
    
    // Создаем пустой массив папок (мы не перемещаем папки)
    GS::Array<API_AttributeFolder> foldersToMove;
    
    // Создаем целевую папку
    API_AttributeFolder targetFolder = {};
    targetFolder.typeID = API_LayerID;
    targetFolder.guid = folderGuid;
    
    // Перемещаем слой в папку
#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] Вызываем ACAPI_Attribute_Move...", false);
#endif
    err = ACAPI_Attribute_Move(foldersToMove, attributesToMove, targetFolder);
#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] ACAPI_Attribute_Move вернул код: %d", false, err);
#endif
    
    if (err != NoError) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Ошибка перемещения слоя в папку '%s' (код: %d, hex: 0x%X)", true, 
            folderPath.ToCStr().Get(), err, (unsigned int)err);
        ACAPI_WriteReport("[LayerHelper] Слой остался в корне, но папка создана: %s", false, folderPath.ToCStr().Get());
#endif
    } else {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Слой успешно перемещен в папку: %s", false, folderPath.ToCStr().Get());
#endif
    }
    
    return true;
}

// ---------------- Скрыть/показать слой ---------------- 
bool SetLayerVisibility(API_AttributeIndex layerIndex, bool hidden)
{
#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] SetLayerVisibility: layer=%s, hidden=%s", false, 
        layerIndex.ToUniString().ToCStr().Get(), hidden ? "true" : "false");
#endif
    
    // Получаем текущий слой
    API_Attribute layer = {};
    layer.header.typeID = API_LayerID;
    layer.header.index = layerIndex;
    
    GSErrCode err = ACAPI_Attribute_Get(&layer);
    if (err != NoError) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Ошибка получения слоя (код: %d)", true, err);
#endif
        return false;
    }
    
    // Устанавливаем/снимаем флаг скрытия
    if (hidden) {
        layer.header.flags |= APILay_Hidden;  // Добавляем флаг скрытия
    } else {
        layer.header.flags &= ~APILay_Hidden; // Снимаем флаг скрытия
    }
    
    // Сохраняем изменения через ACAPI_Attribute_Modify
    err = ACAPI_Attribute_Modify(&layer, nullptr);
    if (err != NoError) {
#ifdef DEBUG_UI_LOGS
        ACAPI_WriteReport("[LayerHelper] Ошибка установки видимости слоя (код: %d)", true, err);
#endif
        return false;
    }
    
#ifdef DEBUG_UI_LOGS
    ACAPI_WriteReport("[LayerHelper] Видимость слоя установлена: hidden=%s", false, hidden ? "true" : "false");
#endif
    return true;
}

// ---------------- Рекурсивная функция для получения слоев из папки и её подпапок ---------------- 
static void GetLayersFromFolderRecursive(
    const GS::Array<GS::UniString>& folderPath,
    GS::UniString currentPathStr,
    GS::Array<LayerInfo>& layersList,
    GS::Array<GS::UniString>& processedPaths)
{
    // Формируем строку пути для проверки циклов
    if (currentPathStr.IsEmpty()) {
        if (folderPath.GetSize() > 0) {
            for (UIndex i = 0; i < folderPath.GetSize(); ++i) {
                if (i > 0) currentPathStr += "/";
                currentPathStr += folderPath[i];
            }
        } else {
            currentPathStr = GS::UniString(""); // Корневая папка
        }
    }
    
    // Проверяем, не обрабатывали ли мы уже эту папку
    for (UIndex i = 0; i < processedPaths.GetSize(); ++i) {
        if (processedPaths[i] == currentPathStr)
            return; // Уже обработана
    }
    processedPaths.Push(currentPathStr);
    
    // Получаем папку для использования в GetFolderContent
    API_AttributeFolder folder = {};
    folder.typeID = API_LayerID;
    
    // Для корневой папки не указываем path и guid - получаем корень
    // Для остальных папок указываем path
    if (folderPath.GetSize() > 0) {
        folder.path = folderPath;
        // Проверяем существование папки
        if (ACAPI_Attribute_GetFolder(folder) != NoError)
            return; // Папка не существует
    }
    
    // Получаем содержимое папки
    API_AttributeFolderContent folderContent = {};
    GSErrCode err = ACAPI_Attribute_GetFolderContent(folder, folderContent);
    
    if (err != NoError) {
        // Ошибка получения содержимого - пропускаем
        return;
    }
    
    // Обрабатываем атрибуты (слои) в этой папке
    // attributeIds содержит массив GUID атрибутов (GS::Guid)
    for (UIndex i = 0; i < folderContent.attributeIds.GetSize(); ++i) {
        GS::Guid attrGuid = folderContent.attributeIds[i];
        
        // Конвертируем GS::Guid в API_Guid
        API_Guid apiGuid = GSGuid2APIGuid(attrGuid);
        
        // Получаем атрибут по GUID
        API_Attribute attr = {};
        attr.header.typeID = API_LayerID;
        attr.header.guid = apiGuid;
        
        // Получаем информацию об атрибуте через GUID
        GSErrCode err = ACAPI_Attribute_Get(&attr);
        if (err == NoError) {
            LayerInfo info;
            info.name = attr.header.name;
            // Удаляем префикс "Слои/" или "Layers/" из пути папки
            info.folder = RemoveRootFolderPrefix(currentPathStr);
            layersList.Push(info);
        }
    }
    
    // Обрабатываем подпапки рекурсивно
    // НЕ показываем корневую папку как отдельный элемент, только её содержимое
    for (UIndex i = 0; i < folderContent.subFolders.GetSize(); ++i) {
        const API_AttributeFolder& subfolder = folderContent.subFolders[i];
        
        // Формируем путь подпапки
        GS::Array<GS::UniString> subfolderPath = subfolder.path;
        GS::UniString subfolderPathStr;
        for (UIndex j = 0; j < subfolderPath.GetSize(); ++j) {
            if (j > 0) subfolderPathStr += "/";
            subfolderPathStr += subfolderPath[j];
        }
        
        // Удаляем префикс "Слои" или "Layers" из пути перед рекурсивным вызовом
        GS::Array<GS::UniString> cleanedSubfolderPath = RemoveRootFolderFromPath(subfolderPath);
        GS::UniString cleanedSubfolderPathStr = RemoveRootFolderPrefix(subfolderPathStr);
        
        // Рекурсивный вызов с очищенным путем
        GetLayersFromFolderRecursive(cleanedSubfolderPath, cleanedSubfolderPathStr, layersList, processedPaths);
    }
}

// ---------------- Получить список всех слоев с их папками ---------------- 
GS::Array<LayerInfo> GetLayersList()
{
    GS::Array<LayerInfo> layersList;
    GS::Array<GS::UniString> processedPaths;

    // Начинаем с корневой папки (пустой путь)
    // Корневая папка не показывается как отдельный элемент, только её содержимое
    GS::Array<GS::UniString> rootPath;
    GetLayersFromFolderRecursive(rootPath, GS::UniString(""), layersList, processedPaths);

    return layersList;
}

} // namespace LayerHelper
