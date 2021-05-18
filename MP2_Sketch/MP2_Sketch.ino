/*
 * Richard Halbert
 * Made for University of Washington
 * CSE 490F MP2 Spring 2021
 * 
 * Runs a game called "Ball Jump" (based on Doodle Jump) on an
 * Adafruit SSD1306 OLED display using an ESP32 microcontroller.
 * There are two modes in the game: (1) normal mode in which the
 * user controls the x-axis of the ball using a joystick and
 * (2) creative mode in which the user controlls the x-axis of
 * the ball using an Adafruit LIS3DH accelerometer. 
 * 
 * Please note: the "left button" controls the top button on
 * whatever screen is currently shown and the "right button"
 * controls the bottom (due to how the breadboard was setup).
 */

#include <stdlib.h>
#include <time.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

/*
 * Setup for the Adafruit SSD1306 OLED display
 * Based on the Adafruit SSD1306 ssd1306_128x64_i2c.ino example
 */
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/*
 * Setup for the Adafruit LIS3DH accelerometer
 * Based on instructions from: https://learn.adafruit.com/adafruit-lis3dh-triple-axis-accelerometer-breakout
 * 
 * Includes methods of running the LIS3DH with software SPI,
 * hardware SPI, or I2C, although this project uses software SPI.
 */
// Used for software SPI
#define LIS3DH_CLK 21
#define LIS3DH_MISO 17
#define LIS3DH_MOSI 16
// Used for hardware & software SPI
#define LIS3DH_CS 19

// software SPI
Adafruit_LIS3DH lis = Adafruit_LIS3DH(LIS3DH_CS, LIS3DH_MOSI, LIS3DH_MISO, LIS3DH_CLK);
// hardware SPI
//Adafruit_LIS3DH lis = Adafruit_LIS3DH(LIS3DH_CS);
// I2C
// Adafruit_LIS3DH lis = Adafruit_LIS3DH();

/*
 * Keys for the buzzer
 * From the Makeability Lab Intro to Arduino/Input/A simple piano lesson:
 * https://makeabilitylab.github.io/physcomp/arduino/piano.html
 */
#define KEY_C 262  // 261.6256 Hz
#define KEY_E 330  // 329.6276 Hz
#define KEY_G 392  // 391.9954 Hz

/*
 * Wiring constants
 */
const int JOYSTICK_LR_INPUT_PIN = A5;
const int LEFT_BUTTON_INPUT_PIN = 14;
const int RIGHT_BUTTON_INPUT_PIN = 32;
const int VIBRO_OUTPUT_PIN = 15;
const int PIEZO_OUTPUT_PIN = 33;

/*
 * Background constants
 */
const int DEBOUNCE_WINDOW = 40;
const int BUTTON_OUTLINE = 5;
const int JOYSTICK_WINDOW = 20;
const int VIBRATION_TIME = 40;
const int BUZZER_TIME = 500;
const int TONE_PWM_CHANNEL = 0; 

/*
 * Gameplay constants
 */
const int BALL_RADIUS = 4;
const int INITIAL_X_POS = SCREEN_HEIGHT / 2;
const int INITIAL_Y_POS = SCREEN_WIDTH - BALL_RADIUS;
const int MAX_X_VELOCITY = 6;
const int MIN_Y_VELOCITY = -12;
const int GRAVITY = 1;
const int PLATFORM_RATE = 48;
const int INITIAL_PLATFORM_SIZE = SCREEN_HEIGHT / 2;
const int MIN_PLATFORM_SIZE = 10;
const int PLATFORM_REDUCTION_SCORE = 150;
const int PLATFORM_REDUCTION_RATE = 2;

/*
 * This enum represents the current state the game is in.
 */
enum GAME_STATE{
  HOME,
  RUNNING,
  OVER,
  NUM_STATES
};

/*
 * Background variables
 */
enum GAME_STATE currentState = HOME;
int leftButtonLastVal = LOW;
int rightButtonLastVal = LOW;
boolean normalMode = true;
int highScore = 0;
int platformRefreshCount = 0;
boolean initialPlatformsSet = true;

/*
 * Gameplay variables
 */
int platforms[SCREEN_WIDTH][2];
int platformWidth = INITIAL_PLATFORM_SIZE;
int ballXPos = INITIAL_X_POS;
int ballYPos = INITIAL_Y_POS;
float ballYSpeed = MIN_Y_VELOCITY;
int currScore = 0;

void setup() {
  Serial.begin(9600);

  // start the OLED display
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3D)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  oled.clearDisplay();

  // vertical orientation for OLED
  oled.setRotation(3);

  // start the accelerometer
  if (!lis.begin(0x18)) {   // change this to 0x19 for alternative i2c address
    Serial.println("Couldnt start");
    while (1) yield();
  }

  // set remaining pin modes
  pinMode(JOYSTICK_LR_INPUT_PIN, INPUT);
  pinMode(LEFT_BUTTON_INPUT_PIN, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON_INPUT_PIN, INPUT_PULLUP);
  pinMode(VIBRO_OUTPUT_PIN, OUTPUT);

  // use ledc for the ESP32 for tones
  ledcAttachPin(PIEZO_OUTPUT_PIN, TONE_PWM_CHANNEL);

  // initialize the platforms
  srand(time(NULL));
  resetPlatforms();
}

void loop() {
  oled.clearDisplay();
  
  if (currentState == HOME) {
    renderHome();
  } else if (currentState == RUNNING) {
    renderGame();
  } else if (currentState == OVER) {
    renderGameOver();
  }
  
  oled.display();
}

/*
 * Renders the home screen with normal/creative mode buttons
 */
void renderHome() {
  int16_t x, y;
  uint16_t textWidth, textHeight;
  const char introText[] = "Ball Jump";
  
  oled.setTextSize(1);
  oled.setTextColor(WHITE, BLACK);
  
  oled.getTextBounds(introText, 0, 0, &x, &y, &textWidth, &textHeight);
  int16_t textX = oled.width() / 2 - textWidth / 2;
  int16_t textY = oled.height() / 4 - textHeight / 2;
  oled.setCursor(textX, textY);
  oled.print(introText);

  // normal mode button (joystick)
  const char normalText[] = "Normal";

  oled.getTextBounds(normalText, 0, 0, &x, &y, &textWidth, &textHeight);
  textX = oled.width() / 2 - textWidth / 2;
  textY = oled.height() / 2 - textHeight / 2;
  oled.setCursor(textX, textY);
  oled.print(normalText);
  
  oled.drawRect(textX - BUTTON_OUTLINE, textY - BUTTON_OUTLINE, textWidth + 2 * BUTTON_OUTLINE, textHeight + 2 * BUTTON_OUTLINE, SSD1306_WHITE);

  // creative mode button (accelerometer)
  const char creativeText[] = "Creative";

  oled.getTextBounds(creativeText, 0, 0, &x, &y, &textWidth, &textHeight);
  textX = oled.width() / 2 - textWidth / 2;
  textY = oled.height() * 3 / 4 - textHeight / 2;
  oled.setCursor(textX, textY);
  oled.print(creativeText);
  
  oled.drawRect(textX - BUTTON_OUTLINE, textY - BUTTON_OUTLINE, textWidth + 2 * BUTTON_OUTLINE, textHeight + 2 * BUTTON_OUTLINE, SSD1306_WHITE);

  int leftButtonCurr = digitalRead(LEFT_BUTTON_INPUT_PIN);
  int rightButtonCurr = digitalRead(RIGHT_BUTTON_INPUT_PIN);

  if (isButtonPressed(LEFT_BUTTON_INPUT_PIN, leftButtonCurr, leftButtonLastVal)) {
    leftButtonLastVal = leftButtonCurr;
    if (leftButtonCurr == LOW) {
      // start normal mode
      currentState = RUNNING;
      normalMode = true;
      platformRefreshCount = 0;
      currScore = 0;
      ballXPos = INITIAL_X_POS;
      ballYPos = INITIAL_Y_POS;
      ballYSpeed = MIN_Y_VELOCITY;
    }
  } else if (isButtonPressed(RIGHT_BUTTON_INPUT_PIN, rightButtonCurr, rightButtonLastVal)) {
    rightButtonLastVal = rightButtonCurr;
    if (rightButtonCurr == LOW) {
      // start creative mode
      currentState = RUNNING;
      normalMode = false;
      platformRefreshCount = 0;
      currScore = 0;
      ballXPos = INITIAL_X_POS;
      ballYPos = INITIAL_Y_POS;
      ballYSpeed = MIN_Y_VELOCITY;
    }
  }
}

/*
 * Renders the game itself, updating game state
 */
void renderGame() {
  // draw the platforms
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    if (platforms[i][0] != -1) {
      oled.drawLine(platforms[i][0], i, platforms[i][0] + platforms[i][1], i, SSD1306_WHITE);
    }
  }
  
  // update platforms
  boolean newPlatform = !initialPlatformsSet && platformRefreshCount % PLATFORM_RATE == 0;
  initialPlatformsSet = false;
    
  for (int j = SCREEN_WIDTH - 1; j > 0; j--) {
    platforms[j][0] = platforms[j - 1][0];
    platforms[j][1] = platforms[j - 1][1];
  }

  if (newPlatform) {
    platforms[0][0] = rand() % (oled.width() - platformWidth);
    platforms[0][1] = platformWidth;
  } else {
    platforms[0][0] = -1;
    platforms[0][1] = -1;
  }
    
  platformRefreshCount++;

  if (normalMode) {
    // use joystick for x value
    int joystickVal = analogRead(JOYSTICK_LR_INPUT_PIN);
    
    if (joystickVal < 2000 - JOYSTICK_WINDOW || joystickVal > 2000 + JOYSTICK_WINDOW) {
      int ballXSpeed = -1 * map(joystickVal, 0, 4095, -1 * MAX_X_VELOCITY, MAX_X_VELOCITY);
      ballXPos += ballXSpeed;
    }
  } else {
    // use accelerometer for x value
    lis.read();
    sensors_event_t event;
    lis.getEvent(&event);
    
    int ballXSpeed = -1 * map(event.acceleration.x, -10, 10, -1 * MAX_X_VELOCITY, MAX_X_VELOCITY);
    ballXPos += ballXSpeed;
  }

  // don't let the ball move past the left/right of oled
  if (ballXPos < BALL_RADIUS) {
    ballXPos = BALL_RADIUS;
  } else if (ballXPos > oled.width() - BALL_RADIUS) {
    ballXPos = oled.width() - BALL_RADIUS;
  }

  // update y position
  ballYPos += ballYSpeed / 2;

  // don't let the ball move past the top of the oled
  if (ballYPos < BALL_RADIUS) {
    ballYPos = BALL_RADIUS;
    ballYSpeed = 0;
  }

  // check if the player has lost
  if (ballYPos > oled.height() - BALL_RADIUS) {
    currentState = OVER;
    playGameOverSound();
    platformWidth = INITIAL_PLATFORM_SIZE;
    resetPlatforms();
    initialPlatformsSet = true;
    return;
  }

  // check if the player has hit a platform
  if (hitPlatform()) {
    ballYSpeed = MIN_Y_VELOCITY;
    digitalWrite(VIBRO_OUTPUT_PIN, HIGH);
    delay(VIBRATION_TIME);
    digitalWrite(VIBRO_OUTPUT_PIN, LOW);
  } else {
    ballYSpeed += GRAVITY;
  }

  // update current score
  int currPosScore = platformRefreshCount + oled.height() - ballYPos;
  if (currPosScore > currScore) {
    currScore = currPosScore;
  }

  // update platform width
  if (currScore % PLATFORM_REDUCTION_SCORE == 0 && platformWidth > MIN_PLATFORM_SIZE) {
    platformWidth -= PLATFORM_REDUCTION_RATE;
  }

  // draw the player
  oled.drawCircle(ballXPos, ballYPos, BALL_RADIUS, SSD1306_WHITE);

  // write out the player's current score
  int16_t x, y;
  uint16_t textWidth, textHeight;
  const char currScoreText[] = "Ball Jump";
  
  oled.setTextSize(1);
  oled.setTextColor(WHITE, BLACK);
  
  const char scoreText[] = "Score:";
  oled.getTextBounds(scoreText, 0, 0, &x, &y, &textWidth, &textHeight);
  oled.setCursor(0, textHeight);
  oled.print(scoreText);

  char buff[33];
  itoa(currScore, buff, 10);
  oled.setCursor(textWidth, textHeight);
  oled.print(buff);
}

/*
 * Renders the game over screen with current/high scores and buttons to restart or return to home
 */
void renderGameOver() {
  // set new high score
  if (currScore > highScore) {
    highScore = currScore;
  }

  int16_t x, y;
  uint16_t textWidth, textHeight;
  const char overText[] = "Game Over";
  
  oled.setTextSize(1);
  oled.setTextColor(WHITE, BLACK);
    
  oled.getTextBounds(overText, 0, 0, &x, &y, &textWidth, &textHeight);
  int16_t textX = oled.width() / 2 - textWidth / 2;
  int16_t textY = textHeight / 2;
  oled.setCursor(textX, textY);
  oled.print(overText);

  // write out the player's score
  const char yourText[] = "Your";
  oled.setCursor(0, oled.height() / 4 - textHeight);
  oled.print(yourText);

  const char scoreText[] = "Score:";
  oled.getTextBounds(scoreText, 0, 0, &x, &y, &textWidth, &textHeight);
  oled.setCursor(0, oled.height() / 4);
  oled.print(scoreText);

  char buff[33];
  itoa(currScore, buff, 10);
  oled.setCursor(textWidth, oled.height() / 4);
  oled.print(buff);

  // write out the high score
  const char highText[] = "High";
  oled.setCursor(0, oled.height() / 2 - textHeight);
  oled.print(highText);

  oled.setCursor(0, oled.height() / 2);
  oled.print(scoreText);
  
  itoa(highScore, buff, 10);
  oled.setCursor(textWidth, oled.height() / 2);
  oled.print(buff);

  // restart button (same mode)
  const char restartText[] = "Restart";

  oled.getTextBounds(restartText, 0, 0, &x, &y, &textWidth, &textHeight);
  textX = oled.width() / 2 - textWidth / 2;
  textY = oled.height() * 3 / 4 - textHeight;
  oled.setCursor(textX, textY);
  oled.print(restartText);
  
  oled.drawRect(textX - BUTTON_OUTLINE, textY - BUTTON_OUTLINE, textWidth + 2 * BUTTON_OUTLINE, textHeight + 2 * BUTTON_OUTLINE, SSD1306_WHITE);

  // home button
  const char homeText[] = "Home";

  oled.getTextBounds(homeText, 0, 0, &x, &y, &textWidth, &textHeight);
  textX = oled.width() / 2 - textWidth / 2;
  textY += textHeight * 3;
  oled.setCursor(textX, textY);
  oled.print(homeText);
  
  oled.drawRect(textX - BUTTON_OUTLINE, textY - BUTTON_OUTLINE, textWidth + 2 * BUTTON_OUTLINE, textHeight + 2 * BUTTON_OUTLINE, SSD1306_WHITE);

  int leftButtonCurr = digitalRead(LEFT_BUTTON_INPUT_PIN);
  int rightButtonCurr = digitalRead(RIGHT_BUTTON_INPUT_PIN);

  if (isButtonPressed(LEFT_BUTTON_INPUT_PIN, leftButtonCurr, leftButtonLastVal)) {
    // restart current mode
    leftButtonLastVal = leftButtonCurr;
    if (leftButtonCurr == LOW) {
      currentState = RUNNING;
      platformRefreshCount = 0;
      currScore = 0;
      ballXPos = INITIAL_X_POS;
      ballYPos = INITIAL_Y_POS;
      ballYSpeed = MIN_Y_VELOCITY;
    }
  } else if (isButtonPressed(RIGHT_BUTTON_INPUT_PIN, rightButtonCurr, rightButtonLastVal)) {
    // go home
    rightButtonLastVal = rightButtonCurr;
    if (rightButtonCurr == LOW) {
      currentState = HOME;
    }
  }
}

/*
 * Checks if the button at buttonPin is pressed, using debouncing
 */
boolean isButtonPressed(int buttonPin, int buttonCurr, int lastButtonVal) {
  delay(DEBOUNCE_WINDOW);
  int buttonCurr2 = digitalRead(buttonPin);
  return buttonCurr == buttonCurr2 && lastButtonVal != buttonCurr;
}

/*
 * Checks if the player has hit a platform
 */
boolean hitPlatform() {
  int ballBottom = ballYPos + BALL_RADIUS;
  if (ballYSpeed > 0 && ballYPos <= oled.height() - BALL_RADIUS) {
    int maxVal = min(int(ballBottom + ballYSpeed), SCREEN_WIDTH);
    for (int i = ballBottom; i < maxVal; i++) {
      if (platforms[i][0] != -1 && ballXPos >= platforms[i][0] && ballXPos <= platforms[i][0] + platforms[i][1]) {
        return true;
      }
    }
    return false;
  } else {
    return false;
  }
}

/*
 * Plays the noise when the player loses
 */
void playGameOverSound() {
  ledcWriteNote(TONE_PWM_CHANNEL, NOTE_G, 4);
  delay(BUZZER_TIME);
  ledcWriteNote(TONE_PWM_CHANNEL, NOTE_E, 4);
  delay(BUZZER_TIME);
  ledcWriteNote(TONE_PWM_CHANNEL, NOTE_C, 4);
  delay(BUZZER_TIME);
  ledcWrite(TONE_PWM_CHANNEL, 0);
}

/*
 * Resets the platforms whenever a player gets into a new game
 */
void resetPlatforms() {
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    if (i % PLATFORM_RATE == 0) {
      platforms[i][0] = rand() % (oled.width() - platformWidth);
      platforms[i][1] = platformWidth;
    } else {
      platforms[i][0] = -1;
      platforms[i][1] = -1;
    }
  }
}
