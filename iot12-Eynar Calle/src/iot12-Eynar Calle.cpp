/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/*
   Projeto Final
   Trabalho Final –Alternativa 2   
*/
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

/*******************************************************************************
    Librerias
*******************************************************************************/
#include "DHT.h"
#include "esp_wifi.h"
#include <Arduino.h>
#include <WiFi.h>
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>

#include <Fonts/FreeSerif9pt7b.h>

#include "ThingSpeak.h"

#include "LittleFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

/*******************************************************************************
    Definicion de variables
*******************************************************************************/
/* Display OLED SSD1306 */
#define OLED_WIDTH (128) // Ancho de display OLED (pixels)
#define OLED_HEIGHT (64) // Altura de display OLED (pixels)
#define OLED_ADDRESS (0x3C) // Direccion I²C  display
static Adafruit_SSD1306 display // objeto de control SSD1306
    (OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// Configurar para acceso red Wifi
const char* WIFI_SSID = "FLIA CALLE2"; 
const char* WIFI_PASSWORD = "EYNAR2021";
WiFiClient client; // declarar client para ThingSpeak

// Crea objeto Webserver na porta 80 (padrão HTTP)
AsyncWebServer server(80);

// Variáveis String para almacenar valorea página HTML de provisionamento
String g_ssid;
String g_password;
String g_thingspeak_channel;
String g_thingspeak_key;
String g_disp;

// Caminhos de arquivos para guardar los valores
const char* g_ssidPath = "/ssid.txt";
const char* g_passwordPath = "/password.txt";
const char* g_dispPath = "/disp.txt";
const char* g_thingspeak_channelPath = "/channel.txt";
const char* g_thingspeak_keyPath = "/key.txt";

// Sensor DHT11
#define DHT_READ (15)
#define DHT_TYPE DHT11
DHT dht(DHT_READ, DHT_TYPE);
float g_temperature;
float g_humidity;

// Control de temporizacion periódica
unsigned long g_previousMillis = 0;
const long g_interval = 30000;

/*******************************************************************************
    Implementação: Funções auxiliares
*******************************************************************************/
void littlefsInit()
{
    if (!LittleFS.begin(true)) {
        Serial.println("Error al montar sistema de archivos LittleFS");
        return;
    }
    Serial.println("Sistema de archivos LittleFS montado exitosamente.");
}

// Lee arquivos con LittleFS
String readFile(const char* path)
{
    Serial.printf("Lendo archivo: %s\r\n", path);

    File file = LittleFS.open(path);
    if (!file || file.isDirectory()) {
        Serial.printf("\r\nfalla al abrir archivo... %s", path);
        return String();
    }

    String fileContent;
    while (file.available()) {
        fileContent = file.readStringUntil('\n');
        break;
    }
    Serial.printf("Archivo Leido: %s\r\n", path);
    return fileContent;
}

// Escribe archivos con LittleFS
void writeFile(const char* path, const char* message)
{
    Serial.printf("Escriviendo archivo: %s\r\n", path);

    File file = LittleFS.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("\r\nfalla al abrir archivo... %s", path);
        return;
    }
    if (file.print(message)) {
        Serial.printf("\r\narchivo %s editado.", path);
    } else {
        Serial.printf("\r\nescritura en archivo %s fallo... ", path);
    }
}

// Callbacks para requisicion de recursos servidor
void serverOnGetRoot(AsyncWebServerRequest* request)
{
    request->send(LittleFS, "/index.html", "text/html");
}

void serverOnGetStyle(AsyncWebServerRequest* request)
{
    request->send(LittleFS, "/style.css", "text/css");
}

void serverOnGetFavicon(AsyncWebServerRequest* request)
{
    request->send(LittleFS, "/favicon.png", "image/png");
}

void serverOnPost(AsyncWebServerRequest* request)
{
    int params = request->params();

    for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
            if (p->name() == "ssid") {
                g_ssid = p->value().c_str();
                Serial.print("SSID definido como ");
                Serial.println(g_ssid);

                // Escribe WIFI_SSID en archivo
                writeFile(g_ssidPath, g_ssid.c_str());
            }
            if (p->name() == "password") {
                g_password = p->value().c_str();
                Serial.print("Senha definida como ");
                Serial.println(g_password);

                // Escribe WIFI_PASSWORD en arquivo
                writeFile(g_passwordPath, g_password.c_str());
            }
            if (p->name() == "disp") {
                g_disp = p->value().c_str();
                Serial.print("Dispositivo: ");
                Serial.println(g_disp);

                // Escribe disp en arquivo
                writeFile(g_dispPath, g_disp.c_str());
            }
            if (p->name() == "channel") {
                g_thingspeak_channel = p->value().c_str();
                Serial.print("Canal ThingSpeak: ");
                Serial.println(g_thingspeak_channel);

                // Escribe disp en arquivo
                writeFile(g_thingspeak_channelPath, g_thingspeak_channel.c_str());
            }
            if (p->name() == "key") {
                g_thingspeak_key = p->value().c_str();
                Serial.print("Key ThingSpeak: ");
                Serial.println(g_thingspeak_key);

                // Escribe disp en arquivo
                writeFile(g_thingspeak_keyPath, g_thingspeak_key.c_str());
            }
        }
    }
    // envia mensages de texto simples al browser
    request->send(200, "text/plain", "Finalizado - o ESP32 vai reiniciar e se conectar ao seu AP definido.");

    // Reinicia o ESP32
    delay(2000);
    ESP.restart();
}

// Inicializa a conexion Wifi
bool initWiFi()
{
    // Si el valor de g ssid no es nulo, la página de usuario proporcionó una red Wifi.
    // servidor. Si es así, ESP32 se iniciará en modo AP.
    if (g_ssid == "") {
        Serial.println("SSID indefinido (ainda não foi escrito no arquivo, ou a leitura falhou).");
        return false;
    }

    // Si hay un SSID y una CONTRASEÑA guardados, conéctese a esta red.
    WiFi.mode(WIFI_STA);
    delay(2000);
    WiFi.begin(g_ssid.c_str(), g_password.c_str());
    Serial.println("Conectando a Wifi...");

    unsigned long currentMillis = millis();
    g_previousMillis = currentMillis;

    while (WiFi.status() != WL_CONNECTED) {
        currentMillis = millis();
        if (currentMillis - g_previousMillis >= g_interval) {
            Serial.println("Falha em conectar.");
            return false;
        }
    }

    // Muestra la dirección IP local obtenida
    Serial.println(WiFi.localIP());

    //  Empezar ThingSpeak
    ThingSpeak.begin(client);
    Serial.println("ThingSpeak Iniciado.");

    return true;
}

// Realiza el redondeo de los valores del sensor (a 2 decimales)
double round2(double value)
{
    return (int)(value * 100 + 0.5) / 100.0;
}

// Lea la temperatura (en Celsius) y la humedad del sensor DHT11
esp_err_t sensorRead()
{
    g_temperature = dht.readTemperature();
    g_humidity = dht.readHumidity();
    // Comprueba si alguna lectura falló
    if (isnan(g_humidity) || isnan(g_temperature)) {
        Serial.printf("\r\n[sensorRead] Erro - leitura inválida...");
        return ESP_FAIL;
    } else {
        return ESP_OK;
    }
}

void sensorPublish()
{
    // Rutina para enviar al canal iot12 directo usando ThingSpeak lib
    // Envía datos a la plataforma ThingSpeak. Cada dato del sensor se establece en un campo diferente.
    int errorCode;
    ThingSpeak.setField(1, g_temperature);
    ThingSpeak.setField(2, g_humidity);
    errorCode = ThingSpeak.writeFields((long)g_thingspeak_channel.c_str(), g_thingspeak_key.c_str());
    if (errorCode != 200) {
        Serial.println("Erro ao atualizar os canais - código HTTP: " + String(errorCode));
    } else {
        Serial.printf("\r\n[sensorPublish] Dados publicado no ThingSpeak. Canal: %lu ", g_thingspeak_channel.c_str());
    }
}

/*******************************************************************************
   Implementación: Setup & Loop
*******************************************************************************/
void setup()
{
    // Registro de tablero inicial
    Serial.begin(115200);
    Serial.print("\r\n --- Exercicio Final iot12 ThingSpeak--- \n");

    // Inicie el sistema de archivos
    littlefsInit();

    // Configura LED_BUILTIN (GPIO2) como pin de salída
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Cargar valores leídos con LittleFS
    g_ssid = readFile(g_ssidPath);
    Serial.println(g_ssid);
    g_password = readFile(g_passwordPath);
    Serial.println(g_password);
    g_disp = readFile(g_dispPath);
    Serial.println(g_disp);
    g_thingspeak_channel = readFile(g_thingspeak_channelPath);
    Serial.println(g_thingspeak_channel);
    g_thingspeak_key = readFile(g_thingspeak_keyPath);
    Serial.println(g_thingspeak_key);

    // iniciar display
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    display.setTextColor(WHITE);
    display.clearDisplay();
    display.display();

    // Inicializa o sensor DHT11
    dht.begin();

    // compruebe si está conectado a un AP, de lo contrario, cree uno
    if (!initWiFi()) {
        // Establecer ESP32 en modo AP
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);

        Serial.print("Access Point criado com endereço IP ");
        Serial.println(WiFi.softAPIP());

        // Devoluciones de llamada de la página principal del servidor de aprovisionamiento
        server.on("/", HTTP_GET, serverOnGetRoot);
        server.on("/style.css", HTTP_GET, serverOnGetStyle);
        server.on("/favicon.png", HTTP_GET, serverOnGetFavicon);

        // Al hacer clic en el botón "Enviar" para enviar las credenciales, el servidor recibirá un
        // Solicitud de tipo POST, manejada a continuación
        server.on("/", HTTP_POST, serverOnPost);

        // Como todavía no hay credenciales para acceder a la red wifi,
        // Inicie el servidor web en modo AP
        server.begin();

        // Borra la pantalla de visualización y muestra el nombre del ejemplo
        display.clearDisplay();

        // Mostrar nombre del dispositivo
        display.setCursor(0, 0);
        display.printf("Acesse a rede '%s'.\nUtilize a senha '%s'.\n", WIFI_SSID, WIFI_PASSWORD);
        // Actualiza la pantalla de visualización OLED
        display.display();
    }
}

void loop()
{
    unsigned long currentMillis = millis();

    if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_MODE_STA) {
        // Cada ms "intervalo" publica datos en temas adecuados
        if (currentMillis - g_previousMillis >= g_interval) {

            g_previousMillis = currentMillis;
            // Lee los datos del sensor y los publica si la lectura no falló
            if (sensorRead() == ESP_OK) {
                sensorPublish();
            }

            // Borra la pantalla de visualización y muestra el nombre del ejemplo
            display.clearDisplay();

            // Mostrar nombre del dispositivo
            display.setCursor(50, 0);
            display.printf("%s", g_disp.c_str());

            // Muestra la temperatura en la pantalla
            display.drawRoundRect(0, 18, 126, 16, 6, WHITE);
            display.setCursor(4, 22);
            display.printf("Temperatura: %0.1fC", dht.readTemperature());

            // Muestra la humedad en la pantalla
            display.drawRoundRect(0, 46, 126, 16, 6, WHITE);
            display.setCursor(4, 50);
            display.printf("Humedad: %0.1f%", dht.readHumidity());

            // Actualiza la pantalla de visualización OLED
            display.display();
        }
    } else {
        if (currentMillis - g_previousMillis >= g_interval) {

            g_previousMillis = currentMillis;
        }
        digitalWrite(LED_BUILTIN, HIGH);
        delay(1000);
        digitalWrite(LED_BUILTIN, LOW);
        delay(1000);
    }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////