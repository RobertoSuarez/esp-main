#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Adafruit_Fingerprint.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266HTTPClient.h>
#include <TaskScheduler.h>
#include <uri/UriBraces.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>

// Programación del dispositivo

/*
  codigo infrarrojo para el tv
  E0E040BF - apagar
  E0E0E01F - subir volumen
  E0E0D02F - bajar volumen
*/

// Declaración de funciones
void Servidores();
void ObteniendoLahuella();
void AuthFingerPrint();
uint8_t RegistrarUsuario(uint32_t id);
void RegistrarPersona();
uint8_t deleteFingerprint(uint8_t id);
void addIndex();
void addRequestHandlers();
void apagarLuces();
void encederLuces();
void encenderAire();
void encenderProyector();
void abrirPuerta();
void listenIR();
void showUTEQ();
void showIP();

// implementacion de servidor http y websocket
//

String correo_usuario = "";

const char * SSID = "arduino";
const char * PASSWORD = "arduino123456";

int valorSensor = 0;

// Puertos
int PUERTA = 0;     // D3
int LUCES = 2;      // D4
int AIRE = 14;      // D5 prender el aire - resive los datos de IR
int PROYECTOR = 12; // D6 envia los datos del bluetooth
int FINGER_RX = 13; // RX
int FINGER_TX = 15; // TX
int RECV_PIN =  10; // S3
int FOTOPIN = A0;

// Intervalos de tiempos
const unsigned long intervaloFinger = 1000UL;
unsigned long eventoFinger = 1000UL;


// IR -- configuración para los infrarrojos
IRrecv irrecv(14); // D5 - anteriormente era el aire.
decode_results resultado;

IRsend irsend(12); // D6 - anterirormente era para el proyector.

// Configuración para la pantalla
#define SCREEN_WIDTH 128 // ancho pantalla OLED
#define SCREEN_HEIGHT 32 // alto pantalla OLED

// Objeto de la clase Adafruit_SSD1306
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define USE_SERIAL Serial

ESP8266WiFiMulti WiFiMulti;

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
const int capacidad = 200;

// http clientes
HTTPClient http;
WiFiClient wifiClient;

// configuración para el fingerprint
SoftwareSerial mySerial(FINGER_RX, FINGER_TX);

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
//Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial1);

//SoftwareSerial espcam(AIRE, PROYECTOR);

// definición de las tareas.
Scheduler runner;
Task TaskServer(0, TASK_FOREVER, &Servidores);
Task TaskAuth(0, TASK_FOREVER, &AuthFingerPrint);
Task TaskRegistraUsuario(0, TASK_FOREVER, &RegistrarPersona);

StaticJsonDocument<300> usuarioPuertaDoc;

// funcionamos para manejar la pantalla
void setStyleDefault(uint8_t size)
{
  display.clearDisplay();
  display.display();
  delay(50);
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
}

void showMenssage(String msg)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
  delay(50);
}

void write_uteq(void)
{
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 0);
  display.cp437(true);

  display.print("UTEQ");
  display.display();

  delay(2000);
}

// Esta función trabaja con un task en task eschulede
void RegistrarPersona()
{
  showMenssage("Registrando persona");
  delay(2000);
  // showMenssage("Ingrese su huella");
  // delay(2000);
  // showMenssage("Usuario registrado");
  // delay(2000);
  StaticJsonDocument<500> docCorreo;
  String url = "http://blooming-tundra-94814.herokuapp.com/api/usuarios/email/" + correo_usuario;
  http.begin(wifiClient, url);
  int httpCode = http.GET();
  if (httpCode > 0)
  {
    if (httpCode == 404)
    {
      showMenssage("El correo no valido");
      delay(2000);
    }
    else if (httpCode == 200)
    {
      String payload = http.getString();
      DeserializationError error = deserializeJson(docCorreo, payload);

      if (error)
      {
        showMenssage("Error en registro");
        delay(2000);
      }
      else
      {
        String nombre = docCorreo["nombre"];
        showMenssage("Nombre: " + nombre);
        delay(2000);

        uint32_t id_usuario = docCorreo["id"];
        showMenssage("Id: " + String(id_usuario));
        delay(2000);

        // inicia el proceso de registro de huella
        RegistrarUsuario(id_usuario);
      }
    }
    else
    {
      showMenssage("Codigo: " + String(httpCode));
      delay(2000);
    }
    http.end();
  }
  else
  {
    showMenssage("API REST no responde");
    delay(5000);
  }

  TaskAuth.enable();
  TaskRegistraUsuario.disable();
}

// RegistrarUsuario toma el id donde se almacenara la huella
// en el fingerprint
uint8_t RegistrarUsuario(uint32_t id)
{

  int p = -1;

  // la primera toma de la huella
  Serial.print("Esperando un dedo válido para guardar como #");
  showMenssage("Esperando la huella");
  Serial.println(id);
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      USE_SERIAL.println("Image taken");
      showMenssage("Imagen tomada");
      delay(2000);
      break;
    case FINGERPRINT_NOFINGER:
      USE_SERIAL.print(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      USE_SERIAL.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      USE_SERIAL.println("Imaging error");
      break;
    default:
      USE_SERIAL.println("Unknown error");
      break;
    }
    delay(100);
  }

  // OK success!

  p = finger.image2Tz(1);
  switch (p)
  {
  case FINGERPRINT_OK:
    USE_SERIAL.println("Image converted");
    showMenssage("Imagen convertida");
    delay(2000);
    break;
  case FINGERPRINT_IMAGEMESS:
    USE_SERIAL.println("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    USE_SERIAL.println("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    USE_SERIAL.println("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    USE_SERIAL.println("Could not find fingerprint features");
    return p;
  default:
    USE_SERIAL.println("Unknown error");
    return p;
  }

  // Serial.println("Remove finger");
  USE_SERIAL.println("Remover el dedo");
  showMenssage("Quitar la huella");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER)
  {
    p = finger.getImage();
  }
  USE_SERIAL.print("ID ");
  USE_SERIAL.println(id);
  p = -1;
  USE_SERIAL.println("Vuelva a colocar el mismo dedo");
  showMenssage("Vuelva a colocar el mismo dedo");
  delay(2000);
  // Segunda toma de la huella
  while (p != FINGERPRINT_OK)
  {
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      USE_SERIAL.println("Imagen tomada");
      showMenssage("Imagen tomada");
      delay(2000);
      break;
    case FINGERPRINT_NOFINGER:
      USE_SERIAL.print("\r.");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      USE_SERIAL.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      USE_SERIAL.println("Imaging error");
      break;
    default:
      USE_SERIAL.println("Unknown error");
      break;
    }

    delay(100);
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p)
  {
  case FINGERPRINT_OK:
    USE_SERIAL.println("Imagen convertida");
    showMenssage("Imagen convertida");
    delay(2000);
    break;
  case FINGERPRINT_IMAGEMESS:
    USE_SERIAL.println("Image too messy");
    return p;
  case FINGERPRINT_PACKETRECIEVEERR:
    USE_SERIAL.println("Communication error");
    return p;
  case FINGERPRINT_FEATUREFAIL:
    USE_SERIAL.println("Could not find fingerprint features");
    return p;
  case FINGERPRINT_INVALIDIMAGE:
    USE_SERIAL.println("Could not find fingerprint features");
    return p;
  default:
    USE_SERIAL.println("Unknown error");
    return p;
  }

  // OK converted!
  USE_SERIAL.print("Creando el modelo para #");
  USE_SERIAL.println(id);
  // creando el modelo de la huella

  showMenssage("Creando el modelo");
  delay(2000);

  p = finger.createModel();
  if (p == FINGERPRINT_OK)
  {
    USE_SERIAL.println("Prints matched!");
    showMenssage("Modelo creado");
    delay(2000);
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    USE_SERIAL.println("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_ENROLLMISMATCH)
  {
    USE_SERIAL.println("Fingerprints did not match");
    showMenssage("Las huellas dactilares no coinciden");
    delay(2000);
    return p;
  }
  else
  {
    USE_SERIAL.println("Unknown error");
    showMenssage("Error desconocido");
    delay(2000);
    return p;
  }

  USE_SERIAL.print("ID ");
  USE_SERIAL.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK)
  {

    USE_SERIAL.print("Almacenada \n");
    showMenssage("Huella almacenada");
    delay(2000);
    return p; // esto agrege (analizarlo mas)
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    USE_SERIAL.println("Communication error");
    return p;
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    USE_SERIAL.println("Could not store in that location");
    showMenssage("No se pudo almacenar en esa ubicación");
    delay(2000);
    return p;
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    USE_SERIAL.println("Error writing to flash");
    return p;
  }
  else
  {
    USE_SERIAL.println("Unknown error");
    return p;
  }
}

// funciones para controlar las diferentes acciones
// void handlerRegistrarHuella(StaticJsonDocument<capacidad> documento, uint8_t num)
// {
//   StaticJsonDocument<200> docCorreo;
//   uint32_t id_usuario = 0;

//   String correo = documento["correo"];
//   showMenssage("Registrando huella");

//   String url = "http://blooming-tundra-94814.herokuapp.com/api/usuarios/email/" + correo;
//   http.begin(wifiClient, url);
//   int httpCode = http.GET();
//   if (httpCode > 0)
//   {
//     String payload = http.getString();
//     http.end();

//     DeserializationError error = deserializeJson(docCorreo, payload);

//     if (error)
//     {
//       showMenssage("Error en registro");
//       delay(2000);
//       return;
//     }

//     id_usuario = docCorreo["id_usuario"];
//     RegistrarUsuario(id_usuario);
//   }
//   else
//   {

//     showMenssage("API REST no responde");
//     delay(5000);
//   }
// }

uint8_t client_esp = 0;
uint8_t client_app = 0;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{

  StaticJsonDocument<capacidad> doc;

  switch (type)
  {
  case WStype_DISCONNECTED:
    USE_SERIAL.printf("[%u] Disconnected!\n", num);
    break;
  case WStype_CONNECTED:
  {
    IPAddress ip = webSocket.remoteIP(num);
    USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    char bufferRespuesta[100];
    DynamicJsonDocument docConectado(1024);
    docConectado["msg"] = "Cliente conentado correctamente";
    serializeJson(docConectado, bufferRespuesta);
    // send message to client
    webSocket.sendTXT(num, bufferRespuesta);
  }
  break;
  case WStype_TEXT:
    USE_SERIAL.printf("[%u] get Text: %s\n", num, payload);

    // parseamos el json
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      USE_SERIAL.println("Error al convertir le json");
      USE_SERIAL.println(error.c_str());
      break;
    }

    String accion = doc["accion"];
    String tipo = doc["tipo"];

    USE_SERIAL.printf("Acción: %s, tipo de dispositivo: %s \n", accion.c_str(), tipo.c_str());

    if (accion == "registrar_huella") {
      USE_SERIAL.println("Iniciar el proceso de registrar una huella en el otro esp");
      //webSocket.broadcastTXT("iniciar_registro");
      USE_SERIAL.printf("cliente_esp: %d\n", client_esp);
      if (client_esp > 0) {
        char bufferRegistrar[100];
        DynamicJsonDocument docRegistrar(1024);
        docRegistrar["accion"] = "registrar_huella";
        docRegistrar["email"] = doc["email"];
        serializeJson(docRegistrar, bufferRegistrar);
        //USE_SERIAL.println(bufferRegistrar);
        webSocket.sendTXT(client_app, bufferRegistrar);
      } else {
        char docInfoString[100];
        DynamicJsonDocument docInfo(1024);
        docInfo["msg"] = "No esta conectado el otro esp";
        serializeJson(docInfo, docInfoString);
        webSocket.sendTXT(client_app, docInfoString);
      }
    }

    if (accion == "usuario_conectado") {
      USE_SERIAL.println("Un dispositivo se conecto");
      if (tipo == "app") {
        client_app = num;
      } else if (tipo == "dev") {
        client_esp = num;
      }
    }


    break;
  }
}

void Servidores()
{
  webSocket.loop();
  server.handleClient();
  listenIR();
}

StaticJsonDocument<300> docUsuario;

void AuthFingerPrint()
{
  // if (espcam.available()) {
  //   USE_SERIAL.println("Info resivida");
    
  //   String payloadESP32 = espcam.readString();
  //   USE_SERIAL.println(payloadESP32);
  //   if (String("abrir") == payloadESP32) {
  //     USE_SERIAL.println("se debe abrir la puerta...");
  //   }
  // }

  

  // valorSensor = analogRead(FOTOPIN);
  // USE_SERIAL.print("Valor de luz: "); USE_SERIAL.println(valorSensor);

  delay(100);

  // si esta función realiza 100 intentos de obtener huella y no tiene resultado se termina
  // USE_SERIAL.println("inicamos el proceso de analizar la huella");
  showMenssage("Esperando una huella valida... \n" + WiFi.localIP().toString());
  // delay(2000);
  int contador = 0;
  bool buscar = true;
  uint8_t p = -1;
  // while (contador < 100 && buscar)
  // {

  // analizando la huella
  // USE_SERIAL.printf("Intento %d \n", contador);
  // hasta que tome la huella este while seguira iterando
  while (p != FINGERPRINT_OK)
  {
    contador++;
    p = finger.getImage();
    switch (p)
    {
    case FINGERPRINT_OK:
      USE_SERIAL.println("Huella tomada correctamente");
      showMenssage("Huella tomada");
      delay(1000);
      break;
    case FINGERPRINT_NOFINGER:
      // USE_SERIAL.println("Huella no colocada");
      break;

    default:
      break;
    }

    if (contador == 80)
    {
      return;
    }
    // delay(100);
  }

  p = finger.image2Tz();
  switch (p)
  {
  case FINGERPRINT_OK:
    USE_SERIAL.println("Imagen convertida");
    showMenssage("Imagen convertida");
    delay(1000);
    break;
  default:
    showMenssage("Conversión de imagen error.");
    return;
  }

  // buscamos la huella en la base de datos del fingerprint
  p = finger.fingerSearch();
  switch (p)
  {
  case FINGERPRINT_OK:
    USE_SERIAL.println("Busqueda realizada");
    USE_SERIAL.print("ID Huella: ");
    USE_SERIAL.println(finger.fingerID);
    buscar = false;
    break;
  case FINGERPRINT_NOTFOUND:
    USE_SERIAL.println("Huella no valida");
    showMenssage("Huella no valida");
    delay(3000);
    return;
  default:
    return;
  }
  // }
  // USE_SERIAL.println("Salido del while");

  if (!buscar && finger.fingerID > 0)
  {
    showMenssage("Usuario ID: " + String(finger.fingerID));
    delay(2000);

    if (WiFi.status() == WL_CONNECTED)
    {

      String url = "http://blooming-tundra-94814.herokuapp.com/api/usuarios/authentication/" + (String)finger.fingerID;
      http.begin(wifiClient, url);
      int httpCode = http.GET();
      if (httpCode > 0)
      {
        if (httpCode == 200)
        {
          // USE_SERIAL.println(http.getString());
          DeserializationError error = deserializeJson(docUsuario, http.getString());
          if (error)
          {
            USE_SERIAL.println("Error al convertir le json");
            USE_SERIAL.println(error.c_str());
            return;
          }

          String nombreUsuario = docUsuario["nombre"];
          bool acceso = docUsuario["acceso_valido"];

          USE_SERIAL.print("Nombre: ");
          USE_SERIAL.println(nombreUsuario);
          USE_SERIAL.print("Acceso: ");
          USE_SERIAL.println(acceso);
          if (acceso)
          {
            showMenssage("Bienvenido \n" + nombreUsuario);

            // se abre la puerta
            // digitalWrite(PUERTA, LOW);
            // digitalWrite(PUERTA, HIGH);

            abrirPuerta();

            uint8_t segundos = 5;
            for (int segundo = segundos; segundo != 0; segundo--)
            {
              setStyleDefault(1);
              display.setCursor(0, 2);
              display.println(nombreUsuario);
              display.setTextSize(2);
              display.setCursor((display.width() / 2) - 5, display.height() / 2);
              display.println(segundo);
              display.display();
              delay(1000);
            }


            showMenssage("Puerta cerrada");
          }
          else
          {
            showMenssage(nombreUsuario + " no tiene permitido acceder.");
            delay(5000);
          }

          // si el usuario es profesor, por ahora todo usuario que inicie se
          // realiza estos procesos.
          if (true) {
            valorSensor = analogRead(FOTOPIN);
            if (valorSensor < 1000) {
              // prender la luz
              encederLuces();
            }
            encenderAire();
            encenderProyector();
          }

        }
        else
        {
          showMenssage("No responde la API Rest full");
          delay(2000);
        }

        http.end();
      }

      USE_SERIAL.print("Code: ");
      USE_SERIAL.println(httpCode);
    }
  }

  delay(100);
}

uint8_t deleteFingerprint(uint8_t id)
{
  uint8_t p = -1;

  p = finger.deleteModel(id);

  if (p == FINGERPRINT_OK)
  {
    Serial.println("Deleted!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not delete in that location");
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
  }
  else
  {
    Serial.print("Unknown error: 0x");
    Serial.println(p, HEX);
  }

  return p;
}

void setup()
{
  // put your setup code here, to run once:
  USE_SERIAL.begin(9600);
  delay(100);

  irrecv.enableIRIn();
  irsend.begin();

  //irsend.sendNEC(0x20DF10EF, 32);
  
  
  

  USE_SERIAL.println();

  for (uint8_t t = 4; t > 0; t--)
  {
    USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
    USE_SERIAL.flush();
    delay(1000);
  }

  // Estableciendo el modo de los pines
  // puerta y luces
  pinMode(PUERTA, OUTPUT);
  pinMode(LUCES, OUTPUT);
  pinMode(PROYECTOR, OUTPUT);
  //pinMode(AIRE, OUTPUT);
  // fotorrisistor
  pinMode(FOTOPIN, INPUT);

  // Valores por defecto de los componentes
  // Las luces inician estando apgadas
  digitalWrite(LUCES, HIGH);
  digitalWrite(PUERTA, HIGH);

  // inician apagados
  digitalWrite(PROYECTOR, LOW);
  //digitalWrite(AIRE, LOW);

  valorSensor = analogRead(FOTOPIN);
  if (valorSensor < 1000) {
    // luces prendidas
    digitalWrite(LUCES, LOW);
  }

  // iniciamos la pantalla oled
  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    USE_SERIAL.println("pantalla iniciada");
    display.display();
    delay(2000);
    display.clearDisplay();

    showMenssage("UTEQ");
  }
  else
  {
    USE_SERIAL.println("error en la pantalla");
    for (;;)
      ;
  }

  showUTEQ();
  delay(2000);
  


  // iniciamos comunicación con el fingerprint
  finger.begin(57600);
  delay(5);
  if (finger.verifyPassword())
  {
    Serial.println("Se a detectado un sensor de huella");
  }
  else
  {
    //Serial.println("No hay comunicación con el fingerprint");
  }

  // configuraciones del ssid y password
  WiFiMulti.addAP(SSID, PASSWORD);

  while (WiFiMulti.run() != WL_CONNECTED)
  {
    delay(100);
  }

  USE_SERIAL.print("IP: ");
  USE_SERIAL.println(WiFi.localIP());
  USE_SERIAL.println(WiFi.macAddress());

  showIP();
  

  // start webSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  if (MDNS.begin("esp8266"))
  {
    USE_SERIAL.println("MDNS responder started");
  }

  addRequestHandlers();
  addIndex();

  server.begin();
  // Add service to MDNS
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);

  // Agregación de tareas e habilitación
  runner.addTask(TaskAuth);
  runner.addTask(TaskServer);
  runner.addTask(TaskRegistraUsuario);
  TaskAuth.enable();
  TaskServer.enable();
}

void loop()
{
  webSocket.loop();
  server.handleClient();
}

void apagarLuces() {
  digitalWrite(LUCES, HIGH);
}

void encederLuces() {
  digitalWrite(LUCES, LOW);
}


void encenderAire() {
  showMenssage("Aire encendido");
  irsend.sendPanasonicAC32(0xB27BE0, 32);
  delay(1000);
  showIP();
}

void encenderProyector() {
  irsend.sendEpson(0x849BA517, 32);
  showMenssage("Proyector encendido");
  delay(1000);
  showIP();
}

// Abre el rele por 2 segundos
void abrirPuerta() {
  showMenssage("Abrir la puerta");
  digitalWrite(PUERTA, LOW);
  delay(500);
  digitalWrite(PUERTA, HIGH);
  showIP();
}

void listenIR() {
  if (irrecv.decode(&resultado)) {
    
    switch (resultado.decode_type) {
      default:
      case UNKNOWN:      Serial.print("UNKNOWN");       break ;
      case NEC:          Serial.print("NEC");           break ;
      case SONY:         Serial.print("SONY");          break ;
      case RC5:          Serial.print("RC5");           break ;
      case RC6:          Serial.print("RC6");           break ;
      case DISH:         Serial.print("DISH");          break ;
      case SHARP:        Serial.print("SHARP");         break ;
      case JVC:          Serial.print("JVC");           break ;
      //    case SANYO:        Serial.print("SANYO");         break ;
      //    case MITSUBISHI:   Serial.print("MITSUBISHI");    break ;
      case SAMSUNG:      Serial.print("SAMSUNG");       break ;
      case LG:           Serial.print("LG");            break ;
      case WHYNTER:      Serial.print("WHYNTER");       break ;
      //    case AIWA_RC_T501: Serial.print("AIWA_RC_T501");  break ;
      case PANASONIC:    Serial.print("PANASONIC");     break ;
      case DENON:        Serial.print("Denon");         break ;
    }
    USE_SERIAL.println();
    USE_SERIAL.println(resultado.value, HEX);
    irrecv.resume();
  }
}

void addRequestHandlers() {

 

  // encender y apagar el proyector
  server.on(UriBraces("/proyector-encender/"), []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);

    // int val = 0;
    // val = digitalRead(PROYECTOR);
    // digitalWrite(PROYECTOR, !val);
    // val = digitalRead(PROYECTOR);

    encenderProyector();

    server.send(200, "application/json", "{\"estado\": 1 }"); });

  // Estado del proyecto
  server.on(UriBraces("/proyector-estado/"), []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);

    int val = 0;
    val = digitalRead(PROYECTOR);
    //digitalWrite(LUCES, !val);

    server.send(200, "application/json", "{\"estado\": " + String(val) + "}"); });

  // encender y apagar el Aire acondicinado
  server.on(UriBraces("/aire-encender/"), []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);

    // int val = 0;
    // val = digitalRead(AIRE);
    // digitalWrite(AIRE, !val);
    // val = digitalRead(AIRE);

    encenderAire();

    server.send(200, "application/json", "{\"estado\": 1 }"); });

  // Estado del aire
  server.on(UriBraces("/aire-estado/"), []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);

    int val = 0;
    val = digitalRead(AIRE);
    //digitalWrite(LUCES, !val);

    server.send(200, "application/json", "{\"estado\": " + String(val) + "}"); });

  server.on(UriBraces("/puerta/{}"), []()
            {
              server.sendHeader("Access-Control-Allow-Origin", "*", true);
              String accion = server.pathArg(0);
              if (accion == "abrir_puerta")
              {
                USE_SERIAL.println("Se debe abrir la puerta");
                digitalWrite(PUERTA, LOW);
                server.send(200, "text/plain", "Acción: " + accion);
              }
              else if (accion == "cerrar_puerta")
              {
                USE_SERIAL.println("Se debe cerrar la puerta");
                digitalWrite(PUERTA, HIGH);
                server.send(200, "text/plain", "Acción: " + accion);
              }
              else
              {
                server.send(400, "application/json", "{error: \"acción no encontrada\"}");
              } });

  server.on(UriBraces("/puerta"), HTTP_POST, []() {
    
    server.sendHeader("Access-Control-Allow-Origin", "*", true);
    Serial.println(server.arg("plain"));
    if (server.arg("plain") != "") {
      Serial.println("verificamos la luminosidad");
      DeserializationError error = deserializeJson(usuarioPuertaDoc, server.arg("plain"));
      if (error) {
        Serial.println("Error al convertir los datos del usuario");
        Serial.println(error.c_str());
        return;
      }

      String tipoUsuario = usuarioPuertaDoc["tipo_usuario"];
      String nombre_usuario = usuarioPuertaDoc["nombre"];
      showMenssage(nombre_usuario);
      delay(500);
      // si es administrador o profesor se revisa la luminosidad.
      if (tipoUsuario == "administrador") {
          valorSensor = analogRead(FOTOPIN);
          if (valorSensor < 1000) {
            // prender la luz
            encederLuces();
          }
          encenderAire();
          encenderProyector();
      }
    }
    // int val = 0;
    // val = digitalRead(PUERTA);
    // digitalWrite(PUERTA, !val);
    // val = digitalRead(PUERTA);

    abrirPuerta();

    server.send(200, "application/json", "{\"estado\": 1 }"); });

  server.on(UriBraces("/puerta-estado/"), []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);

    int val = 0;
    val = digitalRead(PUERTA);
    //digitalWrite(LUCES, !val);

    server.send(200, "application/json", "{\"estado\": " + String(val) + "}"); });

  server.on(UriBraces("/luces/"), []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);

    int val = 0;
    val = digitalRead(LUCES);
    digitalWrite(LUCES, !val);
    val = !digitalRead(LUCES);

    server.send(200, "application/json", "{\"estado\": " + String(val) + "}"); });

  server.on(UriBraces("/luces-estado/"), []()
            {
    server.sendHeader("Access-Control-Allow-Origin", "*", true);

    int val = 0;
    val = !digitalRead(LUCES);
    //digitalWrite(LUCES, !val);

    server.send(200, "application/json", "{\"estado\": " + String(val) + "}"); });

  server.on(UriBraces("/luces/{}"), []()
            {
              server.sendHeader("Access-Control-Allow-Origin", "*", true);
              String accion = server.pathArg(0);

              if (accion == "encender_luces")
              {
                USE_SERIAL.println("Se debe prender las luces");
                digitalWrite(LUCES, LOW);
                server.send(200, "text/plain", "Acción: " + accion);
              }
              else if (accion == "apagar_luces")
              {
                USE_SERIAL.println("Se debe apagar las luces");
                digitalWrite(LUCES, HIGH);
                server.send(200, "text/plain", "Acción: " + accion);
              }
              else
              {

                server.send(400, "application/json", "{error: \"acción no encontrada\"}");
              } });

  server.on(UriBraces("/registrar/{}"), []
            {
      correo_usuario = server.pathArg(0);
      showMenssage(correo_usuario);
      delay(2000);
      TaskAuth.disable();
      TaskRegistraUsuario.enable();
      server.sendHeader("Access-Control-Allow-Origin", "*", true);
      server.send(200, "application/json", "Registro de huella de usuario iniciado"); });

  server.on(UriBraces("/eliminar-huella/{}"), []
            {
    TaskAuth.disable();
    String id_string = server.pathArg(0);
    showMenssage("Eliminando: " + id_string);

    USE_SERIAL.print("id en int: "); USE_SERIAL.println(id_string.toInt());
    delay(2000);

    deleteFingerprint(id_string.toInt());

    server.sendHeader("Access-Control-Allow-Origin", "*", true);
    server.send(200, "text/plain", "Huella eliminada " + id_string);
    TaskAuth.enable(); });


    server.on(UriBraces("/dispositivos/estados/"), []()
    {
      server.sendHeader("Access-Control-Allow-Origin", "*", true);

      int estadoAire = 0;
      int estadoProyector = 0;
      int estadoLuces = 0;
      int estadoPuerta = 0;
      estadoAire = digitalRead(AIRE);
      estadoProyector = digitalRead(PROYECTOR);
      estadoLuces = digitalRead(LUCES);
      estadoPuerta = digitalRead(PUERTA);

      String respuesta = "{\"aire\": " + String(estadoAire) + ", \"proyector\": "+ String(estadoProyector)+", \"luces\": "+ String(estadoLuces)+", \"puerta\": "+ String(estadoPuerta)+"}";

      server.send(200, "application/json", respuesta); 

    });


    server.on(UriBraces("/capturar-ir"), []() {
      if (irrecv.decode(&resultado)) {
        USE_SERIAL.println(resultado.value, HEX);
      } else {
        USE_SERIAL.println("No existen resutlado");
      }

      server.send(200, "text/plain", "capturado");
    });

    server.on(UriBraces("/enviar/{}"), []() {
/*
codigo infrarrojo para el tv
E0E040BF - apagar
E0E0E01F - subir volumen
E0E0D02F - bajar volumen
*/
        String comando = server.pathArg(0);
        if (comando == "apagar") {
          // irsend.sendNEC(0xE0E040BF, 32);
          irsend.sendSAMSUNG(0xE0E040BF, 32);
        } else if (comando = "subir") {
          // irsend.sendNEC(0xE0E0E01F, 32);
          irsend.sendSAMSUNG(0xE0E0E01F, 32);
        } else if (comando = "bajar") {
          // irsend.sendNEC(0xE0E0D02F, 32);
          irsend.sendSAMSUNG(0xE0E0D02F, 32);
        }
        

        server.send(200, "text/plain", "enviador");
    });
}

void showUTEQ() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("UTEQ");
  display.display();
}

void showIP() {
  String infoNet = "\n" + WiFi.localIP().toString() + "\n" + WiFi.macAddress();
  showMenssage(infoNet);
}

void addIndex() {

   // handle index
  server.on("/", []()
            {
        // send index.html
        //server.send(200, "text/html", "<html><head><script>var connection = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);connection.onopen = function () {  connection.send('Connect ' + new Date()); }; connection.onerror = function (error) {    console.log('WebSocket Error ', error);};connection.onmessage = function (e) {  console.log('Server: ', e.data);};function sendRGB() {  var r = parseInt(document.getElementById('r').value).toString(16);  var g = parseInt(document.getElementById('g').value).toString(16);  var b = parseInt(document.getElementById('b').value).toString(16);  if(r.length < 2) { r = '0' + r; }   if(g.length < 2) { g = '0' + g; }   if(b.length < 2) { b = '0' + b; }   var rgb = '#'+r+g+b;    console.log('RGB: ' + rgb); connection.send(rgb); }</script></head><body>LED Control:<br/><br/>R: <input id=\"r\" type=\"range\" min=\"0\" max=\"255\" step=\"1\" oninput=\"sendRGB();\" /><br/>G: <input id=\"g\" type=\"range\" min=\"0\" max=\"255\" step=\"1\" oninput=\"sendRGB();\" /><br/>B: <input id=\"b\" type=\"range\" min=\"0\" max=\"255\" step=\"1\" oninput=\"sendRGB();\" /><br/></body></html>"); 
        String index = "hola, mundo";
        server.send(200, "text/html", index); });
}