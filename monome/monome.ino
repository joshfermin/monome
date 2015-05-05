/////////////////////////
// DEBUG DEFINITIONS ////               
/////////////////////////
#define DEBUG
#define DEBUG2 
#define DEBUG3 
#define DEBUG_TIMING
#define DEBUG_MONOME

////////////////////////
// DEFINED CONSTANTS////
////////////////////////
#define BUFFER_LENGTH    3     // 3 bytes gives us 24 samples
#define NUM_ROWS         8
#define NUM_COLUMNS      8
#define NUM_INPUTS       NUM_ROWS+NUM_COLUMNS      // 8 rows, 8 columns
#define NUM_BUTTONS      NUM_ROWS * NUM_COLUMNS    // 64 buttons
#define TARGET_LOOP_TIME 744  // (1/56 seconds) / 24 samples = 744 microseconds per sample 

#define SERIAL9600
#include "settings.h"

/////////////////////////
// MAKEY MAKEY STRUCT ///
/////////////////////////
typedef struct {
  byte pinNumber;
  int keyCode;
  int timePressed;
  byte measurementBuffer[BUFFER_LENGTH]; 
  boolean oldestMeasurement;
  byte bufferSum;
  boolean pressed;
  boolean prevPressed;
} 
MakeyMakeyInput;
MakeyMakeyInput inputs[NUM_INPUTS];

// create some buttons to keep track of LED states
typedef struct {
  boolean pressed;
  boolean state;
  boolean highlight;
}
Button;
Button buttons [NUM_BUTTONS];
uint32_t ledColor = 0;
uint32_t ledHighlightColor = 0;
uint32_t ledHighlightOnColor = 0;

/////////////////////////
// NEOPIXELS ////////////
/////////////////////////
// http://learn.adafruit.com/adafruit-neopixel-uberguide/neomatrix-library
// set the Neopixel pin to 0 - D0
#define NEO_PIN 0
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel strip = Adafruit_NeoPixel(8, NEO_PIN, NEO_GRB + NEO_KHZ800);

///////////////////////////////////
// VARIABLES //////////////////////
///////////////////////////////////
int bufferIndex = 0;
byte byteCounter = 0;
byte bitCounter = 0;
byte inByte;

int pressThreshold;
int releaseThreshold;
int triggerThresh = 200;
boolean inputChanged;


/*
  KEY MAPPINGS
  0-5, pin D0 to D5 - pin D0 controls the Neopixels
  6 - click button pad 
  7 - space button pad
  8 - down arrow pad
  9-11, input status LED pin numbers (? unused)
  12 - up arrow pad
  13 - left arrow pad
  14 - pin D14 (unused)
  15 - right arrow pad
  16 - pin D16 (controls LED that indicates when a key is pressed)
  17 - (? unused)
  18-23, pin A0 to A5
*/

int pinNumbers[NUM_INPUTS] = {        
  // Rows ///////////////////////// 
  8,     // row 1 = down arrow pad
  15,    // row 2 = right arrow pad
//  7,     // row 3 = space button pad 
//  6,     // row 4 = click
//  2,     // row 5 = pin D2
//  3,     // row 6 = pin D3
//  4,     // row 7 = pin D4
//  5,     // row 8 = pin D5
  // Columns ///////////////////////
  12,     // column 1 = up arrow pad
  13,     // column 2 = left arrow pad
//  18,     // column 3 = A0
//  19,     // column 4 = A1
//  20,     // column 5 = A2
//  21,     // column 6 = A3
//  22,     // column 7 = A4
//  23      // column 8 = A5
};


// LED that indicates when key is pressed
const int outputK = 16;
byte ledCycleCounter = 0;

// timing
int loopTime = 0;
int prevTime = 0;
int loopCounter = 0;


///////////////////////////
// FUNCTIONS //////////////
///////////////////////////
void initializeArduino();
void initializeInputs();
void initializeNeopixels();
void updateMeasurementBuffers();
void updateBufferSums();
void updateBufferIndex();
void updateInputStates();
void addDelay();
void updateOutLED();
void updateNeopixels();

//////////////////////
// SETUP /////////////
//////////////////////
void setup() 
{
  initializeArduino();
  initializeInputs();
  initializeNeopixels();
  delay(100);
}

////////////////////
// MAIN LOOP ///////
////////////////////
void loop() 
{ 
  checkSerialInput();
  updateMeasurementBuffers();
  updateBufferSums();
  updateBufferIndex();
  updateInputStates();
  updateOutLED();
  addDelay();
}

//////////////////////////
// INITIALIZE ARDUINO
//////////////////////////
void initializeArduino() {
  Serial.begin(9600);  
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  /* Set up input pins 
   DEactivate the internal pull-ups, since we're using external resistors */
  for (int i=0; i<NUM_INPUTS; i++)
  {
    pinMode(pinNumbers[i], INPUT);
    digitalWrite(pinNumbers[i], LOW);
  }
  
  pinMode(outputK, OUTPUT);
  digitalWrite(outputK, LOW);


#ifdef DEBUG
  delay(4000); // allow us time to reprogram in case things are freaking out
#endif
}

///////////////////////////
// INITIALIZE INPUTS
///////////////////////////
void initializeInputs() {

  float thresholdPerc = SWITCH_THRESHOLD_OFFSET_PERC;
  float thresholdCenterBias = SWITCH_THRESHOLD_CENTER_BIAS/50.0;
  float pressThresholdAmount = (BUFFER_LENGTH * 8) * (thresholdPerc / 100.0);
  float thresholdCenter = ( (BUFFER_LENGTH * 8) / 2.0 ) * (thresholdCenterBias);
  pressThreshold = int(thresholdCenter + pressThresholdAmount);
  releaseThreshold = int(thresholdCenter - pressThresholdAmount);

#ifdef DEBUG
  Serial.println(pressThreshold);
  Serial.println(releaseThreshold);
#endif

  for (int i=0; i<NUM_INPUTS; i++) {
    inputs[i].pinNumber = pinNumbers[i];
    inputs[i].keyCode = keyCodes[i];

    for (int j=0; j<BUFFER_LENGTH; j++) {
      inputs[i].measurementBuffer[j] = 0;
    }
    inputs[i].oldestMeasurement = 0;
    inputs[i].bufferSum = 0;

    inputs[i].pressed = false;
    inputs[i].prevPressed = false;

#ifdef DEBUG
    Serial.println(i);
#endif

  }
}

void checkSerialInput() {
  /*
    here is what the incoming serial data means
    0 => no incoming serial data
    1-64 => strip.setPixelColor(i, ledColor) where i is between 0-63 (turn LED on)
    65-129 => strip.setPixelColor(i, 0) where i is between 0-63 (turn LED off)
    129 => clear monome
    130-137 => highlight columns 0-7
    >138 => corresponds to a color change of the LEDs
  */
  if (Serial.available() > 0) {
    // get incoming byte:
    inByte = Serial.read();
      
    // turn LED on
    if (inByte < NUM_BUTTONS + 1) {
      buttons[inByte-1].state = true;
      //updateNeopixels;
    }
    // turn LED off
    else if (inByte > NUM_BUTTONS && inByte < NUM_BUTTONS*2 + 1) {
      buttons[inByte-(NUM_BUTTONS+1)].state = false;
      //updateNeopixels();
    }
    // clear monome
    else if (inByte == NUM_BUTTONS*2 + 1) clearMonome();
    // highlight column
    else if (inByte > NUM_BUTTONS*2+1 && inByte < (NUM_BUTTONS)*2+2+NUM_COLUMNS) highlightColumn(inByte-(NUM_BUTTONS*2+2));
    // change ledColor
    //else ledColor = inByte - NUM_BUTTONS*2+2+NUM_COLUMNS;
  }
}

///////////////////////////
// INITIALIZE Neopixels
///////////////////////////
void initializeNeopixels() {
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  rainbow(20);
  clearNeopixels();
  setColors();
}



//////////////////////////////
// UPDATE MEASUREMENT BUFFERS
//////////////////////////////
void updateMeasurementBuffers() {

  for (int i=0; i<NUM_INPUTS; i++) {

    // store the oldest measurement, which is the one at the current index,
    // before we update it to the new one 
    // we use oldest measurement in updateBufferSums
    byte currentByte = inputs[i].measurementBuffer[byteCounter];
    inputs[i].oldestMeasurement = (currentByte >> bitCounter) & 0x01; 

    // make the new measurement
    boolean newMeasurement = digitalRead(inputs[i].pinNumber);

    // invert so that true means the switch is closed, i.e. touched
    newMeasurement = !newMeasurement; 

    // store it    
    if (newMeasurement) {
      currentByte |= (1<<bitCounter);
    } 
    else {
      currentByte &= ~(1<<bitCounter);
    }
    inputs[i].measurementBuffer[byteCounter] = currentByte;
  }
}

///////////////////////////
// UPDATE BUFFER SUMS
///////////////////////////
void updateBufferSums() {

  // the bufferSum is a running tally of the entire measurementBuffer
  // add the new measurement and subtract the old one

  for (int i=0; i<NUM_INPUTS; i++) {
    byte currentByte = inputs[i].measurementBuffer[byteCounter];
    boolean currentMeasurement = (currentByte >> bitCounter) & 0x01; 
    if (currentMeasurement) {
      inputs[i].bufferSum++;
    }
    if (inputs[i].oldestMeasurement) {
      inputs[i].bufferSum--;
    }
  }  
}

///////////////////////////
// UPDATE BUFFER INDEX
///////////////////////////
void updateBufferIndex() {
  bitCounter++;
  if (bitCounter == 8) {
    bitCounter = 0;
    byteCounter++;
    if (byteCounter == BUFFER_LENGTH) {
      byteCounter = 0;
    }
  }
}

///////////////////////////
// UPDATE INPUT STATES
///////////////////////////
void updateInputStates() {
  inputChanged = false;
  for (int i=0; i<NUM_INPUTS; i++) {
    inputs[i].prevPressed = inputs[i].pressed; // store previous pressed state (only used for mouse buttons)
    if (inputs[i].pressed) {
      if (inputs[i].bufferSum < releaseThreshold) {  
        inputChanged = true;
        inputs[i].pressed = false;
               
        
        /* TODO - can we reset the entire column or row here rather than updating the entire monome?
        One problem may be the fact that keys are constantly released- if, for example, another
        key is triggered (holding two keys at once), it's possible that the key release is flickered
        between the two keys. Effect: LED cell that flickers on/off rather than solid color.
        */
        
        if(i<8) resetRow(i);
        else resetColumn(i);
        updateMonome();
      }
    } 
    else if (!inputs[i].pressed) {
      if (inputs[i].bufferSum > pressThreshold) {  // input becomes pressed
        inputChanged = true;
        inputs[i].pressed = true; 
        updateMonome(); 
      }
    }
  }
#ifdef DEBUG3
  if (inputChanged) {
    Serial.println("change");
  }
#endif
}


///////////////////////////
// UPDATE MONOME
///////////////////////////
void updateMonome() {
  for(int i=0; i< NUM_ROWS; i++) {
    if(inputs[i].pressed){
      for(int j=0; j< NUM_COLUMNS; j++) {
        if(inputs[j+ NUM_ROWS].pressed) {
          int index = i*NUM_COLUMNS + j;
          if (!buttons[index].pressed) {
            if(!buttons[index].state) { 
              buttons[index].state = true;
              updateNeopixels();
#ifdef SERIAL9600 
              // add 1 to differentiate index from 0 bytes of serial data
              Serial.write(index+1);
#endif              
#ifdef DEBUG_MONOME
              Serial.print("button ");
              Serial.print(index);
              Serial.println(" ON");
#endif
            }
            else {
              buttons[index].state = false;
              updateNeopixels();
#ifdef SERIAL9600              
              Serial.write(index+1+NUM_BUTTONS);
#endif  
#ifdef DEBUG_MONOME
              Serial.print("button ");
              Serial.print(index);
              Serial.println(" OFF");
#endif
            }
            buttons[index].pressed = true;
          }
        }
      }
    }
  }
}

void resetRow(int rowNum) {
  for(int i=(rowNum*NUM_COLUMNS); i<(rowNum*NUM_COLUMNS+NUM_COLUMNS); i++) {
    buttons[i].pressed = false;
  }
}

void resetColumn(int column) {
  for(int i=column; i<NUM_COLUMNS; i++) {
    buttons[i*NUM_COLUMNS+column].pressed = false;
  }
}

void clearMonome() {
  clearNeopixels();
  for(int i=0; i<NUM_BUTTONS; i++) {
    buttons[i].state = false;
    buttons[i].highlight = false;
  }
}

void highlightColumn(int column) {
  for (int i=0; i<NUM_COLUMNS; i++) {
    // highlight the colum; buttons that are already on get a diff color
    buttons[column+i*NUM_COLUMNS].highlight = true;
    // turn off the column that was previously highlighted
    if (column == 0) {
       buttons[(NUM_COLUMNS-1)+i*NUM_COLUMNS].highlight = false;
    }
    else {
      buttons[column-1+i*NUM_COLUMNS].highlight = false;
    }
  }
  updateNeopixels();
}


///////////////////////////
// UPDATE NEOPIXELS
///////////////////////////
void updateNeopixels() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int stripIndex = getStripIndex(i);
    if (buttons[i].state) {
      if (buttons[i].highlight) strip.setPixelColor(stripIndex, ledHighlightOnColor);
      else strip.setPixelColor(stripIndex, ledColor);
    }
    else {
      if(buttons[i].highlight) strip.setPixelColor(stripIndex, ledHighlightColor);
      else strip.setPixelColor(stripIndex, 0);
    }
  }
  strip.show();
}

// since we zig zag the Neopixel strip, test if this is an even or odd row
// and return the strip index that corresponds to the monome index
int getStripIndex(int i) {
  if((i/NUM_ROWS)%2 == 0) return i;
  return (i/NUM_ROWS)*NUM_ROWS + NUM_COLUMNS - 1 - (i%NUM_COLUMNS);
}

void clearNeopixels() {
  for(int i=0; i<NUM_BUTTONS; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
}

///////////////////////////
// ADD DELAY
///////////////////////////
void addDelay() {

  loopTime = micros() - prevTime;
  if (loopTime < TARGET_LOOP_TIME) {
    int wait = TARGET_LOOP_TIME - loopTime;
    delayMicroseconds(wait);
  }

  prevTime = micros();

#ifdef DEBUG_TIMING
  if (loopCounter == 0) {
    int t = micros()-prevTime;
    Serial.println(t);
  }
  loopCounter++;
  loopCounter %= 999;
#endif

}


///////////////////////////
// UPDATE OUT LED
///////////////////////////
void updateOutLED() {
  boolean keyPressed = 0;
  for (int i=0; i<NUM_INPUTS; i++) {
    if (inputs[i].pressed) {
        keyPressed = 1;
#ifdef DEBUG
        Serial.print("Key ");
        Serial.print(i);
        Serial.println(" pressed");
#endif
    }
  }

  if (keyPressed) {
    digitalWrite(outputK, HIGH);
  }
  else {       
    digitalWrite(outputK, LOW);
  }
}


///////////////////////////
// NEOPIXEL COLORS
///////////////////////////
// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

void rainbow(uint8_t wait) {
  uint16_t i, j;
  for(j=0; j<256; j++) {
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel((i+j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

void setColors() {
  ledColor = strip.Color(10, 255, 79);
  ledHighlightColor = strip.Color(23, 114, 255);
  ledHighlightOnColor = strip.Color(10, 255, 79);
}


