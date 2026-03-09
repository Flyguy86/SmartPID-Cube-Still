#pragma once
#include "config.h"

void initOutputs();
void setOutput(int index, bool state);
bool getOutput(int index);
void setSSRPWM(int duty);
int  getSSRPWM();
void allOutputsOff();

// PID / Run profile
void          initPID();
void          updatePID();      // Call from main loop (uses sensor data)
void          startProfile();
void          stopProfile();
RunState      getRunState();
unsigned long getHoldRemaining();  // seconds remaining in hold phase
int           getCurrentStep();    // 0-based index of current step
int           getTotalSteps();     // total steps in profile

// PID Auto-Tune (relay-based Ziegler-Nichols)
void startAutoTune(int sensorIdx);
void stopAutoTune();
void updateAutoTune();    // Call from main loop
bool isAutoTuning();
int  getAutoTuneSensor(); // -1 if not tuning
