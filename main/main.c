#include "entradas_kincony.h"
#include "saidas_digitais_kincony.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_rtu_slave_esp.h"
#include "mqtt_kincony.h"
#include "wifi_kincony.h"
#include "ota_github.h"
#include "logica_controle.h"
#include "config_server_kincony.h"

void app_main(void)
{
    // Inicialização dos componentes
    ESP_ERROR_CHECK(Entradas_Kincony_Iniciar());
    ESP_ERROR_CHECK(Saidas_Kincony_Iniciar());

    Logica_Controle_Iniciar();

    // Criado por Eraldo Bispo — carrega WiFi/broker da NVS (ou do menuconfig na primeira vez)
    ESP_ERROR_CHECK(Config_Server_Kincony_Iniciar());

    char wifi_ssid[32];
    char wifi_senha[64];
    char broker_uri[128];
    char mqtt_usuario[64];
    char mqtt_senha[64];

    Config_Server_Kincony_GetWifiSsid(wifi_ssid, sizeof(wifi_ssid));
    Config_Server_Kincony_GetWifiSenha(wifi_senha, sizeof(wifi_senha));
    Config_Server_Kincony_GetBrokerUri(broker_uri, sizeof(broker_uri));
    Config_Server_Kincony_GetMqttUsuario(mqtt_usuario, sizeof(mqtt_usuario));
    Config_Server_Kincony_GetMqttSenha(mqtt_senha, sizeof(mqtt_senha));

    //inicializa o wifi antes do mqtt para garantir que a conexão esteja pronta
    ESP_ERROR_CHECK(Wifi_Kincony_Init(wifi_ssid, wifi_senha));

    // Criado por Eraldo Bispo — se a senha WiFi configurada pelo painel estiver errada, o ESP nao consegue
    // mais ser acessado pela rede. Aqui aguardamos o resultado da conexao e, se falhar, revertemos
    // automaticamente para o ultimo WiFi que funcionou (salvo como backup ao editar pelo painel).
    if (!Wifi_Kincony_EsperarResultado(30000))
    {
        if (Config_Server_Kincony_RestaurarBackupWifi() == ESP_OK)
        {
            esp_restart();
        }
        else
        {
            // Editado por Eraldo Bispo — nem o WiFi atual nem o backup foram encontrados (ex: ESP
            // levado para outro local sem nenhuma rede conhecida). Liga um Access Point proprio
            // para o painel continuar acessivel sem precisar de cabo USB.
            Wifi_Kincony_IniciarModoEmergenciaAP();
        }
    }

    // Criado por Eraldo Bispo — painel web de configuracao, acessivel pelo IP do ESP (ver no monitor)
    // apos conectar. Login padrao: usuario "aquapulse", senha "aquapulse2026" (alteravel no menuconfig)
    ESP_ERROR_CHECK(Config_Server_Kincony_IniciarHttp());

    //inicia mqtt
    Mqtt_Kincony_Init(broker_uri, mqtt_usuario, mqtt_senha);

    // Editado por Eraldo Bispo — OTA agora roda em task propria com pilha dedicada (10240 bytes),
    // em vez de chamado direto no loop da main. TLS+HTTP+cJSON do OTA estavam estourando a pilha
    // da task main (vApplicationStackOverflowHook).
    Ota_Github_IniciarTask();

    while (1)
    {
    // Processar entradas digitais
    Entradas_Kincony_Processar();
    // Processar lógica de controle do sistema
    Logica_Controle_Processar();
    // Editado por Eraldo Bispo - 18/06/2026 22:17 - Atualiza a saida 06 (alarme do controlador)
    // com base na falha geral (grupos, entradas/saidas offline) e na conexao MQTT/WiFi. Ver
    // motivo completo no comentario de Logica_Controle_AtualizarAlarme() em logica_controle.c.
    Logica_Controle_AtualizarAlarme(Mqtt_Kincony_IsConectado(), Wifi_Kincony_IsConectado());
    // Processar MQTT (publicação de monitoramento e recebimento de comandos)
    Mqtt_Kincony_Processar();

    // Editado por Eraldo Bispo - 18/06/2026 22:17 - Reduzido de 5000ms para 200ms. O comando de
    // ligar/desligar grupo (via MQTT) so e aplicado na saida dentro de Logica_Controle_Processar(),
    // chamado uma vez por volta deste loop; com o delay de 5s, o comando ficava ate 5s "parado"
    // esperando a proxima volta antes do motor realmente partir. O timeout de confirmacao de
    // partida/parada (LOGICA_TIMEOUT_PARTIDA_MS / LOGICA_TIMEOUT_PARADA_MS, 5000ms) continua sendo
    // so o prazo para o feedback confirmar - a saida agora liga assim que o comando chega (no
    // maximo 200ms de atraso), e so cai em FALHA se o feedback nao confirmar dentro do timeout.
    // Entradas_Kincony_Processar() e Mqtt_Kincony_Processar() ja se autolimitam internamente
    // (TEMPO_LEITURA_MS / MQTT_PUBLICACAO_MONITORAMENTO_MS), entao rodar o loop mais rapido nao
    // sobrecarrega o I2C nem o MQTT.
    vTaskDelay(pdMS_TO_TICKS(200));
     }
    }
