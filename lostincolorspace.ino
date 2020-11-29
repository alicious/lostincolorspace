#include <string.h>
#include <inttypes.h>

#define PULSE_LENGTH 2000
#define INTAKE_FACE 0
#define CHANNELS 3
#define COLOR_MSG_LENGTH ( CHANNELS * sizeof(byte) )

#define GAMETIMER_MS 120000
Timer gameTimer;

// BLINK STATES
const byte PRIMARY    = 0;
const byte CHIP       = 1;
const byte GOAL       = 2;
const byte SHH_GOAL   = 3;
const byte WINNER     = 4;
const byte LOSER      = 5;
const byte SETUP      = 6;
const byte BOARD_INIT = 7;
const byte SCOREBOARD = 8;
const byte COUNTDOWN  = 9;
byte currentState = SETUP;
bool goalHidden = false;

#define MAX_LEVEL 10
byte chipLevel = 0; 

// PRIMARIES AND COLOR STATE
const byte R = 0;
const byte G = 1;
const byte B = 2;
byte currentPrimary = R;
byte rotationOffset = 0;

const byte primaryDoseMap[] = { 47,79,143,255,143,79 };

byte rgb[CHANNELS];

byte undoBuffer[CHANNELS] = { 0, 0, 0 };
  
// ANIMATION
bool animatingMixIn = false;
long animationStart = 0;

#define COUNTDOWN_INTERVAL 250
#define SCOREBOARD_INTERVAL 400

// BUTTON CLICKS
typedef byte buttonClick;
const buttonClick SINGLE = 0;
const buttonClick DOUBLE = 1;
const buttonClick LONG   = 2;
const buttonClick NONE   = 3;
buttonClick click = NONE;

// MESSAGE TYPES
typedef byte messageType;
const messageType SET_PRIMARY = 0;
const messageType RESET_CHIP  = 1;
const messageType REQUEST     = 2;
const messageType SEND_COLOR  = 3;
const messageType WIN         = 4;
const messageType LOSE        = 5;
const messageType NO_MESSAGE  = 6;

const byte* msg[6] = { NULL,NULL,NULL,NULL,NULL,NULL };
byte msgLength[] = { 0,0,0,0,0,0 };

byte winnerDock = 7; // aka undefined
bool requestSent = true;

// TEST RESULTS
typedef byte testResult;
const testResult CLOSE_ENOUGH = 0;
const testResult NEED_RED     = 1;
const testResult NEED_GREEN   = 2;
const testResult NEED_BLUE    = 3;

Color colorNeeded = WHITE;

// BOARD INIT MESSAGES
typedef byte boardInitMsg;
const boardInitMsg SET_GREEN          = 0;
const boardInitMsg SET_BLUE           = 1;

byte neighbors[2] = { 7, 7 }; // aka undefined (the "seventh" face)
byte neighborCount = 0;
 
// MESSAGE PASSING FUNCTIONS

// convenience function for sendDatagramOnFace() that includes a message type
int __attribute__((noinline)) sendMessage( messageType t, const void *data, byte len , byte face ) { 
  if ( ( len + 1 ) > IR_DATAGRAM_LEN ) return 1;

  byte fullMessage[IR_DATAGRAM_LEN];
  fullMessage[0] = t;
  memcpy( fullMessage + 1, data, len );

  sendDatagramOnFace( fullMessage, len + 1, face );
  return 0;
}

// unpacks message from sendMessage, without including type
const byte __attribute__((noinline)) *getMessage( uint8_t f ) {
  return (msg[f] + 1);
}

// unpacks message type from sendMessage
messageType __attribute__((noinline)) getMessageType( byte f ) { 
  if ( msgLength[f] == 0 ) return NO_MESSAGE;
  return msg[f][0];
}

// MAIN LOOPS
void setup() {
  randomize();
  rgbInit();
  gameTimer.never();
}

void loop() {

  neighborCount = countNeighbors();
  setValueSentOnAllFaces( neighborCount );

  FOREACH_FACE(f) {
    if ( isDatagramReadyOnFace(f) ) {
      msgLength[f] = getDatagramLengthOnFace(f);
      msg[f] = getDatagramOnFace(f);
  
      messageType t = getMessageType(f);
      const byte* m = getMessage(f);

      if ( t == SET_PRIMARY ) {
        if ( *m == SET_GREEN ) {
          rotationOffset = 6 - ( f + 1 );
          primaryInit( G );
        } else if ( *m == SET_BLUE ) {
          rotationOffset = 6 - ( f + 2 );
          primaryInit( B );
        }
      } else if ( t == RESET_CHIP ) {
        resetChip();
      }
    }
  }
  
  if ( buttonSingleClicked() ) click = SINGLE;
  else if ( buttonDoubleClicked() ) click = DOUBLE;
  else if ( buttonMultiClicked() ) boardInit();
  else if ( buttonLongPressed() ) click = LONG;
  else click = NONE;

  switch ( currentState ) {
    case CHIP:
      chipLoop();
      break;
    case PRIMARY:
      primaryLoop();
      break;
    case GOAL:
      goalLoop();
      break;
    case WINNER:
      winnerLoop();
      break;
    case LOSER:
      loserLoop();
      break;
    case SETUP:
      setupLoop();
      break;
    case SCOREBOARD:
      scoreboardLoop();
      break;
    case COUNTDOWN:
      countdownLoop();
      break;
  }

  FOREACH_FACE(f) {
    if ( msgLength[f] != 0 ) {
      markDatagramReadOnFace( f );
      msgLength[f] = 0;
    }
  }
}

void chipLoop() {

  if ( gameTimer.isExpired() || chipLevel == MAX_LEVEL) {
    currentState = SCOREBOARD;
    setColor( OFF );
    animationStart = millis();
    return;
  }

  // try to get color on intake face
  if ( isValueReceivedOnFaceExpired(INTAKE_FACE) ) {
    requestSent = false;
  } else if ( !requestSent ) {
    sendMessage( REQUEST, rgb, 3, INTAKE_FACE );
    requestSent = true;
  }

  // MESSAGE HANDLING
  FOREACH_FACE(f) {

    messageType t = getMessageType(f);
    const byte* m = getMessage(f);

    if ( t == REQUEST ) {
      sendMessage( SEND_COLOR, rgb, COLOR_MSG_LENGTH, f );
    } else if ( ( t == SEND_COLOR ) && ( f == INTAKE_FACE ) ) {
      mixIn( m[R], m[G], m[B] );
    } else if ( t == WIN ) {
      currentState = WINNER;
      chipLevel++;
    } else if ( t == LOSE ) {
      currentState = LOSER;
      switch ( *m ) {
        case NEED_RED:
          colorNeeded = RED;
          break;
        case NEED_GREEN:
          colorNeeded = GREEN;
          break;
        case NEED_BLUE:
          colorNeeded = BLUE;
          break;
      }
    }
  }
  
  // CLICK HANDLING
  switch ( click ) {
    case SINGLE: 
      undo();
      break;
    case DOUBLE:
      setBlank();
      break;
    case LONG:
      if ( ( rgb[R] + rgb[G] + rgb[B] ) == 0 ) {
        startGameTimer();
      } else {
        currentState = GOAL;
      }
      break;
  }

  // DISPLAY

  if ( animatingMixIn ) {

    long animationElapsed = millis() - animationStart;
    if ( animationElapsed < 300 ) {
      //byte phase = animationElapsed/50 + 1;
      byte phase = animationElapsed/100 + 1;
      FOREACH_FACE(f) {
        if ( ( f == phase ) || ( ( 6 - f ) == phase ) ) {
          setColorOnFace( OFF, f );
        }
        /*
        } else {
          setColorOnFace( makeColorRGB( rgb[R], rgb[G], rgb[B] ), f );
        }
        */
      }
    } else {
      animatingMixIn = false;
    }

  } else {
    FOREACH_FACE(f) {
      if ( f != INTAKE_FACE ) {
        Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
        setColorOnFace( displayColor, f );
      } else {
        pulse( WHITE, f );
      }
    }
  }
  
}

void primaryLoop() {

  // MESSAGE HANDLING & DISPLAY
  FOREACH_FACE(f) {
    rgb[currentPrimary] = primaryDoseMap[(f+rotationOffset) % 6];

    messageType t = getMessageType(f);

    if ( t == REQUEST ) {
      sendMessage( SEND_COLOR, rgb, COLOR_MSG_LENGTH, f );
    }
    Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
    setColorOnFace( displayColor, f );
  }

  // CLICK HANDLING
  switch ( click ) {
    case SINGLE: 
      cyclePrimary();
      break;
    case DOUBLE:
      cyclePrimary();
      cyclePrimary();
      break;
  }

  // DISPLAY
  FOREACH_FACE(f) {
  }
}

void goalLoop() {

  // MESSAGE HANDLING
  if ( ( !goalHidden ) 
    && ( winnerDock < 7 ) 
    && ( isValueReceivedOnFaceExpired( winnerDock ) ) ) {
    winnerDock = 7;
    randGoalInit();
  } else {
    FOREACH_FACE(f) {
      messageType t = getMessageType(f);
      if ( t == REQUEST ) {
        const byte* m = getMessage(f);
        byte result = checkColor( m[R], m[G], m[B] );
        if ( result == CLOSE_ENOUGH ) {
          sendMessage( WIN, NULL, 0, f );
          if ( !goalHidden ) winnerDock = f;
        } else {
          sendMessage( LOSE, &result, 1, f );
        }
      }
    }
  }

  // CLICK HANDLING
  switch ( click ) {
    case SINGLE:
      goalHidden = !goalHidden;
      break;
    case DOUBLE:
      randGoalInit();
      break;
    case LONG:
      currentState = CHIP;
      break;
  }

  // DISPLAY
  if ( goalHidden ) {
    setColor( dim( WHITE, 20 ) );
  } else { 
    FOREACH_FACE(f) {
      Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
      setColorOnFace( displayColor, f );
    }
  }

}

void winnerLoop() {
  
  FOREACH_FACE(f) {
    Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
    if ( ( ( millis()/100 ) % 6 )  == f ) {
      setColorOnFace( displayColor, f );
    } else {
      setColorOnFace( OFF, f );
    }
  }

  if ( isValueReceivedOnFaceExpired( INTAKE_FACE ) ) {
    setBlank();
  }

}

void loserLoop() {

  if ( isValueReceivedOnFaceExpired( INTAKE_FACE ) ) {
    currentState = CHIP;
  }
   
  FOREACH_FACE(f) {
    pulse( colorNeeded, f );
  }
}

void setupLoop() {
  if ( millis() > 1000 ) currentState = CHIP;
}

void scoreboardLoop() {

  if ( click == DOUBLE ) { 
    resetChip();
    return;
  } else if ( click == LONG ) {
    resetChip();
    startGameTimer();
    return; 
  }

  long animationElapsed = millis() - animationStart;
  const int intro = 2000;

  if ( animationElapsed < intro ) {
    if ( animationElapsed < ( intro - 500 ) ) {
      FOREACH_FACE(f) sparkle( YELLOW, f );
    } else {
      setColor( OFF );
    }
  }
  
  byte interval = ( animationElapsed - intro ) / SCOREBOARD_INTERVAL;
  
  if ( interval <= chipLevel ) {
    if ( interval < 5 ) {
      setColorOnFace( WHITE, interval );
    } else if ( interval == 5 ) {
      setColor( OFF );
      setColorOnFace( RED, 5 );
    } else {
      setColorOnFace( WHITE, ( interval - 6 ) );
    }  
  } else if ( chipLevel == MAX_LEVEL ) {
    FOREACH_FACE(f) {
      pulse( ORANGE, f );
    }
  } 
}

void countdownLoop() {
  long animationElapsed = millis() - animationStart;
  
  if ( animationElapsed <= COUNTDOWN_INTERVAL ) {
    setColor( WHITE );
  } else if ( animationElapsed < ( COUNTDOWN_INTERVAL * 7 ) ) {
    FOREACH_FACE(f) {
      if ( animationElapsed > ( (f+1) * COUNTDOWN_INTERVAL ) ) {
        setColorOnFace( OFF, f );
      }
    }
  } else {
    setBlank();
  } 

}

void pulse( Color col, byte f ) {
  int pulseProgress = millis() % PULSE_LENGTH;
  byte pulseMapped = map( pulseProgress, 0, PULSE_LENGTH, 0, 255 );
  byte dimness = sin8_C( pulseMapped );
  setColorOnFace( dim( col, dimness ), f );
}

void sparkle( Color col, byte f ) {
  if ( ( millis() % 5 ) == f ) {
    setColorOnFace( col, f );
  } else {
    setColorOnFace( OFF, f );
  }
}

testResult checkColor( byte r, byte g, byte b ) {
  int32_t rdiff = ((int32_t)rgb[R]) - r;
  int32_t gdiff = ((int32_t)rgb[G]) - g;
  int32_t bdiff = ((int32_t)rgb[B]) - b;

  int32_t colorDistance = (rdiff * rdiff) + (gdiff * gdiff) + (bdiff * bdiff);
  testResult result;

  if ( ( colorDistance < 7000 ) ) { // 8000 too easy, 6000 too hard
    result = CLOSE_ENOUGH;
  } else {
    if ( ( ( gdiff != 0 ) && ( gdiff >= rdiff) && ( gdiff >= bdiff ) )
       || ( ( rgb[R] == 0 ) && ( rgb[B] == 0 ) ) ) {
      result = NEED_GREEN;
    } else if ( ( ( bdiff != 0 ) && ( bdiff >= rdiff ) && ( bdiff >= gdiff ) ) 
      || ( ( rgb[R] == 0 ) && ( rgb[G] == 0 ) ) ) {
      result = NEED_BLUE;
    } else {
      result = NEED_RED;
    }
  }
  
  return result;

}

byte countNeighbors() {
  byte count = 0;
  FOREACH_FACE(f) {
    if ( !isValueReceivedOnFaceExpired(f) ) count++;
  }
  return count;
}

void boardInit() {
  byte whichNeighbor = 0;

  FOREACH_FACE(f) { 
    if ( !isValueReceivedOnFaceExpired(f) && whichNeighbor < 2 ) {
      neighbors[whichNeighbor] = f;
      whichNeighbor++; 
    }
  }
  // make 6 and 0 adjacent in the same clockwise order as other pairs
  if ( ( neighbors[0] == 0 ) && ( neighbors[1] == 5 ) ) {
    neighbors[0] = 5;
    neighbors[1] = 0;
  }
 
  if ( isAlone() ) {
    randGoalInit();
  } else if ( isDuo() ) {
    makeChipPrimaryPair();
  } else if ( isTriangle() ) {
    makePrimaryBank();
  } else {
    FOREACH_FACE(f) { 
      sendMessage( RESET_CHIP, NULL, 0, f );
    }
    resetChip(); 
  }

}

bool isDuo() {
  if ( ( neighborCount == 1 ) && 
       ( getLastValueReceivedOnFace(neighbors[0]) == 1 ) ) { 
    return true;
  } else {
    return false;
  }
}

bool isTriangle() {
  if ( ( neighborCount == 2 ) &&
       ( getLastValueReceivedOnFace( neighbors[0] ) == 2) &&   
       ( getLastValueReceivedOnFace( neighbors[1] ) == 2) &&   
       ( ( neighbors[0] == 5 ) || ( ( neighbors[1] - neighbors[0] ) == 1 ) )
     ) 
  { return true; } else { return false; }
}

void cyclePrimary() {
  currentPrimary = ( currentPrimary + 1 ) % CHANNELS;
  primaryInit(currentPrimary);
}

void countDownDisplay() {
}

int32_t biggest ( int32_t r, int32_t g, int32_t b ) {
  int32_t result = r;
  if ( g > result ) result = g;
  if ( b > result ) result = b;
  return result;
}


// STATE SETTING FUNCTIONS
void setBlank() {
  currentState = CHIP;
  memcpy ( undoBuffer, rgb, 3 );
  rgbInit();
}

void resetChip() {
  chipLevel = 0;
  gameTimer.never();
  setBlank();
}
 
void primaryInit( byte p ) {
  currentState = PRIMARY;
  currentPrimary = p;
  rgbInit();
}

void randGoalInit() {
  currentState = GOAL;

  do {
    byte r = random(255);
    byte g = random(255);
    byte b = random(255);
    setEqualized( random(255), random(255), random(255) );
  } while( ( rgb[R] + rgb[G] + rgb[B] ) < 300 );
}

void rgbInit() {
  for (byte i = 0; i < CHANNELS; i++) {
    rgb[i] = 0;
  }
}

void setEqualized( uint32_t r, uint32_t g, uint32_t b ) {
    memcpy ( undoBuffer, rgb, 3 );
    uint32_t big = biggest( r, g, b );
    rgb[R] = map( r, 0, big, 0, 255 );
    rgb[G] = map( g, 0, big, 0, 255 );
    rgb[B] = map( b, 0, big, 0, 255 );
}

void mixIn ( int32_t r, int32_t g, int32_t b ) {
  if ( ( r == 0 ) && ( g == 0 ) && ( b == 0 ) ) return;
  animationStart = millis();
  animatingMixIn = true;

  int32_t r2 = r + (int32_t)rgb[R];
  int32_t g2 = g + (int32_t)rgb[G];
  int32_t b2 = b + (int32_t)rgb[B];
  
  setEqualized( r2, g2, b2 );
}

void makeChipPrimaryPair() {
  primaryInit( R );

  sendMessage( RESET_CHIP, NULL, 0, neighbors[0] );
}

void makePrimaryBank() {
  primaryInit( R );
  rotationOffset = ( 6 - neighbors[1] ) + 5;

  sendMessage( SET_PRIMARY, &SET_GREEN, 1, neighbors[0] );
  sendMessage( SET_PRIMARY, &SET_BLUE, 1, neighbors[1] );
}
void undo() {
  rgb[R] = undoBuffer[R];
  rgb[G] = undoBuffer[G];
  rgb[B] = undoBuffer[B];
}

void startGameTimer() {
  currentState = COUNTDOWN;
  chipLevel = 0;
  animationStart = millis();
  gameTimer.set(GAMETIMER_MS);
}

