#pragma once

// Optional, build-time metadata embedded in release images. The Console uses
// this tag to identify a binary without asking operators for board internals.
// Custom applications can omit FLOVA_OTA_METADATA_ENABLED and use the
// advanced upload fallback instead.

#if defined(FLOVA_OTA_METADATA_ENABLED)
#ifndef FLOVA_FIRMWARE_TARGET
#error "FLOVA_FIRMWARE_TARGET is required when OTA metadata is enabled"
#endif
#ifndef FLOVA_OTA_BOOT_LAYOUT_VERSION
#error "FLOVA_OTA_BOOT_LAYOUT_VERSION is required when OTA metadata is enabled"
#endif

#define FLOVA_OTA_METADATA_KV(key, value) key "\0" value "\0"

// Keep this as a visible, bounded ASCII tag. It is deliberately independent
// of Device Link so the server can inspect the uploaded .bin before release.
static const char flovaFirmwareMetadata[] __attribute__((used)) =
    "flovainf\0"
    FLOVA_OTA_METADATA_KV("schema", "1")
    FLOVA_OTA_METADATA_KV("version", FLOVA_FIRMWARE_VERSION)
    FLOVA_OTA_METADATA_KV("target", FLOVA_FIRMWARE_TARGET)
    FLOVA_OTA_METADATA_KV("boot-layout", FLOVA_OTA_BOOT_LAYOUT_VERSION)
    FLOVA_OTA_METADATA_KV("ota-contract", "2")
    "\0";

// The pointer is assigned by FlovaClient::begin() so link-time garbage
// collection cannot discard the tag from the final firmware image.
static const char* volatile flovaFirmwareMetadataRuntimePointer = nullptr;

#undef FLOVA_OTA_METADATA_KV
#endif
