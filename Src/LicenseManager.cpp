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
	HMODULE hModule = ACAPI_GetOwnResModule();
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
	UIndex equalPos = trimmed.Find('=');
	if (equalPos == MaxUIndex) {
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
	for (UIndex i = 0; i < dateStr.GetLength(); ++i) {
		if (i != 4 && i != 7) {
			if (dateStr[i] < '0' || dateStr[i] > '9') {
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

