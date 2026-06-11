/*
 * mqtt_kincony.c
 *
 * Driver para comunicação MQTT do firmware Kincony.
 * Conecta em broker Mosquitto e permite publicar/subscrever tópicos.
 *
 * Criado em: 2024-06-01
 * Autor: DANIEL
 * Versão: 1.0
 */
 
/*------------------------ EXEMPLO DE USO NO MAIN.C ------------------------
INCLUINDO O CABEÇALHO
#include "mqtt_kincony.h"

void app_main(void)
{
-------------------------- INICIANDO WIFI PRIMEIRO ---------------------------

    ESP_ERROR_CHECK(Wifi_Kincony_Init("NOME_DA_REDE", "SENHA_DA_REDE"));

-------------------------- INICIANDO MQTT ---------------------------

    ESP_ERROR_CHECK(Mqtt_Kincony_Init(MQTT_KINCONY_BROKER_URI));

    while (1)
    {
-------------------------- PUBLICANDO DADOS MQTT ---------------------------

        if (Mqtt_Kincony_IsConectado())
        {
            Mqtt_Kincony_PublicarStatus("online");

            Mqtt_Kincony_PublicarEntradas(Entradas_Kincony_GetEstadoTratado());

            Mqtt_Kincony_PublicarSaidas(Saidas_Kincony_GetEstado());
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}*/

#include "mqtt_kincony.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

#define TAG "MQTT_KINCONY"

static esp_mqtt_client_handle_t mqtt_client = NULL;

static bool mqtt_conectado = false;

static void Mqtt_Kincony_EventHandler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            mqtt_conectado = true;

            ESP_LOGI(TAG, "MQTT conectado");

            esp_mqtt_client_publish(
                mqtt_client,
                MQTT_TOPIC_STATUS,
                "online",
                0,
                1,
                1
            );

            esp_mqtt_client_subscribe(
                mqtt_client,
                MQTT_TOPIC_COMANDO_SAIDA,
                1
            );

            ESP_LOGI(TAG, "Inscrito no topico: %s", MQTT_TOPIC_COMANDO_SAIDA);
            break;

        case MQTT_EVENT_DISCONNECTED:
            mqtt_conectado = false;
            ESP_LOGW(TAG, "MQTT desconectado");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Mensagem recebida");
            ESP_LOGI(TAG, "Topico: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Dados: %.*s", event->data_len, event->data);

            /*
                EXEMPLO DE COMANDO RECEBIDO:

                Topico:
                kincony/comando/saida

                Payload:
                1=ON
                1=OFF
                2=ON
                2=OFF

                Aqui depois você pode tratar e chamar:
                Saidas_Kincony_Ligar(SAIDA_1);
                Saidas_Kincony_Desligar(SAIDA_1);
            */
            break;

        case MQTT_EVENT_ERROR:
            mqtt_conectado = false;
            ESP_LOGE(TAG, "Erro MQTT");
            break;

        default:
            break;
    }
}

esp_err_t Mqtt_Kincony_Init(const char *broker_uri)
{
    if (broker_uri == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = broker_uri,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_config);

    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "Erro ao criar cliente MQTT");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        Mqtt_Kincony_EventHandler,
        NULL
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao registrar evento MQTT");
        return ret;
    }

    ret = esp_mqtt_client_start(mqtt_client);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro ao iniciar MQTT");
        return ret;
    }

    ESP_LOGI(TAG, "MQTT iniciado no broker: %s", broker_uri);

    return ESP_OK;
}

esp_err_t Mqtt_Kincony_Publicar(const char *topico, const char *mensagem)
{
    if (mqtt_client == NULL || topico == NULL || mensagem == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mqtt_conectado)
    {
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        topico,
        mensagem,
        0,
        1,
        0
    );

    if (msg_id < 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t Mqtt_Kincony_PublicarStatus(const char *mensagem)
{
    return Mqtt_Kincony_Publicar(MQTT_TOPIC_STATUS, mensagem);
}

esp_err_t Mqtt_Kincony_PublicarEntradas(uint8_t entradas)
{
    char payload[16];

    snprintf(payload, sizeof(payload), "%u", entradas);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_ENTRADAS, payload);
}

esp_err_t Mqtt_Kincony_PublicarSaidas(uint8_t saidas)
{
    char payload[16];

    snprintf(payload, sizeof(payload), "%u", saidas);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_SAIDAS, payload);
}

bool Mqtt_Kincony_IsConectado(void)
{
    return mqtt_conectado;
}