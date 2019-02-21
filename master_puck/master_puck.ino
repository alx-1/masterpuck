
#include <TimerOne.h>

/*
 * FEATURE: TAP BPM INPUT

//////#define TAP_PIN 7
//////#define TAP_PIN_POLARITY RISING

//////#define MINIMUM_TAPS 3
//////#define EXIT_MARGIN 150 // If no tap after 150% of last tap interval -> measure and set
 */
 
/*
 * FEATURE: DIMMER BPM INPUT
 */
//#define DIMMER_INPUT_PIN A0  // le piton start/stop est à A0
//#define DIMMER_CHANGE_MARGIN 10 //  15 // Big value to make sure this doesn't interfere. Tweak as needed.

/*
 * FEATURE: DIMMER BPM INCREASE/DECREASE
 */
#define DIMMER_CHANGE_PIN A1 // dial
#define DEAD_ZONE 220 // 90
#define CHANGE_THRESHOLD 5000 // 3000
#define RATE_DIVISOR 6 // 5

/*
 * FEATURE: BLINK TEMPO LED
 */
#define BLINK_OUTPUT_PIN 5

#define BLINK_PIN_POLARITY 0  // 0 = POSITIVE, 255 - NEGATIVE
#define BLINK_TIME 4 // How long to keep LED lit in CLOCK counts (so range is [0,24])

/*
 * FEATURE: SYNC PULSE OUTPUT
 */
 /*
#define SYNC_OUTPUT_PIN 8 // Can be used to drive sync analog sequencer (Korg Monotribe etc ...)
#define SYNC_PIN_POLARITY 0 // 0 = POSITIVE, 255 - NEGATIVE
*/

/*
 * FEATURE: Send MIDI start/stop
 */
/////////#define START_STOP_INPUT_PIN ÉTAIT A1

#define START_STOP_INPUT_PIN A0 // piton
#define START_STOP_PIN_POLARITY 1024 // 0 = POSITIVE, 1024 = NEGATIVE

#define MIDI_START 0xFA
#define MIDI_STOP 0xFC

#define DEBOUNCE_INTERVAL 500L // Milliseconds

/*
 * FEATURE: EEPROM BPM storage
 */
#define EEPROM_ADDRESS 0 // Where to save BPM
#ifdef EEPROM_ADDRESS
#include <EEPROM.h>
#endif

/*
 * FEATURE: MIDI forwarding
 */
#define MIDI_FORWARD

/*
 * GENERAL PARAMETERS
 */
#define MIDI_TIMING_CLOCK 0xF8
#define CLOCKS_PER_BEAT 24
#define MINIMUM_BPM 400 // Used for debouncing
#define MAXIMUM_BPM 3000 // Used for debouncing

long intervalMicroSeconds;
int bpm;  // BPM in tenths of a BPM!!

boolean initialized = false;
long minimumTapInterval = 60L * 1000 * 1000 * 10 / MAXIMUM_BPM;
long maximumTapInterval = 60L * 1000 * 1000 * 10 / MINIMUM_BPM;

volatile long firstTapTime = 0;
volatile long lastTapTime = 0;
volatile long timesTapped = 0;

volatile int blinkCount = 0;

int lastDimmerValue = 0;

boolean playing = false;
long lastStartStopTime = 0;


#ifdef DIMMER_CHANGE_PIN
long changeValue = 0;
#endif


////////// ECRAN /////


#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// SCL GPIO5
// SDA GPIO4
#define OLED_RESET 0  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);

#define NUMFLAKES 1
#define XPOS 0
#define YPOS 1
#define DELTAY 4


#define LOGO16_GLCD_HEIGHT 48
#define LOGO16_GLCD_WIDTH  48
static const unsigned char PROGMEM logo16_glcd_bmp[] =
{ 
  
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11001111, B11111111, B11111111,
B11111111, B11111110, B00000001, B11001111, B11111111, B11111111,
B11111111, B11110000, B00000000, B11001111, B11111111, B11111111,
B11111111, B11000000, B00000000, B11100111, B11111111, B11111111,
B11111111, B10000000, B00000000, B01100111, B11111111, B11111111,
B11111110, B00000000, B00000000, B01110011, B11111111, B11111111,
B11111100, B00000000, B00000000, B00110011, B11111111, B11111111,
B11111100, B00000000, B00000000, B00110001, B11111111, B11111111,
B11111000, B00000000, B00000000, B00111001, B11111111, B10011111,
B11111000, B00000000, B00000000, B00011001, B11111111, B10011111,
B11110000, B00000000, B00000000, B00011100, B11111111, B00011111,
B11110000, B00000000, B00000000, B00011100, B11111111, B00001111,
B11100000, B00000000, B00000000, B00001110, B01111111, B00011111,
B11100000, B00000000, B00000000, B00001110, B01111100, B00000011,
B11100000, B00000000, B00000000, B00000110, B00111100, B00000011,
B11100000, B00000000, B00000000, B00000111, B00111100, B00000011,
B11100000, B00000000, B00000000, B00000011, B00011111, B10011111,
B11100000, B00000000, B00001110, B00000011, B10011111, B00001111,
B11100000, B00000000, B00111111, B10000011, B10011111, B00001111,
B11100000, B00000000, B01111111, B11000001, B11001111, B10011111,
B11100000, B00000000, B01111111, B11000001, B11001111, B00001111,
B11100000, B00000000, B01111111, B11000000, B11100111, B11111111,
B11100000, B00000000, B01111111, B11000000, B11100000, B00000001,
B11100000, B00000000, B01111111, B11000000, B01100000, B00000000,
B11100000, B00000000, B01111111, B11000000, B01110000, B00000000,
B11100000, B00000000, B01111111, B11000000, B00110000, B00000000,
B11100000, B00000000, B01111111, B11000000, B00111000, B00000000,
B11100000, B00000000, B11111111, B11111111, B11111000, B00000000,
B11111111, B11111111, B11111111, B11111111, B11111000, B00000000,
B11111111, B11111111, B11111111, B11111111, B11111101, B00100001,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111,
B11111111, B11111111, B11111111, B11111111, B11111111, B11111111 
};
 

#if (SSD1306_LCDHEIGHT != 48)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

void setup()   {
Serial.begin(38400);
  //  Set MIDI baud rate:
  Serial1.begin(31250);

  // Set pin modes
#ifdef BLINK_OUTPUT_PIN
  pinMode(BLINK_OUTPUT_PIN, OUTPUT);
#endif
#ifdef SYNC_OUTPUT_PIN
  pinMode(SYNC_OUTPUT_PIN, OUTPUT);
#endif
#ifdef DIMMER_INPUT_PIN
  pinMode(DIMMER_INPUT_PIN, INPUT);
#endif
#ifdef START_STOP_INPUT_PIN
  pinMode(START_STOP_INPUT_PIN, INPUT);
#endif

#ifdef EEPROM_ADDRESS
  // Get the saved BPM value from 2 stored bytes: MSB LSB
  bpm = EEPROM.read(EEPROM_ADDRESS) << 8;
  bpm += EEPROM.read(EEPROM_ADDRESS + 1);
  if (bpm < MINIMUM_BPM || bpm > MAXIMUM_BPM) {
    bpm = 1200;
  }
#endif

#ifdef TAP_PIN
  // Interrupt for catching tap events
  attachInterrupt(digitalPinToInterrupt(TAP_PIN), tapInput, TAP_PIN_POLARITY);
#endif

  // Attach the interrupt to send the MIDI clock and start the timer
  Timer1.initialize(intervalMicroSeconds);
  Timer1.setPeriod(calculateIntervalMicroSecs(bpm));
  Timer1.attachInterrupt(sendClockPulse);

#ifdef DIMMER_INPUT_PIN
  // Initialize dimmer value
  lastDimmerValue = analogRead(DIMMER_INPUT_PIN);
  // Serial.print("hey input dim : ");
  Serial.println("lastDimmerValue : ");
  Serial.println(lastDimmerValue);
  #endif


  ////// ECRAN //////////

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  // init done

  // Clear the buffer.
  display.clearDisplay();
  
 // draw a bitmap icon and 'animate' movement
  testdrawbitmap(logo16_glcd_bmp, LOGO16_GLCD_HEIGHT, LOGO16_GLCD_WIDTH);
  
  // text display tests
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(bpm);
  display.println("BPM");
  display.display();
  delay(2000);
  display.clearDisplay();

 
}


void loop() {

 // Serial.println("hello world");

  long now = micros();

#ifdef TAP_PIN
  /*
   * Handle tapping of the tap tempo button
   */
  if (timesTapped > 0 && timesTapped < MINIMUM_TAPS && (now - lastTapTime) > maximumTapInterval) {
    // Single taps, not enough to calculate a BPM -> ignore!
      Serial.println("Ignoring lone taps!");
    timesTapped = 0;
  } else if (timesTapped >= MINIMUM_TAPS) {
    long avgTapInterval = (lastTapTime - firstTapTime) / (timesTapped - 1);
    if ((now - lastTapTime) > (avgTapInterval * EXIT_MARGIN / 100)) {
      bpm = 60L * 1000 * 1000 * 10 / avgTapInterval;
      updateBpm(now);
  
      // Update blinkCount to make sure LED blink matches tapped beat
      blinkCount = ((now - lastTapTime) * 24 / avgTapInterval) % CLOCKS_PER_BEAT;

      timesTapped = 0;
    }
  }
#endif

//////////// PAS UTILISÉ ////////////
#ifdef DIMMER_INPUT_PIN
  /*
   * Handle change of the dimmer input
   */
  int curDimValue = analogRead(DIMMER_INPUT_PIN);
  Serial.println("DIMMER INPUT PIN : ");
  Serial.println(curDimValue);
  if (curDimValue > lastDimmerValue + DIMMER_CHANGE_MARGIN
      || curDimValue < lastDimmerValue - DIMMER_CHANGE_MARGIN) {
    // We've got movement!!
    bpm = map(curDimValue, 0, 1024, MINIMUM_BPM, MAXIMUM_BPM);

    updateBpm(now);
    lastDimmerValue = curDimValue;
  }
#endif


////////////// C'EST CETTE OPTION POUR LE MASTER PUCK /////////////

#ifdef DIMMER_CHANGE_PIN
  int curDimValue = analogRead(DIMMER_CHANGE_PIN);
  
  //Serial.print("DEBUG DIMMER CHANGE PIN : ");
  //Serial.println(curDimValue);

  // (320 - 1023) MIN ET MAX
  // MILIEU EST 675
  int MILIEU = 675;
 

  if (bpm > MINIMUM_BPM && curDimValue < (MILIEU - DEAD_ZONE)) {
    int val = (MILIEU - DEAD_ZONE - curDimValue) / RATE_DIVISOR;
    changeValue += val * val;
  } else if (bpm < MAXIMUM_BPM && curDimValue > (MILIEU + DEAD_ZONE)) {
    int val = (curDimValue - MILIEU - DEAD_ZONE) / RATE_DIVISOR;
    changeValue += val * val;
  } else {
    changeValue = 0;
  }
  if (changeValue > CHANGE_THRESHOLD) {
    bpm += curDimValue < MILIEU ? -1 : 1;
    updateBpm(now);
    changeValue = 0;
  }


  
#endif

#ifdef START_STOP_INPUT_PIN
  /*
   * Check for start/stop button pressed
   */
  
  boolean startStopPressed;
  
      if(analogRead(START_STOP_INPUT_PIN) >= 0 && analogRead(START_STOP_INPUT_PIN) <= 50) {
    startStopPressed = 1;
  } else {
    startStopPressed = 0;
  }

  if (startStopPressed && (lastStartStopTime + (DEBOUNCE_INTERVAL * 1000)) < now) {
    startOrStop();
    lastStartStopTime = now;
    //Serial.println("start/stop");
  }
#endif

#ifdef MIDI_FORWARD
  /*
   * Forward received serial data
   */
  while (Serial1.available()) {
    int b = Serial1.read();
    Serial1.write(b);
  }
#endif
} // FIN DU LOOP ////  




void testdrawbitmap(const uint8_t *bitmap, uint8_t w, uint8_t h) {
  uint8_t icons[NUMFLAKES][3];

  // initialize
   int counter = 0;
  for (uint8_t f=0; f< NUMFLAKES; f++) {
    //icons[f][XPOS] = random(display.width());
    icons[f][XPOS] = 0;
    icons[f][YPOS] = 0;
    icons[f][DELTAY] = random(5) + 1;

    Serial.print("x: ");
    Serial.print(icons[f][XPOS], DEC);
    Serial.print(" y: ");
    Serial.print(icons[f][YPOS], DEC);
    Serial.print(" dy: ");
    Serial.println(icons[f][DELTAY], DEC);
  }

  while (counter < 3) {
    // draw each icon
    for (uint8_t f=0; f< NUMFLAKES; f++) {
      display.drawBitmap(icons[f][XPOS], icons[f][YPOS], bitmap, w, h, WHITE);
    }
    display.display();
    delay(200);

    // then erase it + move it
    for (uint8_t f=0; f< NUMFLAKES; f++) {
      display.drawBitmap(icons[f][XPOS], icons[f][YPOS], bitmap, w, h, BLACK);
      // move it
   
      icons[f][XPOS] += icons[f][DELTAY];
      // if its gone, reinit
      if (icons[f][XPOS] > display.width()) {
  
        icons[f][XPOS] = 0;
        icons[f][YPOS] = 0;
        icons[f][DELTAY] = random(5) + 2;
        counter++;
      }
    }
   }
} // FIN DE TEST DRAW BITMAP

////// LES FONCTIONS /////



///// LES FONCTIONS /////
void tapInput() {
  long now = micros();
  if (now - lastTapTime < minimumTapInterval) {
    return; // Debounce
  }

  if (timesTapped == 0) {
    firstTapTime = now;
  }

  timesTapped++;
  lastTapTime = now;
  Serial.println("Tap!");
}

void startOrStop() {
  if (!playing) {
    // Serial.println("Start playing");
    Serial.println("1");
    Serial1.write(MIDI_START);
  } else {
    //Serial.println("Stop playing");
    Serial.println("0");
    Serial1.write(MIDI_STOP);
  }
  playing = !playing;
}

void sendClockPulse() {
  // Write the timing clock byte
  Serial1.write(MIDI_TIMING_CLOCK);

  blinkCount = (blinkCount + 1) % CLOCKS_PER_BEAT;
  if (blinkCount == 0 && playing) {
    // Turn led on
#ifdef BLINK_OUTPUT_PIN
    analogWrite(BLINK_OUTPUT_PIN, 255 - BLINK_PIN_POLARITY);
#endif

#ifdef SYNC_OUTPUT_PIN
    // Set sync pin to HIGH
    analogWrite(SYNC_OUTPUT_PIN, 255 - SYNC_PIN_POLARITY);
#endif
  } else {
#ifdef SYNC_OUTPUT_PIN
    if (blinkCount == 1) {
      // Set sync pin to LOW
      analogWrite(SYNC_OUTPUT_PIN, 0 + SYNC_PIN_POLARITY);
    }
#endif
#ifdef BLINK_OUTPUT_PIN
    if (blinkCount == BLINK_TIME) {
      // Turn led on
      analogWrite(BLINK_OUTPUT_PIN, 0 + BLINK_PIN_POLARITY);
    }
#endif
  }
}

void updateBpm(long now) {
  // Update the timer
  long interval = calculateIntervalMicroSecs(bpm);
  Timer1.setPeriod(interval);

#ifdef EEPROM_ADDRESS
  // Save the BPM in 2 bytes, MSB LSB
  EEPROM.write(EEPROM_ADDRESS, bpm / 256);
  EEPROM.write(EEPROM_ADDRESS + 1, bpm % 256);
#endif

  Serial.print("Set BPM to: ");
  Serial.print(bpm / 10);
  Serial.print('.');
  Serial.println(bpm % 10);
 Serial.println(bpm);
 // check if last BPM digit is a '0' //
 
    // text display tests
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(bpm / 10);
  if(bpm % 10 == 0){
  display.println("BPM"); 
  }
  else{
  display.print('.');
  display.println(bpm % 10);
  }
  
  display.display();
  //delay(2000);
  display.clearDisplay();

  

}

long calculateIntervalMicroSecs(int bpm) {
  // Take care about overflows!
  return 60L * 1000 * 1000 * 10 / bpm / CLOCKS_PER_BEAT;
}








