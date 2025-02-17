#include <vector>                     // Biblioteca para usar vetores (std::vector)
#include <Arduino.h>                  // Biblioteca principal do Arduino
#if defined(ESP8266)
#include <ESP8266WiFi.h>              // Biblioteca WiFi para ESP8266
#else
#include <WiFi.h>                    // Biblioteca WiFi para ESP32
#endif // ESP8266

#include "model/configuration.h"      // Define a estrutura/configuração do minerador
#include "network.h"                  // Declarações de funções e variáveis de rede
#include "utils/log.h"                // Funções de log (l_info, l_error, l_debug)
#include "leafminer.h"                // Funções específicas do LeafMiner
#include "current.h"                  // Funções/variáveis para gerenciamento do trabalho atual
#include "model/configuration.h"      // (Incluído novamente possivelmente por necessidade de compatibilidade)

// Define constantes para o tamanho dos buffers e tempos de espera
#define NETWORK_BUFFER_SIZE 2048      // Tamanho do buffer para leitura de dados da rede
#define NETWORK_TIMEOUT 1000 * 60       // Tempo de timeout (em milissegundos)
#define NETWORK_DELAY 1222              // Um delay fixo usado entre tentativas (em milissegundos)
#define NETWORK_WIFI_ATTEMPTS 2         // Número máximo de tentativas para conectar ao WiFi
#define NETWORK_STRATUM_ATTEMPTS 2      // Número máximo de tentativas para conectar ao host (pool)
#define MAX_PAYLOAD_SIZE 256            // Tamanho máximo de um payload (mensagem) em bytes
#define MAX_PAYLOADS 10                 // Número máximo de payloads que podem ser enfileirados

// Cria uma instância do objeto WiFiClient para gerenciar a conexão TCP
WiFiClient client = WiFiClient();

// Define uma tag de log para identificar mensagens deste módulo
char TAG_NETWORK[8] = "Network";

// Variáveis globais para gerenciamento de IDs e estado de conexão
uint64_t id = 0;                      // Contador global para geração de IDs únicos
uint64_t requestJobId = 0;            // ID da requisição de trabalho atual
uint8_t isRequestingJob = 0;          // Flag indicando se já está solicitando um trabalho
uint32_t authorizeId = 0;             // ID usado na autorização
uint8_t isAuthorized = 0;             // Flag indicando se a autorização foi bem-sucedida

// Declaração externa da configuração (definida em outro módulo)
extern Configuration configuration;

// Array de strings para armazenar payloads enfileirados e contador
char payloads[MAX_PAYLOADS][MAX_PAYLOAD_SIZE]; // Armazena mensagens a serem enviadas
size_t payloads_count = 0;            // Número de payloads atualmente enfileirados

/**
 * @brief Gera o próximo ID para as requisições de rede.
 *
 * Se o ID atual atingir UINT64_MAX, ele é reiniciado para 1; caso contrário, incrementa.
 *
 * @return O próximo ID (uint64_t).
 */
uint64_t nextId()
{
    return (id == UINT64_MAX) ? 1 : ++id;
}

/**
 * @brief Verifica se o dispositivo está conectado à rede WiFi e ao host.
 *
 * Se não estiver conectado, tenta reconectar ao WiFi e ao host (pool).
 *
 * @return 1 se conectado com sucesso, -1 em caso de falha.
 */
short isConnected()
{
    // Se já estiver conectado ao WiFi e o cliente TCP estiver conectado, retorna sucesso.
    if (WiFi.status() == WL_CONNECTED && client.connected())
    {
        return 1;
    }

    uint16_t wifi_attemps = 0;

    // Tenta conectar ao WiFi até NETWORK_WIFI_ATTEMPTS vezes.
    while (wifi_attemps < NETWORK_WIFI_ATTEMPTS)
    {
        l_info(TAG_NETWORK, "Connecting to %s...", configuration.wifi_ssid.c_str());
        WiFi.begin(configuration.wifi_ssid.c_str(), configuration.wifi_password.c_str());
        wifi_attemps++;
        delay(500);
        // Se a conexão for bem-sucedida, sai do loop.
        if (WiFi.waitForConnectResult() == WL_CONNECTED)
        {
            break;
        }
        delay(1500);
    }

    // Se mesmo após as tentativas não estiver conectado, exibe erro e retorna -1.
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        l_error(TAG_NETWORK, "Unable to connect to WiFi");
        return -1;
    }

    // Conexão WiFi estabelecida. Loga informações de rede.
    l_info(TAG_NETWORK, "Connected to WiFi");
    l_info(TAG_NETWORK, "IP address: %s", WiFi.localIP().toString().c_str());
    l_info(TAG_NETWORK, "MAC address: %s", WiFi.macAddress().c_str());

    uint16_t wifi_stratum = 0;

    // Tenta conectar ao host (pool) com NETWORK_STRATUM_ATTEMPTS tentativas.
    while (wifi_stratum < NETWORK_STRATUM_ATTEMPTS)
    {
        l_debug(TAG_NETWORK, "Connecting to host %s...", configuration.pool_url.c_str());
        client.connect(configuration.pool_url.c_str(), configuration.pool_port);
        delay(500);
        if (client.connected())
        {
            break;
        }
        wifi_stratum++;
        delay(1000);
    }

    // Se não conseguir conectar ao host, retorna erro.
    if (!client.connected())
    {
        l_error(TAG_NETWORK, "Unable to connect to host");
        return -1;
    }

    return 1;
}

/**
 * @brief Envia um payload (mensagem) para o servidor.
 *
 * A função envia o payload pela conexão TCP e registra a mensagem enviada via log.
 *
 * @param payload A mensagem (payload) a ser enviada.
 */
void request(const char *payload)
{
    client.print(payload);               // Envia o payload através do cliente TCP
    l_info(TAG_NETWORK, ">>> %s", payload); // Loga a mensagem enviada
}

/**
 * @brief Autoriza a conexão com o pool de mineração.
 *
 * Constrói uma mensagem JSON para autorização e envia para o pool.
 */
void authorize()
{
    char payload[1024];
    uint64_t next_id = nextId();         // Gera o próximo ID para a requisição
    isAuthorized = 0;                    // Reseta a flag de autorização
    authorizeId = next_id;               // Armazena o ID usado para autorização
    // Monta a mensagem JSON de autorização
    sprintf(payload, "{\"id\":%llu,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n", 
            next_id, 
            configuration.wallet_address.c_str(), 
            configuration.pool_password.c_str());
    request(payload);                    // Envia a mensagem
}

/**
 * @brief Inscreve o minerador no pool de mineração.
 *
 * Constrói uma mensagem JSON para inscrição (subscribe) e envia-a.
 */
void subscribe()
{
    char payload[1024];
    // Monta a mensagem JSON para subscribe usando a versão do software (_VERSION)
    sprintf(payload, "{\"id\":%llu,\"method\":\"mining.subscribe\",\"params\":[\"LeafMiner/%s\", null]}\n", 
            nextId(), _VERSION);
    request(payload);                    // Envia a mensagem
}

/**
 * @brief Sugere a dificuldade de mineração para o pool.
 *
 * Constrói uma mensagem JSON para sugerir a dificuldade (mining.suggest_difficulty).
 */
void difficulty()
{
    char payload[1024];
    // Monta a mensagem JSON passando a dificuldade (valor double)
    sprintf(payload, "{\"id\":%llu,\"method\":\"mining.suggest_difficulty\",\"params\":[%f]}\n", 
            nextId(), DIFFICULTY);
    request(payload);                    // Envia a mensagem
}

/**
 * @brief Determina o tipo de resposta recebido do pool.
 *
 * Analisa o objeto JSON e retorna uma string representando o tipo de resposta.
 *
 * @param json O objeto cJSON que representa a resposta.
 * @return Uma string com o tipo de resposta (ex.: "subscribe", "mining.notify", etc.)
 */
const char *responseType(cJSON *json)
{
    const cJSON *result = cJSON_GetObjectItem(json, "result");
    if (result != NULL && cJSON_IsArray(result) && cJSON_GetArraySize(result) > 0)
    {
        const cJSON *item0 = cJSON_GetArrayItem(result, 0);
        if (item0 != NULL && cJSON_IsArray(item0) && cJSON_GetArraySize(item0) > 0)
        {
            const cJSON *item00 = cJSON_GetArrayItem(item0, 0);
            if (item00 != NULL && cJSON_IsArray(item00) && cJSON_GetArraySize(item00) > 0)
            {
                return "subscribe";         // Identifica resposta de inscrição
            }
        }
    }
    else if (cJSON_HasObjectItem(json, "method"))
    {
        // Se existir a chave "method", retorna seu valor
        return cJSON_GetStringValue(cJSON_GetObjectItem(json, "method"));
    }
    else if (cJSON_HasObjectItem(json, "result"))
    {
        const cJSON *result = cJSON_GetObjectItem(json, "result");
        // Verifica se o ID da mensagem corresponde ao authorizeId para identificar autorização
        if (authorizeId == cJSON_GetNumberValue(cJSON_GetObjectItem(json, "id")))
        {
            return "authorized";
        }
        if (cJSON_IsTrue(result))
        {
            return "mining.submit";       // Resposta positiva para um share submetido
        }
        else
        {
            // Se o erro é "Job not found" (código 21), identifica como falha na submissão
            if (cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(json, "error"), 0)) == 21)
            {
                return "mining.submit.fail";
            }
            else if (cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(json, "error"), 0)) == 23)
            {
                return "mining.submit.difficulty_too_low";
            }
            else
            {
                return "mining.submit";
            }
        }
    }

    return "unknown";                   // Caso não se encaixe em nenhum tipo conhecido
}

/**
 * @brief Processa a resposta recebida do pool.
 *
 * Faz o parse do JSON e executa ações conforme o tipo de resposta.
 *
 * @param r A resposta recebida (std::string).
 */
void response(std::string r)
{
    cJSON *json = cJSON_Parse(r.c_str());
    const char *type = responseType(json);
    l_info(TAG_NETWORK, "<<< [%s] %s", type, r.c_str());

    if (strcmp(type, "subscribe") == 0)
    {
        // Trata a resposta de inscrição (subscribe)
        const cJSON *result = cJSON_GetObjectItem(json, "result");
        if (cJSON_IsArray(result) && cJSON_IsArray(cJSON_GetArrayItem(result, 0)) &&
            cJSON_IsArray(cJSON_GetArrayItem(cJSON_GetArrayItem(result, 0), 0)))
        {
            const cJSON *subscribeIdJson = cJSON_GetArrayItem(cJSON_GetArrayItem(cJSON_GetArrayItem(result, 0), 0), 1);
            const cJSON *extranonce1Json = cJSON_GetArrayItem(result, 1);
            const cJSON *extranonce2SizeJson = cJSON_GetArrayItem(result, 2);

            if (cJSON_IsString(subscribeIdJson) && cJSON_IsString(extranonce1Json) && cJSON_IsNumber(extranonce2SizeJson))
            {
                std::string subscribeId = subscribeIdJson->valuestring;
                std::string extranonce1 = extranonce1Json->valuestring;
                int extranonce2_size = extranonce2SizeJson->valueint;
                // Cria um novo objeto Subscribe com os valores recebidos
                Subscribe *subscribe = new Subscribe(subscribeId, extranonce1, extranonce2_size);
                current_setSubscribe(subscribe);
            }
        }
    }
    else if (strcmp(type, "mining.notify") == 0)
    {
        // Trata a notificação de novo trabalho (job) para mineração
        cJSON *params = cJSON_GetObjectItem(json, "params");
        std::string job_id = cJSON_GetArrayItem(params, 0)->valuestring;

        // Verifica se o job recebido é igual ao job atual para evitar processamento duplicado
        if (current_hasJob() && strcmp(current_job->job_id.c_str(), job_id.c_str()) == 0)
        {
            l_error(TAG_NETWORK, "Job is the same as the current one");
            return;
        }

        if (cJSON_IsArray(params) && cJSON_GetArraySize(params) == 9)
        {
            // Extrai os parâmetros do novo trabalho
            std::string prevhash = cJSON_GetArrayItem(params, 1)->valuestring;
            std::string coinb1 = cJSON_GetArrayItem(params, 2)->valuestring;
            std::string coinb2 = cJSON_GetArrayItem(params, 3)->valuestring;
            cJSON *merkle_branch = cJSON_GetArrayItem(params, 4);
            std::string version = cJSON_GetArrayItem(params, 5)->valuestring;
            std::string nbits = cJSON_GetArrayItem(params, 6)->valuestring;
            std::string ntime = cJSON_GetArrayItem(params, 7)->valuestring;
            bool clean_jobs = cJSON_GetArrayItem(params, 8)->valueint == 1;

            // Converte o merkle branch para um vetor de strings
            std::vector<std::string> merkleBranchStrings;
            int merkleBranchSize = cJSON_GetArraySize(merkle_branch);
            for (int i = 0; i < merkleBranchSize; ++i)
            {
                merkleBranchStrings.push_back(cJSON_GetArrayItem(merkle_branch, i)->valuestring);
            }
            requestJobId = nextId();

            // Define o novo job atual com os dados recebidos
            current_setJob(Notification(job_id, prevhash, coinb1, coinb2, merkleBranchStrings, version, nbits, ntime, clean_jobs));
            isRequestingJob = 0;
        }
    }
    else if (strcmp(type, "mining.set_difficulty") == 0)
    {
        // Trata a mensagem para alterar a dificuldade de mineração
        const cJSON *paramsArray = cJSON_GetObjectItem(json, "params");
        if (cJSON_IsArray(paramsArray) && cJSON_GetArraySize(paramsArray) == 1)
        {
            const cJSON *difficultyItem = cJSON_GetArrayItem(paramsArray, 0);
            if (cJSON_IsNumber(difficultyItem))
            {
                double diff = difficultyItem->valuedouble;
                current_setDifficulty(diff);
                l_debug(TAG_NETWORK, "Difficulty set to: %.10f", diff);
            }
        }
    }
    else if (strcmp(type, "authorized") == 0)
    {
        // Se a resposta indicar autorização, registra o sucesso
        l_info(TAG_NETWORK, "Authorized");
        isAuthorized = 1;
    }
    else if (strcmp(type, "mining.submit") == 0)
    {
        // Se um share submetido foi aceito
        l_info(TAG_NETWORK, "Share accepted");
        current_increment_hash_accepted();
    }
    else if (strcmp(type, "mining.submit.difficulty_too_low") == 0)
    {
        // Se o share foi rejeitado por dificuldade baixa
        l_error(TAG_NETWORK, "Share rejected due to low difficulty");
        current_increment_hash_rejected();
    }
    else if (strcmp(type, "mining.submit.fail") == 0)
    {
        // Se o share foi rejeitado
        l_error(TAG_NETWORK, "Share rejected");

        // Se a resposta veio com um ID menor que o do requestJobId, ignora a resposta tardia
        if ((uint64_t)cJSON_GetObjectItem(json, "id")->valueint < requestJobId)
        {
            l_error(TAG_NETWORK, "Late responses, skip them");
        }
        else
        {
            current_job_is_valid = 0;
#if defined(ESP32)
            if (current_job_next != nullptr)
            {
                current_job = current_job_next;
                current_job_next = nullptr;
                current_job_is_valid = 1;
                l_debug(TAG_NETWORK, "Job (next): %s ready to be mined", current_job->job_id.c_str());
                current_increment_processedJob();
            }
#endif
            current_increment_hash_rejected();
        }
    }
    else
    {
        // Se o tipo de resposta não for reconhecido, registra erro
        l_error(TAG_NETWORK, "Unknown response type: %s", type);
    }
    // Libera o objeto JSON para evitar vazamento de memória
    cJSON_Delete(json);
    // Limpa a string de resposta
    r.clear();
}

/**
 * @brief Solicita um novo trabalho (job) para mineração.
 *
 * Se já houver um job válido ou uma requisição em andamento, não solicita novamente.
 * Se não estiver conectado, reinicia a sessão.
 *
 * @return 1 se a requisição foi iniciada, -1 em caso de falha.
 */
short network_getJob()
{
    if (current_job_is_valid == 1)
    {
        l_info(TAG_NETWORK, "Already has a job and don't need a new one");
        return 0;
    }

    if (isRequestingJob == 1)
    {
        l_info(TAG_NETWORK, "Already requesting a job");
        return 0;
    }

    isRequestingJob = 1;

    // Se não conseguir conectar à rede, reseta a sessão e retorna erro
    if (isConnected() == -1)
    {
        current_resetSession();
        return -1;
    }

    // Se não houver uma sessão ativa, faz subscribe, authorize e define a dificuldade
    if (current_getSessionId() == nullptr)
    {
        subscribe();
        authorize();
        difficulty();
    }

    return 1;
}

/**
 * @brief Enfileira um payload para envio posterior.
 *
 * Se a fila não estiver cheia, copia o payload para o array e incrementa o contador.
 *
 * @param payload A mensagem a ser enfileirada.
 */
void enqueue(const char *payload)
{
    if (payloads_count < MAX_PAYLOADS)
    {
        // Copia o payload para a posição atual da fila, garantindo o tamanho máximo
        strncpy(payloads[payloads_count], payload, MAX_PAYLOAD_SIZE - 1);
        payloads_count++;
        l_debug(TAG_NETWORK, "Payload queued: %s", payload);
    }
    else
    {
        l_error(TAG_NETWORK, "Payload queue is full");
    }
}

/**
 * @brief Envia uma submissão de share para o pool.
 *
 * Constrói um payload JSON para enviar o share (resultado da mineração) e o envia.
 *
 * @param job_id ID do trabalho atual.
 * @param extranonce2 Valor extranonce2.
 * @param ntime Tempo no formato ntime.
 * @param nonce O nonce encontrado.
 */
void network_send(const std::string &job_id, const std::string &extranonce2, const std::string &ntime, const uint32_t &nonce)
{
    char payload[MAX_PAYLOAD_SIZE];
    // Monta o payload JSON para submissão de share
    snprintf(payload, sizeof(payload), "{\"id\":%llu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%08x\"]}\n", 
             nextId(), 
             configuration.wallet_address.c_str(), 
             job_id.c_str(), 
             extranonce2.c_str(), 
             ntime.c_str(), 
             nonce);
#if defined(ESP8266)
    request(payload);       // Envia o payload
    network_listen();       // Escuta a resposta imediatamente (modo ESP8266)
#else
    enqueue(payload);       // Para ESP32, enfileira o payload para envio posterior
#endif
}

/**
 * @brief Escuta as mensagens da rede.
 *
 * Lê dados do cliente até encontrar uma nova linha ('\n') e processa a resposta.
 */
void network_listen()
{
    uint32_t start_time = millis();  // Marca o tempo de início
    uint32_t len = 0;

    // Se não estiver conectado, reseta a sessão
    if (isConnected() == -1)
    {
        current_resetSession();
        return; // Trata a falha na conexão
    }

    do
    {
        // Se mais de 5 segundos se passaram, sai do loop
        if (millis() - start_time > 5000)
        {
            l_debug(TAG_NETWORK, "Timeout occurred. Exiting network_listen loop.");
            return;
        }

        char data[NETWORK_BUFFER_SIZE];
        // Lê dados do cliente até encontrar '\n' ou atingir o tamanho do buffer
        len = client.readBytesUntil('\n', data, sizeof(data) - 1);
        l_debug(TAG_NETWORK, "<<< len: %d", len);
        data[len] = '\0';  // Termina a string
        if (data[0] != '\0')
        {
            // Processa a resposta recebida
            response(data);
        }

    } while (len > 0);
}

/**
 * @brief Envia um payload e, em seguida, o remove da fila.
 *
 * Após enviar o payload, percorre a fila de payloads e remove aquele que foi enviado.
 *
 * @param payload A mensagem a ser enviada.
 */
void network_submit(const char *payload)
{
    if (isConnected() == -1)
    {
        current_resetSession();
        return; // Trata a falha na conexão
    }

    request(payload);

    // Remove o payload enviado da fila (shift dos demais elementos)
    for (size_t i = 0; i < payloads_count; ++i)
    {
        if (strcmp(payloads[i], payload) == 0)
        {
            // Move os payloads seguintes para ocupar o lugar do removido
            for (size_t j = i; j < payloads_count - 1; ++j)
            {
                strcpy(payloads[j], payloads[j + 1]);
            }
            payloads_count--;
            break;
        }
    }
}

/**
 * @brief Envia todos os payloads enfileirados.
 */
void network_submit_all()
{
    for (size_t i = 0; i < payloads_count; ++i)
    {
        network_submit(payloads[i]);
    }
}

#if defined(ESP32)
// Define um timeout para a tarefa de rede
#define NETWORK_TASK_TIMEOUT 100
/**
 * @brief Função de tarefa para gerenciamento de rede no ESP32.
 *
 * Em loop, envia todos os payloads enfileirados e escuta as respostas, com um delay fixo entre as iterações.
 *
 * @param pvParameters Parâmetros da tarefa (não utilizado aqui).
 */
void networkTaskFunction(void *pvParameters)
{
    while (1)
    {
        network_submit_all();  // Tenta enviar todos os payloads pendentes
        network_listen();      // Escuta as respostas do pool
        // Delay para evitar saturar a CPU, convertido para ticks do FreeRTOS
        vTaskDelay(NETWORK_TASK_TIMEOUT / portTICK_PERIOD_MS);
    }
}
#endif
