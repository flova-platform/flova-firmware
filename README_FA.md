<div dir="rtl" align="center">
  <h1>FlovaSDK</h1>
  <p><strong>ساخت دستگاه‌های متصل مبتنی بر ESP32 و ESP8266 با پلتفرم فلووا</strong></p>
  <p>
    <a href="README.md">English</a> ·
    <a href="https://docs.flova.ir">مستندات</a> ·
    <a href="examples">نمونه‌ها</a> ·
    <a href="https://github.com/flova-platform/flova-firmware/releases">نسخه‌ها</a>
  </p>
  <p>
    <a href="https://github.com/flova-platform/flova-firmware/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/flova-platform/flova-firmware?sort=semver&amp;style=flat-square"></a>
    <a href="https://registry.platformio.org/libraries/flova-platform/FlovaSDK"><img alt="PlatformIO Registry" src="https://badges.registry.platformio.org/packages/flova-platform/library/FlovaSDK.svg"></a>
    <a href="https://github.com/arduino/library-registry/pull/9043"><img alt="Arduino Library Manager" src="https://img.shields.io/badge/Arduino%20Library%20Manager-FlovaSDK-00878F?style=flat-square&amp;logo=arduino&amp;logoColor=white"></a>
    <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/github/license/flova-platform/flova-firmware?style=flat-square"></a>
  </p>
</div>

FlovaSDK کیت توسعه رسمی C++ برای اتصال دستگاه‌ها به پلتفرم فلووا است. این SDK
برای ESP32 و ESP8266 یکپارچه‌سازی آماده دارد و برای سخت‌افزارهای سفارشی نیز یک
هسته قابل‌حمل C++11 ارائه می‌کند.

> راهنمای کامل راه‌اندازی، دیتاستریم‌ها، پیکربندی دستگاه، OTA و API در
> **[docs.flova.ir](https://docs.flova.ir)** در دسترس است.

## نصب

### PlatformIO

برای ESP32:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = flova-platform/FlovaSDK@^0.2.0
```

برای ESP8266 تنظیم محدودشده BearSSL نیز لازم است:

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
build_flags = -DPIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED
lib_deps = flova-platform/FlovaSDK@^0.2.0
extra_scripts = pre:$PROJECT_LIBDEPS_DIR/${PIOENV}/FlovaSDK/scripts/patch_esp8266_bearssl_nonblocking.py
```

### Arduino IDE

Arduino Library Manager در حال حاضر از ESP32 پشتیبانی می‌کند:

1. از منوی **Tools → Manage Libraries** وارد مدیریت کتابخانه‌ها شوید.
2. عبارت **FlovaSDK** را جست‌وجو کنید.
3. گزینه **Install** را انتخاب کنید.

سپس ورودی ESP32 را به برنامه اضافه کنید:

```cpp
#include <FlovaEsp32.h>
```

برای پروژه‌های ESP8266 از PlatformIO استفاده کنید.

## شروع سریع

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <FlovaEsp32.h>

FlovaEsp32 flovaDevice;
auto relay = flovaDevice.datastream<bool>("relay");

void setup() {
  WiFi.begin("your-wifi", "your-password");
  pinMode(2, OUTPUT);

  relay.onWrite([](bool enabled) {
    digitalWrite(2, enabled ? HIGH : LOW);
  });

  flovaDevice.begin();
}

void loop() {
  flovaDevice.run();
}
```

برای ESP8266 از `<ESP8266WiFi.h>` و `<FlovaEsp8266.h>` استفاده کنید. برای
راه‌اندازی و استفاده در محیط عملیاتی، [نمونه‌ها](examples) را ببینید یا
[مستندات فلووا](https://docs.flova.ir) را دنبال کنید.

## پیوندها

- [مستندات](https://docs.flova.ir)
- [رجیستری PlatformIO](https://registry.platformio.org/libraries/flova-platform/FlovaSDK)
- [نمونه‌ها](examples)
- [نسخه‌های منتشرشده](https://github.com/flova-platform/flova-firmware/releases)
- [گزارش مشکل](https://github.com/flova-platform/flova-firmware/issues)

## مجوز

FlovaSDK تحت [مجوز MIT](LICENSE) منتشر شده است.
