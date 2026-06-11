#ifndef WIFI_KINCONY_H
#define WIFI_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define WIFI_KINCONY_TENTATIVAS_MAX     10

esp_err_t Wifi_Kincony_Init(const char *ssid, const char *senha);

bool Wifi_Kincony_IsConectado(void);
bool Wifi_Kincony_IsFalha(void);

uint8_t Wifi_Kincony_GetTentativas(void);

esp_err_t Wifi_Kincony_Reconectar(void);
esp_err_t Wifi_Kincony_Desconectar(void);

#endif