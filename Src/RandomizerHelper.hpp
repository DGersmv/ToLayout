#ifndef RANDOMIZERHELPER_HPP
#define RANDOMIZERHELPER_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "GSRoot.hpp"

namespace RandomizerHelper {

	// Рандомизировать ширину (A) выбранных элементов на указанный процент
	// percent: процент отклонения (0-100), например 25 означает ±25% от исходного значения
	bool RandomizeWidth(double percent);

	// Рандомизировать длину (B) выбранных элементов на указанный процент
	bool RandomizeLength(double percent);

	// Рандомизировать высоту (C) выбранных элементов на указанный процент
	bool RandomizeHeight(double percent);

	// Рандомизировать углы выбранных элементов (0-360 градусов)
	// Эта функция уже есть в RotateHelper, но мы можем её переиспользовать
	bool RandomizeAngles();

}

#endif // RANDOMIZERHELPER_HPP

