#ifndef UNIT_TEST  // Se UNIT_TEST não estiver definido, compila o código real

#include <Arduino.h>                   // Biblioteca principal do Arduino
#include "leafminer.h"                 // Cabeçalho específico do projeto LeafMiner
#include "utils/log.h"                 // Funções de logging (ex.: l_info, l_error, l_debug)
#include "model/configuration.h"       // Modelo/configuração do minerador (provavelmente para armazenar configurações)
#include "network/network.h"           // Funções relacionadas à rede (conexão com o pool, etc.)
#include "network/accesspoint.h"       // Funções para configurar o modo Access Point (AP)
#include "utils/blink.h"               // Funções para piscar um LED, usado para feedback visual
#include "miner/miner.h"               // Funções relacionadas à mineração
#include "current.h"                   // Pode estar relacionado ao monitoramento de corrente ou tarefas correntes
#include "utils/button.h"              // Funções para leitura e configuração de botões físicos
#include "storage/storage.h"           // Funções para salvar e carregar dados em memória (eeprom, flash, etc.)
#include "network/autoupdate.h"        // Funções para atualização automática do firmware
#include "massdeploy.h"                // Configurações ou funções para implantação em massa

#if defined(HAS_LCD)
#include "screen/screen.h"             // Se houver um LCD conectado, inclui as funções de gerenciamento da tela
#endif // HAS_LCD

// Tag usada para identificar as mensagens de log deste arquivo
char TAG_MAIN[] = "Main";

// Cria um objeto global de configuração
Configuration configuration;

void setup() {
  // Inicializa a comunicação serial com 115200 baud
  Serial.begin(115200);
  // Pequena pausa para estabilizar a conexão serial após o boot
  delay(1500);
  
  // Imprime mensagens iniciais de log com informações de versão, compilação e memória livre
  l_info(TAG_MAIN, "LeafMiner - v.%s - (C: %d)", _VERSION, CORE);
  l_info(TAG_MAIN, "Compiled: %s %s", __DATE__, __TIME__);
  l_info(TAG_MAIN, "Free memory: %d", ESP.getFreeHeap());

  // Informações específicas para ESP32 ou ESP8266:
#if defined(ESP32)
  // Para ESP32, exibe modelo, revisão do chip e ID único (MAC Efuse)
  l_info(TAG_MAIN, "Chip Model: %s - Rev: %d", ESP.getChipModel(), ESP.getChipRevision());
  l_info(TAG_MAIN, "Chip ID: %lld", ESP.getEfuseMac());
#else
  // Para ESP8266, desabilita o watchdog (WDT) e faz uma configuração extra
  l_info(TAG_MAIN, "ESP8266 - Disable WDT");
  ESP.wdtDisable();
  // Essa linha manipula um registro específico (endereço 0x60000900) para possivelmente desabilitar um recurso
  *((volatile uint32_t *)0x60000900) &= ~(1);
#endif // ESP32

  // Inicializa o sistema de armazenamento (por exemplo, carregando a memória flash ou EEPROM)
  storage_setup();
  // Configura os botões e retorna se algum botão foi pressionado (possivelmente forçando o modo Access Point)
  bool force_ap = button_setup();

  // Carrega a configuração armazenada (como SSID, senha, etc.) para o objeto configuration
  storage_load(&configuration);
  // Imprime a configuração carregada para debug
  configuration.print();

  // Se não houver SSID configurado ou se o modo AP foi forçado pelo botão:
  if (configuration.wifi_ssid == "" || force_ap) {
    #ifdef MASS_WIFI_SSID
      // Se existir uma definição MASS_WIFI_SSID, utiliza essas configurações pré-definidas
      configuration.wifi_ssid = MASS_WIFI_SSID;
      configuration.wifi_password = MASS_WIFI_PASS;
      configuration.pool_url = MASS_POOL_URL;
      configuration.pool_password = MASS_POOL_PASSWORD;
      configuration.pool_port = MASS_POOL_PORT;
      configuration.wallet_address = MASS_WALLET;
    #else
      // Caso contrário, configura o ESP como Access Point e encerra o setup
      accesspoint_setup();
      return;
    #endif // MASS_WIFI_SSID
  }

  #if defined(HAS_LCD)
    // Se houver um LCD, inicializa a tela
    screen_setup();
  #else
    // Se não houver LCD, inicializa o sistema de piscar LED para feedback visual
    Blink::getInstance().setup();
    delay(500);
    Blink::getInstance().blink(BLINK_START);
  #endif // HAS_LCD

  // Se a configuração está definida para atualização automática, chama a função de autoupdate
  if (configuration.auto_update == "on") {
    autoupdate();
  }

  // Tenta obter um trabalho (job) da rede, se falhar retorna -1
  if (network_getJob() == -1) {
    // Se não conseguir conectar à rede, exibe mensagem de erro e configura o modo Access Point
    l_error(TAG_MAIN, "Failed to connect to network");
    l_info(TAG_MAIN, "Fallback to AP mode");
    accesspoint_setup();
    return;
  }

  // Agora, dependendo se estiver usando ESP32 ou ESP8266, cria as tarefas ou inicia o listener:
#if defined(ESP32)
  // Para ESP32, para o Bluetooth para liberar recursos
  btStop();
  // Cria uma tarefa para monitorar a corrente (currentTaskFunction) e a fixa no Core 1
  xTaskCreatePinnedToCore(currentTaskFunction, "stale", 1024, NULL, 1, NULL, 1);
  // Cria uma tarefa para ler os botões, também no Core 1
  xTaskCreatePinnedToCore(buttonTaskFunction, "button", 1024, NULL, 2, NULL, 1);
  // Cria uma tarefa para a mineração no Core 1; o parâmetro (void *)0 indica que é a primeira instância
  xTaskCreatePinnedToCore(mineTaskFunction, "miner0", 6000, (void *)0, 10, NULL, 1);
#if CORE == 2
  // Se o ESP32 tiver dois núcleos, cria uma segunda tarefa de mineração no outro núcleo (Core 1, mas com parâmetro (void *)1)
  xTaskCreatePinnedToCore(mineTaskFunction, "miner1", 6000, (void *)1, 11, NULL, 1);
#endif
#elif defined(ESP8266)
  // No ESP8266, que é unicore, inicia a função que escuta a rede (listener)
  network_listen();
#endif
}

void loop() {
  // Se a configuração não tiver um SSID (ou seja, está em modo AP), chama a rotina de loop do Access Point
  if (configuration.wifi_ssid == "") {
    accesspoint_loop();
    return;
  }

  #if defined(ESP8266)
    // Para ESP8266, chama a função miner, passando 0 como parâmetro (pode representar o índice do minerador ou similar)
    miner(0);
  #endif // ESP8266
}

#endif  // Fim do bloco #ifndef UNIT_TEST
