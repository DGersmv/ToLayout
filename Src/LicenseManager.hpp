#ifndef LICENSEMANAGER_HPP
#define LICENSEMANAGER_HPP

#include "ACAPinc.h"
#include "GSRoot.hpp"
#include "UniString.hpp"

// Класс для управления лицензией плагина
class LicenseManager {
public:
	// Результат проверки лицензии
	enum class LicenseStatus {
		Valid,           // Лицензия валидна
		Invalid,          // Лицензия невалидна
		NotFound,        // Файл лицензии не найден
		Expired,         // Лицензия истекла
		ComputerMismatch, // Не совпадает идентификатор компьютера
		PluginMismatch,   // Не совпадает название плагина
		ParseError        // Ошибка чтения файла лицензии
	};

	// Структура с данными лицензии
	struct LicenseData {
		GS::UniString computerId;
		GS::UniString pluginName;
		GS::UniString pluginVersion;
		GS::UniString validUntil;      // Формат: YYYY-MM-DD
		GS::UniString issuedDate;      // Формат: YYYY-MM-DD
		GS::UniString licenseKey;
	};

	// Структура с данными демо-режима
	struct DemoData {
		GS::UniString firstLaunchDate;  // Формат: YYYY-MM-DD
		GS::UniString lastLaunchDate;    // Формат: YYYY-MM-DD
		int launchCount;                 // Количество запусков
	};

	// Проверить лицензию (главная функция)
	// Возвращает LicenseStatus и заполняет licenseData если лицензия найдена
	static LicenseStatus CheckLicense(LicenseData& licenseData);

	// Получить идентификатор текущего компьютера (MAC адрес)
	static GS::UniString GetComputerId();

	// Получить путь к файлу лицензии (проверяет оба места: рядом с .apx и в AppData)
	// Возвращает путь к найденному файлу или первый путь для проверки
	static GS::UniString GetLicenseFilePath();

	// Записать лог проверки лицензии
	static void WriteLicenseLog(LicenseStatus status, const LicenseData& licenseData);
	
	// Записать общий лог в файл (для отладки)
	static void WriteLog(const GS::UniString& message);

	// Демо-режим: проверить, активен ли демо-период (22 дня или 22 запуска)
	// Возвращает true если демо активен, false если истек
	static bool CheckDemoPeriod(DemoData& demoData);

	// Демо-режим: обновить данные демо (увеличить счетчик запусков, обновить дату)
	static void UpdateDemoData();

	// Демо-режим: получить путь к файлу с демо-данными
	static GS::UniString GetDemoFilePath();

	// Сформировать URL для страницы лицензии с данными о плагине из демо-файла
	static GS::UniString BuildLicenseUrl();

private:
	// Получить путь к .apx файлу плагина
	static GS::UniString GetAddonFilePath();
	
	// Получить путь к директории пользовательских данных (AppData\Local\LandscapeHelper)
	static GS::UniString GetUserDataDirectory();
	
	// Работа с реестром: получить дату первого запуска
	static GS::UniString GetFirstLaunchDateFromRegistry();
	
	// Работа с реестром: установить дату первого запуска
	static void SetFirstLaunchDateToRegistry(const GS::UniString& date);
	
	// Работа с реестром: получить счетчик запусков
	static int GetLaunchCountFromRegistry();
	
	// Работа с реестром: установить счетчик запусков
	static void SetLaunchCountToRegistry(int count);

	// Прочитать файл лицензии
	static bool ReadLicenseFile(const GS::UniString& filePath, LicenseData& licenseData);

	// Парсить строку формата KEY=VALUE
	static bool ParseLicenseLine(const GS::UniString& line, GS::UniString& key, GS::UniString& value);

	// Проверить валидность даты (формат YYYY-MM-DD)
	static bool IsValidDate(const GS::UniString& dateStr);

	// Сравнить даты (YYYY-MM-DD), возвращает true если date1 < date2
	static bool CompareDates(const GS::UniString& date1, const GS::UniString& date2);

	// Получить текущую дату в формате YYYY-MM-DD
	static GS::UniString GetCurrentDate();

	// Получить MAC адрес первой сетевой карты
	static GS::UniString GetMacAddress();
};

#endif // LICENSEMANAGER_HPP



