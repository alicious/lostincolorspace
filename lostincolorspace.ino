#include <string.h>
#include <inttypes.h>

#define PULSE_LENGTH 2000
#define INTAKE_FACE 0
#define CHANNELS 3
#define COLOR_MSG_LENGTH ( CHANNELS * sizeof(byte) )

// BLINK STATES
const byte PRIMARY    = 0;
const byte CHIP       = 1;
const byte GOAL       = 2;
const byte WINNER     = 3;
const byte LOSER      = 4;
const byte SETUP      = 5;
const byte BOARD_INIT = 6;
byte currentState = SETUP;

// PRIMARIES 
const byte R = 0;
const byte G = 1;
const byte B = 2;
byte currentPrimary = R;

const byte primaryDoseMap[] = { 47,79,143,255,143,79 };

// BUTTON CLICKS
typedef byte buttonClick;
const buttonClick SINGLE = 0;
const buttonClick MULTI  = 1;
const buttonClick LONG   = 2;
const buttonClick NONE   = 3;
buttonClick click = NONE;

// MESSAGE TYPES
typedef byte messageType;
const messageType SET_PRIMARY = 0;
const messageType SET_BLANK   = 1;
const messageType REQUEST     = 2;
const messageType SEND_COLOR  = 3;
const messageType WIN         = 4;
const messageType LOSE        = 5;
const messageType NO_MESSAGE  = 6;

const byte* msg[6] = { NULL,NULL,NULL,NULL,NULL,NULL };
byte msgLength[] = { 0,0,0,0,0,0 };

bool requestSent = true;

// BOARD INIT MESSAGES
typedef byte boardInitMsg;
const boardInitMsg SET_GREEN          = 0;
const boardInitMsg SET_BLUE           = 1;

byte neighbors[2] = { 7, 7 }; // aka undefined
byte neighborCount = 0;

// TEST RESULTS
typedef byte testResult;
const testResult CLOSE_ENOUGH = 0;
const testResult NEED_RED     = 1;
const testResult NEED_GREEN   = 2;
const testResult NEED_BLUE    = 3;

testResult loseCondition;
unsigned long loseAnimationStart;

byte rgb[CHANNELS];
void rgbInit() {
  for (byte i = 0; i < CHANNELS; i++) {
    rgb[i] = 0;
  }
}

byte undoBuffer[CHANNELS] = { 0, 0, 0 };


// MESSAGE PASSING FUNCTIONS

// convenience function for sendDatagramOnFace() that includes a message type
int sendMessage( messageType t, const void *data, byte len , byte face ) { 
  if ( ( len + 1 ) > IR_DATAGRAM_LEN ) return 1;

  byte fullMessage[IR_DATAGRAM_LEN];
  fullMessage[0] = t;
  memcpy( fullMessage + 1, data, len );

  sendDatagramOnFace( fullMessage, len + 1, face );
  return 0;
}

// unpacks message from sendMessage, without including type
const byte *getMessage( uint8_t f ) {
  return (msg[f] + 1);
}

// unpacks message type from sendMessage
messageType getMessageType( byte f ) { 
  if ( msgLength[f] == 0 ) return NO_MESSAGE;
  return msg[f][0];
}


void primaryInit( byte p ) {
  currentState = PRIMARY;
  currentPrimary = p;
  rgbInit();
}

void randChipInit() {
  setChip( random(255),random(255),random(255) );
}

void setChip ( byte r, byte g, byte b ) {

  memcpy ( undoBuffer, rgb, 3 );

  currentState = CHIP;

  rgb[R] = r; 
  rgb[G] = g; 
  rgb[B] = b;
}

void mixIn ( int32_t r, int32_t g, int32_t b ) {

  int32_t r2 = r + (int32_t)rgb[R];
  int32_t g2 = g + (int32_t)rgb[G];
  int32_t b2 = b + (int32_t)rgb[B];

  auto big = biggest(r2, g2, b2);

  r2 = map( r2, 0, big, 0, 255 );
  g2 = map( g2, 0, big, 0, 255 );
  b2 = map( b2, 0, big, 0, 255 );
  
  setChip( r2, g2, b2 );
}

// MAIN LOOPS
void setup() {
  randomize();
  //randChipInit();
  rgbInit();
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
          primaryInit( G );
        } else if ( *m == SET_BLUE ) {
          primaryInit( B );
        }
      } else if ( t == SET_BLANK ) {
        setChip( 0, 0, 0 );
      }
    }
  }
  
  if ( buttonSingleClicked() ) click = SINGLE;
  else if ( buttonDoubleClicked() ) setChip( 0, 0, 0 );
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
  }

  // DISPLAY
  FOREACH_FACE(f) {
    if ( msgLength[f] != 0 ) {
      markDatagramReadOnFace( f );
      msgLength[f] = 0;
    }
  }
}

void setupLoop() {
  if ( millis() > 1000 ) currentState = CHIP;
}

void chipLoop() {

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
    } else if ( t == LOSE ) {
      currentState = LOSER;
      loseCondition = *m;
      loseAnimationStart = millis();
    }
  }
  
  // CLICK HANDLING
  switch ( click ) {
    case SINGLE: 
      setChip( undoBuffer[R], undoBuffer[G], undoBuffer[B] );
      break;
    case LONG:
      currentState = GOAL;
      break;
  }

  // DISPLAY
  FOREACH_FACE(f) {
    if ( f != INTAKE_FACE ) {
      Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
      setColorOnFace( displayColor, f );
    } else {
      pulse( WHITE, f );
    }
  }
  
}

void primaryLoop() {

  // MESSAGE HANDLING & DISPLAY
  FOREACH_FACE(f) {
    rgb[currentPrimary] = primaryDoseMap[f];

    messageType t = getMessageType(f);

    if ( t == REQUEST ) {
      sendMessage( SEND_COLOR, rgb, COLOR_MSG_LENGTH, f );
    }
    Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
    setColorOnFace( displayColor, f );
  }

  // CLICK HANDLING
  if ( click == SINGLE ) {
    cyclePrimary();
  }

  // DISPLAY
  FOREACH_FACE(f) {
  }
}

void goalLoop() {

  // MESSAGE HANDLING
  FOREACH_FACE(f) {
    messageType t = getMessageType(f);
    if ( t == REQUEST ) {
      const byte* m = getMessage(f);
      byte result = checkColor( m[R], m[G], m[B] );
      if ( result == CLOSE_ENOUGH ) {
        sendMessage( WIN, NULL, 0, f );
      } else {
        sendMessage( LOSE, &result, 1, f );
      }
    }
  }

  // CLICK HANDLING
  if ( click == LONG ) {
    currentState = CHIP;
  }

  // DISPLAY
  FOREACH_FACE(f) {
    Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
    setColorOnFace( displayColor, f );
  }

}

void winnerLoop() {
  if ( isValueReceivedOnFaceExpired( INTAKE_FACE ) ) {
    currentState = CHIP;
  }

  FOREACH_FACE(f) {
    Color displayColor = makeColorRGB( rgb[R], rgb[G], rgb[B] );
    if ( ( ( millis()/50 ) % 6 )  == f ) {
      setColorOnFace( displayColor, f );
    } else {
      setColorOnFace( OFF, f );
    }
  }

}

void loserLoop() {

  if ( isValueReceivedOnFaceExpired( INTAKE_FACE ) ) {
    currentState = CHIP;
  }
   
  unsigned long animationElapsed = millis() - loseAnimationStart; 
  if ( animationElapsed > 1000 ) {
    FOREACH_FACE(f) {
      pulse( RED, f );
    }
  } else {
    FOREACH_FACE(f) {
      if ( ( ( millis()/50 ) % 6 )  == f ) {
        setColorOnFace( RED, f );
      } else {
        setColorOnFace( OFF, f );
      }
    }
  }
}

void pulse( Color col, byte f ) {
  int pulseProgress = millis() % PULSE_LENGTH;
  byte pulseMapped = map( pulseProgress, 0, PULSE_LENGTH, 0, 255 );
  byte dimness = sin8_C( pulseMapped );
  setColorOnFace( dim( col, dimness ), f );
}

testResult checkColor( byte r, byte g, byte b ) {
  int32_t rdiff = ((int32_t)rgb[R]) - r;
  int32_t gdiff = ((int32_t)rgb[G]) - g;
  int32_t bdiff = ((int32_t)rgb[B]) - b;

  //float colorDistance  = sqrt( pow( rdiff, 2 ) + pow( gdiff, 2 ) + pow( bdiff, 2 ) ); 
  int32_t colorDistance = (rdiff * rdiff) + (gdiff * gdiff) + (bdiff * bdiff);
  testResult result;

  if ( ( colorDistance < 8000 ) ) { 
    result = CLOSE_ENOUGH;
  } else {
    if ( ( gdiff > rdiff) && ( gdiff > bdiff ) ) {
      result = NEED_GREEN;
    } else if ( ( bdiff > rdiff ) && ( bdiff > gdiff ) ) {
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

void find2neighbors() {
  if ( neighborCount > 2 ) {
    return;
  } else {
    byte whichNeighbor = 0;

    FOREACH_FACE(f) { 
      if ( !isValueReceivedOnFaceExpired(f) ) {
        neighbors[whichNeighbor] = f;
        whichNeighbor++; 
      }
    }
  }
}

void boardInit() {
  
  find2neighbors();
 
  if ( isAlone() ) {
    randChipInit();
    currentState = GOAL;
  } else if ( isDuo() ) {
    makeChipPrimaryPair();
  } else if ( isTriangle() ) {
    makePrimaryBank();
  } else {
    // FIX set all blank
  }

}

bool isDuo() {
  if ( ( neighborCount == 1 ) && 
       ( getLastValueReceivedOnFace(neighbors[0]) == 1) ) { 
    return true;
  } else {
    return false;
  }
}

bool isTriangle() {
  if ( ( neighborCount == 2 ) &&
       ( getLastValueReceivedOnFace( neighbors[0] ) == 2) &&   
       ( getLastValueReceivedOnFace( neighbors[1] ) == 2) &&   
       ( ( ( neighbors[1] - neighbors[0] ) % 4 ) == 1 )
     ) 
  { return true; } else { return false; }
}

void makeChipPrimaryPair() {
}

void makePrimaryBank() {
  primaryInit( R );

  find2neighbors(); 

  byte setupMsg = SET_GREEN;
  sendMessage( SET_PRIMARY, &setupMsg, 1, neighbors[0] );
  setupMsg = SET_BLUE;
  sendMessage( SET_PRIMARY, &setupMsg, 1, neighbors[1] );
}

void cyclePrimary() {
  currentPrimary = ( currentPrimary + 1 ) % CHANNELS;
  primaryInit(currentPrimary);
}

int32_t biggest ( int32_t r, int32_t g, int32_t b ) {
  int32_t result = r;
  if ( g > result ) result = g;
  if ( b > result ) result = b;
  return result;
}
