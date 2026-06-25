/*
 * rtc_ds1307.h
 *
 * DS1307 + sincronizacao NTP + 4 timers persistentes via NVS.
 *
 * IMPORTANTE:
 * - Este modulo NAO inicializa o barramento I2C.
 * - Usa I2C_NUM_0, que deve estar previamente iniciado pelo driver
 *   de entradas da Kincony.
 */

#ifndef RTC_DS1307_H
#define RTC_DS1307_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t segundo;
    uint8_t minuto;
    uint8_t hora;
    uint8_t dia_semana;
    uint8_t dia;
    uint8_t mes;
    uint16_t ano;
} rtc_ds1307_t;

/* Detecta o DS1307, carrega os timers da NVS e aplica a hora do RTC ao ESP32. */
esp_err_t RTC_DS1307_Iniciar(void);

/* Le e grava data/hora no DS1307. */
esp_err_t RTC_DS1307_LerHorario(rtc_ds1307_t *rtc);
esp_err_t RTC_DS1307_GravarHorario(const rtc_ds1307_t *rtc);

/*
 * Sincroniza o ESP32 via NTP e grava a hora obtida no DS1307.
 * Esta funcao bloqueia enquanto aguarda o NTP; nao chame dentro
 * do callback MQTT. Para o MQTT, use SolicitarSincronizacaoInternet().
 */
esp_err_t RTC_DS1307_AtualizarHorarioInternet(void);

/* Inicia a sincronizacao NTP em uma task separada, sem bloquear o MQTT. */
void RTC_DS1307_SolicitarSincronizacaoInternet(void);

/*
 * Processa timer_set, timer_enable, timer_disable, timer_clear,
 * timer_clear_all, timer_get, rtc_get e rtc_sync.
 *
 * Retornos:
 * - ESP_OK: comando tratado com sucesso;
 * - ESP_ERR_INVALID_ARG: comando reconhecido, mas com dados invalidos;
 * - ESP_ERR_NOT_SUPPORTED: o JSON nao pertence ao RTC/timers.
 */
esp_err_t RTC_DS1307_ProcessarComandoMQTT(
    const char *payload,
    char *resposta,
    size_t tamanho_resposta
);

/*
 * Registra que um comando manual foi aceito.
 * grupo: 1 a LOGICA_CONTROLE_NUM_GRUPOS; 0 representa todos.
 */
void RTC_DS1307_RegistrarComandoManual(uint8_t grupo);

/*
 * Deve ser chamada continuamente no loop principal.
 * A funcao le o RTC aproximadamente uma vez por segundo e executa
 * os eventos dos quatro timers apenas uma vez por minuto.
 */
void RTC_DS1307_Processar(void);

#ifdef __cplusplus
}
#endif

#endif /* RTC_DS1307_H */
