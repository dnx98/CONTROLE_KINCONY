idf_component_register(SRCS
    "main.c"
    "entradas_digitais/entradas_kincony.c"
    "saidas_digitais/saidas_digitais_kincony.c"
    "modbus_rtu_slave/modbus_rtu_slave_esp.c"
    "wifi/wifi_kincony.c"
    "mqtt/mqtt_kincony.c"
    "ota/ota_github.c"
    "logica_controle/logica_controle.c"
    # Criado por Eraldo Bispo — painel web de configuracao de WiFi/broker (copiado do projeto do Eraldo)
    "config_server/config_server_kincony.c"

    INCLUDE_DIRS
    "."
    "entradas_digitais"
    "saidas_digitais"
    "modbus_rtu_slave"
    "wifi"
    "mqtt"
    "ota"
    "logica_controle"
    "config_server"
    # Editado por Eraldo Bispo — corrigidos nomes dos componentes gerenciados para IDF v6: esp-mqtt e cjson saíram do core
    REQUIRES
    freertos
    driver
    esp-modbus
    esp_wifi
    esp_event
    nvs_flash
    espressif__esp-mqtt
    esp_http_client
    esp_https_ota
    esp-tls
    espressif__cjson
    app_update
    # Criado por Eraldo Bispo — necessarios pelo painel web (config_server_kincony.c)
    esp_http_server
    esp_hw_support

    # Criado por Eraldo Bispo — logo AquaPulse embutida no firmware, servida em /logo.jpg
    EMBED_FILES
    "config_server/logo_aquapulse.jpg"
)

# Criado por Eraldo Bispo — o ninja gerado pelo CMake so reconfigura sozinho (e regera o
# sdkconfig.h) quando arquivos como CMakeLists.txt ou sdkconfig mudam; Kconfig.projbuild nao
# fazia parte dessa lista. Isso forca o ninja a reconfigurar sempre que Kconfig.projbuild for
# editado, mesmo sem rodar idf.py reconfigure manualmente.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.projbuild"
)