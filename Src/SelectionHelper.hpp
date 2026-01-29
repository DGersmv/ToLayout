#ifndef SELECTIONHELPER_HPP
#define SELECTIONHELPER_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "GSRoot.hpp"

namespace SelectionHelper {

    enum SelectionModification { RemoveFromSelection, AddToSelection };

    struct ElementInfo {
        GS::UniString guidStr;   // GUID элемента в строковом виде
        GS::UniString typeName;  // Человекочитаемое имя типа (Объект, Лампа, Колонна...)
        GS::UniString elemID;    // Имя/ID из Archicad
        GS::UniString layerName; // Имя слоя элемента
    };

    // Получить список выделенных элементов
    GS::Array<ElementInfo> GetSelectedElements ();

    // Добавить или удалить элемент по GUID
    void ModifySelection (const GS::UniString& elemGuidStr, SelectionModification modification);

    // Изменить ID всех выделенных элементов
    bool ChangeSelectedElementsID (const GS::UniString& baseID);

    // Результат применения выделения
    struct ApplyCheckedSelectionResult {
        UInt32 applied;    // Количество успешно применённых элементов
        UInt32 requested;   // Количество запрошенных элементов
    };

    // Применить выделение по списку GUID
    ApplyCheckedSelectionResult ApplyCheckedSelection (const GS::Array<API_Guid>& guids);

    // Результат обновления ID по списку GUID
    struct UpdateElementsIdResult {
        UInt32 updated;    // Успешно обновлено
        UInt32 requested;  // Запрошено
    };

    // Обновить ID элементов по списку GUID
    UpdateElementsIdResult UpdateElementsID (const GS::Array<API_Guid>& guids, const GS::UniString& newID);

} // namespace SelectionHelper

#endif // SELECTIONHELPER_HPP
