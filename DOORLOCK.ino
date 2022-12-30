#include <EEPROM.h>

/*
  Arduino RFID Access Control
                                                WORKING
                                              +---------+
  +------------------------------------------->READ TAGS<-------------------------------------+
  |                                      +--------------------+                               |
  |                                      |                    |                               |
  |                                      |                    |                               |
  |                                 +----v-----+        +-----v----+                          |
  |                                 |MASTER TAG|        |OTHER TAGS|                          |
  |                                 +--+-------+        ++---------+---+                      |
  |                                    |                 |             |                      |
  |                                    |                 |             |                      |
  |                              +-----v---+        +----v----+   +----v------+               |
  |                 +------------+READ TAGS+---+    |KNOWN TAG|   |UNKNOWN TAG|               |
  |                 |            +-+------++   |    +-----------+ +------------------+        |
  |                 |              |      |    |                |                    |        |
  |            +----v-----+   +----v----+ | +--v--------+     +-v----------+  +------v----+   |
  |            |MASTER TAG|   |KNOWN TAG| | |UNKNOWN TAG|     |GRANT ACCESS|  |DENY ACCESS|   |
  |            +----------+   +---+-----+ \ +-----+-----+     +-----+------+  +-----+-----+   |
  |                 |             |        \     |                 |               |          |
  |               +----+     +----v------+ |  +--v---+             |               +----------+
  +               |EXIT|     |DELETE FROM| |  |ADD TO|             |                          |
  |               +-+--+     |  EEPROM   | |  |EEPROM|             |                          |
  +-----------------+        +-----------+-+--+------+             +--------------------------+



                                      CONNECTION(s)
                       +-------------=============-------------+
                       |    P.BUTTON(reset)  -   A0 (to -ve)   |
                       |     P.BUTTON(lock)  -   A5 (to -ve)   |
                       |   P.BUTTON(unlock)  -   A4 (to -ve)   |
                       |                                       |
                       |              RELAY  -   D5            |
                       |                                       |
                       |                                       |
                       |                                       |
                       |       SERVO SIGNAL  -   D4            |
                       |                                       |
                       |           RFID RST  -   D9            |
                       |           RFID SDA  -   D10           |
                       |          RFID MOSI  -   MOSI(ICSP)|D11|
                       |          RFID MISO  -   MISO(ICSP)|D12|
                       |           RFID SCK  -   SCK (ICSP)|D13|
                       +---------------------------------------+

*/

#include <EEPROM.h>   //Stores card even after power off
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

#define buzzer 8

#define relay 4     // Set Relay Pin
#define wipeB 3     // Button pin for WipeMode (push button)

#define lock A3
#define unlock A1

#define stopRead A0

#define light 5
#define plug 6

boolean match = false;
boolean programMode = false;
boolean replaceMaster = false;

int Received=0;

int successRead;



byte storedCard[4];
byte readCard[4];
byte masterCard[4];

#define SS_PIN 10
#define RST_PIN 9
MFRC522 mfrc522(SS_PIN, RST_PIN);

Servo myservo;

//int light = 3;
//int plug = 4;

int itsONled[] = {0,0,0};

///////////////////////////////////////// Setup ///////////////////////////////////
void setup() {
  //Arduino Pin Configuration
  myservo.attach(2);   //servo pin
  
//  irrecv.enableIRIn();
  
  pinMode(wipeB, INPUT_PULLUP);
  pinMode(relay, OUTPUT);
  pinMode(buzzer, OUTPUT);

  pinMode(lock, INPUT_PULLUP);
  pinMode(unlock, INPUT_PULLUP);
  pinMode(stopRead, INPUT_PULLUP);

  pinMode(light, OUTPUT);
  pinMode(plug, OUTPUT);

  digitalWrite(light, HIGH);
  digitalWrite(plug, HIGH);
  
  digitalWrite(relay, LOW);    // Make sure door is locked
  myservo.write(0);
  digitalWrite(buzzer, LOW);
  
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();
  

  Serial.println(F("Access Control v3.4"));
  ShowReaderDetails();

  if (digitalRead(wipeB) == LOW) {  // when button pressed pin should get low, button connected to ground
    Serial.println(F("Wipe Button Pressed"));
    Serial.println(F("You have 15 seconds to Cancel"));
    Serial.println(F("This will be remove all records and cannot be undone"));
    digitalWrite(buzzer, HIGH);
    delay(1000);
    digitalWrite(buzzer, LOW);
    delay(3000);
    digitalWrite(buzzer, HIGH);
    delay(1000);
    digitalWrite(buzzer, LOW);
    delay(3000);
    digitalWrite(buzzer, HIGH);
    delay(1000);
    digitalWrite(buzzer, LOW);
    delay(3000);
    digitalWrite(buzzer, HIGH);
    delay(1000);
    digitalWrite(buzzer, LOW);
    delay(2000);
  //  delay(15000);                        // Give user enough time to cancel operation
    if (digitalRead(wipeB) == LOW) {    // If button still be pressed, wipe EEPROM
      Serial.println(F("Starting Wiping EEPROM"));
      for (int x = 0; x < EEPROM.length(); x = x + 1) {    //Loop end of EEPROM address
        if (EEPROM.read(x) == 0) {              //If EEPROM address 0
          // do nothing, already clear, go to the next address in order to save time and reduce writes to EEPROM
        }
        else {
          EEPROM.write(x, 0);       // if not write 0 to clear, it takes 3.3mS
        }
      }
      Serial.println(F("EEPROM Successfully Wiped"));
      digitalWrite(buzzer, LOW);
      delay(200);
      digitalWrite(buzzer, HIGH);
      delay(200);
      digitalWrite(buzzer, LOW);
      delay(200);
      digitalWrite(buzzer, HIGH);
      delay(200);
      digitalWrite(buzzer, LOW);
    }
    else {
      Serial.println(F("Wiping Cancelled"));
      digitalWrite(buzzer, LOW);
      
    }
  }
  // Check if master card defined, if not let user choose a master card
  // This also useful to just redefine Master Card
  // You can keep other EEPROM records just write other than 143 to EEPROM address 1
  // EEPROM address 1 should hold magical number which is '143'
  if (EEPROM.read(1) != 143) {
    Serial.println(F("No Master Card Defined"));
    Serial.println(F("Scan A PICC to Define as Master Card"));
    do {
      successRead = getID();            // sets successRead to 1 when we get read from reader otherwise 0
      digitalWrite(buzzer, HIGH);
      delay(200);
      digitalWrite(buzzer, LOW);
      delay(200);
    }
    while (!successRead);                  // Program will not go further while you not get a successful read
    for ( int j = 0; j < 4; j++ ) {        // Loop 4 times
      EEPROM.write( 2 + j, readCard[j] );  // Write scanned PICC's UID to EEPROM, start from address 3
    }
    EEPROM.write(1, 143);                  // Write to EEPROM we defined Master Card.
    Serial.println(F("Master Card Defined"));
  }
  Serial.println(F("-------------------"));
  Serial.println(F("Master Card's UID"));
  for ( int i = 0; i < 4; i++ ) {          // Read Master Card's UID from EEPROM
    masterCard[i] = EEPROM.read(2 + i);    // Write it to masterCard
    Serial.print(masterCard[i], HEX);
  }
  Serial.println("");
  Serial.println(F("-------------------"));
  Serial.println(F("Everything Ready"));
  Serial.println(F("Waiting PICCs to be scanned"));
  cycleLeds();    // Everything ready lets give user some feedback by cycling leds





  
}


///////////////////////////////////////// Main Loop ///////////////////////////////////
void loop () {


  
  do {
    successRead = getID();  // sets successRead to 1 when we get read from reader otherwise 0

    if (digitalRead(wipeB) == LOW) {
      Serial.println(F("Wipe Button Pressed"));
      Serial.println(F("Master Card will be Erased! in 10 seconds"));
      digitalWrite(buzzer, HIGH);
      delay(1000);
      digitalWrite(buzzer, LOW);
      delay(2000);
      digitalWrite(buzzer, HIGH);
      delay(1000);
      digitalWrite(buzzer, LOW);
      delay(2000);
      digitalWrite(buzzer, HIGH);
      delay(1000);
      digitalWrite(buzzer, LOW);
      delay(2000);
      digitalWrite(buzzer, HIGH);
      delay(1000);
      digitalWrite(buzzer, LOW);
//      delay(10000);
      if (digitalRead(wipeB) == LOW) {
        EEPROM.write(1, 0);                  // Reset Magic Number.
        Serial.println(F("Restart device to re-program Master Card"));
        while (1);
      }
    }
    
    if (programMode) {
      cycleLeds();              // Program Mode cycles through RGB waiting to read a new card
    }
    
    else {
      normalModeOn();     // Normal mode, blue Power LED is on, all others are off
    }
    
    if (digitalRead(lock) == LOW) {
            locks();
    }
    
    if (digitalRead(unlock) == LOW) {
      pinMode (lock , OUTPUT);
            unlocks();
    }
    
    else {
      pinMode (lock , INPUT_PULLUP);  
    }

    
/////////....................................////////////////////
/////////....................................////////////////////
/////////....................................////////////////////
    if(Serial.available()>0) { 
      Received = Serial.read();  
    }

    if (Received == 'D') {
      granted();
      Received=0;  
    }

    if (Received == 'a') {
      digitalWrite(light, LOW);
      Received=0;  
    }
    
    if (Received == 'A') {
      digitalWrite(light, HIGH);
      Received=0;  
    }
    
    if (Received == 'b') {
      digitalWrite(plug, LOW);
      Received=0;  
    }

    if (Received == 'B') {
      digitalWrite(plug, HIGH);
      Received=0;  
    }

/////////....................................////////////////////
/////////....................................////////////////////
/////////....................................////////////////////
  }
  
  while (!successRead);   //the program will not go further while you not get a successful read
  
  if (programMode) {
    if ( isMaster(readCard) ) { //If master card scanned again exit program mode
      Serial.println(F("Master Card Scanned"));
      Serial.println(F("Exiting Program Mode"));
      Serial.println(F("-----------------------------"));
      digitalWrite(buzzer, HIGH);
      delay(200);
      digitalWrite(buzzer, LOW);
      delay(200);
      digitalWrite(buzzer, HIGH);
      delay(200);
      digitalWrite(buzzer, LOW);
      delay(200);
      digitalWrite(buzzer, HIGH);
      delay(200);
      digitalWrite(buzzer, LOW);
      
      programMode = false;
      return;
    }
    else {
      if ( findID(readCard) ) { // If scanned card is known delete it
        Serial.println(F("I know this PICC, removing..."));
        deleteID(readCard);
        Serial.println("-----------------------------");
        Serial.println(F("Scan a PICC to ADD or REMOVE to EEPROM"));
      }
      else {                    // If scanned card is not known add it
        Serial.println(F("I do not know this PICC, adding..."));
        writeID(readCard);
        Serial.println(F("-----------------------------"));
        Serial.println(F("Scan a PICC to ADD or REMOVE to EEPROM"));
      }
    }
  }
  else {
    if ( isMaster(readCard)) {    // If scanned card's ID matches Master Card's ID enter program mode
      programMode = true;
      Serial.println(F("Hello Master - Entered Program Mode"));
      int count = EEPROM.read(0);   // Read the first Byte of EEPROM that
      Serial.print(F("I have "));     // stores the number of ID's in EEPROM
      Serial.print(count);
      Serial.print(F(" record(s) on EEPROM"));
      Serial.println("");
      Serial.println(F("Scan a PICC to ADD or REMOVE to EEPROM"));
      Serial.println(F("Scan Master Card again to Exit Program Mode"));
      Serial.println(F("-----------------------------"));
    }
    else {
      if (digitalRead(stopRead) == LOW) {
        Serial.println(F("Door Is Locked"));
        deniedforstopRead();
      }

      
      
      else {
        if ( findID(readCard) ) { // If not, see if the card is in the EEPROM
          Serial.println(F("Welcome, You shall pass"));
          granted();         // Open the door lock for 3000 ms
        }
        else {      // If not, show that the ID was not valid
          Serial.println(F("You shall not pass"));
          denied();
        }
      }
    }
  }
////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////

}
/////////////////////////////////////////  Access Granted    ///////////////////////////////////
void granted () {
  pinMode (lock , OUTPUT);
  digitalWrite(buzzer, HIGH);
  digitalWrite(relay, LOW);     // Unlock door!
  myservo.write(0);
  delay(500);
  digitalWrite(buzzer, LOW);
  delay(2500);
}

///////////////////////////////////////// Access Denied  ///////////////////////////////////
void denied() {
//digitalWrite(relay, HIGH);    // Make sure door is locked
//myservo.write(90);
  pinMode (lock , INPUT_PULLUP);
  
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);

}

/////////////////////////////////////////stpRead////////////////////////////////////////////
void deniedforstopRead() {
  digitalWrite(buzzer, HIGH);
  delay(100);
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, HIGH);
  delay(500);
  digitalWrite(buzzer, LOW);
}


///////////////////////////////////////// Get PICC's UID ///////////////////////////////////
int getID() {
  // Getting ready for Reading PICCs
  if ( ! mfrc522.PICC_IsNewCardPresent()) { //If a new PICC placed to RFID reader continue
    return 0;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()) {   //Since a PICC placed get Serial and continue
    return 0;
  }
  // There are Mifare PICCs which have 4 byte or 7 byte UID care if you use 7 byte PICC
  // I think we should assume every PICC as they have 4 byte UID
  // Until we support 7 byte PICCs
  Serial.println(F("Scanned PICC's UID:"));
  for (int i = 0; i < 4; i++) {  //
    readCard[i] = mfrc522.uid.uidByte[i];
    Serial.print(readCard[i], HEX);
  }
  Serial.println("");
  mfrc522.PICC_HaltA(); // Stop reading
  return 1;
}

void ShowReaderDetails() {
  // Get the MFRC522 software version
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print(F("MFRC522 Software Version: 0x"));
  Serial.print(v, HEX);
  if (v == 0x91)
    Serial.print(F(" = v1.0"));
  else if (v == 0x92)
    Serial.print(F(" = v2.0"));
  else
    Serial.print(F(" (unknown),probably a chinese clone?"));
  Serial.println("");
  // When 0x00 or 0xFF is returned, communication probably failed
  if ((v == 0x00) || (v == 0xFF)) {
    Serial.println(F("WARNING: Communication failure, is the MFRC522 properly connected?"));
    Serial.println(F("SYSTEM HALTED: Check connections."));
    while (true); // do not go further
  }
}

///////////////////////////////////////// Cycle Leds (Program Mode) ///////////////////////////////////
void cycleLeds() {
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(200);
}

//////////////////////////////////////// Normal Mode Led  ///////////////////////////////////
void normalModeOn () {
  digitalWrite(buzzer, LOW);
   // Make sure Door is Locked
}

//////////////////////////////////////// Read an ID from EEPROM //////////////////////////////
void readID( int number ) {
  int start = (number * 4 ) + 2;    // Figure out starting position
  for ( int i = 0; i < 4; i++ ) {     // Loop 4 times to get the 4 Bytes
    storedCard[i] = EEPROM.read(start + i);   // Assign values read from EEPROM to array
  }
}

///////////////////////////////////////// Add ID to EEPROM   ///////////////////////////////////
void writeID( byte a[] ) {
  if ( !findID( a ) ) {     // Before we write to the EEPROM, check to see if we have seen this card before!
    int num = EEPROM.read(0);     // Get the numer of used spaces, position 0 stores the number of ID cards
    int start = ( num * 4 ) + 6;  // Figure out where the next slot starts
    num++;                // Increment the counter by one
    EEPROM.write( 0, num );     // Write the new count to the counter
    for ( int j = 0; j < 4; j++ ) {   // Loop 4 times
      EEPROM.write( start + j, a[j] );  // Write the array values to EEPROM in the right position
    }
    successWrite();
    Serial.println(F("Succesfully added ID record to EEPROM"));
    
  }
  else {
    failedWrite();
    Serial.println(F("Failed! There is something wrong with ID or bad EEPROM"));
  }
}

///////////////////////////////////////// Remove ID from EEPROM   ///////////////////////////////////
void deleteID( byte a[] ) {
  if ( !findID( a ) ) {     // Before we delete from the EEPROM, check to see if we have this card!
    failedWrite();      // If not
    Serial.println(F("Failed! There is something wrong with ID or bad EEPROM"));
  }
  else {
    int num = EEPROM.read(0);   // Get the numer of used spaces, position 0 stores the number of ID cards
    int slot;       // Figure out the slot number of the card
    int start;      // = ( num * 4 ) + 6; // Figure out where the next slot starts
    int looping;    // The number of times the loop repeats
    int j;
    int count = EEPROM.read(0); // Read the first Byte of EEPROM that stores number of cards
    slot = findIDSLOT( a );   // Figure out the slot number of the card to delete
    start = (slot * 4) + 2;
    looping = ((num - slot) * 4);
    num--;      // Decrement the counter by one
    EEPROM.write( 0, num );   // Write the new count to the counter
    for ( j = 0; j < looping; j++ ) {         // Loop the card shift times
      EEPROM.write( start + j, EEPROM.read(start + 4 + j));   // Shift the array values to 4 places earlier in the EEPROM
    }
    for ( int k = 0; k < 4; k++ ) {         // Shifting loop
      EEPROM.write( start + j + k, 0);
    }
    successDelete();
    Serial.println(F("Succesfully removed ID record from EEPROM"));
  }
}

///////////////////////////////////////// Check Bytes   ///////////////////////////////////
boolean checkTwo ( byte a[], byte b[] ) {
  if ( a[0] != NULL )       // Make sure there is something in the array first
    match = true;       // Assume they match at first
  for ( int k = 0; k < 4; k++ ) {   // Loop 4 times
    if ( a[k] != b[k] )     // IF a != b then set match = false, one fails, all fail
      match = false;
  }
  if ( match ) {      // Check to see if if match is still true
    return true;      // Return true
  }
  else  {
    return false;       // Return false
  }
}

///////////////////////////////////////// Find Slot   ///////////////////////////////////
int findIDSLOT( byte find[] ) {
  int count = EEPROM.read(0);       // Read the first Byte of EEPROM that
  for ( int i = 1; i <= count; i++ ) {    // Loop once for each EEPROM entry
    readID(i);                // Read an ID from EEPROM, it is stored in storedCard[4]
    if ( checkTwo( find, storedCard ) ) {   // Check to see if the storedCard read from EEPROM
      // is the same as the find[] ID card passed
      return i;         // The slot number of the card
      break;          // Stop looking we found it
    }
  }
}

///////////////////////////////////////// Find ID From EEPROM   ///////////////////////////////////
boolean findID( byte find[] ) {
  int count = EEPROM.read(0);     // Read the first Byte of EEPROM that
  for ( int i = 1; i <= count; i++ ) {    // Loop once for each EEPROM entry
    readID(i);          // Read an ID from EEPROM, it is stored in storedCard[4]
    if ( checkTwo( find, storedCard ) ) {   // Check to see if the storedCard read from EEPROM
      return true;
      break;  // Stop looking we found it
    }
    else {    // If not, return false
    }
  }
  return false;
}

///////////////////////////////////////// Write Success to EEPROM   ///////////////////////////////////
// Flashes the green LED 3 times to indicate a successful write to EEPROM
void successWrite() {
  digitalWrite(buzzer, LOW);
  delay(1000);
  digitalWrite(buzzer, HIGH);
  delay(400);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(1000);
}

///////////////////////////////////////// Write Failed to EEPROM   ///////////////////////////////////
// Flashes the red LED 3 times to indicate a failed write to EEPROM
void failedWrite() {
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(200);
}

///////////////////////////////////////// Success Remove UID From EEPROM  ///////////////////////////////////
// Flashes the blue LED 3 times to indicate a success delete to EEPROM
void successDelete() {
  digitalWrite(buzzer, LOW);
  delay(1000);
  digitalWrite(buzzer, HIGH);
  delay(500);
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(200);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer, LOW);
  delay(1000);
}

////////////////////// Check readCard IF is masterCard   ///////////////////////////////////
// Check to see if the ID passed is the master programing card
boolean isMaster( byte test[] ) {
  if ( checkTwo( test, masterCard ) )
    return true;
  else
    return false;
}

void locks() {
 
  myservo.write(90);
  digitalWrite(relay, HIGH);
}


void unlocks() {
  
  
//  digitalWrite (lock ,HIGH);

  myservo.write(0);
  digitalWrite(relay, LOW); 


//  digitalWrite (lock ,LOW);
  
}
