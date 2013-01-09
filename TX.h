/****************************************************
 * MatCatLRS transmitter code
 ****************************************************/
unsigned char RF_channel = 0;
unsigned int channelFaker = 0;
unsigned char FSstate = 0; // 1 = waiting timer, 2 = send FS, 3 sent waiting btn release
unsigned long FStime = 0;  // time when button went down...

unsigned long lastSent = 0;

volatile unsigned char ppmAge = 0; // age of PPM data


volatile unsigned int startPulse = 0;
volatile byte         ppmCounter = PPM_CHANNELS; // ignore data until first sync pulse

#define TIMER1_FREQUENCY_HZ 50
#define TIMER1_PRESCALER    8
#define TIMER1_PERIOD       (F_CPU/TIMER1_PRESCALER/TIMER1_FREQUENCY_HZ)

#ifndef MATCATLRS_HW
#ifdef USE_ICP1 // Use ICP1 in input capture mode
/****************************************************
 * Interrupt Vector
 ****************************************************/
ISR(TIMER1_CAPT_vect) {
  unsigned int stopPulse = ICR1;

  // Compensate for timer overflow if needed
  unsigned int pulseWidth = ((startPulse > stopPulse) ? TIMER1_PERIOD : 0) + stopPulse - startPulse;

  if (pulseWidth > 5000) {      // Verify if this is the sync pulse (2.5ms)
    ppmCounter = 0;             // -> restart the channel counter
    ppmAge = 0;                 // brand new PPM data received
  }
  else if ((pulseWidth>1400) && (ppmCounter < PPM_CHANNELS)) { // extra channels will get ignored here
    PPM[ppmCounter] = servoUs2Bits(pulseWidth/2); // Store measured pulse length (converted)
    ppmCounter++;                     // Advance to next channel
  } else {
    ppmCounter = PPM_CHANNELS; // glitch ignore rest of data
  }
  startPulse = stopPulse;         // Save time at pulse start
}

void setupPPMinput() {
  // Setup timer1 for input capture (PSC=8 -> 0.5ms precision, top at 20ms)
  TCCR1A = ((1<<WGM10)|(1<<WGM11));
  TCCR1B = ((1<<WGM12)|(1<<WGM13)|(1<<CS11)|(1<<ICES1));
  OCR1A = TIMER1_PERIOD;
  TIMSK1 |= (1<<ICIE1); // Enable timer1 input capture interrupt
}
#else // sample PPM using pinchange interrupt
ISR(PPM_Signal_Interrupt){

  unsigned int time_temp;

  if (PPM_Signal_Edge_Check) {// Only works with rising edge of the signal
    time_temp = TCNT1; // read the timer1 value
    TCNT1 = 0; // reset the timer1 value for next
    if (time_temp > 5000) {// new frame detection (>2.5ms)  
      ppmCounter = 0;             // -> restart the channel counter
      ppmAge = 0;                 // brand new PPM data received
    } else if ((time_temp > 1400) && (ppmCounter<PPM_CHANNELS)) {
      PPM[ppmCounter] = servoUs2Bits(time_temp/2); // Store measured pulse length (converted)
      ppmCounter++;                     // Advance to next channel
    } else {
      ppmCounter=PPM_CHANNELS; // glitch ignore rest of data
    }
  }
}

void setupPPMinput() {
  // Setup timer1 for input capture (PSC=8 -> 0.5ms precision, top at 20ms)
  TCCR1A = ((1<<WGM10)|(1<<WGM11));
  TCCR1B = ((1<<WGM12)|(1<<WGM13)|(1<<CS11));
  OCR1A = TIMER1_PERIOD;
  TIMSK1 = 0;
  PPM_Pin_Interrupt_Setup
}
#endif
#endif
void scannerMode(){
  char c;
  unsigned long nextConfig[4] = {0,0,0,0};
  unsigned long startFreq=430000000, endFreq=440000000, nrSamples=500, stepSize=50000;
  unsigned long currentFrequency = startFreq;
  unsigned long currentSamples = 0;
  unsigned char nextIndex=0;
  unsigned char rssiMin=0, rssiMax=0;
  unsigned long rssiSum=0;
  Red_LED_OFF;
  Green_LED_OFF;
#ifndef MATCATLRS_HW  
  digitalWrite(BUZZER, LOW);
#endif
  Serial.println("scanner mode");
  to_rx_mode();
  while(1) {
    while(Serial.available()) {
      c=Serial.read();
      switch (c) {
        case '#':
          nextIndex=0;
          nextConfig[0]=0;
          nextConfig[1]=0;
          nextConfig[2]=0;
          nextConfig[3]=0;
          break;
        case ',':
          nextIndex++;
          if (nextIndex==4) {
            nextIndex=0;
            startFreq = nextConfig[0] * 1000000UL; // MHz
            endFreq   = nextConfig[1] * 1000000UL; // MHz 
            nrSamples = nextConfig[2]; // count
            stepSize  = nextConfig[3] * 10000UL;   // 10kHz
            currentFrequency = startFreq;
            currentSamples = 0;
            // set IF filtter BW (kha)
            if (stepSize < 20000) {
              spiWriteRegister(0x1c,0x32); // 10.6kHz
            } else if (stepSize < 30000) {
              spiWriteRegister(0x1c,0x22); // 21.0kHz
            } else if (stepSize < 40000) {
              spiWriteRegister(0x1c,0x26); // 32.2kHz
            } else if (stepSize < 50000) {
              spiWriteRegister(0x1c,0x12); // 41.7kHz
            } else if (stepSize < 60000) {
              spiWriteRegister(0x1c,0x15); // 56.2kHz
            } else if (stepSize < 70000) {
              spiWriteRegister(0x1c,0x01); // 75.2kHz
            } else if (stepSize < 100000) {
              spiWriteRegister(0x1c,0x03); // 90.0kHz
            } else {
              spiWriteRegister(0x1c,0x05); // 112.1kHz
            }
          }
          break;
        default:
          if (( c >= '0') && (c <= '9')) {
            c-='0';
            nextConfig[nextIndex] = nextConfig[nextIndex] * 10 + c;
          }
      }
    }
    if (currentSamples == 0) {
      // retune base
      rfmSetCarrierFrequency(currentFrequency);
      rssiMax=0; rssiMin=255; rssiSum=0;
      delay(1);
    }
    if (currentSamples < nrSamples) {
      unsigned char val = rfmGetRSSI();
      rssiSum+=val;
      if (val>rssiMax) rssiMax=val;
      if (val<rssiMin) rssiMin=val;
      currentSamples++;
    } else {
      Serial.print(currentFrequency/10000UL);
      Serial.print(',');
      Serial.print(rssiMax);
      Serial.print(',');
      Serial.print(rssiSum/currentSamples);
      Serial.print(',');
      Serial.print(rssiMin);
      Serial.println(',');
      currentFrequency+=stepSize;
      if (currentFrequency > endFreq) {
        currentFrequency=startFreq;
      }
      currentSamples=0;
    }
  }
  //never exit!!
}


void handleCLI(char c) {
  switch(c) {
    case '?': bindPrint();
              break;
    case '#': scannerMode();
              break;
  }
}

void bindMode() {
  unsigned long prevsend = millis();
  init_rfm(1);
  while (Serial.available()) Serial.read(); // flush serial
  while(1) {
    if (millis() - prevsend > 200) {
      prevsend=millis();
      Green_LED_ON;
#ifndef MATCATLRS_HW
      digitalWrite(BUZZER, HIGH); // Buzzer on
#endif
      tx_packet((unsigned char*)&bind_data, sizeof(bind_data));
      Green_LED_OFF;
#ifndef MATCATLRS_HW
      digitalWrite(BUZZER, LOW); // Buzzer off
#endif
    }
    while (Serial.available()) handleCLI(Serial.read());
  }
}

void checkButton(){
#ifndef MATCATLRS_HW

  unsigned long time,loop_time;

  if (digitalRead(BTN)==0) // Check the button
    {
    delay(200); // wait for 200mS when buzzer ON
    digitalWrite(BUZZER, LOW); // Buzzer off

    time = millis();  //set the current time
    loop_time = time;

    while ((digitalRead(BTN)==0) && (loop_time < time + 4800)) {
      // wait for button reelase if it is already pressed.
      loop_time = millis();
    }

    // Check the button again, If it is still down reinitialize
    if (0 == digitalRead(BTN)) {
      int bzstate=HIGH;
      digitalWrite(BUZZER,bzstate);
      loop_time = millis();
      while (0 == digitalRead(BTN)) { // wait for button to release
        if ((millis()-loop_time) > 200) {
          loop_time=millis();
          bzstate=!bzstate;
          digitalWrite(BUZZER,bzstate);
          Serial.print("!");
        }
      }
      digitalWrite(BUZZER,LOW);
      randomSeed(micros()); // button release time in us should give us enough seed
      bindInitDefaults();
      bindRandomize();   
      bindWriteEeprom();
      bindPrint();
    }
    // Enter binding mode, automatically after recoding or when pressed for shorter time.      
    Serial.println("Entering binding mode\n");
    bindMode();
  }
#endif
}

void checkFS(void){
#ifndef MATCATLRS_HW

  switch (FSstate) {
    case 0:
      if (!digitalRead(BTN)) {
        FSstate=1;
        FStime=millis();
      }
      break;
    case 1:
      if (!digitalRead(BTN)) {
        if ((millis() - FStime) > 1000) {
          FSstate = 2;
          digitalWrite(BUZZER, HIGH); // Buzzer on
        }
      } else {
        FSstate = 0;
      }
      break;
    case 2:
      if (digitalRead(BTN)) {
        digitalWrite(BUZZER, LOW); // Buzzer off
        FSstate=0;
      }
      break;
  }
#endif
}

void setup() {

  //RF module pins
  pinMode(SDO_pin, INPUT); //SDO
  pinMode(SDI_pin, OUTPUT); //SDI
  pinMode(SCLK_pin, OUTPUT); //SCLK
  pinMode(IRQ_pin, INPUT); //IRQ
  pinMode(nSel_pin, OUTPUT); //nSEL

  //LED and other interfaces
  pinMode(Red_LED, OUTPUT); //RED LED
  pinMode(Green_LED, OUTPUT); //GREEN LED
#ifndef MATCATLRS_HW
  pinMode(BUZZER, OUTPUT); //Buzzer
  pinMode(BTN, INPUT); //Buton
  pinMode(PPM_IN, INPUT); //PPM from TX
#endif
  Serial.begin(SERIAL_BAUD_RATE);
  
  if (bindReadEeprom()) {
    Serial.print("Loaded settings from EEPROM\n");
  } else {
    Serial.print("EEPROM data not valid, reiniting\n");
    bindInitDefaults();
    bindWriteEeprom();
    Serial.print("EEPROM data saved\n");
  }
#ifndef MATCATLRS_HW
  setupPPMinput();
#endif
  init_rfm(0);
  rfmSetChannel(bind_data.hopchannel[RF_channel]);

  sei();

#ifndef MATCATLRS_HW
  digitalWrite(BUZZER, HIGH);
  digitalWrite(BTN, HIGH);
#endif
  Red_LED_ON ;
  delay(100);

  checkButton();

  Red_LED_OFF;
#ifndef MATCATLRS_HW
  digitalWrite(BUZZER, LOW);
#endif
  ppmAge = 255;
  rx_reset();

}

void loop() {
  #ifdef MATCATLRS_HW
  if (ppmAge > 8) {
    PPM[0] += 1;
    PPM[1] += 2;
    PPM[3] += 4;
    PPM[4] += 6;
    PPM[5] += 10;
    PPM[6] +=8;
    PPM[7] += 3;
    if (PPM[0] > 1023) {PPM[0] = PPM[0] - 1023;}
    if (PPM[1] > 1023) {PPM[1] = PPM[1] - 1023;}
    if (PPM[2] > 1023) {PPM[2] = PPM[2] - 1023;}
    if (PPM[3] > 1023) {PPM[3] = PPM[3] - 1023;}
    if (PPM[4] > 1023) {PPM[4] = PPM[4] - 1023;}
    if (PPM[5] > 1023) {PPM[5] = PPM[5] - 1023;}
    if (PPM[6] > 1023) {PPM[6] = PPM[6] - 1023;}
    if (PPM[7] > 1023) {PPM[7] = PPM[7] - 1023;}
    ppmAge = 0;
  }
  #endif
  if (spiReadRegister(0x0C)==0) // detect the locked module and reboot
  {
    Serial.println("module locked?");
    Red_LED_ON;
    init_rfm(0);
    rx_reset();
    Red_LED_OFF;
  }

  unsigned long time = micros();

  if ((time - lastSent) >= modem_params[bind_data.modem_params].interval) {
    lastSent = time;
    if (ppmAge < 8) {
      unsigned char tx_buf[11];
      ppmAge++;

      // Construct packet to be sent
      if (FSstate == 2) {
        tx_buf[0] = 0xF5; // save failsafe
        Red_LED_ON
      } else {
        tx_buf[0] = 0x5E; // servo positions
        Red_LED_OFF

      }

      cli(); // disable interrupts when copying servo positions, to avoid race on 2 byte variable
      tx_buf[1]=(PPM[0] & 0xff);
      tx_buf[2]=(PPM[1] & 0xff);
      tx_buf[3]=(PPM[2] & 0xff);
      tx_buf[4]=(PPM[3] & 0xff);
      tx_buf[5]=((PPM[0] >> 8) & 3) | (((PPM[1] >> 8) & 3)<<2) | (((PPM[2] >> 8) & 3)<<4) | (((PPM[3] >> 8) & 3)<<6);
      tx_buf[6]=(PPM[4] & 0xff);
      tx_buf[7]=(PPM[5] & 0xff);
      tx_buf[8]=(PPM[6] & 0xff);
      tx_buf[9]=(PPM[7] & 0xff);
      tx_buf[10]=((PPM[4] >> 8) & 3) | (((PPM[5] >> 8) & 3)<<2) | (((PPM[6] >> 8) & 3)<<4) | (((PPM[7] >> 8) & 3)<<6);
     sei();

      //Green LED will be on during transmission
      Green_LED_ON ;

      // Send the data over RF
      tx_packet(tx_buf,11);

      //Hop to the next frequency
      RF_channel++;
      if ( RF_channel >= bind_data.hopcount ) RF_channel = 0;
      rfmSetChannel(bind_data.hopchannel[RF_channel]);


    } else {
      if (ppmAge==8) {
        Red_LED_ON
      }
      ppmAge=9;
      // PPM data outdated - do not send packets
    }

  }

  //Green LED will be OFF
  Green_LED_OFF;

  checkFS();
}

