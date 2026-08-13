#include <mcp2515.h>
#include <EEPROM.h>
//-------------------------------------- Definiciones ------------------------------//
#define pinTension A4
#define RELE2 PD7
// pin que marca si el cargador est� conectado
// detecta un 1 cuando esta desconectado y un 0 cuando esta conectado
// entrada 3 (no se que es)
#define pinDectectaCargador PD5
//Con este pin anal�gico A5 se puede leer la corriente
#define pinCorriente A3
#define suavizado 30
//-----------------------------------------------------------------------------------//
//------------------------------------- Variables ----------------------------------//
double analogicoTension = 0;
double tension = 0;
int enchufeConectado = 0;
double corrienteAnalogica = 0.0;
double corrienteDigital = 0.0;
//Lo utilizo como contador
int contador30 = 0;
//Sirve para activar la carga
uint8_t activoCarga=0;
//Estructura
typedef struct {
  float tensionLeida;
  float corrienteLeida;
} corrienteEstructura;
corrienteEstructura corriente;
int estadoRelayDos;
//----------------------------------------------------------------------------------//


//---------------------------- Comunicaci�n CAN --------------------------//
MCP2515 mcp2515(10);
struct can_frame tramaCorriente;
struct can_frame tramaTension;
struct can_frame canMsg;
//------------------------------------------------------------------------//

void setup() {

  //--------------------------- Configuraci�n pines ----------------------//
  pinMode (pinTension, INPUT);  //sensor tension
  pinMode(RELE2, OUTPUT);  //rele para cortar cargador
  pinMode(pinDectectaCargador, INPUT);  //rele que lee los 12v cuando se conecta el cargador
  digitalWrite(RELE2, HIGH); //Escribe un 1 para activar el cargador
  pinMode(pinCorriente, INPUT); //sensor de corriente
  //---------------------------------------------------------------------//
  Serial.begin(9600);
  //------------------------------ Leo estado anterior -------------------//
  estadoRelayDos = EEPROM.read(estadoRelayDos);
  //---------------------------------------------------------------------//
  
  //------------------------------ Comunicaci�n CAN ----------------------//
  tramaCorriente.can_id = 880;
  tramaCorriente.can_dlc = 3;
  tramaCorriente.data[0] = 0x00;
  tramaCorriente.data[1] = 0x00;
  //Lo modifico para guarda el valor 
  tramaCorriente.data[2] = 0x00;

  tramaTension.can_id = 890;
  tramaTension.can_dlc = 2;
  tramaTension.data[0] = 0x00;
  tramaTension.data[1] = 0x00;
  mcp2515.reset();
  mcp2515.setBitrate(CAN_250KBPS, MCP_16MHZ);
  mcp2515.setNormalMode();
  //----------------------------------------------------------------------//
}
//--------------------------------- Loop ------------------------------------------------------------------//
void loop() {
  //----------------- Detecta y muestra si el cargador ------------------//
  //Detecta el cargador  y env�a un 0
  enchufeConectado = digitalRead(pinDectectaCargador); //o digitalRead?? ver el releee
  //--------------------------------------------------------------------//
  //----------------- Leer la tensión ----------------------------------//
  leerTension();
  //-------------------------------------------------------------------//
  //------------------- Chequeo la tensión para que no cargue demás --//
  chequeoTension();
  //------------------------------------------------------------------//
  //--------------------------Enviar Tensión -------------------------------//
  enviarTension();
  //-----------------------------------------------------------------//
  //------------------------- Leer la corriente -----------------------------------//
  promedioCorriente();
  enviarCorriente(corriente.corrienteLeida);
  //----------------------------------------------------------------//
  // ------------------------ Informe en consola -------------------//
  informeSerial();
  //---------------------------------------------------------------//
  reciboTrama();
}
//------------------------------------------------------------------------------------------------------//
//-------------------------------------------- Recibo trama desde QT -----------------------------------//
void reciboTrama(){
  while (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK){
    if(canMsg.can_id == 0x123){
      switch(canMsg.data[0]){
        case 2:{
          digitalWrite(RELE2,LOW);
          Serial.println("Bajo Relay 2");
          EEPROM.write(estadoRelayDos,0);
          break;
        }
        case 3:{
          digitalWrite(RELE2,HIGH);
          Serial.println("Alto Relay 2");
          EEPROM.write(estadoRelayDos,1);
          break;
        }
      }
    }
  }
}
//------------------------------------------------------------------------------------------------------//

//------------------------------------------ Chequeo de la carga ---------------------------------------//
void chequeoTension(){
  if (enchufeConectado == HIGH) {
    if (tension > 83.5) { //poner aca el numero que tiene que i por la tabla de equivalencias
      digitalWrite(RELE2, LOW); //lo bajo
    }
    else if(activoCarga == 1){
      activoCarga=0;
      digitalWrite(RELE2, HIGH);
    }
  }
  if(contador30++ == 60*30){
    activoCarga=1;
    contador30=0;
  }
}
//------------------------------------------------------------------------------------------------------//
//---------------------------------------- Informo en monitor serial -----------------------------------//
void informeSerial(){
  Serial.print("Detecta el cargador, si lo detecta envía un 0, en caso contrario un 1");
  Serial.println(enchufeConectado);
  Serial.print("Tension ");
  Serial.println(tension);
  Serial.print("Voltaje medido: ");
  Serial.print(corriente.tensionLeida);
  Serial.print(" V | Corriente: ");
  Serial.print(corriente.corrienteLeida);
  Serial.println(" A");
}
//-------------------------------------------------------------------------------------------------------//
//------------------------------------------- Leo tensión -----------------------------------------------//
//Lee la tensión y además suaviza 
void leerTension(){
  float sumaT=0.0;
  for(int j=0; j<suavizado; j++){
    analogicoTension = analogRead(pinTension);
    //Convierto el valor 
    sumaT+=((analogicoTension * 5.0 / 1023.0) * (100 / 5));
  }
  tension=sumaT/suavizado;
}
//-------------------------------------------------------------------------------------------------------//
//------------------------------------------ Leo corriente ----------------------------------------------//
// Función que promedia la corriente leída. 
void promedioCorriente() {
  const float vRef = 2.425;            // Tensi�n de offset a 0 A (2.5 V)
  const float sensibilidad = 0.0267;  // Sensibilidad Canal 1: 267 mV/A -> 0.0267 V/A
  const float alimentacionHall = 4.85; //Tension con el cual se alimenta el sensor.
  float sumaC = 0.0;
  float sumaV = 0.0;
  for (int i = 0; i < suavizado; i++) {
    int corrienteAnalogica = analogRead(pinCorriente);
    // 1. Convertir la lectura digital (0-1023) a Voltaje real (0.0 V - 5.0 V)
    float voltajeMedido = (5.0 / 1023.0) * corrienteAnalogica;
    sumaV += voltajeMedido;
    if (voltajeMedido < 0.2) {
      Serial.println("Voltaje es menor que 0.2 esta fuera de los rango negativos 0.2-2.5/0.004=-575 A");
    } else if (voltajeMedido > 4.8) {
      Serial.println("Voltaje es mayor a 4.8 esta fuera del rango positivo 5.0-2.5/0.004=625 A, el canal 2 mide a lo mucho +250A");
    } else {
      // 2. Aplicar la f�rmula del datasheet: I = (Uout - Uo) / S
      sumaC += ((5 * voltajeMedido) / (alimentacionHall) - vRef) / sensibilidad;
    }
    delay(5);
  }

  corriente.corrienteLeida = sumaC /suavizado;
  corriente.tensionLeida = sumaV / suavizado;
}
//------------------------------------------------------------------------------------------------------//
//---------------------------------------- Envío Corriente -----------------------------------------------//
void enviarCorriente(float corrienteArg) {
  int16_t corrienteEscalada = (int16_t)(corrienteArg * 100.0);
  // Guardado est�ndar en 2 bytes para mantener signo y decimales
  tramaCorriente.data[0] = (uint8_t)(corrienteEscalada >> 8);
  tramaCorriente.data[1] = (uint8_t)(corrienteEscalada & 0xFF);
  Serial.print("corriente entera: ");
  Serial.println(tramaCorriente.data[0]);
  Serial.print("Corriente decimal: ");
  Serial.println(tramaCorriente.data[1]);
  if (mcp2515.sendMessage(&tramaCorriente) == MCP2515::ERROR_OK) {}else{      Serial.println("Error SPI al intentar enviar mensaje.");}
}
//------------------------------------------------------------------------------------------------------//
//--------------------------------------- Envío Tnesión ------------------------------------------------//
// Función que envía la tensión.
void enviarTension(){
    //Envio trama de tension
    int tensionEntera = (int)(tension);
    int tensionDecimal = (tension - tensionEntera) * 100;
    tramaTension.data[0] = tensionEntera;
    tramaTension.data[1] = tensionDecimal;
    Serial.println("Esto se ejecuta");
    if (mcp2515.sendMessage(&tramaTension) == MCP2515::ERROR_OK) {
    } else {
      Serial.println("Error SPI al intentar enviar mensaje.");
    }
}
//------------------------------------------------------------------------------------------------------//
