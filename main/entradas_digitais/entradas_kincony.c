/*
 * entradas_kincony.c
 *
 * Driver para leitura das entradas digitais do módulo de relés Kincony.
 * Utiliza o PCF8574 para ler o estado das entradas via I2C.
 *
 * Criado em: 2024-06-01
 * Autor: DANIEL 
 * Versão: 1.0
 */

/*------------------------ EXEMPLO DE USO NO MAIN.C ------------------------
INCLUINDO O CABEÇALHO
#include "entradas_kincony.h"

static i2c_master_bus_handle_t i2c_bus = NULL;

void app_main(void)
{   

-------------------------- INICIANDO O BUS I2C --------------------------- 
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = 22,
        .sda_io_num = 21,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ---------------------------- INICIANDO O MÓDULO DE ENTRADAS ----------------------
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_ERROR_CHECK(Entradas_Kincony_Init(i2c_bus));

    while (1)
    {
    ------------------------------ ATUALIZANDO E LENDO AS ENTRADAS ----------------------
        Entradas_Kincony_Atualizar();

        if (Entradas_Kincony_Get(ENTRADA_1))
        {
            printf("Entrada 1 acionada\n");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}*/

#include "entradas_kincony.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ENTRADAS_KINCONY"

static i2c_master_dev_handle_t entradas_dev = NULL;

static uint8_t entradas_raw = 0xFF;
static uint8_t entradas_tratadas = 0x00;
static bool entradas_online = false;

esp_err_t Entradas_Kincony_Init(i2c_master_bus_handle_t i2c_bus)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ENTRADAS_KINCONY_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &entradas_dev);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "PCF8574 entradas iniciado no endereco 0x%02X", ENTRADAS_KINCONY_ADDR);
        entradas_online = true;
    }
    else
    {
        ESP_LOGE(TAG, "Erro ao iniciar PCF8574 entradas");
        entradas_online = false;
    }

    return ret;
}

esp_err_t Entradas_Kincony_Atualizar(void)
{
    if (entradas_dev == NULL)
    {
        entradas_online = false;
        return ESP_FAIL;
    }

    uint8_t valor_lido = 0xFF;

    esp_err_t ret = i2c_master_receive(
        entradas_dev,
        &valor_lido,
        1,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK)
    {
        entradas_online = false;
        ESP_LOGW(TAG, "Falha ao ler entradas");
        return ret;
    }

    entradas_raw = valor_lido;

    /*
        PCF8574:
        Entrada normal aberta/pull-up = 1
        Entrada acionada por negativo = 0

        Por isso inverte:
        bit 1 = entrada acionada
        bit 0 = entrada desligada
    */
    entradas_tratadas = ~entradas_raw;

    entradas_online = true;

    return ESP_OK;
}

bool Entradas_Kincony_Get(entrada_kincony_t entrada)
{
    if (entrada >= ENTRADAS_KINCONY_QTD)
    {
        return false;
    }

    return (entradas_tratadas & (1 << entrada)) != 0;
}

uint8_t Entradas_Kincony_GetRaw(void)
{
    return entradas_raw;
}

uint8_t Entradas_Kincony_GetEstadoTratado(void)
{
    return entradas_tratadas;
}

bool Entradas_Kincony_IsOnline(void)
{
    return entradas_online;
}