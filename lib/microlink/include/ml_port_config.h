/**
 * @file ml_port_config.h
 * @brief PlatformIO / Arduino-ESP32 Port Configuration for MicroLink
 */

#pragma once

#ifndef WIREGUARD_CRYPTO_REFC
#define WIREGUARD_CRYPTO_REFC 1
#endif

/* Memory & Peer limits */
#ifndef CONFIG_ML_MAX_PEERS
#define CONFIG_ML_MAX_PEERS 16
#endif

#ifndef CONFIG_ML_NVS_MAX_PEERS
#define CONFIG_ML_NVS_MAX_PEERS 64
#endif

/* Large tailnet buffer sizes (allocated in PSRAM) */
#ifndef CONFIG_ML_H2_BUFFER_SIZE_KB
#define CONFIG_ML_H2_BUFFER_SIZE_KB 512
#endif

#ifndef CONFIG_ML_JSON_BUFFER_SIZE_KB
#define CONFIG_ML_JSON_BUFFER_SIZE_KB 512
#endif

#ifndef CONFIG_ML_DEVICE_NAME
#define CONFIG_ML_DEVICE_NAME "snapmatrix-cyd"
#endif

#ifndef CONFIG_ML_PRIORITY_PEER_IP
#define CONFIG_ML_PRIORITY_PEER_IP ""
#endif

/* Cellular modem is disabled on SnapMatrix WiFi boards */
#undef CONFIG_ML_ENABLE_CELLULAR

/* MicroLink built-in HTTP server is disabled (SnapMatrix has its own WebUI) */
#undef CONFIG_ML_ENABLE_CONFIG_HTTPD

/* Zero-copy mode disabled (use BSD socket mode for rock-solid stability) */
#undef CONFIG_ML_ZERO_COPY_WG
