#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#define I2C_MASTER_SCL_IO      15
#define I2C_MASTER_SDA_IO      4
#define I2C_MASTER_FREQ_HZ      100000

#define PCF8574_ADDR            0x24

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t pcf8574 = NULL;

static void pcf8574_write(uint8_t data)
{
    i2c_master_transmit(pcf8574, &data, 1, 100 / portTICK_PERIOD_MS);
}

void app_main(void)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8574_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_config, &pcf8574));

    while (1)
    {
        for (uint8_t i = 0; i < 6; i++)
        {
            uint8_t saida = ~(1 << i);  
            pcf8574_write(saida);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        pcf8574_write(0xFF);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}