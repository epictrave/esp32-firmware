// Copyright (c) Janghun LEE. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full
// license information.

#ifndef FIRMWARE_H
#define FIRMWARE_H

#ifdef __cplusplus
extern "C" {
#endif
#include "stdlib.h"
#include "string.h"

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

#include "device_twin_state.h"

#include "parson.h"

typedef struct FIRMWARE_TAG {
  char* url;
  char* pem;
  bool is_latest_version;
} Firmware;
esp_err_t firmware_init(void);
esp_err_t firmware_set_cert_pem(char* pem);
esp_err_t firmware_set_url(char* url);
void firmware_parse_from_json(const char* json, DEVICE_TWIN_STATE update_state);
void firmware_update(void);
bool firmware_is_latest_version(void);
#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_H */