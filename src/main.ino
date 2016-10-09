//#include "Arduino.h"
#include "ArduinoHttpClient.h"
#include "ArduinoJson.h"
#include "ESP8266WiFi.h"

extern "C" {
#include "user_interface.h"
}

//#define ___debug

// Thingspeak
String apiKey = "YA33JGJN9ODHF945";
const char *apiServer = "api.thingspeak.com";
unsigned long channelNumber = 156889;
String talkBackAPIKey = "WNAL40G6FOJK284T";
String talkBackID = "10253";
// f1 = Consumo | f2 = ligado | f3 = tempLimit | f4 = tempUsuario

// Wifi data
const char *ssid = "ovni_2.4GHz";
const char *password = "naotemsenha";

// Declare
WiFiClient client;
HttpClient http = HttpClient(client, apiServer, 80);
int turned_on = 0;
unsigned long nextPost;
int tempPin = 17; // ADC pin
int relePin = 5;  // Digital pin
float tempMedia = 20;
float tempLimit = 0;
float tempUsuario = 20;
float tempAtual = 0;
bool data_change = true;

void setup() {
  // pinmode
  pinMode(relePin, OUTPUT);
  digitalWrite(relePin, HIGH); // HIGH IS OFF

  // global vars
  nextPost = millis();

  // serial
  Serial.begin(115200);
  Serial.println("\r\nSerial Start");

  // wifi
  wifi_set_sleep_type(NONE_SLEEP_T);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // getLimit
  getValuesOnServer();
  Serial.println("Setup done");
}

void resetWifiAndHttp(int statusCode) {
  if (statusCode < 0) {
    Serial.print("Reconnect all");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    http.stop();
    http = HttpClient(client, apiServer, 80);
  }
}

void ativaDesativaRele(bool ativar) {
  Serial.println("rele: " + String(ativar));
  digitalWrite(relePin, !ativar);
  turned_on = ativar;
  data_change = true;
  Serial.println("---------------------------------------");
}

void getValuesOnServer() {
  String response;
  int statusCode = 0;
  StaticJsonBuffer<200> jsonBuffer;
#ifdef ___debug
  Serial.println("GET tempLimit");
#endif
  http.get("/channels/" + String(channelNumber) + "/feeds/last.json");

  // read the status code and body of the response
  statusCode = http.responseStatusCode();
  response = http.responseBody();
  String json = "";
  if (response.length() >= 5) { // remove invalid chars
    int first = response.indexOf("{");
    int last = response.lastIndexOf("}");
    char js[200];
    json = response.substring(first, last + 1);
    json.toCharArray(js, 200);
    JsonObject &root = jsonBuffer.parseObject(js);
    if (!root.success()) {
      Serial.println("Json fails:" + json + "<-");
      tempLimit = 60;
      tempUsuario = -1;
    } else {
      float temp = root["field3"];
      float tempU = root["field4"];
#ifdef ___debug
      Serial.println("field3: " + String(temp));
      Serial.println("field4: " + String(tempU));
#endif
      tempUsuario = float(tempU);
      tempLimit = float(temp);
    }
  }

#ifdef ___debug
  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("getTempLimitResponse: ");

  Serial.println(response);
#endif
  resetWifiAndHttp(statusCode);
  Serial.println("---------------------------------------");
}

void getTalkBack() {
  String response;
  int statusCode = 0;
  Serial.println("GET commands");
  http.get("/talkbacks/" + talkBackID + "/commands/execute?api_key=" +
           talkBackAPIKey);

  // read the status code and body of the response
  statusCode = http.responseStatusCode();
  response = http.responseBody();

  if (response.indexOf("limit") >= 0) {
    int pos = response.indexOf("limit");
    String val = response.substring(pos + 6);
    tempLimit = val.toFloat();
  }
  if (response.indexOf("power") >= 0) {
    int pos = response.indexOf("power");
    String val = response.substring(pos + 6);
    if (val == "on") {
      tempLimit = 51;
      ativaDesativaRele(true);
      tempUsuario = (tempUsuario + tempAtual) / 2;
      Serial.println("ligado pelo usuario");
    } else if (val == "off") {
      tempLimit = 50;
      ativaDesativaRele(false);
      Serial.println("Desligado pelo usuario");
    }
  }
#ifdef ___debug
  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("talkBackResponse: ");
  Serial.println(response);
#endif
  resetWifiAndHttp(statusCode);
  Serial.println("---------------------------------------");
}

void postOnThingSpeak() {
  String response;
  String fields = "";
  int statusCode = 0;
  String contentType = "application/x-www-form-urlencoded";
  String postData = "api_key=" + apiKey;
  fields += "&field1=" + String(tempMedia);
  fields += "&field2=" + String(turned_on);
  fields += "&field3=" + String(tempLimit);
  fields += "&field4=" + String(tempUsuario);
  postData += fields;
  http.post("/update", contentType, postData.c_str());

  // read the status code and body of the response
  statusCode = http.responseStatusCode();
  response = http.responseBody();
  //#ifdef ___debug
  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("postData: ");
  Serial.println(postData);
  // Serial.print("fields: ");
  // Serial.println(fields);
  Serial.print("postOnThingSpeakResponse: ");
  Serial.println(response);
  //#endif
  data_change = false;
  nextPost = millis() + 15000; // ThingSpeak min delay
  resetWifiAndHttp(statusCode);
  Serial.println("---------------------------------------");
}

void loop() {
  float temp = 0;
  int amostras = 30;

  for (size_t i = 0; i < amostras; i++) { // amostras para estabilizar o valor
    // Serial.println("analog: " + String(float(analogRead(tempPin))));
    temp += ((1.1 * float(analogRead(tempPin)) * 100) / 1023);
    // temp += (float(analogRead(tempPin)) * 0.1);
    delay(20);
  }
  tempAtual = tempMedia = (temp / amostras);

  if (millis() >= nextPost && data_change) {
    postOnThingSpeak();
  } else { // Not in sequence
    getTalkBack();
  }

  Serial.println("tempMedia/tempLimit: " + String(tempMedia) + ":" +
                 String(tempLimit));
  Serial.println("turned_on: " + String(turned_on));
  if (tempLimit < 50) {
    if (tempMedia > tempLimit && turned_on == 0) {
      ativaDesativaRele(true);
    } else if (tempMedia <= tempLimit && turned_on == 1) {
      ativaDesativaRele(false);
    }
  }

  delay(1000);
}
