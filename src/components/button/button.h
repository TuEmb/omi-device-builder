#ifndef BUTTON_H
#define BUTTON_H

/** Configure the user button GPIO. */
int button_init(void);

/** Start button handling (work/callbacks). */
void activate_button_work(void);

/** Register a BLE button service (no-op stub in this build). */
void register_button_service(void);

/** Power the device off (button long-press / critical battery). */
void turnoff_all(void);

#endif // BUTTON_H
