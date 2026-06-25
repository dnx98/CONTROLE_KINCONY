/*
 * mqtt_kincony.c
 *
 * MQTT do Controle Kincony.
 *
 * Funcao deste modulo:
 * - conectar ao broker Mosquitto;
 * - receber comandos MQTT;
 * - encaminhar comandos para a maquina de estado em logica_controle.c;
 * - publicar monitoramento de entradas, saidas, estados e falhas.
 *
 * IMPORTANTE:
 * Este arquivo NAO aciona saidas diretamente.
 * Quem decide se pode ligar/desligar eh a logica_controle.
 */

#include "mqtt_kincony.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "logica_controle.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"

#include "cJSON.h"
#include "esp_timer.h"

#include "esp_system.h"
#include "ota_github.h"

#include "rtc_ds1307.h"

extern uint8_t versao_firmware_atual;

static void tratar_comando_cmd_json(const char *payload);

#define TAG "MQTT_KINCONY"

#define MQTT_QOS_COMANDO        1
#define MQTT_QOS_MONITORAMENTO  1
#define MQTT_RETAIN_STATUS      1
#define MQTT_RETAIN_MONITOR     0

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_conectado = false;
static TickType_t mqtt_tick_ultima_publicacao = 0;

static bool topico_igual(const esp_mqtt_event_handle_t event, const char *topico)
{
    size_t tam_topico = strlen(topico);

    if ((size_t)event->topic_len != tam_topico)
    {
        return false;
    }

    return (strncmp(event->topic, topico, event->topic_len) == 0);
}

static void copiar_evento_para_string(char *destino, size_t tamanho_destino, const char *origem, int tamanho_origem)
{
    if (destino == NULL || tamanho_destino == 0)
    {
        return;
    }

    size_t copiar = (tamanho_origem < (int)(tamanho_destino - 1)) ? (size_t)tamanho_origem : (tamanho_destino - 1);
    memcpy(destino, origem, copiar);
    destino[copiar] = '\0';
}

static void publicar_ack_comando(
    uint8_t grupo,
    const char *acao,
    esp_err_t ret
)
{
    char payload[256];

    snprintf(
        payload,
        sizeof(payload),
        "{"
            "\"type\":\"command_ack\","
            "\"group\":%u,"
            "\"action\":\"%s\","
            "\"result\":\"%s\","
            "\"remote\":%s"
        "}",
        grupo,
        acao,
        (ret == ESP_OK) ? "ok" : "blocked",
        Logica_Controle_IsRemoto() ? "true" : "false"
    );

    Mqtt_Kincony_Publicar(
        MQTT_TOPIC_STATE,
        payload
    );
}


static void montar_status_payload(char *buffer, size_t tamanho, const char *status)
{
    snprintf(buffer, tamanho,
             "{\"status\":\"%s\",\"fw\":\"%s\"}",
             status,
             FIRMWARE_VERSION);
}

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
        {
            mqtt_conectado = true;
            ESP_LOGI(TAG, "MQTT conectado ao Mosquitto");

            char status_payload[96];
            montar_status_payload(status_payload, sizeof(status_payload), "online");
            
            esp_mqtt_client_publish(
                mqtt_client,
                MQTT_TOPIC_STATUS,
                status_payload,
                0,
                MQTT_QOS_MONITORAMENTO,
                MQTT_RETAIN_STATUS
            );

            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_CMD, MQTT_QOS_COMANDO);
            ESP_LOGI(TAG, "Inscrito: %s", MQTT_TOPIC_CMD);

            RTC_DS1307_SolicitarSincronizacaoInternet();

            Mqtt_Kincony_PublicarMonitoramento();
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
        {
            mqtt_conectado = false;
            ESP_LOGW(TAG, "MQTT desconectado");
            break;
        }

case MQTT_EVENT_DATA:
{
    char topico[96];
    char payload[256];

    copiar_evento_para_string(
        topico,
        sizeof(topico),
        event->topic,
        event->topic_len
    );

    copiar_evento_para_string(
        payload,
        sizeof(payload),
        event->data,
        event->data_len
    );

    ESP_LOGI(TAG, "Topico: %s", topico);
    ESP_LOGI(TAG, "Payload: %s", payload);

    if (topico_igual(event, MQTT_TOPIC_CMD))
    {
        tratar_comando_cmd_json(payload);
    }
    else
    {
        ESP_LOGW(TAG, "Topico de comando nao tratado: %s", topico);
    }

    Mqtt_Kincony_PublicarMonitoramento();
    break;
}

        case MQTT_EVENT_ERROR:
        {
            mqtt_conectado = false;
            ESP_LOGE(TAG, "Erro MQTT");
            break;
        }

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
    .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,

    .credentials.username = "administrador",
    .credentials.authentication.password = "Administrador2026",

    .session.last_will.topic = MQTT_TOPIC_STATUS,
    .session.last_will.msg = "{\"status\":\"offline\",\"fw\":\"" FIRMWARE_VERSION "\"}",
    .session.last_will.msg_len = sizeof("{\"status\":\"offline\",\"fw\":\"" FIRMWARE_VERSION "\"}") - 1,
    .session.last_will.qos = MQTT_QOS_MONITORAMENTO,
    .session.last_will.retain = true,
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

void Mqtt_Kincony_Processar(void)
{
    if (!mqtt_conectado)
    {
        return;
    }

    TickType_t agora = xTaskGetTickCount();

    if ((agora - mqtt_tick_ultima_publicacao) >= pdMS_TO_TICKS(MQTT_PUBLICACAO_MONITORAMENTO_MS))
    {
        mqtt_tick_ultima_publicacao = agora;
        Mqtt_Kincony_PublicarMonitoramento();
    }
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
        MQTT_QOS_MONITORAMENTO,
        MQTT_RETAIN_MONITOR
    );

    if (msg_id < 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t Mqtt_Kincony_PublicarStatus(const char *status)
{
    char payload[96];

    montar_status_payload(payload, sizeof(payload), status);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_STATUS, payload);
}

esp_err_t Mqtt_Kincony_PublicarMonitoramento(void)
{
    if (!mqtt_conectado)
    {
        return ESP_FAIL;
    }

    char payload[768];
    int pos = 0;

    int64_t ts = esp_timer_get_time() / 1000000;

    pos += snprintf(payload + pos, sizeof(payload) - pos,
        "{"
        "\"id\":\"aerador-01\","
        "\"ts\":%lld,"
        "\"mode\":\"%s\","
        "\"online\":true,"
        "\"alarm\":%s,"
        "\"oxygen\":0,"
        "\"groups\":[",
        ts,
        Logica_Controle_IsRemoto() ? "remoto" : "local",
        Logica_Controle_IsFalhaGeral() ? "true" : "false"
    );

    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        bool out = Logica_Controle_GetComandoGrupo((logica_grupo_t)i);
        bool fb = Logica_Controle_GetFeedbackGrupo((logica_grupo_t)i);
        bool fault = Logica_Controle_GetFalhaGrupo((logica_grupo_t)i) != LOGICA_FALHA_NENHUMA;

        pos += snprintf(payload + pos, sizeof(payload) - pos,
            "%s{\"g\":%u,\"out\":%s,\"fb\":%s,\"fault\":%s,\"src\":\"manual\"}",
            (i == 0) ? "" : ",",
            i + 1,
            out ? "true" : "false",
            fb ? "true" : "false",
            fault ? "true" : "false"
        );
    }

    snprintf(payload + pos, sizeof(payload) - pos, "]}");

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_STATE, payload);
}

bool Mqtt_Kincony_IsConectado(void)
{
    return mqtt_conectado;
}
static void tratar_comando_cmd_json(const char *payload)
{
    cJSON *root = cJSON_Parse(payload);

    if (root == NULL)
    {
        ESP_LOGW(TAG, "JSON invalido: %s", payload);
        return;
    }

    cJSON *group = cJSON_GetObjectItem(root, "group");
    cJSON *action = cJSON_GetObjectItem(root, "action");
    cJSON *src = cJSON_GetObjectItem(root, "src");

    if (!cJSON_IsString(action))
    {
        ESP_LOGW(TAG, "Payload CMD sem action valida");
        cJSON_Delete(root);
        return;
    }

    if (src != NULL && cJSON_IsString(src))
    {
        ESP_LOGI(TAG, "Comando origem: %s", src->valuestring);

        
    }

/*
 * Primeiro oferece o JSON ao modulo RTC/timers.
 *
 * Se o comando for timer_set, timer_get, rtc_sync etc.,
 * o proprio rtc_ds1307 trata e retorna uma resposta JSON.
 *
 * Se nao for um comando de RTC/timer, retorna
 * ESP_ERR_NOT_SUPPORTED e o MQTT continua tratando
 * reset, OTA e comandos manuais.
 */
char resposta_rtc[768] = {0};

esp_err_t ret_rtc = RTC_DS1307_ProcessarComandoMQTT(
    payload,
    resposta_rtc,
    sizeof(resposta_rtc)
);

if (ret_rtc != ESP_ERR_NOT_SUPPORTED)
{
    ESP_LOGI(
        TAG,
        "Comando RTC processado: ret=%s resposta=%s",
        esp_err_to_name(ret_rtc),
        resposta_rtc
    );

    if (resposta_rtc[0] != '\0')
    {
        Mqtt_Kincony_Publicar(
            MQTT_TOPIC_STATUS_RTC,
            resposta_rtc
        );
    }

    cJSON_Delete(root);
    return;
}

    if (strcmp(action->valuestring, "reset_esp") == 0)
    {
        Mqtt_Kincony_PublicarStatus("resetting");
        vTaskDelay(pdMS_TO_TICKS(500));

        cJSON_Delete(root);
                esp_restart();
        return;
    }

    if (strcmp(action->valuestring, "ota_enable") == 0)
    {
        versao_firmware_atual = 1;

        Mqtt_Kincony_Publicar(
            MQTT_TOPIC_STATE,
            "{\"ota_enable\":true,\"versao_firmware_atual\":1}"
        );

        cJSON_Delete(root);
        return;
    }
        if (strcmp(action->valuestring, "reset_faults") == 0)
{
    Logica_Controle_ResetarFalhas();

    Mqtt_Kincony_Publicar(
        MQTT_TOPIC_STATE,
        "{\"reset_faults\":true}"
    );

    cJSON_Delete(root);
    return;
}


    if (!cJSON_IsNumber(group))
    {
        ESP_LOGW(TAG, "Comando de grupo sem campo group");
        cJSON_Delete(root);
        return;
    }

    int grupo = group->valueint;
    bool ligar;

    if (strcmp(action->valuestring, "on") == 0)
    {
        ligar = true;
    }
    else if (strcmp(action->valuestring, "off") == 0)
    {
        ligar = false;
    }
    else
    {
        ESP_LOGW(TAG, "Action invalida: %s", action->valuestring);
        cJSON_Delete(root);
        return;
    }


if (grupo == 0)
{
    /*
     * Grupo zero representa todos os grupos.
     * O override manual so eh registrado quando
     * a logica realmente aceita o comando.
     */
    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        esp_err_t ret = Logica_Controle_SetComandoGrupo(
            (logica_grupo_t)i,
            ligar
        );

        if (ret == ESP_OK)
        {
            RTC_DS1307_RegistrarComandoManual(i + 1);
        }

        publicar_ack_comando(
            i + 1,
            ligar ? "ON" : "OFF",
            ret
        );
    }
}
else if (grupo >= 1 && grupo <= LOGICA_CONTROLE_NUM_GRUPOS)
{
    esp_err_t ret = Logica_Controle_SetComandoGrupo(
        (logica_grupo_t)(grupo - 1),
        ligar
    );

    if (ret == ESP_OK)
    {
        RTC_DS1307_RegistrarComandoManual((uint8_t)grupo);
    }

    publicar_ack_comando(
        (uint8_t)grupo,
        ligar ? "ON" : "OFF",
        ret
    );
}
else
{
    ESP_LOGW(TAG, "Grupo invalido: %d", grupo);

Mqtt_Kincony_Publicar(
    MQTT_TOPIC_STATE,
    "{"
        "\"type\":\"command_ack\","
        "\"result\":\"invalid_group\""
    "}"
);
}

    cJSON_Delete(root);
}