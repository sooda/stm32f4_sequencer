#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

void synth_init(void);
int32_t synth_sample(void);
int synth_note_on(int midinote, int instrument);
int synth_note_off(int midinote, int instrument);

#endif
