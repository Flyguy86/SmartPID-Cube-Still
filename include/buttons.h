#ifndef BUTTONS_H
#define BUTTONS_H

/// Button event types
enum ButtonEvent {
    BTN_NONE = 0,
    BTN_UP_PRESS,
    BTN_DOWN_PRESS,
    BTN_SELECT_PRESS,
    BTN_BACK_PRESS,
    BTN_SELECT_LONG    // Long press (>1s) on Select
};

/// Initialize button GPIOs
void buttons_init();

/// Poll buttons and return latest event (call every loop)
ButtonEvent buttons_poll();

#endif // BUTTONS_H
