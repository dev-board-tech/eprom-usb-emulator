#include <arduFPGA-app-common-arduino.h>

enum {
  STATE_MACHINE_NONE,
  STATE_MACHINE_LOAD
}stateMachine_e = STATE_MACHINE_NONE;


sTimer sendCharTimer;

char rcvBuf[65] = {0};
int rcvBufPtr = 0;

int writePtr = 0;

void executeCommand(char *cmd);

void setup() {
  Serial1.begin(115200);
  sendCharTimer.SetInterval(1000);
  sendCharTimer.Start();
}

void loop() {
  if(Serial1.available()) {
    char c = Serial1.read();
    if(c == '\n' || c == '\r') {
      executeCommand(rcvBuf);
      rcvBufPtr = 0;
    } else {
      if(rcvBufPtr < sizeof(rcvBuf) - 1) {
        rcvBuf[rcvBufPtr] = c;
        rcvBufPtr++;
        rcvBuf[rcvBufPtr] = '\0';
      }
    }
  }
}

void executeCommand(char *cmd) {
  if(!strcmp_P(cmd, PSTR("BOOT-LOADER"))) {
    switch(stateMachine_e) {
      case STATE_MACHINE_NONE:
        writePtr = 0;
        stateMachine_e = STATE_MACHINE_LOAD;
        break;
    }
  } else if(!strcmp_P(cmd, PSTR("LOAD"))) {
    switch(stateMachine_e) {
      case STATE_MACHINE_NONE:
        writePtr = 0;
        stateMachine_e = STATE_MACHINE_LOAD;
        break;
    }
  }
}
