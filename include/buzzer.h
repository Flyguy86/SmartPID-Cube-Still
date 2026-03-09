#ifndef BUZZER_H
#define BUZZER_H

/// Initialize the buzzer pin
void buzzer_init();

/// Short beep (button feedback)
void buzzer_beep();

/// Double beep (action confirmed)
void buzzer_confirm();

/// Long beep (alarm / error)
void buzzer_alarm();

/// Non-blocking update (handles tone timing)
void buzzer_update();

#endif // BUZZER_H
