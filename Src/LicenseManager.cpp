#include "LicenseManager.hpp"
#include "APICommon.h"

#ifdef GS_WIN
#include <Windows.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include <cstdio>
#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <cstdio>
#endif

#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>

// =============================================================================
// Получить идентификатор компьютера (MAC адрес)
// =============================================================================

GS::UniString LicenseManager::GetComputerId()
{
	return GetMacAddress();
}

// =============================================================================
// Получить MAC адрес первой сетевой карты
// =============================================================================

GS::UniString LicenseManager::GetMacAddress()
{
#ifdef GS_WIN
	IP_ADAPTER_INFO adapterInfo[16];
	DWORD dwBufLen = sizeof(adapterInfo);
	DWORD dwStatus = GetAdaptersInfo(adapterInfo, &dwBufLen);

	if (dwStatus == ERROR_SUCCESS) {
		PIP_ADAPTER_INFO pAdapterInfo = adapterInfo;
		// Берем первый адаптер с MAC адресом
		do {
			if (pAdapterInfo->AddressLength == 6) {
				// Форматируем MAC адрес как XX-XX-XX-XX-XX-XX
				char macStr[32];
				sprintf_s(macStr, sizeof(macStr), "%02X-%02X-%02X-%02X-%02X-%02X",
					(unsigned char)pAdapterInfo->Address[0],
					(unsigned char)pAdapterInfo->Address[1],
					(unsigned char)pAdapterInfo->Address[2],
					(unsigned char)pAdapterInfo->Address[3],
					(unsigned char)pAdapterInfo->Address[4],
					(unsigned char)pAdapterInfo->Address[5]);
				return GS::UniString(macStr);
			}
			pAdapterInfo = pAdapterInfo->Next;
		} while (pAdapterInfo);
	}
	
	// Если не удалось получить MAC, возвращаем пустую строку
	return GS::UniString("UNKNOWN-MAC");
#else
	// Для Mac - упрощенная версия (можно расширить)
	return GS::UniString("MAC-UNKNOWN");
#endif
}

// =============================================================================
// Получить путь к .apx файлу плагина
// =============================================================================

GS::UniString LicenseManager::GetAddonFilePath()
{
#ifdef GS_WIN
	HMODULE hModule = reinterpret_cast<HMODULE>(ACAPI_GetOwnResModule());
	if (hModule == nullptr) {
		return GS::UniString();
	}

	wchar_t modulePath[MAX_PATH];
	DWORD length = GetModuleFileNameW(hModule, modulePath, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		return GS::UniString();
	}

	return GS::UniString(modulePath);
#else
	// Для Mac - нужно использовать другой подход
	return GS::UniString();
#endif
}

// =============================================================================
// Получить путь к файлу лицензии (рядом с .apx файлом)
// =============================================================================

GS::UniString LicenseManager::GetLicenseFilePath()
{
	GS::UniString addonPath = GetAddonFilePath();
	if (addonPath.IsEmpty()) {
		return GS::UniString();
	}

	// Убираем расширение .apx и добавляем .lic
	GS::UniString licensePath = addonPath;
	
	// Находим последнюю точку
	UIndex lastDot = licensePath.FindLast('.');
	if (lastDot != MaxUIndex) {
		licensePath = licensePath.GetSubstring(0, lastDot);
	}
	
	licensePath += ".lic";
	return licensePath;
}

// =============================================================================
// Парсить строку формата KEY=VALUE
// =============================================================================

bool LicenseManager::ParseLicenseLine(const GS::UniString& line, GS::UniString& key, GS::UniString& value)
{
	// Убираем пробелы в начале и конце
	GS::UniString trimmed = line;
	trimmed.Trim();
	
	// Пропускаем пустые строки и комментарии
	if (trimmed.IsEmpty() || trimmed.GetSubstring(0, 1) == GS::UniString("#")) {
		return false;
	}

	// Ищем знак равно
	Int32 equalPos = trimmed.FindFirst(GS::UniString("="));
	if (equalPos == -1) {
		return false;
	}

	key = trimmed.GetSubstring(0, equalPos);
	key.Trim();
	
	value = trimmed.GetSubstring(equalPos + 1, trimmed.GetLength() - equalPos - 1);
	value.Trim();

	return !key.IsEmpty();
}

// =============================================================================
// Прочитать файл лицензии
// =============================================================================

bool LicenseManager::ReadLicenseFile(const GS::UniString& filePath, LicenseData& licenseData)
{
#ifdef GS_WIN
	FILE* fp = _wfopen(filePath.ToUStr().Get(), L"r");
#else
	FILE* fp = fopen(filePath.ToCStr().Get(), "r");
#endif
	
	if (fp == nullptr) {
		return false;
	}

	char lineBuffer[512];
	while (fgets(lineBuffer, sizeof(lineBuffer), fp) != nullptr) {
		GS::UniString line(lineBuffer);
		GS::UniString key, value;

		if (ParseLicenseLine(line, key, value)) {
			if (key == GS::UniString("COMPUTER_ID")) {
				licenseData.computerId = value;
			} else if (key == GS::UniString("PLUGIN_NAME")) {
				licenseData.pluginName = value;
			} else if (key == GS::UniString("PLUGIN_VERSION")) {
				licenseData.pluginVersion = value;
			} else if (key == GS::UniString("VALID_UNTIL")) {
				licenseData.validUntil = value;
			} else if (key == GS::UniString("ISSUED_DATE")) {
				licenseData.issuedDate = value;
			} else if (key == GS::UniString("LICENSE_KEY")) {
				licenseData.licenseKey = value;
			}
		}
	}

	fclose(fp);
	return true;
}

// =============================================================================
// Проверить валидность даты (формат YYYY-MM-DD)
// =============================================================================

bool LicenseManager::IsValidDate(const GS::UniString& dateStr)
{
	if (dateStr.GetLength() != 10) {
		return false;
	}

	// Проверяем формат YYYY-MM-DD
	if (dateStr[4] != '-' || dateStr[7] != '-') {
		return false;
	}

	// Проверяем что остальные символы - цифры
	GS::UniString digits = GS::UniString("0123456789");
	for (UIndex i = 0; i < dateStr.GetLength(); ++i) {
		if (i != 4 && i != 7) {
			GS::UniString chStr = dateStr.GetSubstring(i, 1);
			if (!digits.Contains(chStr)) {
				return false;
			}
		}
	}

	return true;
}

// =============================================================================
// Сравнить даты (YYYY-MM-DD), возвращает true если date1 < date2
// =============================================================================

bool LicenseManager::CompareDates(const GS::UniString& date1, const GS::UniString& date2)
{
	// Простое сравнение строк работает для формата YYYY-MM-DD
	return date1 < date2;
}

// =============================================================================
// Получить текущую дату в формате YYYY-MM-DD
// =============================================================================

GS::UniString LicenseManager::GetCurrentDate()
{
	std::time_t now = std::time(nullptr);
	std::tm* timeinfo;
#ifdef GS_WIN
	std::tm timeinfoBuf;
	timeinfo = &timeinfoBuf;
	localtime_s(timeinfo, &now);
#else
	std::tm timeinfoBuf;
	timeinfo = localtime_r(&now, &timeinfoBuf);
#endif
	
	char dateStr[16];
#ifdef GS_WIN
	sprintf_s(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
		timeinfo->tm_year + 1900,
		timeinfo->tm_mon + 1,
		timeinfo->tm_mday);
#else
	snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
		timeinfo->tm_year + 1900,
		timeinfo->tm_mon + 1,
		timeinfo->tm_mday);
#endif
	
	return GS::UniString(dateStr);
}

// =============================================================================
// Проверить лицензию (главная функция)
// =============================================================================

LicenseManager::LicenseStatus LicenseManager::CheckLicense(LicenseData& licenseData)
{
	// Получаем путь к файлу лицензии
	GS::UniString licensePath = GetLicenseFilePath();
	if (licensePath.IsEmpty()) {
		WriteLicenseLog(LicenseStatus::NotFound, licenseData);
		return LicenseStatus::NotFound;
	}

	// Читаем файл лицензии
	if (!ReadLicenseFile(licensePath, licenseData)) {
		WriteLicenseLog(LicenseStatus::NotFound, licenseData);
		return LicenseStatus::NotFound;
	}

	// Проверяем что все поля заполнены
	if (licenseData.computerId.IsEmpty() ||
		licenseData.pluginName.IsEmpty() ||
		licenseData.validUntil.IsEmpty()) {
		WriteLicenseLog(LicenseStatus::ParseError, licenseData);
		return LicenseStatus::ParseError;
	}

	// Проверяем идентификатор компьютера
	GS::UniString currentComputerId = GetComputerId();
	if (currentComputerId != licenseData.computerId) {
		WriteLicenseLog(LicenseStatus::ComputerMismatch, licenseData);
		return LicenseStatus::ComputerMismatch;
	}

	// Проверяем название плагина (ожидаем LandscapeHelper_AC29 или Browser_Repl_Int)
	GS::UniString expectedPluginName1 = GS::UniString("LandscapeHelper_AC29");
	GS::UniString expectedPluginName2 = GS::UniString("Browser_Repl_Int");
	if (licenseData.pluginName != expectedPluginName1 && 
		licenseData.pluginName != expectedPluginName2) {
		WriteLicenseLog(LicenseStatus::PluginMismatch, licenseData);
		return LicenseStatus::PluginMismatch;
	}

	// Проверяем дату окончания действия
	if (!IsValidDate(licenseData.validUntil)) {
		WriteLicenseLog(LicenseStatus::ParseError, licenseData);
		return LicenseStatus::ParseError;
	}

	GS::UniString currentDate = GetCurrentDate();
	if (CompareDates(licenseData.validUntil, currentDate)) {
		WriteLicenseLog(LicenseStatus::Expired, licenseData);
		return LicenseStatus::Expired;
	}

	// Все проверки пройдены
	WriteLicenseLog(LicenseStatus::Valid, licenseData);
	return LicenseStatus::Valid;
}

// =============================================================================
// Записать лог проверки лицензии
// =============================================================================

void LicenseManager::WriteLicenseLog(LicenseStatus status, const LicenseData& licenseData)
{
	GS::UniString logPath = GetLicenseFilePath();
	if (logPath.IsEmpty()) {
		return;
	}

	// Меняем расширение на .log
	UIndex lastDot = logPath.FindLast('.');
	if (lastDot != MaxUIndex) {
		logPath = logPath.GetSubstring(0, lastDot);
	}
	logPath += ".log";

	// Открываем файл для добавления (append mode)
#ifdef GS_WIN
	FILE* fp = _wfopen(logPath.ToUStr().Get(), L"a");
#else
	FILE* fp = fopen(logPath.ToCStr().Get(), "a");
#endif
	
	if (fp == nullptr) {
		return;
	}

	// Формируем строку лога
	GS::UniString statusStr;
	GS::UniString currentDate = GetCurrentDate();

	switch (status) {
		case LicenseStatus::Valid:
			statusStr = "VALID";
			break;
		case LicenseStatus::Invalid:
			statusStr = "INVALID";
			break;
		case LicenseStatus::NotFound:
			statusStr = "NOT_FOUND";
			break;
		case LicenseStatus::Expired:
			statusStr = "EXPIRED";
			break;
		case LicenseStatus::ComputerMismatch:
			statusStr = "COMPUTER_MISMATCH";
			break;
		case LicenseStatus::PluginMismatch:
			statusStr = "PLUGIN_MISMATCH";
			break;
		case LicenseStatus::ParseError:
			statusStr = "PARSE_ERROR";
			break;
	}

	GS::UniString logLine = GS::UniString("[") + currentDate + GS::UniString("] ");
	logLine += GS::UniString("Status: ") + statusStr;
	logLine += GS::UniString(" | Computer: ") + GetComputerId();
	
	if (!licenseData.pluginName.IsEmpty()) {
		logLine += GS::UniString(" | Plugin: ") + licenseData.pluginName;
	}
	
	if (!licenseData.validUntil.IsEmpty()) {
		logLine += GS::UniString(" | Valid until: ") + licenseData.validUntil;
	}
	
	logLine += GS::UniString("\n");

	// Конвертируем UniString в UTF-8 для записи
#ifdef GS_WIN
	// На Windows используем UTF-16 напрямую
	fputws(logLine.ToUStr().Get(), fp);
#else
	// На других платформах конвертируем в UTF-8
	GS::UniString utf8Line = logLine;
	fputs(utf8Line.ToCStr().Get(), fp);
#endif

	fclose(fp);
}

// =============================================================================
// Записать общий лог в файл (для отладки)
// =============================================================================

void LicenseManager::WriteLog(const GS::UniString& message)
{
	GS::UniString logPath = GetLicenseFilePath();
	if (logPath.IsEmpty()) {
		return;
	}

	// Меняем расширение на .log
	UIndex lastDot = logPath.FindLast('.');
	if (lastDot != MaxUIndex) {
		logPath = logPath.GetSubstring(0, lastDot);
	}
	logPath += ".log";

	// Открываем файл для добавления (append mode)
#ifdef GS_WIN
	FILE* fp = _wfopen(logPath.ToUStr().Get(), L"a");
#else
	FILE* fp = fopen(logPath.ToCStr().Get(), "a");
#endif
	
	if (fp == nullptr) {
		return;
	}

	GS::UniString currentDate = GetCurrentDate();
	GS::UniString logLine = GS::UniString("[") + currentDate + GS::UniString("] ") + message + GS::UniString("\n");

	// Конвертируем UniString в UTF-8 для записи
#ifdef GS_WIN
	// На Windows используем UTF-16 напрямую
	fputws(logLine.ToUStr().Get(), fp);
#else
	// На других платформах конвертируем в UTF-8
	GS::UniString utf8Line = logLine;
	fputs(utf8Line.ToCStr().Get(), fp);
#endif

	fclose(fp);
}

// =============================================================================
// Демо-режим: получить путь к файлу с демо-данными
// =============================================================================

GS::UniString LicenseManager::GetDemoFilePath()
{
	GS::UniString addonPath = GetAddonFilePath();
	if (addonPath.IsEmpty()) {
		return GS::UniString();
	}

	// Меняем расширение на .demo
	UIndex lastDot = addonPath.FindLast('.');
	if (lastDot != MaxUIndex) {
		addonPath = addonPath.GetSubstring(0, lastDot);
	}
	addonPath += ".demo";

	return addonPath;
}

// =============================================================================
// Демо-режим: проверить, активен ли демо-период (22 дня или 22 запуска)
// =============================================================================

bool LicenseManager::CheckDemoPeriod(DemoData& demoData)
{
	const int MAX_LAUNCHES = 22;
	const int MAX_DAYS = 22;

	GS::UniString demoPath = GetDemoFilePath();
	if (demoPath.IsEmpty()) {
		// Если не можем получить путь, считаем что демо истек
		return false;
	}

	// Читаем файл демо-данных
#ifdef GS_WIN
	FILE* fp = _wfopen(demoPath.ToUStr().Get(), L"r");
#else
	FILE* fp = fopen(demoPath.ToCStr().Get(), "r");
#endif

	if (fp == nullptr) {
		// Файл не существует - это первый запуск, создаем демо-данные
		demoData.firstLaunchDate = GetCurrentDate();
		demoData.lastLaunchDate = GetCurrentDate();
		demoData.launchCount = 1;
		return true; // Демо активен
	}

	// Читаем данные из файла
	char line[256];
	demoData.launchCount = 0;
	demoData.firstLaunchDate = GS::UniString();
	demoData.lastLaunchDate = GS::UniString();

	while (fgets(line, sizeof(line), fp) != nullptr) {
		GS::UniString uniLine(line);
		GS::UniString key, value;
		if (ParseLicenseLine(uniLine, key, value)) {
			if (key == GS::UniString("FIRST_LAUNCH_DATE")) {
				demoData.firstLaunchDate = value;
			} else if (key == GS::UniString("LAST_LAUNCH_DATE")) {
				demoData.lastLaunchDate = value;
			} else if (key == GS::UniString("LAUNCH_COUNT")) {
				// Конвертируем строку в число
				GS::UniString countStr = value;
				demoData.launchCount = 0;
				for (UIndex i = 0; i < countStr.GetLength(); ++i) {
					GS::UniString ch = countStr.GetSubstring(i, 1);
					if (ch >= GS::UniString("0") && ch <= GS::UniString("9")) {
						GS::UniString chStr = ch.GetSubstring(0, 1);
						demoData.launchCount = demoData.launchCount * 10 + (chStr.ToCStr().Get()[0] - '0');
					}
				}
			}
		}
	}

	fclose(fp);

	// Если данные не найдены, создаем новые
	if (demoData.firstLaunchDate.IsEmpty()) {
		demoData.firstLaunchDate = GetCurrentDate();
		demoData.lastLaunchDate = GetCurrentDate();
		demoData.launchCount = 1;
		return true;
	}

	// Проверяем лимит запусков
	if (demoData.launchCount >= MAX_LAUNCHES) {
		return false; // Демо истек по количеству запусков
	}

	// Проверяем лимит дней
	GS::UniString currentDate = GetCurrentDate();
	if (CompareDates(demoData.firstLaunchDate, currentDate)) {
		// Вычисляем разницу в днях
		int year1 = 0, month1 = 0, day1 = 0, year2 = 0, month2 = 0, day2 = 0;
		if (demoData.firstLaunchDate.GetLength() >= 10 && currentDate.GetLength() >= 10) {
			GS::UniString firstYear = demoData.firstLaunchDate.GetSubstring(0, 4);
			GS::UniString firstMonth = demoData.firstLaunchDate.GetSubstring(5, 2);
			GS::UniString firstDay = demoData.firstLaunchDate.GetSubstring(8, 2);
			GS::UniString currYear = currentDate.GetSubstring(0, 4);
			GS::UniString currMonth = currentDate.GetSubstring(5, 2);
			GS::UniString currDay = currentDate.GetSubstring(8, 2);

			// Конвертируем строки в числа
			for (UIndex i = 0; i < firstYear.GetLength(); ++i) {
				GS::UniString chStr = firstYear.GetSubstring(i, 1);
				char ch = chStr.ToCStr().Get()[0];
				if (ch >= '0' && ch <= '9') year1 = year1 * 10 + (ch - '0');
			}
			for (UIndex i = 0; i < firstMonth.GetLength(); ++i) {
				GS::UniString chStr = firstMonth.GetSubstring(i, 1);
				char ch = chStr.ToCStr().Get()[0];
				if (ch >= '0' && ch <= '9') month1 = month1 * 10 + (ch - '0');
			}
			for (UIndex i = 0; i < firstDay.GetLength(); ++i) {
				GS::UniString chStr = firstDay.GetSubstring(i, 1);
				char ch = chStr.ToCStr().Get()[0];
				if (ch >= '0' && ch <= '9') day1 = day1 * 10 + (ch - '0');
			}
			for (UIndex i = 0; i < currYear.GetLength(); ++i) {
				GS::UniString chStr = currYear.GetSubstring(i, 1);
				char ch = chStr.ToCStr().Get()[0];
				if (ch >= '0' && ch <= '9') year2 = year2 * 10 + (ch - '0');
			}
			for (UIndex i = 0; i < currMonth.GetLength(); ++i) {
				GS::UniString chStr = currMonth.GetSubstring(i, 1);
				char ch = chStr.ToCStr().Get()[0];
				if (ch >= '0' && ch <= '9') month2 = month2 * 10 + (ch - '0');
			}
			for (UIndex i = 0; i < currDay.GetLength(); ++i) {
				GS::UniString chStr = currDay.GetSubstring(i, 1);
				char ch = chStr.ToCStr().Get()[0];
				if (ch >= '0' && ch <= '9') day2 = day2 * 10 + (ch - '0');
			}

			// Вычисляем разницу в днях
			std::tm tm1 = {};
			tm1.tm_year = year1 - 1900;
			tm1.tm_mon = month1 - 1;
			tm1.tm_mday = day1;
			std::tm tm2 = {};
			tm2.tm_year = year2 - 1900;
			tm2.tm_mon = month2 - 1;
			tm2.tm_mday = day2;

			std::time_t time1 = std::mktime(&tm1);
			std::time_t time2 = std::mktime(&tm2);
			double diffSeconds = std::difftime(time2, time1);
			int diffDays = static_cast<int>(diffSeconds / (24 * 60 * 60));

			if (diffDays >= MAX_DAYS) {
				return false; // Демо истек по времени
			}
		}
	}

	return true; // Демо активен
}

// =============================================================================
// Демо-режим: обновить данные демо (увеличить счетчик запусков, обновить дату)
// =============================================================================

void LicenseManager::UpdateDemoData()
{
	DemoData demoData;
	GS::UniString currentDate = GetCurrentDate();

	// Читаем текущие данные
	GS::UniString demoPath = GetDemoFilePath();
	if (demoPath.IsEmpty()) {
		return;
	}

#ifdef GS_WIN
	FILE* fp = _wfopen(demoPath.ToUStr().Get(), L"r");
#else
	FILE* fp = fopen(demoPath.ToCStr().Get(), "r");
#endif

	if (fp != nullptr) {
		char line[256];
		demoData.launchCount = 0;
		demoData.firstLaunchDate = GS::UniString();
		demoData.lastLaunchDate = GS::UniString();

		while (fgets(line, sizeof(line), fp) != nullptr) {
			GS::UniString uniLine(line);
			GS::UniString key, value;
			if (ParseLicenseLine(uniLine, key, value)) {
				if (key == GS::UniString("FIRST_LAUNCH_DATE")) {
					demoData.firstLaunchDate = value;
				} else if (key == GS::UniString("LAST_LAUNCH_DATE")) {
					demoData.lastLaunchDate = value;
				} else if (key == GS::UniString("LAUNCH_COUNT")) {
					GS::UniString countStr = value;
					demoData.launchCount = 0;
					for (UIndex i = 0; i < countStr.GetLength(); ++i) {
						GS::UniString ch = countStr.GetSubstring(i, 1);
						if (ch >= GS::UniString("0") && ch <= GS::UniString("9")) {
							GS::UniString chStr = ch.GetSubstring(0, 1);
							demoData.launchCount = demoData.launchCount * 10 + (chStr.ToCStr().Get()[0] - '0');
						}
					}
				}
			}
		}
		fclose(fp);
	}

	// Если это первый запуск, устанавливаем дату первого запуска
	if (demoData.firstLaunchDate.IsEmpty()) {
		demoData.firstLaunchDate = currentDate;
	}

	// Увеличиваем счетчик запусков
	demoData.launchCount++;
	demoData.lastLaunchDate = currentDate;

	// Записываем обновленные данные
#ifdef GS_WIN
	fp = _wfopen(demoPath.ToUStr().Get(), L"w");
#else
	fp = fopen(demoPath.ToCStr().Get(), "w");
#endif

	if (fp != nullptr) {
		// Формируем строки для записи
		char line[256];
#ifdef GS_WIN
		sprintf_s(line, sizeof(line), "FIRST_LAUNCH_DATE=%s\n", demoData.firstLaunchDate.ToCStr().Get());
		fputs(line, fp);
		sprintf_s(line, sizeof(line), "LAST_LAUNCH_DATE=%s\n", demoData.lastLaunchDate.ToCStr().Get());
		fputs(line, fp);
		sprintf_s(line, sizeof(line), "LAUNCH_COUNT=%d\n", demoData.launchCount);
		fputs(line, fp);
#else
		snprintf(line, sizeof(line), "FIRST_LAUNCH_DATE=%s\n", demoData.firstLaunchDate.ToCStr().Get());
		fputs(line, fp);
		snprintf(line, sizeof(line), "LAST_LAUNCH_DATE=%s\n", demoData.lastLaunchDate.ToCStr().Get());
		fputs(line, fp);
		snprintf(line, sizeof(line), "LAUNCH_COUNT=%d\n", demoData.launchCount);
		fputs(line, fp);
#endif
		fclose(fp);
	}
}

