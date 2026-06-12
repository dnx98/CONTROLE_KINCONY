#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_rtu_slave_esp.h"
#include "mqtt_kincony.h"
#include "wifi_kincony.h"
#include "ota_github.h"
#include "logica_controle.h"

void app_main(void)
{ 

    ESP_ERROR_CHECK(Entradas_Kincony_Iniciar());
        ESP_ERROR_CHECK(Saidas_Kincony_Iniciar());

        
    Logica_Controle_Iniciar();
       
    ESP_ERROR_CHECK(Wifi_Kincony_Init("iPhone de Daniel", "12345679"));

    Mqtt_Kincony_Init(MQTT_KINCONY_BROKER_URI);

    uint8_t umavez = 1;

    while (1)
    {
     if(umavez)
        {
            if (Wifi_Kincony_IsConectado())
        {
            ota_github_check_update();
            umavez = 0;
         } else
        {
            umavez = 1;
         }
        }

    Entradas_Kincony_Processar();
    Logica_Controle_Processar();
    Mqtt_Kincony_Processar();       

        vTaskDelay(pdMS_TO_TICKS(1000));

     }   
    }
