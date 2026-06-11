#ifndef SAIDAS_DIGITAIS_KINCONY_H
#define SAIDAS_DIGITAIS_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define SAIDAS_KINCONY_ADDR        0x24
#define SAIDAS_KINCONY_QTD         8

typedef enum
{
    SAIDA_1 = 0,
    SAIDA_2,
    SAIDA_3,
    SAIDA_4,
    SAIDA_5,
    SAIDA_6,
    SAIDA_7,
    SAIDA_8
} saida_kincony_t;

esp_err_t Saidas_Kincony_Init(i2c_master_bus_handle_t i2c_bus);

esp_err_t Saidas_Kincony_Set(saida_kincony_t saida, bool estado);
esp_err_t Saidas_Kincony_Ligar(saida_kincony_t saida);
esp_err_t Saidas_Kincony_Desligar(saida_kincony_t saida);
esp_err_t Saidas_Kincony_Toggle(saida_kincony_t saida);

esp_err_t Saidas_Kincony_DesligarTodas(void);
esp_err_t Saidas_Kincony_EscreverRaw(uint8_t valor);

bool Saidas_Kincony_Get(saida_kincony_t saida);
uint8_t Saidas_Kincony_GetEstado(void);
uint8_t Saidas_Kincony_GetRaw(void);

bool Saidas_Kincony_IsOnline(void);

#endif