<div dir="rtl" align="center">
  <h1>FlovaSDK</h1>
  <p><strong>ساخت دستگاه‌های متصل مبتنی بر ESP32 و ESP8266 با پلتفرم فلووا</strong></p>
  <p>
    <a href="README.md">English</a> ·
    <a href="https://docs.flova.ir">مستندات</a> ·
    <a href="examples">نمونه‌ها</a> ·
    <a href="https://github.com/flova-platform/flova-firmware/tags">نسخه‌های SDK</a>
  </p>
  <p>
    <a href="https://github.com/flova-platform/flova-firmware/tags"><img alt="نسخه SDK" src="https://img.shields.io/github/v/tag/flova-platform/flova-firmware?filter=v*&amp;sort=semver&amp;style=flat-square&amp;label=SDK"></a>
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

در پروژه ESP32 در PlatformIO، این خط را اضافه کنید:

```ini
lib_deps = flova-platform/FlovaSDK@^0.3.2
```

برای ESP8266 از فایل آماده‌ی `extras/platformio/esp8266/platformio.ini`
استفاده کنید؛ این فایل چیدمان حافظه‌ی لازم برای heap در IRAM را فعال می‌کند:

```ini
lib_deps = flova-platform/FlovaSDK@^0.3.2
build_flags =
  -DPIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED
```

### Arduino IDE

نسخه 0.3.2 از ESP32 و ESP8266 در Arduino IDE پشتیبانی می‌کند:

1. از منوی **Tools → Manage Libraries** وارد مدیریت کتابخانه‌ها شوید.
2. عبارت **FlovaSDK** را جست‌وجو کنید.
3. گزینه **Install** را انتخاب کنید.

سپس ورودی ESP32 را به برنامه اضافه کنید:

```cpp
#include <FlovaEsp32.h>
```

برای ESP8266، هدر `<FlovaEsp8266.h>` را اضافه کنید و در مسیر
**Tools → MMU → 16KB cache + 48KB IRAM and 2nd Heap (shared)** این گزینه را
پیش از کامپایل انتخاب کنید.

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

## firmware یونیورسال از SDK

firmware یونیورسال را در پروژه خودتان build و upload کنید. ابزار انتخاب‌شده
تصویر کامل برد، شامل bootloader و جدول پارتیشن، را می‌سازد و نیازی به دریافت
فایل firmware آماده نیست.

برای ESP32، `FlovaSDK` را از Arduino Library Manager یا رجیستری PlatformIO
نصب کنید و از `<FlovaUniversalEsp32.h>` استفاده کنید:

```cpp
#include <Arduino.h>
#include <FlovaUniversalEsp32.h>

FlovaUniversalEsp32 device;

void setup() {
  Serial.begin(115200);
  device.begin();
}

void loop() { device.run(); }
```

از یک برد ESP32 استفاده کنید. Arduino IDE یا PlatformIO تصویر کامل را upload
می‌کند.

برای ESP8266، از هدر `<FlovaUniversalEsp8266.h>` در Arduino IDE یا PlatformIO استفاده کنید.
جزئیات زمان اتصال و حافظه موردنیاز در [راهنمای انتقال داده](packages/TRANSPORT.md) آمده است.

## پیوندها

- [مستندات](https://docs.flova.ir)
- [رجیستری PlatformIO](https://registry.platformio.org/libraries/flova-platform/FlovaSDK)
- [نمونه‌ها](examples)
- [نسخه‌های SDK](https://github.com/flova-platform/flova-firmware/tags)
- [گزارش مشکل](https://github.com/flova-platform/flova-firmware/issues)

## مجوز

FlovaSDK تحت [مجوز MIT](LICENSE) منتشر شده است.
