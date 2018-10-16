/*
 * Made by Morten Trab - 2018
 * 
 * Code adapted and cleaned from http://www.kildal.dk/forbrugsdata/elmaler-arduino-kode/
 *  
 */
 
#include <SoftwareSerialInverted.h>
#include <SPI.h>
#include <avr/wdt.h>

// Kamstrup registers
word const kamnums[] = { 0x0001,0x03ff,0x041e,0x041f,0x0420,0x0002,0x0434,0x0435,0x0436,0x0438,0x0439,0x043a };
char* kamstrings[]   = { "Energy in","Current Power","Voltage p1","Voltage p2","Voltage p3","Energy out","Current p1","Current p2","Current p3","Power p1","Power p2","Power p3" };
#define KAMNUMREGS 12

// Units
char*  units[65] = {"","Wh","kWh","MWh","GWh","j","kj","Mj",
	"Gj","Cal","kCal","Mcal","Gcal","varh","kvarh","Mvarh","Gvarh",
        "VAh","kVAh","MVAh","GVAh","kW","kW","MW","GW","kvar","kvar","Mvar",
        "Gvar","VA","kVA","MVA","GVA","V","A","kV","kA","C","K","l","m3",
        "l/h","m3/h","m3xC","ton","ton/h","h","hh:mm:ss","yy:mm:dd","yyyy:mm:dd",
        "mm:dd","","bar","RTC","ASCII","m3 x 10","ton x 10","GJ x 10","minutes","Bitfield",
        "s","ms","days","RTC-Q","Datetime"};

// Pin definitions
#define PIN_KAM_RX 7  // D7 to Kamstrup IR interface RX
#define PIN_KAM_TX 6  // D6 to Kamstrup IR interface TX

// Kamstrup optical IR serial
#define KAMTIMEOUT 300  // Kamstrup timeout after transmit
#define KAMBAUD 9600    // Kamstrup baud rate
SoftwareSerial kamSer(PIN_KAM_RX, PIN_KAM_TX, true);  // Initialize serial

void setup() {
  // setup pins
  pinMode(PIN_KAM_RX,INPUT);
  pinMode(PIN_KAM_TX,OUTPUT);
    
  
  // initialize serial interface
  Serial.begin(115200);
  
  kamSer.begin(KAMBAUD);
}

void loop() {
  Serial.println("-- Aflæser Kamstrup Elmåler --");  
  for (int kreg = 0; kreg < KAMNUMREGS; kreg++) {
    kamReadReg(kreg);
  }
  delay(2000);
};

// kamReadReg - read a Kamstrup register
void kamReadReg(unsigned short kreg) {

  byte recvmsg[30];  // buffer of bytes to hold the received data
  float rval;        // this will hold the final value

  // prepare message to send and send it
  byte sendmsg[] = { 0x3f, 0x10, 0x01, (kamnums[kreg] >> 8), (kamnums[kreg] & 0xff) };
  kamSend(sendmsg, 5);

  // listen if we get an answer
  unsigned short rxnum = kamReceive(recvmsg);

  // check if number of received bytes > 0 
  if(rxnum != 0){
    
    // decode the received message
    rval = kamDecode(kreg,recvmsg);
    
    // print out received value to terminal (debug)
    Serial.print(kamstrings[kreg]);
    Serial.print(": ");
    Serial.print(rval);
    Serial.print(" ");
    Serial.println(units[recvmsg[4]]);
  }
}

// kamSend - send data to Kamstrup meter
void kamSend(byte const *msg, int msgsize) {

  // append checksum bytes to message
  byte newmsg[msgsize+2];
  for (int i = 0; i < msgsize; i++) { newmsg[i] = msg[i]; }
  newmsg[msgsize++] = 0x00;
  newmsg[msgsize++] = 0x00;
  int c = crc_1021(newmsg, msgsize);
  newmsg[msgsize-2] = (c >> 8);
  newmsg[msgsize-1] = c & 0xff;

  // build final transmit message - escape various bytes
  byte txmsg[20] = { 0x80 };   // prefix
  int txsize = 1;
  for (int i = 0; i < msgsize; i++) {
    if (newmsg[i] == 0x06 or newmsg[i] == 0x0d or newmsg[i] == 0x1b or newmsg[i] == 0x40 or newmsg[i] == 0x80) {
      txmsg[txsize++] = 0x1b;
      txmsg[txsize++] = newmsg[i] ^ 0xff;
    } else {
      txmsg[txsize++] = newmsg[i];
    }
  }
  txmsg[txsize++] = 0x0d;  // EOF

  // send to serial interface
  for (int x = 0; x < txsize; x++) {
    kamSer.write(txmsg[x]);
  }

}

// kamReceive - receive bytes from Kamstrup meter
unsigned short kamReceive(byte recvmsg[]) {

  byte rxdata[50];  // buffer to hold received data
  unsigned long rxindex = 0;
  unsigned long starttime = millis();
  
  kamSer.flush();  // flush serial buffer - might contain noise

  byte r;
  
  // loop until EOL received or timeout
  while(r != 0x0d){
    
    // handle rx timeout
    if(millis()-starttime > KAMTIMEOUT) {
      Serial.println("Timed out listening for data");
      return 0;
    }

    // handle incoming data
    if (kamSer.available()) {

      // receive byte
      r = kamSer.read();
      if(r != 0x40) {  // don't append if we see the start marker
        // append data
        rxdata[rxindex] = r;
        rxindex++; 
      }

    }
  }

  // remove escape markers from received data
  unsigned short j = 0;
  for (unsigned short i = 0; i < rxindex -1; i++) {
    if (rxdata[i] == 0x1b) {
      byte v = rxdata[i+1] ^ 0xff;
      if (v != 0x06 and v != 0x0d and v != 0x1b and v != 0x40 and v != 0x80){
        Serial.print("Missing escape ");
        Serial.println(v,HEX);
      }
      recvmsg[j] = v;
      i++; // skip
    } else {
      recvmsg[j] = rxdata[i];
    }
    j++;
  }
  
  // check CRC
  if (crc_1021(recvmsg,j)) {
    Serial.println("CRC error: ");
    return 0;
  }
  
  return j;
  
}

// kamDecode - decodes received data
float kamDecode(unsigned short const kreg, byte const *msg) {
  // skip if message is not valid
  if (msg[0] != 0x3f or msg[1] != 0x10) {
    return false;
  }
  if (msg[2] != (kamnums[kreg] >> 8) or msg[3] != (kamnums[kreg] & 0xff)) {
    return false;
  }
    
  // decode the mantissa
  long x = 0;
  for (int i = 0; i < msg[5]; i++) {
    x <<= 8;
    x |= msg[i + 7];
  }
  
  // decode the exponent
  int i = msg[6] & 0x3f;
  if (msg[6] & 0x40) {
    i = -i;
  };
  float ifl = pow(10,i);
  if (msg[6] & 0x80) {
    ifl = -ifl;
  }

  // return final value
  return (float )(x * ifl);

}

// crc_1021 - calculate crc16
long crc_1021(byte const *inmsg, unsigned int len){
  long creg = 0x0000;
  for(unsigned int i = 0; i < len; i++) {
    int mask = 0x80;
    while(mask > 0) {
      creg <<= 1;
      if (inmsg[i] & mask){
        creg |= 1;
      }
      mask>>=1;
      if (creg & 0x10000) {
        creg &= 0xffff;
        creg ^= 0x1021;
      }
    }
  }
  return creg;
}
