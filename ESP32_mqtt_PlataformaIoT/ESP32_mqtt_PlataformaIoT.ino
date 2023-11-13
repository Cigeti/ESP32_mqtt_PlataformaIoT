#include <Arduino.h>
#include "Splitter.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "DHTesp.h"  //Libreria para el sensor de temperatura 


//CONEXION CON IoTPROJECTS
String dId = "";
String webhook_pass = "";
String webhook_endpoint = "";
const char *mqtt_server= "";


//CONFIGURACION DE WiFi
const char *wifi_ssid = "Nombre de la red";
const char *wifi_password = "Contraseña";

const long sendDBInterval = 300000;


//CONFIGURACION DE SENSORES Y ACTUADORES
struct Config                               //PINS-INPUTS (json construct setup)
{
  float sensor_1;
  float sensor_2;
  //float temperature;
  int ledstate;
};
Config config;

//PINS-OUTPUTS
#define led 2
int pinDHT = 15;

//Functions definitions
void sendToDashboard(const Config & config);
bool get_mqtt_credentials();
void check_mqtt_connection();
bool reconnect();
void process_sensors();
void process_actuators();
void connect_to_IoTCRv2();
void send_data_to_broker();
void callback(char *topic, byte *payload, unsigned int length);
void process_incoming_msg(String topic, String incoming);
void print_stats();
void clear();
DHTesp dht; //Instanciamos el DHT

//Global Vars
WiFiClient espclient;
PubSubClient client(espclient);
Splitter splitter;
DynamicJsonDocument mqtt_data_doc(2048);

long lastReconnectAttemp = 0;
long varsLastSend[20];
String last_received_msg = "";
String last_received_topic = "";
long lastStats = 0;
long lastsendToDB = 0;



//_________________________________SET-UP_______________________________________
void setup()
{
  
  Serial.begin(115200);
  pinMode(led, OUTPUT);
  dht.setup(pinDHT, DHTesp::DHT22);
  clear();

  Serial.print("\n\n\nWiFi Connection in Progress" );
  WiFi.begin(wifi_ssid, wifi_password);

  int counter = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    counter++;

    if (counter > 10)
    {
      Serial.print("  ⤵");
      Serial.print("\n\n         Ups WiFi Connection Failed :( ");
      Serial.println(" -> Restarting..." );
      delay(2000);
      ESP.restart();
    }
  }

  Serial.print("  ⤵" );
  //Printing local ip
  Serial.println( "\n\n         WiFi Connection -> SUCCESS :)" );
  Serial.print("\n         Local IP -> ");
  Serial.print(WiFi.localIP());
  delay(5000);

  get_mqtt_credentials();
  client.setCallback(callback);
}


//__________________________________LOOP________________________________________
void loop()
{
  check_mqtt_connection();
  client.loop();
  process_sensors();
  sendToDashboard(config);
  print_stats();
 
}


//________________________________SENSORES ⤵____________________________________
void process_sensors()
{
   TempAndHumidity data = dht.getTempAndHumidity();
 
  //get temperature simulation
  config.sensor_1 = data.temperature;
  //get humidity simulation
  config.sensor_2 = data.humidity;

  //get led status
  mqtt_data_doc["variables"][6]["last"]["value"] = (HIGH == digitalRead(led));

}


//________________________PUBLICAR EN IoTPROJECTS ⤵_____________________________
void sendToDashboard(const Config & config)
{
  if (!(millis() - lastsendToDB > sendDBInterval))
  {
//*********************CADA POSICIÓN ES UN WIDGET QUE CREASTE*******************


    mqtt_data_doc["variables"][0]["last"]["value"] = config.sensor_1;
                                                       //posición 1 del template
    mqtt_data_doc["variables"][1]["last"]["value"] = config.sensor_2;
                                                       //posición 2 del template
    mqtt_data_doc["variables"][2]["last"]["value"] = config.sensor_1;
                                                       //posición 3 del template
    mqtt_data_doc["variables"][3]["last"]["value"] = config.sensor_2;


//******************************************************************************
    send_data_to_broker();
  }
  else
  {
    Serial.println("ENVIANDO A BASE DE DATOS");
  // send_data_to_DB();
   lastsendToDB = millis();
  }
}


//________________________________ACTUADORES ⤵__________________________________
void process_actuators()
{
  if (mqtt_data_doc["variables"][1]["last"]["value"] == true)
                                                       //posición 4 del template
  {
    digitalWrite(led, HIGH);
    //mqtt_data_doc["variables"][4]["last"]["value"] = "";
    varsLastSend[6] = 0;                               //posición 6 del template
  }
  else if (mqtt_data_doc["variables"][1]["last"]["value"] == false)
                                                       //posición 4 del template
  {
    digitalWrite(led, LOW);
    //mqtt_data_doc["variables"][5]["last"]["value"] = "";
    varsLastSend[6] = 0;                               //posición 6 del template
  }
}


//_________________________________PRINTS ⤵_____________________________________
void print_stats()
{
  long now = millis();

  if (now - lastStats > 2000)
  {
    lastStats = millis();
    clear();
    Serial.print("\n");
    Serial.print( "\n╔══════════════════════════╗" );
    Serial.print( "\n║       SYSTEM STATS       ║" );
    Serial.print( "\n╚══════════════════════════╝" );
    Serial.print("\n\n");
    Serial.print("\n\n");

    Serial.print( "# \t Name \t\t Var \t\t Type  \t\t Count  \t\t Last V \n\n");
    for (int i = 0; i < mqtt_data_doc["variables"].size(); i++)
    {
      String variableFullName = mqtt_data_doc["variables"][i]["variableFullName"];
      String variable = mqtt_data_doc["variables"][i]["variable"];
      String variableType = mqtt_data_doc["variables"][i]["variableType"];
      String lastMsg = mqtt_data_doc["variables"][i]["last"];
      long counter = mqtt_data_doc["variables"][i]["counter"];

      Serial.println(String(i) + " \t " + variableFullName.substring(0,5) + " \t\t " + variable.substring(0,10) + " \t " + variableType.substring(0,5) + " \t\t " + String(counter).substring(0,10) + " \t\t " + lastMsg);
    }
    Serial.print( "\n\n Last Incomming Msg -> " + last_received_msg);
  }
}
 
//________________________________OBTENER_CREDENCIALES ⤵________________________
bool get_mqtt_credentials()
{


  Serial.print( "\n\n\nGetting MQTT Credentials from WebHook ⤵");
  delay(1000);

  String toSend = "dId=" + dId + "&password=" + webhook_pass;

  HTTPClient http;
  http.begin(webhook_endpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int response_code = http.POST(toSend);

  if (response_code < 0)
  {
    Serial.print("\n\n         Error Sending Post Request :( " );
    http.end();
    return false;
  }

  if (response_code != 200)
  {
    Serial.print("\n\n         Error in response :(   e-> " + response_code);
    http.end();
    return false;
  }

  if (response_code == 200)
  {
    String responseBody = http.getString();

    Serial.print( "\n\n         Mqtt Credentials Obtained Successfully :) " );

    deserializeJson(mqtt_data_doc, responseBody);
    http.end();
    delay(1000);
  }

  return true;

}

//____________________________CHECK_MQTT_CONECTION ⤵____________________________
void check_mqtt_connection()
{

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print( "\n\n         Ups WiFi Connection Failed :( ");
    Serial.println(" -> Restarting...");
    delay(15000);
    ESP.restart();
  }

  if (!client.connected())
  {

    long now = millis();

    if (now - lastReconnectAttemp > 5000)
    {
      lastReconnectAttemp = millis();
      if (reconnect())
      {
        lastReconnectAttemp = 0;
      }
    }
  }
}

//________________________________SEND_TO_BROKER ⤵______________________________
void send_data_to_broker()
{

  long now = millis();

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++)
  {

    if (mqtt_data_doc["variables"][i]["variableType"] == "output")
    {
      continue;
    }

    int freq = mqtt_data_doc["variables"][i]["variableSendFreq"];

    if (now - varsLastSend[i] > freq * 1000)
    {
      varsLastSend[i] = millis();
      mqtt_data_doc["variables"][i]["last"]["save"] = 0;

      String str_root_topic = mqtt_data_doc["topic"];
      String str_variable = mqtt_data_doc["variables"][i]["variable"];
      String topic = str_root_topic + str_variable + "/sdata";

      String toSend = "";

      serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);

      client.publish(topic.c_str(), toSend.c_str());


      //STATS
      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;

    }
  }
}

//______________________________________________________________________________
void send_data_to_DB()
{
  long now = millis();

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++)
  {

    if (mqtt_data_doc["variables"][i]["variableType"] == "output")
    {
      continue;
    }

    mqtt_data_doc["variables"][i]["last"]["save"] = 1;

    String str_root_topic = mqtt_data_doc["topic"];
    String str_variable = mqtt_data_doc["variables"][i]["variable"];
    String topic = str_root_topic + str_variable + "/sdata";

    String toSend = "";

    serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);
    client.publish(topic.c_str(), toSend.c_str());
    Serial.print(" Mqtt ENVIADO:) " );

      //STATS
    long counter = mqtt_data_doc["variables"][i]["counter"];
    counter++;
    mqtt_data_doc["variables"][i]["counter"] = counter;
  }
}


//________________________________RECONNECT ⤵___________________________________
bool reconnect()
{

  if (!get_mqtt_credentials())
  {
    Serial.println("\n\n      Error getting mqtt credentials :( \n\n RESTARTING IN 10 SECONDS");

    delay(10000);
    ESP.restart();
  }

  //Setting up Mqtt Server
  client.setServer(mqtt_server, 1883);

  Serial.print("\n\n\nTrying MQTT Connection ⤵");

  String str_client_id = "device_" + dId + "_" + random(1, 9999);
  const char *username = mqtt_data_doc["username"];
  const char *password = mqtt_data_doc["password"];
  String str_topic = mqtt_data_doc["topic"];

  if (client.connect(str_client_id.c_str(), username, password))
  {
    Serial.print( "\n\n         Mqtt Client Connected :) ");
    delay(2000);

    client.subscribe((str_topic + "+/actdata").c_str());
    return true;
  }
  else
  {
    Serial.print( "\n\n         Mqtt Client Connection Failed :( " );
  }
}


//________________________________SENSORES ⤵____________________________________
//TEMPLATE ⤵
void process_incoming_msg(String topic, String incoming){

  last_received_topic = topic;
  last_received_msg = incoming;

  String variable = splitter.split(topic, '/', 2);

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++ ){

    if (mqtt_data_doc["variables"][i]["variable"] == variable){

      DynamicJsonDocument doc(256);
      deserializeJson(doc, incoming);
      mqtt_data_doc["variables"][i]["last"] = doc;

      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;
    }
  }

  process_actuators();

}

//________________________________CALLBACK ⤵____________________________________
void callback(char *topic, byte *payload, unsigned int length)
{

  String incoming = "";

  for (int i = 0; i < length; i++)
  {
    incoming += (char)payload[i];
  }

  incoming.trim();

  process_incoming_msg(String(topic), incoming);
}



//________________________________CLEAR_SERIAL ⤵________________________________
void clear()
{
  Serial.write(27);    // ESC command
  Serial.print("[2J"); // clear screen command
  Serial.write(27);
  Serial.print("[H"); // cursor to home command

}
