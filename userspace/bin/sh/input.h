#ifndef SKALD_INPUT_H
#define SKALD_INPUT_H

typedef enum {
  INPUT_CHAR,
  INPUT_UP,
  INPUT_DOWN,
  INPUT_LEFT,
  INPUT_RIGHT,
  INPUT_HOME,
  INPUT_END,
  INPUT_BACKSPACE,
  INPUT_ENTER,
  INPUT_TAB
} input_type_t;

typedef struct {
  input_type_t type;
  char character;
} input_event_t;

input_event_t read_key(void);
#endif
