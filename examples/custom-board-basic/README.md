# Custom board

This example compiles with a normal C++11 compiler and has no Arduino, ESP, Wi-Fi, or MQTT dependency. Implement `flova::Link`, `flova::Storage`, `flova::Clock`, and `flova::Logger` using your board or PLC SDK, then construct `flova::Device`.

The link exchanges structured bounded `flova::Message` values. An MQTT, BLE, LoRaWAN, serial, CAN, fieldbus, or gateway adapter owns its wire encoding and credential setup.

Build from the repository root with CMake or the documented direct compiler command.
