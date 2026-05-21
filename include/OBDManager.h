#ifndef OBD_MANAGER_H
#define OBD_MANAGER_H

#include <Arduino.h>
#include "buffer.h"

// Initialize the OBD2 TCP client connection
bool connectOBD();

// Disconnect from Wican OBD TCP server
void disconnectOBD();

// Periodic keep-alive task (Tester Present)
void runOBDKeepAlive();

// Query all Group A cyclic parameters (every 60s)
bool queryGroupA(TelemetryData& data);

// Query all Group B slow poll parameters (every 10min)
bool queryGroupB(TelemetryData& data);

// Run the full simulation fallback (in case Wican or OBD queries fail)
void generateSimulatedTelemetry(TelemetryData& data, bool slowGroup);

#endif // OBD_MANAGER_H
