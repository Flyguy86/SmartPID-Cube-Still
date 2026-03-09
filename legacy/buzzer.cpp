#include "buzzer.h"
#include "config.h"
#include <Arduino.h>

// Non-blocking tone sequencer
struct ToneStep {
    int freq;
    unsigned long durationMs;
};

static ToneStep sequence[6];
static int seqLen = 0;
static int seqIdx = 0;
static unsigned long stepStart = 0;
static bool playing = false;

void buzzer_init() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("[BZR] Buzzer initialized");
}

static void startSequence(ToneStep* steps, int count) {
    for (int i = 0; i < count && i < 6; i++) sequence[i] = steps[i];
    seqLen = count;
    seqIdx = 0;
    stepStart = millis();
    playing = true;
    if (sequence[0].freq > 0) {
        tone(BUZZER_PIN, sequence[0].freq);
    }
}

void buzzer_beep() {
    ToneStep steps[] = {{ 2000, 50 }, { 0, 0 }};
    startSequence(steps, 2);
}

void buzzer_confirm() {
    ToneStep steps[] = {{ 1500, 80 }, { 0, 40 }, { 2500, 80 }, { 0, 0 }};
    startSequence(steps, 4);
}

void buzzer_alarm() {
    ToneStep steps[] = {{ 3000, 300 }, { 0, 100 }, { 3000, 300 }, { 0, 0 }};
    startSequence(steps, 4);
}

void buzzer_update() {
    if (!playing) return;

    unsigned long now = millis();
    if (now - stepStart >= sequence[seqIdx].durationMs) {
        seqIdx++;
        if (seqIdx >= seqLen) {
            noTone(BUZZER_PIN);
            playing = false;
            return;
        }
        stepStart = now;
        if (sequence[seqIdx].freq > 0) {
            tone(BUZZER_PIN, sequence[seqIdx].freq);
        } else {
            noTone(BUZZER_PIN);
        }
    }
}
