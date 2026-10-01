# Linux device example

Install the package, link `Flova::Linux`, and provide a `flova::Link` for the
chosen network or gateway transport. The example uses `/var/lib/flova/device`
for durable runtime state and keeps ESP32/ESP8266 board composition separate.

The Linux OTA updater stages a verified release and hands activation to the
service supervisor. The running process does not replace its own executable.
