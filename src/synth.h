#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

void synth_init(void);
int32_t synth_sample(void);
int synth_note_on(int midinote, int instrument, float notevel);
int synth_note_off(int midinote, int instrument);
void synth_dump(void);
void synth_setparams(float f);
void synth_setvolume(float f);
void synth_kill(void);

#endif
