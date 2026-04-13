#include "simulator.h"

#include <Arduino.h>

#include "app_state.h"

namespace sender
{

void updateSimulatedEnvironment()
{
    if (millis() - g_lastSensorAt < SENSOR_INTERVAL_MS)
    {
        return;
    }

    g_lastSensorAt = millis();

    g_temperature += random(-10, 11) / 10.0f;
    g_humidity += random(-20, 21) / 10.0f;

    g_temperature = constrain(g_temperature, 20.0f, 40.0f);
    g_humidity = constrain(g_humidity, 30.0f, 90.0f);

    g_latitude += random(-4, 5) / 100000.0;
    g_longitude += random(-4, 5) / 100000.0;
}

} // namespace sender
