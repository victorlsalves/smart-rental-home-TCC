#include <WiFi.h>
#include <PubSubClient.h>
#include <PZEM004Tv30.h>

// --- Configurações de Rede Wi-Fi ---
//const char* ssid = "CLARO_2GE637F7";
//const char* password = "38E637F7";
// --- Configurações de Rede Wi-Fi ---
const char* ssid = "oi";
const char* password = "12345678";

// --- Configurações do Broker MQTT ---
const char* mqtt_server = "broker.iracema.xyz";
const int mqtt_port = 1883;
const char* mqtt_user = "@@@@@";
const char* mqtt_password = "@@@@@";

// --- Tópicos MQTT ---
const char* topic_pzem = "lapada/tcc_casa_veraneio/medicoes";      // Tópico de ENVIO (ESP -> Broker)
const char* topic_comando = "lapada/tcc_casa_veraneio/comando_rele";  // Tópico de RECEBIMENTO (Broker -> ESP)

WiFiClient espClient;
PubSubClient client(espClient);

#if defined(ESP32)
PZEM004Tv30 pzem(Serial2, 16, 17);
#else
PZEM004Tv30 pzem(Serial2);
#endif

#define RELAY_PIN 33

// Variável para controlar o tempo de envio dos dados sem travar o código
unsigned long ultimaLeitura = 0;
const long intervaloLeitura = 5000; // Envia dados do PZEM a cada 5 segundos

// Função que monitora os Wi-Fi
void setup_wifi() {
    delay(10);
    Serial.println();
    Serial.print("Conectando-se a ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi conectado!");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());
}

// FUNÇÃO CALLBACK: Executada automaticamente quando o site envia uma mensagem
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Mensagem recebida no tópico: ");
    Serial.println(topic);

    // Converte o payload (conteúdo da mensagem) para uma String
    String msg = "";
    for (int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    Serial.print("Conteúdo do comando: ");
    Serial.println(msg);

    // Verifica se o comando é para o tópico correto
    if (String(topic) == topic_comando) {
        if (msg == "1" || msg == "ligar" || msg == "ON") {
            Serial.println("--> Comando recebido: LIGANDO o relé");
            digitalWrite(RELAY_PIN, LOW); // Liga o Relé (Tomada com energia)
        } 
        else if (msg == "0" || msg == "desligar" || msg == "OFF") {
            Serial.println("--> Comando recebido: DESLIGANDO o relé");
            digitalWrite(RELAY_PIN, HIGH); // Desliga o Relé (Tomada sem energia)
        }
    }
}

// Função para reconectar ao MQTT e assinar os tópicos
void reconnect() {
    while (!client.connected()) {
        Serial.print("Tentando conexão MQTT...");
        
        String clientId = "ESP32Client-";
        clientId += String(random(0xffff), HEX);
        
        if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
            Serial.println("Conectado ao broker MQTT!");
            
            // MUITO IMPORTANTE: Inscreve a ESP32 para escutar o tópico de comando do site
            client.subscribe(topic_comando);
            Serial.println("Inscrito no tópico de comando com sucesso.");
        } else {
            Serial.print("Falhou, rc=");
            Serial.print(client.state());
            Serial.println(" Tentando novamente em 5 segundos...");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    setup_wifi();
    
    // Configura o Broker e define qual função cuida das mensagens recebidas
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback); 

    // AUMENTA O LIMITE DE TAMANHO DA MENSAGEM (Resolve o problema de não chegar no broker)
    client.setBufferSize(512);

    pinMode(RELAY_PIN, OUTPUT); 
    digitalWrite(RELAY_PIN, HIGH); // Começa desligado
    
    Serial.print("Custom Address: ");
    Serial.println(pzem.readAddress(), HEX);
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        setup_wifi();
    }
    if (!client.connected()) {
        reconnect();
    }
    
    // Mantém a comunicação MQTT ativa e processa mensagens recebidas instantaneamente
    client.loop(); 

    // Bloco que roda a cada 5 segundos (sem travar a ESP) para ler o PZEM e enviar pro site
    unsigned long tempoAtual = millis();
    if (tempoAtual - ultimaLeitura >= intervaloLeitura) {
        ultimaLeitura = tempoAtual;

        float tensao = pzem.voltage();
        float corrente = pzem.current();
        float potencia = pzem.power();
        float energia = pzem.energy();
        float frequencia = pzem.frequency();
        float pf = pzem.pf();

        if (isnan(tensao) || isnan(corrente) || isnan(potencia)) {
            Serial.println("Erro ao ler dados do PZEM004T");
        } else {
            // Mostra no Serial Monitor
            Serial.print("Tensão: "); Serial.print(tensao); Serial.println(" V");
            Serial.print("Corrente: ");   Serial.print(corrente);   Serial.println(" A");
            Serial.print("Potência: ");   Serial.print(potencia);   Serial.println(" W");
            Serial.print("Energia: ");    Serial.print(energia, 3); Serial.println(" kWh");
            Serial.print("Frequência: "); Serial.print(frequencia, 1); Serial.println(" Hz");
            Serial.print("PF: ");         Serial.println(pf);

            // Monta o JSON para enviar ao banco de dados do site
            String payload = "{";
            payload += "\"tensao\":" + String(tensao) + ",";
            payload += "\"corrente\":" + String(corrente) + ",";
            payload += "\"potencia\":" + String(potencia) + ",";
            payload += "\"energia\":" + String(energia, 3) + ",";
            payload += "\"frequencia\":" + String(frequencia, 1) + ",";
            payload += "\"pf\":" + String(pf);
            payload += "}";

            // Publica as medições no broker
            if (client.publish(topic_pzem, payload.c_str())) {
                Serial.println("--> Medições enviadas ao broker com SUCESSO!");
            } else {
                Serial.println("--> ERRO: Falha ao enviar medições ao broker.");
            }
        }
        Serial.println("-------------------------");
    }
}