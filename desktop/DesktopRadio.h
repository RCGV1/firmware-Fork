#pragma once
#include <string>

// Local official-protobuf fixture; no hardware, network or native cryptography.
void desktopRadioInit();
void desktopRadioLoop();
std::string desktopSimulationState();
bool desktopSimulationScenario(const char *name);
