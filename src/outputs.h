#pragma once
#include "config.h"

void initOutputs();
void setOutput(int index, bool state);
bool getOutput(int index);
void setSSRPWM(int duty);
int  getSSRPWM();
void updateSSRPWM();       // Software time-proportioning — call from scheduler
void allOutputsOff();

// PID / Run profile
void          initPID();
void          updatePID();      // Call from main loop (uses sensor data)
void          startProfile();
void          stopProfile();
RunState      getRunState();
unsigned long getHoldRemaining();  // seconds remaining in hold phase
unsigned long getRunElapsed();     // seconds since profile started
unsigned long getStepElapsed();    // seconds since current step started
int           getCurrentStep();    // 0-based index of current step
int           getTotalSteps();     // total steps in profile

// PID Auto-Tune (relay-based Ziegler-Nichols)
void startAutoTune(int sensorIdx);
void stopAutoTune();
void updateAutoTune();    // Call from main loop
bool isAutoTuning();
int  getAutoTuneSensor(); // -1 if not tuning
