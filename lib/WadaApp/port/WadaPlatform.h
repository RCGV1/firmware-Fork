#pragma once
#include <stdint.h>
void wadaSpiLock();
void wadaSpiUnlock();
uint16_t wadaBatteryMillivolts();
void wadaInputActivity();
uint32_t wadaRtcNow();
void wadaSetRtc(uint32_t);

void wadaInitPacketApi();

bool wadaPhoneConnected();
void wadaPollUiActivity();
