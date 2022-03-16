#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <MFRC522.h>
#include <SPI.h>

#define RX_PIN 5           // bt module TXD pin
#define TX_PIN 6           // bt module RXD pin
#define RST_PIN 9          // Configurable, see typical pin layout above
#define SS_PIN 10          // Configurable, see typical pin layout above

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance
LiquidCrystal_I2C lcd(0x3f, 16, 2); // Create Lcd monitor instance
SoftwareSerial bluetooth(RX_PIN, TX_PIN, false); // Create bluetooth instance

char buf[48];                       // Message buffer
int chainPos = 0;                   // Supply chain position
char chainNode[16] = "Production";  // Supply chain node
long debouncing_time = 150; 
volatile unsigned long last_micros;
 
MFRC522::MIFARE_Key key;
SemaphoreHandle_t interruptSemaphore;

/*
 *  INIT SETUP
 */
void setup() {
  lcd.init();                       // Init Lcd monitor
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(chainNode);
  bluetooth.begin(4800);            // Init bluetooth module
  pinMode(2, INPUT_PULLUP);
  // Prepare key - all keys are set to FFFFFFFFFFFFh at chip delivery from the factory.
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  xTaskCreate(TaskReadCard, "ReadCard", 128, NULL, 0, NULL);
  xTaskCreate(TaskChainNode, "ChainNode", 180, NULL, 0, NULL);
  interruptSemaphore = xSemaphoreCreateBinary();
  if (interruptSemaphore != NULL) {
    attachInterrupt(digitalPinToInterrupt(2), debounceInterrupt, LOW);
  }
  Serial.begin(9600);               // Initialize serial communications with the PC
  SPI.begin();                      // Init SPI bus
  mfrc522.PCD_Init();               // Init MFRC522 card
}

/*
 *  MAIN LOOP
 */
void loop() {
}

/*
 *  READ RFID CARD
 */
void TaskReadCard(void *pvParameters) {
  (void) pvParameters;
  char uid[9] = "";
  byte nib1, nib2;
    
  for (;;) {
    // Reset the loop if no new card present on the sensor/reader
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        nib1 = (mfrc522.uid.uidByte[i] >> 4) & 0x0F;
        nib2 = (mfrc522.uid.uidByte[i] >> 0) & 0x0F;
        uid[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        uid[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
      }
      uid[mfrc522.uid.size*2] = '\0';
      
      strcpy(buf, "{node:\"");
      strcat(buf, chainNode);
      strcat(buf, "\", obj:\"");
      strcat(buf, uid);
      strcat(buf, "\"}");
      bluetooth.println(buf);
    
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
  }
}

/*
 *  CHANGE SUPPLY CHAIN NODE
 */
void TaskChainNode(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    if (xSemaphoreTake(interruptSemaphore, portMAX_DELAY) == pdPASS) {
      chainPos = (chainPos + 1) % 4;
      switch (chainPos) {
        case 0:
          strcpy(chainNode, "Production"); break;
        case 1:
          strcpy(chainNode, "Warehouse"); break;
        case 2:
          strcpy(chainNode, "Distribution"); break;
        case 3:
          strcpy(chainNode, "Delivery"); break;
      }
      lcd.clear();
      lcd.print(chainNode);
    }
  }
}

void interruptHandler() {
  xSemaphoreGiveFromISR(interruptSemaphore, NULL);
}

void debounceInterrupt() {
  if((long)(micros() - last_micros) >= debouncing_time * 1000) {
    interruptHandler();
    last_micros = micros();
  }
}
