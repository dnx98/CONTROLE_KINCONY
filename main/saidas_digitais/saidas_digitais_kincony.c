/*
 * saidas_digitais_kincony.c
 *
 * Driver para acionamento das saídas digitais do módulo de relés Kincony.
 * Utiliza o PCF8574 para escrever o estado das saídas via I2C.
 *
 * Criado em: 2024-06-01
 * Autor: DANIEL
 * Versão: 1.0
 */

/*------------------------ EXEMPLO DE USO NO MAIN.C ------------------------
INCLUINDO O CABEÇALHO
#include "saidas_digitais_kincony.h"

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

---------------------------- INICIANDO O MÓDULO DE SAÍDAS ----------------------
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_ERROR_CHECK(Saidas_Kincony_Init(i2c_bus));

    while (1)
    {
------------------------------ ACIONANDO SAÍDAS ----------------------
        Saidas_Kincony_Ligar(SAIDA_1);

        vTaskDelay(pdMS_TO_TICKS(1000));

        Saidas_Kincony_Desligar(SAIDA_1);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}*/

#include "saidas_digitais_kincony.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "SAIDAS_KINCONY"

static i2c_master_dev_handle_t saidas_dev = NULL;

static uint8_t saidas_estado = 0x00;
static uint8_t saidas_raw = 0xFF;
static bool saidas_online = false;

static esp_err_t Saidas_Kincony_Enviar(void)
{
    if (saidas_dev == NULL)
    {
        saidas_online = false;
        return ESP_FAIL;
    }

    /*
        PCF8574:
        Para este módulo, considerando saída acionada em nível baixo:

        bit 0 no PCF8574 = saída ligada
        bit 1 no PCF8574 = saída desligada

        No firmware:
        saidas_estado bit 1 = saída ligada
        saidas_estado bit 0 = saída desligada

        Por isso inverte antes de enviar:
    */
    saidas_raw = ~saidas_estado;

    esp_err_t ret = i2c_master_transmit(
        saidas_dev,
        &saidas_raw,
        1,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK)
    {
        saidas_online = false;
        ESP_LOGW(TAG, "Falha ao escrever saidas");
        return ret;
    }

    saidas_online = true;

    return ESP_OK;
}

esp_err_t Saidas_Kincony_Init(i2c_master_bus_handle_t i2c_bus)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SAIDAS_KINCONY_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &saidas_dev);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "PCF8574 saidas iniciado no endereco 0x%02X", SAIDAS_KINCONY_ADDR);
        saidas_online = true;

        return Saidas_Kincony_DesligarTodas();
    }
    else
    {
        ESP_LOGE(TAG, "Erro ao iniciar PCF8574 saidas");
        saidas_online = false;
    }

    return ret;
}

esp_err_t Saidas_Kincony_Set(saida_kincony_t saida, bool estado)
{
    if (saida >= SAIDAS_KINCONY_QTD)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (estado)
    {
        saidas_estado |= (1 << saida);
    }
    else
    {
        saidas_estado &= ~(1 << saida);
    }

    return Saidas_Kincony_Enviar();
}

esp_err_t Saidas_Kincony_Ligar(saida_kincony_t saida)
{
    return Saidas_Kincony_Set(saida, true);
}

esp_err_t Saidas_Kincony_Desligar(saida_kincony_t saida)
{
    return Saidas_Kincony_Set(saida, false);
}

esp_err_t Saidas_Kincony_Toggle(saida_kincony_t saida)
{
    if (saida >= SAIDAS_KINCONY_QTD)
    {
        return ESP_ERR_INVALID_ARG;
    }

    saidas_estado ^= (1 << saida);

    return Saidas_Kincony_Enviar();
}

esp_err_t Saidas_Kincony_DesligarTodas(void)
{
    saidas_estado = 0x00;

    return Saidas_Kincony_Enviar();
}

esp_err_t Saidas_Kincony_EscreverRaw(uint8_t valor)
{
    /*
        Escreve o valor lógico tratado:
        bit 1 = saída ligada
        bit 0 = saída desligada
    */
    saidas_estado = valor;

    return Saidas_Kincony_Enviar();
}

bool Saidas_Kincony_Get(saida_kincony_t saida)
{
    if (saida >= SAIDAS_KINCONY_QTD)
    {
        return false;
    }

    return (saidas_estado & (1 << saida)) != 0;
}

uint8_t Saidas_Kincony_GetEstado(void)
{
    return saidas_estado;
}

uint8_t Saidas_Kincony_GetRaw(void)
{
    return saidas_raw;
}

bool Saidas_Kincony_IsOnline(void)
{
    return saidas_online;
}