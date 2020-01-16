// Copyright (c) Janghun LEE. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full
// license information.

#include "firmware.h"

#define BUFFSIZE 1024
#define URL_SIZE 1024
#define PEM_SIZE 4096

static const char* TAG = "firmware";
static Firmware firmware;
static char ota_write_data[BUFFSIZE + 1] = {0};

static esp_err_t validate_image_header(esp_app_desc_t* new_app_info) {
  if (new_app_info == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* last_invalid_app =
      esp_ota_get_last_invalid_partition();

  esp_app_desc_t running_app_info;
  esp_app_desc_t invalid_app_info;
  ESP_LOGI(TAG, "New firmware version: %s", new_app_info->version);

  if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
    ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
  }
  if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) ==
      ESP_OK) {
    ESP_LOGI(TAG, "Last invalid firmware version: %s",
             invalid_app_info.version);
  }
  if (last_invalid_app != NULL) {
    if (memcmp(invalid_app_info.version, new_app_info->version,
               sizeof(new_app_info->version)) == 0) {
      ESP_LOGW(TAG, "New version is the same as invalid version.");
      ESP_LOGW(TAG,
               "Previously, there was an attempt to launch the firmware with "
               "%s version, but it failed.",
               invalid_app_info.version);
      ESP_LOGW(TAG,
               "The firmware has been rolled back to the previous version.");
      return ESP_FAIL;
    }
  }
  if (memcmp(new_app_info->version, running_app_info.version,
             sizeof(new_app_info->version)) == 0) {
    ESP_LOGW(TAG,
             "Current running version is the same as a new. We will not "
             "continue the update.");
    firmware.is_latest_version = true;
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t firmware_init(void) {
  firmware.pem = NULL;
  firmware.url = NULL;
  firmware.is_latest_version = false;
  return ESP_OK;
}

esp_err_t firmware_set_cert_pem(char* pem) {
  if (pem == NULL) return ESP_FAIL;
  size_t length = strlen(pem) + 1;
  if (firmware.pem != NULL) {
    free(firmware.pem);
  }
  firmware.pem = (char*)malloc(sizeof(char) * (length));
  strncpy(firmware.pem, pem, length);
  firmware.is_latest_version = false;
  ESP_LOGI(TAG, "Setting firmware pem : %s", firmware.pem);
  return ESP_OK;
}
esp_err_t firmware_set_url(char* url) {
  if (url == NULL) return ESP_FAIL;
  size_t length = strlen(url) + 1;
  if (firmware.url != NULL) {
    free(firmware.url);
  }
  firmware.url = (char*)malloc(sizeof(char) * (length));
  strncpy(firmware.url, url, length);
  ESP_LOGI(TAG, "Setting firmware url : %s %d", firmware.url, strlen(url));
  firmware.is_latest_version = false;
  return ESP_OK;
}

void firmware_parse_from_json(const char* json,
                              DEVICE_TWIN_STATE update_state) {
  JSON_Value* root_value = json_parse_string(json);
  JSON_Object* root_object = json_value_get_object(root_value);
  char name[50];
  if (update_state == UPDATE_PARTIAL) {
    sprintf(name, "firmware");
  } else if (update_state == UPDATE_COMPLETE) {
    sprintf(name, "desired.firmware");
  }
  JSON_Object* json_object_firmware =
      json_object_dotget_object(root_object, name);
  if (json_object_get_value(json_object_firmware, "url") != NULL) {
    char* url = (char*)json_object_get_string(json_object_firmware, "url");
    firmware_set_url(url);
  }
  if (json_object_get_value(json_object_firmware, "pem") != NULL) {
    char* pem = (char*)json_object_get_string(json_object_firmware, "pem");
    firmware_set_cert_pem(pem);
  }
  json_value_free(root_value);
}
void firmware_update(void) {
  esp_err_t err;
  esp_ota_handle_t update_handle = 0;
  const esp_partition_t* update_partition = NULL;
  esp_http_client_handle_t client = NULL;
  if (firmware.url == NULL || firmware.pem == NULL) {
    ESP_LOGE(TAG, "firmware url or cert pem is null");
    goto OTA_FAIL;
  }
  esp_http_client_config_t config = {
      .url = firmware.url,
      .cert_pem = firmware.pem,
  };
  client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialise HTTP connection");
    goto OTA_FAIL;
  }
  err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    goto OTA_FAIL;
  }
  esp_http_client_fetch_headers(client);
  update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
           update_partition->subtype, update_partition->address);
  assert(update_partition != NULL);

  int binary_file_length = 0;
  /*deal with all receive packet*/
  bool image_header_was_checked = false;
  while (1) {
    int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
    if (data_read < 0) {
      ESP_LOGE(TAG, "Error: SSL data read error");
      goto OTA_FAIL;
    } else if (data_read > 0) {
      if (image_header_was_checked == false) {
        esp_app_desc_t new_app_info;
        if (data_read > sizeof(esp_image_header_t) +
                            sizeof(esp_image_segment_header_t) +
                            sizeof(esp_app_desc_t)) {
          memcpy(&new_app_info,
                 &ota_write_data[sizeof(esp_image_header_t) +
                                 sizeof(esp_image_segment_header_t)],
                 sizeof(esp_app_desc_t));
          err = validate_image_header(&new_app_info);
          if (err != ESP_OK) {
            goto OTA_FAIL;
          };
          image_header_was_checked = true;
          err =
              esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            goto OTA_FAIL;
          }
          ESP_LOGI(TAG, "esp_ota_begin succeeded");
        }
      }
      err =
          esp_ota_write(update_handle, (const void*)ota_write_data, data_read);
      if (err != ESP_OK) {
        goto OTA_FAIL;
      }
      binary_file_length += data_read;
      ESP_LOGD(TAG, "Written image length %d", binary_file_length);
    } else if (data_read == 0) {
      ESP_LOGI(TAG, "Connection closed,all data received");
      break;
    }
  }
  ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);
  if (esp_ota_end(update_handle) != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed!");
    goto OTA_FAIL;
  }
  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!",
             esp_err_to_name(err));
    goto OTA_FAIL;
  }
  ESP_LOGI(TAG, "Prepare to restart system!");
  esp_restart();

OTA_FAIL:
  if (client != NULL) {
    esp_http_client_cleanup(client);
  }
  ESP_LOGE(TAG, "Failed firmware update");
}

bool firmware_is_latest_version(void) {
  if (firmware.is_latest_version) {
    ESP_LOGI(TAG, "Firmware version is up to date");
  } else {
    ESP_LOGI(TAG, "Firmware update is required");
  }
  return firmware.is_latest_version;
}