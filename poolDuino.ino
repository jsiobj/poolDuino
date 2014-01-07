#include <SoftwareSerial.h>
#include <Streaming.h>
#include <PString.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>

#define SEV_DEBUG  3
#define SEV_INFO   2
#define SEV_ERROR  1
#define SEV_RAW    0

#define printDebug(Sev,Source,Msg,Param,Value) if(outputMode >= Sev) Serial << Sev << ":" << "Source" << ":" << Msg << ":" << Param << ":" << Value << endl;

#define rx_ph      2
#define tx_ph      3
#define rx_orp     5
#define tx_orp     6
#define temp_pw    7
#define mode_pin   8  // To change mode
#define manual_pin 9  // To request immediate reading

#define chip_select_pin 4
#define cs_pin          10

#define temp_pin   A0

#define NOVALUE     -999

#define MODE_STARTUP    0  // On sketch startup, we don't know in which mode we are
#define MODE_ARDUINO    1  // Arduino is automatically requesting the data
#define MODE_MANUAL     2  // Data is only requested "on demand" when pressing switch on manual_pin
#define MODE_CONTINUOUS 3  // WARNING : Cannot use continuous with 2 probes because we can't read 2 SoftSerial ports simultaneously



#define PROBE_EOL (char) 13

#define DATA_FILE "pool.txt"

bool phProbeActive = true;
bool orpProbeActive = true;
bool tempProbeActive = true;

#define PROBE_REQUEST_TIMEOUT 400  // in ms. Reading should not take more than ~378ms (from datasheet) so 400ms should be enough
#define READ_INTERVAL         5000 // in ms

int mode=MODE_ARDUINO;            // Default mode
int previousMode=MODE_STARTUP;
int outputMode=SEV_DEBUG;

SoftwareSerial serial_ph(rx_ph, tx_ph);
SoftwareSerial serial_orp(rx_orp, tx_orp);

#define SERIAL_BUF_SIZE 20
#define BUF_SIZE 20

char buf_ph[SERIAL_BUF_SIZE]={0};
char buf_orp[SERIAL_BUF_SIZE]={0};
//int data_received=0;
float ph=NOVALUE;
float orp=NOVALUE;
float temp=NOVALUE;

char buf_com[SERIAL_BUF_SIZE]={0};
char buf_data[BUF_SIZE]={0};
PString payLoad(buf_data,BUF_SIZE);

// Ethernet & IP configuration
byte mac[] = { 0x90, 0xA2, 0xDA, 0x0D, 0x61, 0x70};
IPAddress ip(192,168,0,11);
EthernetClient client;

//------------------------------------------------------------------------------
// Setup
//-------------------------------------------------------------------------------
void setup(){
  Serial.begin(38400);
  serial_ph.begin(38400);
  serial_orp.begin(38400);
  
  if (!SD.begin(chip_select_pin)) printDebug(SEV_ERROR,"setup()",F("Card failed, or not present"),"","");
  //Serial << F("ERROR:setup:Card failed, or not present") << endl;
  //else Serial << F("DEBUG:setup:SD Card initialized")<<endl;
  
  pinMode(cs_pin, OUTPUT);         // Eth CS
  pinMode(temp_pw,OUTPUT);
  digitalWrite(temp_pin, LOW); // enable pull-up for temp sensor
}

//------------------------------------------------------------------------------
// Main loop
//-------------------------------------------------------------------------------
void loop(){

  // Setting (or not) continous mode
  //  continuousMode=digitalRead(continousModePin);

  //-------------------------------------------------------------------------------
  // Setting Mode if changed
  //-------------------------------------------------------------------------------
  if(mode!=previousMode) {
    printDebug(SEV_DEBUG,"loop()","Mode changed","mode",mode);    
    previousMode=mode;
    
    if(phProbeActive && orpProbeActive && mode==MODE_CONTINUOUS) {
      printDebug(SEV_ERROR,"loop()","Both probe active in continuous mode, disabling ORP","","");
      orpProbeActive=false;
    }
    
    if(mode==MODE_CONTINUOUS && phProbeActive) {
      setContinuous(&serial_ph,true);
      serial_ph.listen();
    }
    else setContinuous(&serial_ph,false);
    
    if(mode==MODE_CONTINUOUS && orpProbeActive) {
      setContinuous(&serial_orp,true);
      serial_orp.listen();
    }
    else setContinuous(&serial_orp,false);
  }

  //-------------------------------------------------------------------------------
  // If in Arduino mode, we ask for a reading of each probe
  //-------------------------------------------------------------------------------
  if(mode==MODE_ARDUINO) {
    
    printDebug(SEV_DEBUG,"readProbeData()","","pH isListening",serial_ph.isListening());
    printDebug(SEV_DEBUG,"readProbeData()","","ORP isListening",serial_orp.isListening());

    if(tempProbeActive) {
      printDebug(SEV_DEBUG,"readProbeData()","Reading temperature","","");
      temp=readTemp(temp_pin);
    }

    if(phProbeActive) {
      printDebug(SEV_DEBUG,"readProbeData()","Requesting pH","","");
      ph=requestRead(&serial_ph,buf_ph);
    }
    
    if(orpProbeActive) {
      printDebug(SEV_DEBUG,"readProbeData()","Requesting ORP","","");
      orp=requestRead(&serial_orp,buf_orp);
    }
  }
  
  if(mode==MODE_CONTINUOUS) {
    printDebug(SEV_DEBUG,"readProbeData()","","pH isListening",serial_ph.isListening());
    printDebug(SEV_DEBUG,"readProbeData()","","ORP isListening",serial_orp.isListening());

    if(phProbeActive) {
      printDebug(SEV_DEBUG,"readProbeData()","Reading pH","","");
      readProbeData(&serial_ph,buf_ph,0); // just reading, no command sent, we're in continous mode
      ph=atof(buf_ph);
      //if(buf_ph[0]) { Serial << buf_ph << PROBE_EOL; buf_ph[0]=0; }
    }
    
    if(orpProbeActive) {
      printDebug(SEV_DEBUG,"readProbeData()","Reading ORP","","");
      readProbeData(&serial_orp,buf_orp,0); // just reading, no command sent, we're in continous mode
      orp=atof(buf_orp);
      //if(buf_orp[0]) { Serial << buf_orp << PROBE_EOL; buf_ph[0]=0; }
    }
  }   
  
  if(ph!=NOVALUE) {
    if(outputMode >= SEV_INFO) Serial.print("PH    : ");
    Serial << buf_ph;
    if(outputMode == SEV_RAW) Serial << PROBE_EOL;
    else Serial << endl;
    payLoad << buf_ph << ",";
    ph=NOVALUE; buf_ph[0]=0;
  }
  else payLoad.print("NaN,");

  if(orp!=NOVALUE) {
    if(outputMode >= SEV_INFO) Serial.print("ORP  : ");
    Serial << buf_orp;
    if(outputMode == SEV_RAW) Serial << PROBE_EOL;
    else Serial << endl;
    payLoad << buf_orp << ',';
    orp=NOVALUE; buf_orp[0]=0;
  }
  else payLoad.print("NaN,");

  if(temp!=NOVALUE) {
    if(outputMode >= SEV_INFO) Serial << "TEMP : "; 
    Serial << temp << endl;
    payLoad.print(temp);
    temp=NOVALUE;
  }
  else payLoad.print("NaN"); 
  
  if(mode==MODE_ARDUINO) {
    writeToFile(&payLoad,DATA_FILE);
    payLoad.begin();
    delay(READ_INTERVAL);  
  }
}

//------------------------------------------------------------------------------
// Writing to file
//------------------------------------------------------------------------------
void writeToFile(PString *payLoad,const char *fileName) {
  
  File dataFile = SD.open(fileName, FILE_WRITE);
  if(dataFile) {
    dataFile << millis() << ',' << buf_data << '\n';
    dataFile.close();
    printDebug(SEV_DEBUG,"writeToFile()","Writing","Data",buf_data);
  }
  else {
    printDebug(SEV_ERROR,"writeToFile()","Writing failed","file",fileName);
  }  
}

//------------------------------------------------------------------------------
// Reading data coming from USB on HW serial (built-in interrupt)
//------------------------------------------------------------------------------
void serialEvent() {

  char buf[16];
  byte received_from_computer=0;
  
  received_from_computer=Serial.readBytesUntil(13,buf,sizeof(buf));
  buf[received_from_computer]=0;

  printDebug(SEV_DEBUG,"serialEvent()","","Command",buf);
    
  if(phProbeActive) sendCommand(&serial_ph,buf);
  if(orpProbeActive) sendCommand(&serial_orp,buf);
}

//------------------------------------------------------------------------------
// 
//------------------------------------------------------------------------------
float requestRead(SoftwareSerial *probeSerial, char *reply) {

  // Asking for probe reading
  sendCommand(probeSerial,"R",reply);
  return atof(reply);
}

//------------------------------------------------------------------------------
// Temp reading
//-------------------------------------------------------------------------------
float readTemp(int pin) {
  digitalWrite(temp_pw,HIGH);   // power up temp sensor
  delay(2);
  
  float vOut=analogRead(temp_pin);
  float temp=vOut;
  temp*=.0048;   // ADC to volts
  temp*=1000;    // mV => V
  temp=0.0512*temp-20.5128;
  
  //printDebug(SEV_DEBUG,"readTemp()","","v out",vOut);
  digitalWrite(temp_pw,LOW);   // power off temp sensor
  return temp;
}

//-------------------------------------------------------------------------------
// Utils
//-------------------------------------------------------------------------------
void setContinuous(SoftwareSerial *probeSerial, boolean setContinuous) {
  if(setContinuous) sendCommand(probeSerial,"C");
  else sendCommand(probeSerial,"E");
}

// sendCommand : we're not expecting a reply
void sendCommand(SoftwareSerial *probeSerial,char *command) {
  printDebug(SEV_DEBUG,"sendCommand()","no reply","command",command);
  probeSerial->print(command);
  probeSerial->print('\r');
}

// sendCommand : we are expecting a reply
void sendCommand(SoftwareSerial *probeSerial,char *command,char *reply) {
  printDebug(SEV_DEBUG,"sendCommand()","reply","command",command);

  probeSerial->listen();         // Because we're expecting a reply, make sure the soft serial is listening
  probeSerial->print(command);
  probeSerial->print('\r');
  
  readProbeData(probeSerial,reply,PROBE_REQUEST_TIMEOUT);

}

//------------------------------------------------------------------------------
// Probe reading
// SoftSerial must have been set to listen BEFORE calling this function
//-------------------------------------------------------------------------------
float readProbeData(SoftwareSerial *probeSerial, char *reply, int timeout) {

  byte charCount=0;
  long int start;
 
  printDebug(SEV_DEBUG,"readProbeData()","","isListening",probeSerial->isListening());

  // If a timeout was set, let's wait to see if there's incoming data 
  // Else we'll try immediately
  if(timeout>0) {
    start=millis();
    while((millis() - start) < PROBE_REQUEST_TIMEOUT && probeSerial->available() == 0 ) ; 
    printDebug(SEV_DEBUG,"readProbeData()","","spent (ms)",millis() - start);
  }
  
  if(probeSerial->available()>0) {
    charCount=probeSerial->readBytesUntil(13,reply,SERIAL_BUF_SIZE);
    reply[charCount]=0;
    printDebug(SEV_DEBUG,"readProbeData()","","Reply",reply);
    printDebug(SEV_DEBUG,"readProbeData()","","chars",charCount);
  }
  else {
    if(timeout>0) printDebug(SEV_DEBUG,"readProbeData()","Timeout","","");
  }
}
