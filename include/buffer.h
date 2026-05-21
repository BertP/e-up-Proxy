#ifndef BUFFER_H
#define BUFFER_H

#include <Arduino.h>

// Struct representing a single parsed telemetry payload
struct TelemetryData {
    float soc;
    float volt;
    float temp;
    float range;
    float power;
    float odo;
    float service_days;
    float service_km;
    float bat_cap;
    float tp_alarm;
    uint32_t ts; // Unix timestamp
    String src;
};

// Initialize the buffer queue system (e.g. check/create the /queue directory)
void initBuffer();

// Push a telemetry payload onto the queue (writes a JSON file)
bool enqueueData(const TelemetryData& data);

// Check if there are items in the queue and load the next one (FIFO)
// Populates filepath and data struct. Returns true if an item is found.
bool getNextQueuedFile(String& filepath, TelemetryData& data);

// Remove the successfully processed queued file from LittleFS
void removeQueuedFile(const String& filepath);

// Get the current total number of items in the queue
size_t getQueueSize();

// Delete all items in the queue
void clearQueue();

#endif // BUFFER_H
