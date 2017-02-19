#include <quaternionFilters.h>
#include <MPU9250.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <SD.h>
#include <config.h>
#include <TimeLib.h>


// Pin definitions
const int intPin = D6;  // These can be changed, 2 and 3 are the Arduinos ext int pins
const int outLed = D1; //Led used for signaling stuff
const int chipSelect = D8;  //used for CS SDcard
const int i2cSDA = D4; //i2c SDA pin
const int i2cSCL = D2; //i2c SCL pin

//Declare Variables
const long logInterval = 100; //interval in milisec to log/stream data
long lastLogTime = 0;
const long wifiReconnInterval = 30000; //Interval for scanning the network and trying to reconnect
long lastWifiReconn = 0;
bool ledState = 0;
int wifiRetry;
String stringOne;
char charBuf[96];
IPAddress timeServerIP;
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
String logFile;

//Configuration parameters:
const IPAddress IP_Remote(172, 16, 0, 10); //IP address to stream live data to
const char* ntpServerName = "ro.pool.ntp.org"; //NTP time server
const unsigned int localUdpPort = 4210; //UDP port to stream data
const bool stream = 1; //if set to 1 start streaming UDP to IP_Remote
const bool logToSD = 1; //if set to 1 start logging data to SD card
#define SerialDebug false  // Set to true to get Serial output for debugging

//Initialize libraries
WiFiUDP Udp;
MPU9250 myIMU;
// set up variables using the SD utility library functions:
Sd2Card card;
SdVolume volume;
SdFile root;

void setup()
{
  Serial.begin(38400);
  pinMode(outLed, OUTPUT);

  //Connect to Wifi
  connWiFi();

  //Query NTP and set local time
  queryNTP ();

  //Initialize i2c pins
  Wire.begin(i2cSDA,i2cSCL);


  // Set up the interrupt pin, its set as active high, push-pull
  pinMode(intPin, INPUT);
  digitalWrite(intPin, LOW);
  pinMode(outLed, OUTPUT);
  digitalWrite(outLed, HIGH);

  //Initialize the MPU 9250
  startMPU9250();

  //If we log to SD card then initialize it
  if(logToSD){
    initSDcard();
    logFile = String(now()) + ".log";
  }
}

void loop()
{
  if (millis() - lastWifiReconn >= wifiReconnInterval) {
    //Scan for our SSID and if in range Connect to Wifi
    scanForSSID();
    lastWifiReconn = millis();
  }

  getMPU9250values();

  stringOne = String(now()) + ",";
  stringOne = stringOne + String((int)1000*myIMU.ax) + "," + String((int)1000*myIMU.ay) + "," + String((int)1000*myIMU.az) + ",";
  stringOne = stringOne + String(myIMU.gx) + "," + String(myIMU.gy) + "," + String(myIMU.gz) + ",";
  stringOne = stringOne + String(myIMU.mx) + "," + String(myIMU.my) + "," + String(myIMU.mz);
  stringOne.toCharArray(charBuf, 96);

  if (millis() - lastLogTime >= logInterval) {
    if (logToSD) {
      File dataFile = SD.open(logFile, FILE_WRITE);
      dataFile.println(charBuf);
      dataFile.close();
    }
    //start UDP stream if active
    if (stream) {
      Udp.beginPacket(IP_Remote,4445);
      Udp.write(charBuf);
      Udp.endPacket();
      }
    lastLogTime = millis();
    toggleLed();
    if(SerialDebug) {
      //Serial.println(charBuf);
    }
  }
}


//Start functions definition
void connWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  for (wifiRetry = 0; wifiRetry < 10; wifiRetry++) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiRetry = 0;
      Serial.println();
      Serial.print("Connected, IP address: ");
      Serial.println(WiFi.localIP());
      //Query NTP and set local time
      break;
    }
    delay(500);
    Serial.print(".");
  }

}

void scanForSSID() {
  if(WiFi.status() != WL_CONNECTED){
    if(WiFi.scanComplete() == -2) {
      Serial.println("Starting network scan....");
      WiFi.scanNetworks(1, 1);
    }

    int networksFound = WiFi.scanComplete();
    if(networksFound > 0){
      for (int i = 0; i < networksFound; i++) {
        if(WiFi.SSID(i) == String(ssid)) {
          Serial.print(ssid);
          Serial.println(" is in range");
          connWiFi();
          queryNTP ();
        }
      }
      WiFi.scanDelete();
    }
  }
}

void toggleLed() {
  if (ledState) {
    digitalWrite(outLed, HIGH);
    ledState = 0;
  }
  else {
    digitalWrite(outLed, LOW);
    ledState = 1;
  }
}

void startMPU9250 () {
  // Read the WHO_AM_I register, this is a good test of communication
  byte c = myIMU.readByte(MPU9250_ADDRESS, WHO_AM_I_MPU9250);
  Serial.print("MPU9250 "); Serial.print("I AM "); Serial.print(c, HEX);
  Serial.print(" I should be "); Serial.println(0x71, HEX);

  if (c == 0x71) // WHO_AM_I should always be 0x68
  {
    Serial.println("MPU9250 is online...");

    // Start by performing self test and reporting values
    myIMU.MPU9250SelfTest(myIMU.SelfTest);
    Serial.print("x-axis self test: acceleration trim within : ");
    Serial.print(myIMU.SelfTest[0],1); Serial.println("% of factory value");
    Serial.print("y-axis self test: acceleration trim within : ");
    Serial.print(myIMU.SelfTest[1],1); Serial.println("% of factory value");
    Serial.print("z-axis self test: acceleration trim within : ");
    Serial.print(myIMU.SelfTest[2],1); Serial.println("% of factory value");
    Serial.print("x-axis self test: gyration trim within : ");
    Serial.print(myIMU.SelfTest[3],1); Serial.println("% of factory value");
    Serial.print("y-axis self test: gyration trim within : ");
    Serial.print(myIMU.SelfTest[4],1); Serial.println("% of factory value");
    Serial.print("z-axis self test: gyration trim within : ");
    Serial.print(myIMU.SelfTest[5],1); Serial.println("% of factory value");

    // Calibrate gyro and accelerometers, load biases in bias registers
    myIMU.calibrateMPU9250(myIMU.gyroBias, myIMU.accelBias);

    myIMU.initMPU9250();
    // Initialize device for active mode read of acclerometer, gyroscope, and
    // temperature
    Serial.println("MPU9250 initialized for active data mode....");

    // Read the WHO_AM_I register of the magnetometer, this is a good test of
    // communication
    byte d = myIMU.readByte(AK8963_ADDRESS, WHO_AM_I_AK8963);
    Serial.print("AK8963 "); Serial.print("I AM "); Serial.print(d, HEX);
    Serial.print(" I should be "); Serial.println(0x48, HEX);

    // Get magnetometer calibration from AK8963 ROM
    myIMU.initAK8963(myIMU.magCalibration);
    // Initialize device for active mode read of magnetometer
    Serial.println("AK8963 initialized for active data mode....");
    if (SerialDebug)
    {
      //  Serial.println("Calibration values: ");
      Serial.print("X-Axis sensitivity adjustment value ");
      Serial.println(myIMU.magCalibration[0], 2);
      Serial.print("Y-Axis sensitivity adjustment value ");
      Serial.println(myIMU.magCalibration[1], 2);
      Serial.print("Z-Axis sensitivity adjustment value ");
      Serial.println(myIMU.magCalibration[2], 2);
    }
  } // if (c == 0x71)
  else
  {
    Serial.print("Could not connect to MPU9250: 0x");
    Serial.println(c, HEX);
    while(1) ; // Loop forever if communication doesn't happen
  }
  delay(3000);
  Serial.println("ax,ay,az,gx,gy,gz,mx,my,mz,q0,qx,qy,qz,yaw,pitch,roll,rate");
}

void getMPU9250values() {
  // If intPin goes high, all data registers have new data
  // On interrupt, check if data ready interrupt
  if (myIMU.readByte(MPU9250_ADDRESS, INT_STATUS) & 0x01)
  {
    myIMU.readAccelData(myIMU.accelCount);  // Read the x/y/z adc values
    myIMU.getAres();

    // Now we'll calculate the accleration value into actual g's
    // This depends on scale being set
    myIMU.ax = (float)myIMU.accelCount[0]*myIMU.aRes; // - accelBias[0];
    myIMU.ay = (float)myIMU.accelCount[1]*myIMU.aRes; // - accelBias[1];
    myIMU.az = (float)myIMU.accelCount[2]*myIMU.aRes; // - accelBias[2];

    myIMU.readGyroData(myIMU.gyroCount);  // Read the x/y/z adc values
    myIMU.getGres();

    // Calculate the gyro value into actual degrees per second
    // This depends on scale being set
    myIMU.gx = (float)myIMU.gyroCount[0]*myIMU.gRes;
    myIMU.gy = (float)myIMU.gyroCount[1]*myIMU.gRes;
    myIMU.gz = (float)myIMU.gyroCount[2]*myIMU.gRes;

    myIMU.readMagData(myIMU.magCount);  // Read the x/y/z adc values
    myIMU.getMres();
    // User environmental x-axis correction in milliGauss, should be
    // automatically calculated
    myIMU.magbias[0] = +470.;
    // User environmental x-axis correction in milliGauss TODO axis??
    myIMU.magbias[1] = +120.;
    // User environmental x-axis correction in milliGauss
    myIMU.magbias[2] = +125.;

    // Calculate the magnetometer values in milliGauss
    // Include factory calibration per data sheet and user environmental
    // corrections
    // Get actual magnetometer value, this depends on scale being set
    myIMU.mx = (float)myIMU.magCount[0]*myIMU.mRes*myIMU.magCalibration[0] -
               myIMU.magbias[0];
    myIMU.my = (float)myIMU.magCount[1]*myIMU.mRes*myIMU.magCalibration[1] -
               myIMU.magbias[1];
    myIMU.mz = (float)myIMU.magCount[2]*myIMU.mRes*myIMU.magCalibration[2] -
               myIMU.magbias[2];
  } // if (readByte(MPU9250_ADDRESS, INT_STATUS) & 0x01)

  // Must be called before updating quaternions!
  myIMU.updateTime();

  MahonyQuaternionUpdate(myIMU.ax, myIMU.ay, myIMU.az, myIMU.gx*DEG_TO_RAD,
                         myIMU.gy*DEG_TO_RAD, myIMU.gz*DEG_TO_RAD, myIMU.my,
                         myIMU.mx, myIMU.mz, myIMU.deltat);


  // Serial print and/or display at 0.5 s rate independent of data rates
  myIMU.delt_t = millis() - myIMU.count;

  // print to serial once per half-second independent of read rate
  if (myIMU.delt_t > 500)
  {
    if(SerialDebug)
    {
      Serial.print((int)1000*myIMU.ax);
      Serial.print("  ");
      Serial.print((int)1000*myIMU.ay);
      Serial.print("  ");
      Serial.print((int)1000*myIMU.az);
      Serial.print("  | ");
      Serial.print(myIMU.gx, 2);
      Serial.print("  ");
      Serial.print(myIMU.gy, 2);
      Serial.print("  ");
      Serial.print(myIMU.gz, 2);
      Serial.print("  | ");
      Serial.print((int)myIMU.mx);
      Serial.print("  ");
      Serial.print((int)myIMU.my);
      Serial.print("  ");
      Serial.print((int)myIMU.mz);
      Serial.print("  |  ");
      Serial.print(*getQ());
      Serial.print("  ");
      Serial.print(*(getQ() + 1));
      Serial.print("  ");
      Serial.print(*(getQ() + 2));
      Serial.print("  ");
      Serial.print(*(getQ() + 3));
      Serial.print("  |  ");
      Serial.print(myIMU.yaw, 2);
      Serial.print("  ");
      Serial.print(myIMU.pitch, 2);
      Serial.print("  ");
      Serial.print(myIMU.roll, 2);
      Serial.print("  ");
      Serial.println((float)myIMU.sumCount/myIMU.sum, 2);
      Serial.printf("Settings heap size: %u\n", ESP.getFreeHeap());
    }
    myIMU.yaw   = atan2(2.0f * (*(getQ()+1) * *(getQ()+2) + *getQ() *
                  *(getQ()+3)), *getQ() * *getQ() + *(getQ()+1) * *(getQ()+1)
                  - *(getQ()+2) * *(getQ()+2) - *(getQ()+3) * *(getQ()+3));
    myIMU.pitch = -asin(2.0f * (*(getQ()+1) * *(getQ()+3) - *getQ() *
                  *(getQ()+2)));
    myIMU.roll  = atan2(2.0f * (*getQ() * *(getQ()+1) + *(getQ()+2) *
                  *(getQ()+3)), *getQ() * *getQ() - *(getQ()+1) * *(getQ()+1)
                  - *(getQ()+2) * *(getQ()+2) + *(getQ()+3) * *(getQ()+3));
    myIMU.pitch *= RAD_TO_DEG;
    myIMU.yaw   *= RAD_TO_DEG;
    // Declination of SparkFun Electronics (40°05'26.6"N 105°11'05.9"W) is
    // 	8° 30' E  ± 0° 21' (or 8.5°) on 2016-07-19
    // - http://www.ngdc.noaa.gov/geomag-web/#declination
    myIMU.yaw   -= 8.5;
    myIMU.roll  *= RAD_TO_DEG;
    if(SerialDebug)
    {
      Serial.print("Yaw, Pitch, Roll: ");
      Serial.print(myIMU.yaw, 2);
      Serial.print(", ");
      Serial.print(myIMU.pitch, 2);
      Serial.print(", ");
      Serial.println(myIMU.roll, 2);
      Serial.print("rate = ");
      Serial.print((float)myIMU.sumCount/myIMU.sum, 2);
      Serial.println(" Hz");
    }
    myIMU.count = millis();
    myIMU.sumCount = 0;
    myIMU.sum = 0;

  }
}

void initSDcard() {
  if (logToSD){
  Serial.print("\nInitializing SD card...");
    // we'll use the initialization code from the utility libraries
    // since we're just testing if the card is working!
    if (!card.init(SPI_HALF_SPEED, chipSelect)) {
      Serial.println("initialization failed. No SD logging.");
      digitalWrite(outLed, HIGH);
      return;
    } else {
      Serial.println("Wiring is correct and a card is present.");
    }

    // print the type of card
    Serial.print("\nCard type: ");
    switch (card.type()) {
      case SD_CARD_TYPE_SD1:
        Serial.println("SD1");
      break;
      case SD_CARD_TYPE_SD2:
        Serial.println("SD2");
      break;
      case SD_CARD_TYPE_SDHC:
      Serial.println("SDHC");
      break;
      default:
        Serial.println("Unknown");
      }

      // Now we will try to open the 'volume'/'partition' - it should be FAT16 or FAT32
      if (!volume.init(card)) {
        Serial.println("Could not find FAT16/FAT32 partition.\nMake sure you've formatted the card");
        return;
      }


      // print the type and size of the first FAT-type volume
      uint32_t volumesize;
      Serial.print("\nVolume type is FAT");
      Serial.println(volume.fatType(), DEC);
      Serial.println();

      volumesize = volume.blocksPerCluster();    // clusters are collections of blocks
      volumesize *= volume.clusterCount();       // we'll have a lot of clusters
      volumesize *= 512;                            // SD card blocks are always 512 bytes
      Serial.print("Volume size (bytes): ");
      Serial.println(volumesize);
      Serial.print("Volume size (Kbytes): ");
      volumesize /= 1024;
      Serial.println(volumesize);
      Serial.print("Volume size (Mbytes): ");
      volumesize /= 1024;
      Serial.println(volumesize);


      Serial.println("\nFiles found on the card (name, date and size in bytes): ");
      root.openRoot(volume);

      // list all files in the card with date and size
      root.ls(LS_R | LS_DATE | LS_SIZE);

      if (!SD.begin(chipSelect)) {
        Serial.println("Card failed, or not present");
        // don't do anything more:
        return;
      }
      Serial.println("card initialized.");
    }
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void queryNTP (){
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("We are not connected to WiFi not syncing to NTP.");
    return;
  }
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  Serial.print("Resolving: ");
  Serial.print(ntpServerName);
  Serial.print(" -> ");
  Serial.println(timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);
  int cb;
  int ntpRetries = 7;
  Serial.println("Waiting for NTP server");
  while (!cb && ntpRetries > 0) {
    Serial.print(".");
    cb = Udp.parsePacket();
    delay(1000);
    ntpRetries--;
  }
  if(cb) {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second

    //Set system time
    setTime(epoch);

    Serial.print("System time is: ");
    Serial.println(now());
  }
}

void countMillis() {
  //Will be used to add millilisec support


}
