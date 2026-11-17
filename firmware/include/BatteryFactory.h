#pragma once
#include "BatterySensor.h"

// Factory implemented by platform-specific source unit per build
IBatterySensor* makeBatterySensor();
