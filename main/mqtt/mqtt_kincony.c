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

static void string_maiuscula(char *texto)
{
    if (texto == NULL)
    {
        return;
    }

    for (uint16_t i = 0; texto[i] != '\0'; i++)
    {
        texto[i] = (char)toupper((unsigned char)texto[i]);
    }
}

static bool payload_liga(const char *payload)
{
    if (payload == NULL)
    {
        return false;
    }

    return (strcmp(payload, "ON") == 0 ||
            strcmp(payload, "LIGA") == 0 ||
            strcmp(payload, "LIGAR") == 0 ||
            strcmp(payload, "1") == 0 ||
            strcmp(payload, "TRUE") == 0);
}

static bool payload_desliga(const char *payload)
{
    if (payload == NULL)
    {
        return false;
    }

    return (strcmp(payload, "OFF") == 0 ||
            strcmp(payload, "DESLIGA") == 0 ||
            strcmp(payload, "DESLIGAR") == 0 ||
            strcmp(payload, "0") == 0 ||
            strcmp(payload, "FALSE") == 0);
}

static void publicar_ack_comando(uint8_t grupo, const char *acao, esp_err_t ret)
{
    char payload[128];

    snprintf(payload, sizeof(payload),
             "{\"grupo\":%u,\"acao\":\"%s\",\"resultado\":\"%s\",\"remoto\":%u}",
             grupo,
             acao,
             (ret == ESP_OK) ? "OK" : "BLOQUEADO",
             Logica_Controle_IsRemoto() ? 1 : 0);

    Mqtt_Kincony_Publicar("kincony/comando/ack", payload);
}

static void tratar_comando_grupo_texto(const char *payload_original)
{
    char payload[64];
    copiar_evento_para_string(payload, sizeof(payload), payload_original, (int)strlen(payload_original));
    string_maiuscula(payload);

    /*
     * Formatos aceitos:
     *   1=ON
     *   1=OFF
     *   1 ON
     *   1 OFF
     *   G1=ON
     *   G1=OFF
     *   ALL=OFF
     *   RESET
     */

    if (strcmp(payload, "RESET") == 0 || strcmp(payload, "RESET_FALHAS") == 0)
    {
        Logica_Controle_ResetarFalhas();
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"acao\":\"RESET_FALHAS\",\"resultado\":\"OK\"}");
        return;
    }

    if (strcmp(payload, "ALL=OFF") == 0 || strcmp(payload, "TODOS=OFF") == 0 || strcmp(payload, "DESLIGAR_TODOS") == 0)
    {
        Logica_Controle_DesligarTodos();
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"acao\":\"DESLIGAR_TODOS\",\"resultado\":\"OK\"}");
        return;
    }

    char *separador = strchr(payload, '=');
    if (separador == NULL)
    {
        separador = strchr(payload, ' ');
    }

    if (separador == NULL)
    {
        ESP_LOGW(TAG, "Comando invalido: %s", payload);
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"resultado\":\"COMANDO_INVALIDO\"}");
        return;
    }

    *separador = '\0';
    char *grupo_txt = payload;
    char *acao_txt = separador + 1;

    if (grupo_txt[0] == 'G')
    {
        grupo_txt++;
    }

    int grupo_num = atoi(grupo_txt);

    if (grupo_num < 1 || grupo_num > LOGICA_CONTROLE_NUM_GRUPOS)
    {
        ESP_LOGW(TAG, "Grupo invalido: %d", grupo_num);
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"resultado\":\"GRUPO_INVALIDO\"}");
        return;
    }

    esp_err_t ret;

    if (payload_liga(acao_txt))
    {
        ret = Logica_Controle_SetComandoGrupo((logica_grupo_t)(grupo_num - 1), true);
        publicar_ack_comando((uint8_t)grupo_num, "ON", ret);
    }
    else if (payload_desliga(acao_txt))
    {
        ret = Logica_Controle_SetComandoGrupo((logica_grupo_t)(grupo_num - 1), false);
        publicar_ack_comando((uint8_t)grupo_num, "OFF", ret);
    }
    else
    {
        ESP_LOGW(TAG, "Acao invalida: %s", acao_txt);
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"resultado\":\"ACAO_INVALIDA\"}");
    }
}

static void tratar_comando_geral(const char *payload_original)
{
    char payload[64];
    copiar_evento_para_string(payload, sizeof(payload), payload_original, (int)strlen(payload_original));
    string_maiuscula(payload);

    if (strcmp(payload, "RESET") == 0 || strcmp(payload, "RESET_FALHAS") == 0)
    {
        Logica_Controle_ResetarFalhas();
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"acao\":\"RESET_FALHAS\",\"resultado\":\"OK\"}");
    }
    else if (strcmp(payload, "OFF") == 0 || strcmp(payload, "DESLIGAR_TODOS") == 0 || strcmp(payload, "ALL_OFF") == 0)
    {
        Logica_Controle_DesligarTodos();
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"acao\":\"DESLIGAR_TODOS\",\"resultado\":\"OK\"}");
    }
    else
    {
        Mqtt_Kincony_Publicar("kincony/comando/ack", "{\"resultado\":\"COMANDO_GERAL_INVALIDO\"}");
    }
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

            esp_mqtt_client_publish(
                mqtt_client,
                MQTT_TOPIC_STATUS,
                "online",
                0,
                MQTT_QOS_MONITORAMENTO,
                MQTT_RETAIN_STATUS
            );

            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_COMANDO_GRUPO, MQTT_QOS_COMANDO);
            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_COMANDO_GERAL, MQTT_QOS_COMANDO);

            ESP_LOGI(TAG, "Inscrito: %s", MQTT_TOPIC_COMANDO_GRUPO);
            ESP_LOGI(TAG, "Inscrito: %s", MQTT_TOPIC_COMANDO_GERAL);

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
            char payload[128];

            copiar_evento_para_string(topico, sizeof(topico), event->topic, event->topic_len);
            copiar_evento_para_string(payload, sizeof(payload), event->data, event->data_len);

            ESP_LOGI(TAG, "Topico: %s", topico);
            ESP_LOGI(TAG, "Payload: %s", payload);

            if (topico_igual(event, MQTT_TOPIC_COMANDO_GRUPO))
            {
                tratar_comando_grupo_texto(payload);
            }
            else if (topico_igual(event, MQTT_TOPIC_COMANDO_GERAL))
            {
                tratar_comando_geral(payload);
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

esp_err_t Mqtt_Kincony_PublicarStatus(const char *mensagem)
{
    return Mqtt_Kincony_Publicar(MQTT_TOPIC_STATUS, mensagem);
}

esp_err_t Mqtt_Kincony_PublicarEntradas(uint8_t entradas)
{
    char payload[64];

    snprintf(payload, sizeof(payload),
             "{\"raw\":%u,\"online\":%u,\"remoto\":%u}",
             entradas,
             Entradas_Kincony_IsOnline() ? 1 : 0,
             Logica_Controle_IsRemoto() ? 1 : 0);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_ENTRADAS, payload);
}

esp_err_t Mqtt_Kincony_PublicarSaidas(uint8_t saidas)
{
    char payload[64];

    snprintf(payload, sizeof(payload),
             "{\"raw\":%u,\"online\":%u}",
             saidas,
             Saidas_Kincony_IsOnline() ? 1 : 0);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_SAIDAS, payload);
}

esp_err_t Mqtt_Kincony_PublicarControle(void)
{
    char payload[192];

    snprintf(payload, sizeof(payload),
             "{\"mqtt\":1,\"remoto\":%u,\"falha_geral\":%u,\"grupos_ligados\":%u,\"falhas\":%u,\"entradas_online\":%u,\"saidas_online\":%u}",
             Logica_Controle_IsRemoto() ? 1 : 0,
             Logica_Controle_IsFalhaGeral() ? 1 : 0,
             Logica_Controle_GetMascaraLigados(),
             Logica_Controle_GetMascaraFalhas(),
             Entradas_Kincony_IsOnline() ? 1 : 0,
             Saidas_Kincony_IsOnline() ? 1 : 0);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_CONTROLE, payload);
}

esp_err_t Mqtt_Kincony_PublicarGrupos(void)
{
    char payload[512];
    int pos = 0;

    pos += snprintf(payload + pos, sizeof(payload) - pos, "{\"grupos\":[");

    for (uint8_t i = 0; i < LOGICA_CONTROLE_NUM_GRUPOS; i++)
    {
        pos += snprintf(payload + pos, sizeof(payload) - pos,
                        "%s{\"id\":%u,\"comando\":%u,\"feedback\":%u,\"estado\":\"%s\",\"falha\":\"%s\"}",
                        (i == 0) ? "" : ",",
                        i + 1,
                        Logica_Controle_GetComandoGrupo((logica_grupo_t)i) ? 1 : 0,
                        Logica_Controle_GetFeedbackGrupo((logica_grupo_t)i) ? 1 : 0,
                        Logica_Controle_EstadoToString(Logica_Controle_GetEstadoGrupo((logica_grupo_t)i)),
                        Logica_Controle_FalhaToString(Logica_Controle_GetFalhaGrupo((logica_grupo_t)i)));

        if (pos >= (int)sizeof(payload))
        {
            break;
        }
    }

    snprintf(payload + pos, sizeof(payload) - pos, "]}");

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_GRUPOS, payload);
}

esp_err_t Mqtt_Kincony_PublicarFalhas(void)
{
    char payload[128];

    snprintf(payload, sizeof(payload),
             "{\"falha_geral\":%u,\"mascara_falhas\":%u,\"entradas_online\":%u,\"saidas_online\":%u}",
             Logica_Controle_IsFalhaGeral() ? 1 : 0,
             Logica_Controle_GetMascaraFalhas(),
             Entradas_Kincony_IsOnline() ? 1 : 0,
             Saidas_Kincony_IsOnline() ? 1 : 0);

    return Mqtt_Kincony_Publicar(MQTT_TOPIC_FALHAS, payload);
}

esp_err_t Mqtt_Kincony_PublicarMonitoramento(void)
{
    if (!mqtt_conectado)
    {
        return ESP_FAIL;
    }

    Mqtt_Kincony_PublicarStatus("online");
    Mqtt_Kincony_PublicarEntradas(Entradas_Kincony_GetEstado());
    Mqtt_Kincony_PublicarSaidas(Saidas_Kincony_GetEstado());
    Mqtt_Kincony_PublicarControle();
    Mqtt_Kincony_PublicarGrupos();
    Mqtt_Kincony_PublicarFalhas();

    return ESP_OK;
}

bool Mqtt_Kincony_IsConectado(void)
{
    return mqtt_conectado;
}
