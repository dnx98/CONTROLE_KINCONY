#ifndef MQTT_KINCONY_H
#define MQTT_KINCONY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"


#define MQTT_KINCONY_BROKER_URI "mqtt://test.mosquitto.org:1883"

/* Topicos de monitoramento */
#define MQTT_TOPIC_STATUS                "kincony/status"
#define MQTT_TOPIC_ENTRADAS              "kincony/monitoramento/entradas"
#define MQTT_TOPIC_SAIDAS                "kincony/monitoramento/saidas"
#define MQTT_TOPIC_CONTROLE              "kincony/monitoramento/controle"
#define MQTT_TOPIC_GRUPOS                "kincony/monitoramento/grupos"
#define MQTT_TOPIC_FALHAS                "kincony/monitoramento/falhas"

/* Topicos de comando */
#define MQTT_TOPIC_COMANDO_GRUPO         "kincony/comando/grupo"
#define MQTT_TOPIC_COMANDO_GERAL         "kincony/comando/geral"

/* Mantido por compatibilidade com seu codigo antigo */
#define MQTT_TOPIC_COMANDO_SAIDA         MQTT_TOPIC_COMANDO_GRUPO

/* Periodo padrao para publicar monitoramento */
#define MQTT_PUBLICACAO_MONITORAMENTO_MS 1000

esp_err_t Mqtt_Kincony_Init(const char *broker_uri);
void Mqtt_Kincony_Processar(void);

esp_err_t Mqtt_Kincony_Publicar(const char *topico, const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarStatus(const char *mensagem);
esp_err_t Mqtt_Kincony_PublicarEntradas(uint8_t entradas);
esp_err_t Mqtt_Kincony_PublicarSaidas(uint8_t saidas);
esp_err_t Mqtt_Kincony_PublicarControle(void);
esp_err_t Mqtt_Kincony_PublicarGrupos(void);
esp_err_t Mqtt_Kincony_PublicarFalhas(void);
esp_err_t Mqtt_Kincony_PublicarMonitoramento(void);

bool Mqtt_Kincony_IsConectado(void);

#endif
