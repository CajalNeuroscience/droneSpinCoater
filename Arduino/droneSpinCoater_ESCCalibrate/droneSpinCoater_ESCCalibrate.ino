/**************************************************************************
Sketch for droneCoater 
Compact spin coater based on a hollow-shafted drone motor
Sketch drives motor and OLED menu


-----------------------------------------------------------------
Sketch here is hastily modified to allow convenient throttle range calibrations on the 
ESC.  Also brake should be in OFF state to avoid jerk at the end of a spin.

ESC needs throttle calibrated at start. Should start with throttle at max, power on 
ESC, drop to min, wait for confirmation from ESC, then power down.

Directions to calibrate:
0. With assembled DroneCoater control module and a drone motor...
1. Upload this sketch to Arduino via USB.
2. Unplug USB and ESC from Arduino shield (VIN and OUTPUT)
3. Power Arduino (NOT ESC) via Arduino power port. 6.5 V - 9 V. 
4. When sketch is done loading and shows main tachometer/timer screen, press button. 
    RPMs should jump to max value. This confirms sketch is working.
5. Release button. Unplug power from Arduino.
6. Plug in VIN only on shield, not OUTPUT. 
7. Replug power into Arduino. 
8. On tach screen, press and hold button. RPMs should stay at max.
9. Plug in OUTPUT. If Arduino resets, go to 5 and repeat.
10. Motor should be sending out series of short beeps. 
11. Release button. After delay motor should send out a set of short then long beeps.
12. Unplug power from Arduino.
13. Plug power into ESC port on control module. Once sketch boots you should be able to make motor jump from 
     min to max speed with shield button. Note if motor freeruns on stop or stops suddenly with a jerk. 
     If the latter (jerk) proceed to the next step. If former (freerun down) then good to go!
14. For brake, repeat procedure from steps 5-12. 
    Change throttle from MAX -> MIN on second tone sequence (long-long-long-long) to toggle brake.
15. Upload normal droneSpinnerSketch and test.







Menu controlled as pages, designated with 'page' variable. Values:

0 - Logo
1 - Main display (tach and timer)
2 - First menu (select program)
3 - Edit values for free-run period
4 - End program action
5 - Sleep page when idle

Logo is splash screen at boot only.

Display proceeds to first menu when boot completes. 
  User can select stored program or 'Free Run' mode
  Program display name, speed, and duration in programNames, programTimes and programSpeeds.
  Currently only single-step protocols are included but a multi-step protocol should 
    be straightforward to add if needed.

Selecting a program enters the Main Display.  
  Speed of motor and countdown timer displayed.
  On completion, proceed to End program action.

Selecting Free Run in first menu enters page 3, value edits for free run mode.
  User can set speed and time for a new program.  
  This program will be stored in memory to re-run if needed.
  Selecting start on this program proceeds to Main display to run this new program.

Adding new programs or editing existing requires updating firmware.
  Adding/editing programs in device UI is a To Do action. 

Rotary encoder rotation changes value or selection point.
  Clicking rotary encoder selects or changes cursor type.

Pushbutton is for immediate stop of program, proceeding immediately to End program action. 

Will go into idle sleep mode when no buttons pushed or no program ends for 
timeToSleep*sleepDivider cycles of main loop. 

 **************************************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "cajalLogo.h"
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <RotaryEncoder.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Servo.h>

// Setup a RotaryEncoder with 2 steps per latch for the 2 signal input pins:
RotaryEncoder encoder(A2, A3, RotaryEncoder::LatchMode::TWO03);

#define buttonPin 3
#define LEDpin 9
#define motorPin 11
#define encoderPin 2

Servo motor;  // create servo object to control the motor

// Cursor spots is location of digit on screen. Give from left->right.
const unsigned int menuCursor[4][2] = {{1, 9}, {1, 23}, {1, 37}, {1, 51}};
const unsigned int editCursor[9][2] = {{5, 25}, {18, 25}, {32, 25}, {44, 25}, {6, 55}, {19, 55}, {37, 55}, {52, 55}, {114, 23}};

// Sleep constants
// Change timeToSleep or sleepDivider to change delay into sleep mode
const unsigned long timeToSleep = 400;
const int sleepDivider = 10;

// Constants for programs names. 
// Currently supports single-step protocols, but future work can have more complex curves
const char programNames[4] [15] = {{"Free run"}, {"3k x 1 min"}, {"5k x 59 min"}, {"1k x 59 min"}}; // Display name in menu
const int programTimes[4] = {3540, 60, 3540, 3540}; // Seconds
const int programSpeed[4] = {2500, 3000, 5000, 1000}; // RPM

// Constants for max speed (RPM) and max time (seconds) for a program in Free run mode
const int minMaxSpeed[2] = {0, 8000};
const int minMaxTime[2] = {0, 5999};

// Initial values for Free Run mode rotor speed and time 
volatile int rotorSet = 1000;
volatile int timeSet = minMaxTime[0];

// Motor speed constants
const unsigned int motorKV = 1700; // KV value of motor (nominal)
const unsigned int supplyVoltage = 5; // Supply voltage
const unsigned int rotorKickStart = 400; // Motor doesn't seem to turn at less than ~900 rpm, so give it a start
const unsigned int speedStep = 100; // Increment of RPM on each loop. Proxy for accelleration.



/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// Cursor volatiles
volatile unsigned int cursorRow = 0;
volatile unsigned int cursorCol = 0;
volatile int cursorLocX = 0;
volatile int cursorLocY = 0;
volatile int numOfCursorSpots = 0;
volatile int cursorSpot = 0;
volatile int stopTime = 0;

// Sleep volatiles
unsigned long sleepTimer = 0;
int sleepDivTick = 0;
bool firstTimeThrough;

volatile int holdSpot = 0;

volatile unsigned long last_interrupt_time = 0;
volatile unsigned long interrupt_time = 0;

char cursorChar = '>';
short cursorState = 1; // 0 = select Active
                       // 1 = edit Active
                       // 2 = select > symbol for main menu program select
bool editCursorActive = false;
bool selectCursorActive = true;

const unsigned int debounceTime = 50;
char dispString[5];
int dispVal = 0;


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LOGO_HEIGHT  64 
#define LOGO_WIDTH   128

volatile bool displayInvert = true;
const unsigned int initSpeed = 0;

volatile unsigned int rotorSpeed = rotorSet;
volatile unsigned int timeElapsed = 0;
volatile long timeOfSequence = 0;
volatile byte sequenceStep = 0;
volatile byte sequenceTotal = 0;
char speedOut[5];
char elapsedOut[18];
volatile bool runProgram = false;
volatile unsigned long startTime;

int selectMenuXstart = 11;
int selectMenuYstart = 9;
int selectMenuYspace = 14;

int page = 0;
int holdPage = 0;
int lastPage = 0;

const unsigned int speedStepDivider = 1; // divisor of loop ticks -> speed steps
int speedStepCounter = 0;
int speedSign = 1;
const unsigned int motorSpeed = motorKV*supplyVoltage;
int outVal = 0;

void setup() {


  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
  //  Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  PCICR |= (1 << PCIE1);    // This enables Pin Change Interrupt 1 that covers the Analog input pins or Port C.
  PCMSK1 |= (1 << PCINT10) | (1 << PCINT11);  // This enables the interrupt for pin 2 and 3 of Port C.

  // Attach interrupts for buttons
  attachInterrupt(digitalPinToInterrupt(encoderPin), encoderPush, RISING);
  attachInterrupt(digitalPinToInterrupt(buttonPin), buttonPush, CHANGE);
  
  display.setTextColor(SSD1306_WHITE);

  drawLogo();    // Draw a small bitmap image

  // Attach motor
  motor.attach(motorPin, 1000, 2000);  // attaches the servo on pin 11 to the servo object
  motor.write(initSpeed);

  // Set up menu for the first time
  page = 2;
  moveCursor();
  setCursorState();

  runProgram = false;

  delay(2500);
  page = 1;
  
}

void loop() {
  // Draw UI on screen
  // Page designates what is drawn in this loop

  motor.write(rotorSpeed);

  switch (page) {
    case 0:
      drawLogo();
      break; 
      
    case 1:
      drawMainDisplay();
      break;
      
    case 2:
      drawFirstMenu();
      break;

    case 3:
      drawValueEditPage();
      break;

    case 4:
      drawProgramEndPage();
      break;

    case 5:
      drawSleepPage();
      break;
  }
}

ISR(PCINT1_vect) {
   encoder.tick(); // just call tick() to check the state.
}

int measureRotorSpeed(){
  // If available with hardware, return measured speed of motor

  // Dummy return set value of rotor speed.
  return rotorSpeed;
}

void startRotor(){
  // Put function to start rotor moving here
  runProgram = true;
  timeElapsed = 0;
  rotorSpeed = rotorKickStart;
  startTime = millis();

  

  page = 1;
  digitalWrite(LEDpin, HIGH);
  motor.write(rotorSpeed);
}

void stopRotor(){
  // Function to stop rotor spinning 
  
  digitalWrite(LEDpin, LOW);

  page = 4;
  timeElapsed = 0;
  rotorSpeed = minMaxSpeed[0];
  runProgram = false;
  sleepDivTick = 0;
  sleepTimer = 0;

  motor.write(rotorSpeed); 
  
}

void moveCursor(){
  // Move position of cursor on screen. 
  // Arrays defined above set positions possible.
  // Position will stop at end points if driven past end point
    cursorSpot = (cursorSpot - (int) encoder.getDirection());

    switch (page){
      case 2:
        // First menu
        numOfCursorSpots = sizeof(menuCursor)/sizeof(menuCursor[cursorSpot]);
        correctCursor();
        cursorLocX = menuCursor[cursorSpot][0];
        cursorLocY = menuCursor[cursorSpot][1];
        break;
        
      case 3:
        // Edit values
        numOfCursorSpots = sizeof(editCursor)/sizeof(editCursor[cursorSpot]);
        correctCursor();
        cursorLocX = editCursor[cursorSpot][0];
        cursorLocY = editCursor[cursorSpot][1];
        break;
    }


    
}

void correctCursor(){
  // Change cursor value to enforce end points
    if (cursorSpot >= numOfCursorSpots){
      cursorSpot = numOfCursorSpots - 1;
    }
    if (cursorSpot < 0){
      cursorSpot = 0;
    }
}

void editDigit() {
  // Use encoder to change value of speed or time in Free Run menu
  unsigned int addIn = 1;
  switch (cursorSpot)
    {

    case 0 ... 3: 
      // Edit speed
        

        for (int i = 0; i < (3 - cursorSpot); i++){
          addIn = addIn*10;
        }
        addIn = addIn*(int) encoder.getDirection();
        
        rotorSet = rotorSet + addIn;
        
        if (rotorSet <= minMaxSpeed[0]){
          rotorSet = minMaxSpeed[0];
        }
        
        if (rotorSet >= minMaxSpeed[1]){
          rotorSet = minMaxSpeed[1];
        }
    break;
    
    case 4 ... 7 :
      // Edit time

        // Cursor shows MM:SS, but value stored as seconds
        // brute force a bit to set addIn value.
        switch (cursorSpot){
          case 7:
            addIn = 1;
            break;

          case 6:
            addIn = 10;
            break;

          case 5:
            addIn = 60;
            break;

          case 4:
            addIn = 600;
            break;
        }

        addIn = addIn*(int) encoder.getDirection();
        
        timeSet = timeSet + addIn;
        
        if (timeSet <= minMaxTime[0]){
          timeSet = minMaxTime[0];
        }
        
        if (timeSet >= minMaxTime[1]){
          timeSet = minMaxTime[1];
        }

    break; 

    case 8 : 
      // Do nothing
    break;
  }
}
    


void setCursorState(){
  // Set status of state variables for cursor
  // Click of encoder wheel cycles through states on page = 3
  switch (page) {
    case 0:
      // Logo page
      // leave as initialization values
      editCursorActive = false;
      break;
    
    case 1:
      // Main speed + time display
      // No cursor, so leave as current values
      editCursorActive = false;
      break;
  
    case 2:
      // Select program menu
      // Char is > and always visible
      cursorState = 2;
      cursorChar = '>';
      editCursorActive = false;
      break;
  
    case 3:
      // Edit value with cursor and encoder rotation
      // Nice and complex
      // cursorState:
      // 0 = select Active (^)
      // 1 = edit Active (=)
      //
      // Needs a 'go!' select event
      // Selected when cursor is in position of 'go' text
  
        if (cursorState >= 1) {
          cursorState = 0;
          editCursorActive = false;
          selectCursorActive = true;
          cursorChar = '^';
        }
        else {
          cursorState = 1;
          editCursorActive = true;
          selectCursorActive = false;
          cursorChar = '=';
        }

        break;

    case 4:
      // End program page
      // no cursor
      editCursorActive = false;
      break;
    
    }
}

void encoderPush() 
// Push button on encoder. 
// Cycle through edit/move modes on page = 3
// Normal behavior is select item indicated by cursor

{
   static unsigned long encoder_last_interrupt_time = 0;
   unsigned long encoder_interrupt_time = millis();
   // If interrupts come faster than 200ms, assume it's a bounce and ignore
   if (encoder_interrupt_time - encoder_last_interrupt_time > debounceTime)
   {
  
      switch (page) {
        case 0:
          // Logo display; do nothing
          break;
          
        case 1:
          // Main display; do nothing
          break;
    
        case 2:
          selectProgram();
          break;
    
        case 3:
          if (cursorSpot == 8){
            // 'GO!' box
            startRotor();
          }
          else {
            // Toggle cursor state and maybe select digit to edit
            setCursorState();
          }
          
          break;
    
        case 4:
          // End program pic
          // Do nothing
          break;
      } // switch on page
   } // if interrupt_time long enough
   encoder_last_interrupt_time = encoder_interrupt_time;

} // fcn def

void selectProgram(){
  // Select program or set values and then execute program
  // From page 2 (select program menu) or from page 3 (edit values from free-run mode)
  switch (page) {
    case 2:
      // Select program
      holdSpot = cursorSpot; // Hold onto the cursor spot for later
      
      switch (cursorSpot){
        case 0:
          // Free run
          // Go to edit values page
          
          page = 3;
          setCursorState(); // need to update cursor here so it'll change the first time
          moveCursor();
          
          break;
        default:
          // Set time and speed values to match program
          // Run program   
          timeSet = programTimes[cursorSpot];
          rotorSet = programSpeed[cursorSpot];       
          startRotor();
          break;
      }
     break;

     default:
      // Do nothing
      break;
  }
}

void buttonPush(){
// Stop rotor
// Show ending program page  
  if (digitalRead(buttonPin)){
    rotorSpeed = minMaxSpeed[0];
  }
  else{
    rotorSpeed = minMaxSpeed[1];
  }
  

}

void drawMainDisplay() {
/* Main screen of device to show on loop
 * Has speed, timeElapsed/timeSet/totalTimeOfSequence and 
 * which step of protocol displayed
 * Page = 1
*/
  display.clearDisplay();

  // RPM
  display.setFont(&FreeMono9pt7b);
  display.setTextSize(1); 
  display.setCursor(87, 25);
  display.println(F("rpm"));

  // rotorSpeed
  display.setFont(&FreeSans18pt7b);
  display.setTextSize(1); 
  display.setCursor(4, 25);
  //sprintf(speedOut, "% 4d", rotorSpeed);
  display.println(rotorSpeed);

  display.drawLine(10, 33, 118, 33, SSD1306_WHITE);

  // Time remaining
  display.setFont(&FreeSans12pt7b);
  display.setTextSize(1); 
  display.setCursor(3, 56);
  sprintf(elapsedOut, "%02d:%02d/%02d:%02d", (timeElapsed / 60), (timeElapsed % 60), (timeSet / 60), (timeSet % 60));
  display.println(elapsedOut);

  display.display();      // Show initial text
}

void drawLogo(void) {
  // Draw logo defined in variable logo_bmp
  // Page = 0
  display.clearDisplay();

  display.drawBitmap(
    (display.width()  - LOGO_WIDTH ) / 2,
    (display.height() - LOGO_HEIGHT) / 2,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
  display.display();
}

void drawFirstMenu(void) {
  // Draw program selection page
  // Page = 2
  display.clearDisplay();
  display.setFont();
  display.setTextSize(1); 

  for (int i = 0; i < sizeof(programNames)/sizeof(i); i++) {
    display.setCursor(selectMenuXstart, selectMenuYstart + i*selectMenuYspace);
    display.println(programNames[i]);
    
  }

  display.setCursor(cursorLocX, cursorLocY);
  display.println(cursorChar);

// Uncomment below to include numeral of current sleepTimer value
// Useful to set values for timeToSleep
  //display.setCursor(100, 20);
 // display.println(sleepTimer);

  display.display();

/*
  programNames
  */
}


void drawValueEditPage() {
  // Draw page to edit time and speed values
  // Page = 4
  display.clearDisplay();

  // RPM
  display.setFont(&FreeMono9pt7b);
  display.setTextSize(1); 
  display.setCursor(58, 22);
  display.println(F("rpm"));

  // rotorSpeed
  display.setFont(&FreeSans12pt7b);
  display.setTextSize(1); 

  int rotorSpeedCursor = 1;
  
  if (rotorSet < 1000){
    rotorSpeedCursor = 14;
  }

  if (rotorSet < 100){
    rotorSpeedCursor = 27;
  }

  if (rotorSet < 10){
    rotorSpeedCursor = 40;
  }

  display.setCursor(rotorSpeedCursor, 22);
  //sprintf(speedOut, "% 4d", rotorSpeed);
  display.println(rotorSet);

    // time
  display.setFont(&FreeMono9pt7b);
  display.setTextSize(1); 
  display.setCursor(65, 52);
  display.println(F("time"));

    // Time remaining
  display.setFont(&FreeSans12pt7b);
  display.setTextSize(1); 
  display.setCursor(2, 52);
  sprintf(elapsedOut, "%02d:%02d", (timeSet / 60), (timeSet % 60));
  display.println(elapsedOut);

  display.setFont();

  display.drawRect(105, 0, 23, 20, SSD1306_WHITE);
  display.setCursor(109, 7);
  display.println("GO!");

  display.setCursor(cursorLocX, cursorLocY);
  display.println(cursorChar);

  display.display();
  
}

void drawProgramEndPage() {
  // Draw page to alert end of program
  // Page = 4
  // Flash display to acknowledge button press
  display.invertDisplay(true);
  delay(400);
  display.invertDisplay(false);

  /*
  // Show 'Program end' on blank screen.
  // Useful for troubleshooting
  display.clearDisplay();
  display.setFont();
  display.setCursor(10, 10);
  display.println("Program end!");

  display.display();

  delay(1000);
*/
  page = 2;
  cursorSpot = holdSpot; // Restore saved spot for cursor on page 2
  moveCursor();
  setCursorState(); 

}

void drawSleepPage() {
  // Draw page for sleep mode
  // Set here to display logo as in boot page
  // Page = 5
  display.clearDisplay();

// Comment this block to make sleep page a blank screen
  display.drawBitmap(    
    (display.width()  - LOGO_WIDTH ) / 2,
    (display.height() - LOGO_HEIGHT) / 2,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);

  display.display();
}
