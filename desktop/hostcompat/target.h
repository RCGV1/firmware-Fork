#pragma once
#include "WadaPlatform.h"
#include "WebBoard.h"
#include "WebDisplay.h"
#include "WebFiles.h"
#include "WebLocation.h"
#include "WebRadio.h"
#include "helpers/ClockFloorRTC.h"
#include <Mesh.h>
#include <helpers/SensorManager.h>
extern WebBoard board;
extern WebRadio radio_driver;
extern ClockFloorRTC rtc_clock;
extern WebSensorManager sensors;
extern WebDisplay display;
class SPIClass;
SPIClass *tdeckSharedSPI();
