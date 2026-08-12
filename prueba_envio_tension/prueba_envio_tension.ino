#include <mcp2515.h>
#include <EEPROM.h>
#define pinTension A4
#define r2 PD7
// pin que marca si el cargador est� conectado
// detecta un 1 cuando esta desconectado y un 0 cuando esta conectado
// entrada 3 (no se que es)
#define pinDectectaCargador PD5
//Con este pin anal�gico A5 se puede leer la corriente
#define pinCorriente A3
#define suavizado 30
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
//---------------------------- Comunicaci�n CAN --------------------------//
MCP2515 mcp2515(10);
struct can_frame trama1;
struct can_frame tramaCorriente;
struct can_frame tramaCorrienteN;
struct can_frame tramaTension;
struct can_frame canMsg;

int i = 0;
int j = 0;
//------------------------------------------------------------------------//

void setup() {

  //--------------------------- Configuraci�n pines ----------------------//
  pinMode (pinTension, INPUT);  //sensor tension
  pinMode(r2, OUTPUT);  //rele para cortar cargador
  pinMode(pinDectectaCargador, INPUT);  //rele que lee los 12v cuando se conecta el cargador
  digitalWrite(r2, HIGH);
  pinMode(pinCorriente, INPUT); //sensor de corriente
  //---------------------------------------------------------------------//
  Serial.begin(9600);
  //------------------------------ Comunicaci�n CAN ----------------------//
  tramaCorriente.can_id = 880;
  tramaCorriente.can_dlc = 2;
  tramaCorriente.data[0] = 0x00;
  tramaCorriente.data[1] = 0x00;

  tramaTension.can_id = 890;
  tramaTension.can_dlc = 2;
  tramaTension.data[0] = 0x00;
  tramaTension.data[1] = 0x00;
  mcp2515.reset();
  mcp2515.setBitrate(CAN_250KBPS, MCP_16MHZ);
  mcp2515.setNormalMode();

  //----------------------------------------------------------------------//
}

void loop() {
  //----------------- Detecta y muestra si el cargador
  Serial.print("Cargador: ");
  Serial.print("Mostrame esto: ");
  enchufeConectado = digitalRead(pinDectectaCargador); //o digitalRead?? ver el releee
  Serial.println(enchufeConectado);
  //Detecta el cargador  y env�a un 0
  analogicoTension = analogRead(pinTension);
  tension = ((analogicoTension * 5.0 / 1023.0) * (100 / 5));
  Serial.print("Tension ");
  Serial.println(tension);
  if (enchufeConectado == HIGH) {
    if (tension > 83.5) { //poner aca el numero que tiene que i por la tabla de equivalencias
      digitalWrite(r2, LOW); //lo bajo
    }
    else if(activoCarga == 1){
      activoCarga=0;
      digitalWrite(r2, HIGH);
    }
  }
  if (j++ == 1) {
    j = 0;
    //Envio trama de tension
    int tensionEntera = (int)(tension);
    int tensionDecimal = (tension - tensionEntera) * 100;
    tramaTension.data[0] = tensionEntera;
    tramaTension.data[1] = tensionDecimal;
    Serial.println("Esto se ejecuta");
    //***************************
    if (mcp2515.sendMessage(&tramaTension) == MCP2515::ERROR_OK) {
      uint8_t canStat = mcp2515.getErrorFlags();

      if (canStat != 0) {
        Serial.print("Error en bus CAN (Flags de error): ");
        Serial.println(canStat, HEX);
      } else {
        Serial.println("Mensaje colocado en el buffer del MCP2515.");
      }
    } else {
      Serial.println("Error SPI al intentar enviar mensaje.");
    }
  }
  else {
    //flag=1;
    Serial.println("MsgTension TX error");
  }
  //**************************/

  //------------------------- Leer la corriente -----------------------------------//
  promedioCorriente();
  Serial.print("Voltaje medido: ");
  Serial.print(corriente.tensionLeida);
  Serial.print(" V | Corriente: ");
  Serial.print(corriente.corrienteLeida);
  Serial.println(" A");
  enviarCorriente(corriente.corrienteLeida);
  if(contador30++ == 1000*60*30){
    activoCarga=1;
    contador30=0;
  }
  //Se que hay un tema de diferenciar la corriente entrante y saliente dependiendo de si la tensi�n es menor que 2.5 o mayor que 2.5,
  //pero no estoy muy  seguro
  delay(1000); //espero 1 segundos y pregunta de nuevo por el enchufe
}
void promedioCorriente() {
  const float vRef = 2.5;            // Tensi�n de offset a 0 A (2.5 V)
  const float sensibilidad = 0.0267;  // Sensibilidad Canal 1: 0.0267 mV/A -> 0.0267 V/A
  const float alimentacionHall = 5.0; //Tension con el cual se alimenta el sensor.
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
void enviarCorriente(float corriente) {
  int corrienteEntera = (int) corriente;
  int corrienteDecimal = (corriente - corrienteEntera) * 100;
  tramaCorriente.data[0] = corrienteEntera;
  tramaCorriente.data[1] = corrienteDecimal;
  if (mcp2515.sendMessage(&tramaCorriente) == MCP2515::ERROR_OK) {}
}
