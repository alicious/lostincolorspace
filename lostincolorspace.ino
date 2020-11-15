#include <string.h>

#define PULSE_LENGTH 3000
#define INTAKE_FACE 0
#define CHANNELS 3
#define COLOR_MSG_LENGTH ( CHANNELS * sizeof(byte) )

enum state { PRIMARY, CHIP, GOAL, WIN, LOSE };
state current_state = CHIP;

enum primary { R, G, B };
int currentPrimary = R;

byte primaryDoseMap[] = { 47,79,143,255,143,79 };
//byte primaryDoseMap[] = { 31,63,127,255,127,63 };

enum button_clicks { SINGLE, DOUBLE, MULTI, LONG, NONE };
button_clicks click = NONE;

enum messageType { REQUEST, SEND_COLOR, SET_PRIMARY, WIN, LOSE, NO_MESSAGE };
enum primaryMsg { SET_GREEN, SET_BLUE, SET_GREEN_AND_BLUE };
const byte* msg[6] = { NULL,NULL,NULL,NULL,NULL,NULL };
byte msgLength[] = { 0,0,0,0,0,0 };

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

byte faceRGBs[6][CHANNELS];
void faceRGBsInit() {
  FOREACH_FACE(f) {
    for (int i = 0; i < CHANNELS; i++) {
      faceRGBs[f][i] = 0;
    }
  }
}

byte undoBuffer[] = { 0, 0, 0 };

void primaryInit( primary p ) {
  current_state = PRIMARY;
  faceRGBsInit();
 
  FOREACH_FACE(f) {
    faceRGBs[f][p] = primaryDoseMap[f];
  }
}

void randChipInit() {
  setChip( random(255),random(255),random(255) );
}

void setChip ( byte r, byte g, byte b ) {

  memcpy ( undoBuffer, faceRGBs[1], 3 );

  current_state = CHIP;

  FOREACH_FACE(f) {
    faceRGBs[f][R] = r; 
    faceRGBs[f][G] = g; 
    faceRGBs[f][B] = b;
  }
}

void setPrimary( const byte* m, byte f ) {
      byte setupMsg;
      if ( *m == SET_GREEN_AND_BLUE ) {
        primaryInit( G );
        setupMsg = SET_BLUE;
        FOREACH_FACE(x) {
          if ( (x != f) && (!isValueReceivedOnFaceExpired(x)) ) {
            sendMessage ( SET_PRIMARY, &setupMsg, 1, x );
            break;
          }
        }
      } else if ( *m == SET_GREEN ) {
        primaryInit( G );
      } else if ( *m == SET_BLUE ) {
        primaryInit( B );
      }
}

void mixIn ( unsigned int r, unsigned int g, unsigned int b ) {

  unsigned int r2 = r + (unsigned int)faceRGBs[1][R];
  unsigned int g2 = g + (unsigned int)faceRGBs[1][G];
  unsigned int b2 = b + (unsigned int)faceRGBs[1][B];

  auto big = biggest(r2, g2, b2);

  r2 = map( r2, 0, big, 0, 255 );
  g2 = map( g2, 0, big, 0, 255 );
  b2 = map( b2, 0, big, 0, 255 );
  
  setChip( r2, g2, b2 );
}

// MAIN LOOPS
void setup() {
  randomize();
  randChipInit();
}

void loop() {
  
  if ( buttonSingleClicked() ) click = SINGLE;
  else if ( buttonDoubleClicked() ) click = DOUBLE;
  else if ( buttonMultiClicked() ) click = MULTI;
  else if ( buttonLongPressed() ) click = LONG;
  else click = NONE;

  FOREACH_FACE(f) {
    if ( isDatagramReadyOnFace(f) ) {
      msgLength[f] = getDatagramLengthOnFace(f);
      msg[f] = getDatagramOnFace(f);
    }
  }

  switch ( current_state ) {
    case CHIP:
      chipLoop();
      break;
    case PRIMARY:
      primaryLoop();
      break;
    case GOAL:
      goalLoop();
  }

  // DISPLAY
  FOREACH_FACE(f) {
    if ( ( f == 0 ) && ( current_state == CHIP ) ) {
      int pulseProgress = millis() % PULSE_LENGTH;
      byte pulseMapped = map( pulseProgress, 0, PULSE_LENGTH, 0, 255 );
      byte dimness = sin8_C( pulseMapped );
      setColorOnFace( dim( WHITE, dimness ), f );
    } else {
      setColorOnFace(
        makeColorRGB(
          faceRGBs[f][R],
          faceRGBs[f][G],
          faceRGBs[f][B]
        ), f
      );
    } 
    if ( msgLength[f] != 0 ) {
      markDatagramReadOnFace( f );
      msgLength[f] = 0;
    }
  }

}

void chipLoop() {

  // MESSAGE HANDLING
  FOREACH_FACE(f) {

    messageType t = getMessageType(f);
    const byte* m = getMessage(f);

    if ( t == SET_PRIMARY ) {
      setPrimary( m, f );
    } else if ( t == REQUEST ) {
      sendMessage( SEND_COLOR, faceRGBs[f], COLOR_MSG_LENGTH, f );
    } else if ( ( t == SEND_COLOR ) && ( f == INTAKE_FACE ) ) {
      mixIn( m[0], m[1], m[2] );
    } else if ( t == WIN ) {
      setChip( 0, 255, 0 );
    } else if ( t == LOSE ) {
      setChip( 255, 0, 0 );
    }
  }
  
  // CLICK HANDLING
  switch ( click ) {
    case SINGLE: 
      if ( isValueReceivedOnFaceExpired(0) ) {
        setChip( undoBuffer[0], undoBuffer[1], undoBuffer[2] );
      } else {
        sendMessage( REQUEST, faceRGBs[1], 3, INTAKE_FACE );
      }
      break;
    case DOUBLE:
      if ( isAlone() ) {
        randChipInit();
      } else {
        makePrimaryBank();
      }
      break;
    case MULTI:
      if ( isAlone() ) {
        faceRGBsInit();
      }
      break;
    case LONG:
      current_state = GOAL;
  }
}

void primaryLoop() {

  // MESSAGE HANDLING
  FOREACH_FACE(f) {
    messageType t = getMessageType(f);
    if ( t == SET_PRIMARY ) {
      const byte* m = getMessage(f);
      setPrimary( m, f );
    }
    if ( t == REQUEST ) {
      sendMessage( SEND_COLOR, faceRGBs[f], COLOR_MSG_LENGTH, f );
    }
  }

  // CLICK HANDLING
  if ( click == DOUBLE ) {
    if ( isAlone() ) {
      randChipInit();
    } else {
      cyclePrimary();
    }
  }
}

void goalLoop() {

  // MESSAGE HANDLING
  FOREACH_FACE(f) {
    messageType t = getMessageType(f);
    if ( t == REQUEST ) {
      const byte* m = getMessage(f);
      byte result = checkColor( m[0], m[1], m[2] );
      if ( result < 10 ) {
        sendMessage( WIN, NULL, 0, f );
      } else {
        sendMessage( LOSE, NULL, 0, f );
      }
    }
  }

  // CLICK HANDLING
  if ( click == LONG ) {
    current_state = CHIP;
  }
}

byte checkColor( byte r, byte g, byte b ) {
  byte diff = abs( faceRGBs[1][0] - r);
  diff += abs( faceRGBs[1][1] - g); 
  diff += abs( faceRGBs[1][2] - b ); 
  return diff;
}

void makePrimaryBank() {
  primaryInit( R );
  
  byte neighborCount = 0;
  byte neighbors[2];
  FOREACH_FACE(f) { 
    if ( !isValueReceivedOnFaceExpired(f) ) {
      neighbors[neighborCount] = f;
      neighborCount++; 
      if ( neighborCount == 2 ) break;
    }
  }

  if ( neighborCount == 1 ) {
    byte setupMsg = SET_GREEN_AND_BLUE;
    sendMessage ( SET_PRIMARY, &setupMsg, 1, neighbors[0] );
  } else if ( neighborCount == 2 ) {
    byte setupMsg = SET_GREEN;
    sendMessage ( SET_PRIMARY, &setupMsg, 1, neighbors[0] );
    setupMsg = SET_BLUE;
    sendMessage ( SET_PRIMARY, &setupMsg, 1, neighbors[1] );
  }
}

void cyclePrimary() {
  currentPrimary = ( currentPrimary + 1 ) % CHANNELS;
  primaryInit(currentPrimary);
}

unsigned int biggest ( unsigned int r, unsigned int g, unsigned int b ) {
  unsigned int result = r;
  if ( g > result ) result = g;
  if ( b > result ) result = b;
  return result;
}
