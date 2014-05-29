#include "synth.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>

/***** Main configuration *****/

#define SAMPLERATE 44100
#define DT (1.0f / SAMPLERATE)
#define PI 3.14159265358979323846
typedef float sample;


/***** Core pipeline definitions: channels, instruments, ADSR *****/

typedef struct AdsrParams {
	float attack, decay, sustain, release; // magic coefs lol
} AdsrParams;

typedef struct Channel Channel;

typedef struct Instrument {
	void (*initfunc)(Channel* ch);
	sample (*oscfunc)(struct Instrument *self, void* state);
	sample (*filtfunc)(struct Instrument *self, void* state, sample input);
	AdsrParams adsrparams;
} Instrument;

#define ADSR_MODE_ATTACK 1
#define ADSR_MODE_DECAY 2
#define ADSR_MODE_RELEASE 4
#define ADSR_MODE_KILLED 8

typedef struct AdsrState {
	int mode;
	float val;
	float tgt;
} AdsrState;

// note magic bitmasks

#define DEADBIT (1<<15)
#define KEYOFFBIT (1<<14)

typedef struct Channel {
	int note; // contains alive data etc
	int filtstateaddr;// would be index to a general state array but we have mem for both
	AdsrState adsrstate;
	Instrument* instr;
	int instrunum;
	sample velocity;
	char oscstate[64];
	char filtstate[64];
} Channel;


/***** ADSR implementation *****/

#define E 2.718281828
#define TGTCOEF (E/(E-1))

sample adsreval(AdsrParams *params, AdsrState *state, int note) {
	if (note & KEYOFFBIT) {
		if (state->mode == ADSR_MODE_RELEASE) {
		} else {
			state->mode = ADSR_MODE_RELEASE;
			state->tgt = (1 - TGTCOEF) * state->val;
		}
	}
	switch (state->mode) {
		case ADSR_MODE_ATTACK:
			state->val += params->attack * (TGTCOEF - state->val);
			if (state->val >= 1.0) {
				state->mode = ADSR_MODE_DECAY;
			}
			break;
		case ADSR_MODE_DECAY:
			state->val += params->decay * (params->sustain - state->val);
			break;
		case ADSR_MODE_RELEASE:
			state->val += params->release * (state->tgt - state->val);
			if (state->val < 0) {
				state->mode = ADSR_MODE_KILLED;
			}
			break;
		case ADSR_MODE_KILLED:
			return -1.0;
			break;
	}
	return state->val;
}

void adsr_init(AdsrState* state) {
	state->mode = ADSR_MODE_ATTACK;
	state->val = 0.0;
}


/***** Filters *****/

typedef struct LowpassParams {
	float coef;
} LowpassParams;

typedef struct LowpassState {
	float val;
	float coef;
} LowpassState;


void trivial_lp_init(void* st, LowpassParams* params) {
	LowpassState *state = st;
	state->coef = params->coef;
	state->val = 0.0;
}

sample trivial_lp_eval(void* st, sample in) {
	LowpassState *state = st;
	state->val += state->coef * (in - state->val);
	return state->val;
}


/***** Oscillators *****/

typedef struct {
	float tick;
	float val;
} OscSawState;

typedef struct {
	OscSawState saw;
	float val;
	float coef;
} OscDpwState;

typedef struct {
	uint32_t current;
} OscNoiseState;

#include "sawticks.c"
#include "dpwcoefs.c"


void osc_noise_init(void* st) {
	OscNoiseState* state = st;
	state->current = 1;
}

sample osc_noise_eval(Instrument *self, void *st) {
	OscNoiseState* state = st;
	uint32_t x = state->current;
	x ^= x << 8;
	x ^= x >> 1;
	x ^= x << 11;
	x &= 0xffffff;
	state->current = x;
	return (float)x / 0xffffff - 0.5;
}

void osc_saw_init(void* st, int note) {
	OscSawState* state = st;
	state->tick = sawticks[note];
	state->val = -1.0;
}

sample osc_saw_eval(Instrument *self, void* st) {
	OscSawState *state = st;
	state->val += state->tick;
	if (state->val > 1.0)
		state->val -= 2.0;
	return state->val;
}

void osc_dpw_init(void* st, int note) {
	OscDpwState* state = st;
	osc_saw_init(st, note);
	state->val = 1.0; // prev saw is -1 * -1
	state->coef = dpwcoefs[note];
}

sample osc_dpw_eval(Instrument *self, void* st) {
	sample a = osc_saw_eval(self, st);
	OscDpwState *state = st;
	a *= a;
	sample dif = state->val - a;
	state->val = a;
	return dif * state->coef;
}


/***** Instruments *****/

typedef struct {
	Instrument base;
	LowpassParams lp;
} BassInstrument;

typedef struct {
	Instrument base;
} NoiseInstrument;


void bass_init(Channel *ch) {
	BassInstrument *ins = (BassInstrument*)ch->instr;
	trivial_lp_init(ch->filtstate, &ins->lp);
	osc_dpw_init(ch->oscstate, ch->note);
}

sample bass_filt(Instrument *self, void* st, sample in) {
	LowpassState *state = st;
	BassInstrument *bass = (BassInstrument*)self;
	state->coef = bass->lp.coef;
	return trivial_lp_eval(state, in);
}

void noise_init(Channel *ch) {
	//NoiseInstrument *ins = (NoiseInstrument*)ch->instr;
	osc_noise_init(ch->oscstate);
}


#define FiltTrivLpK (DT*2*PI)
#define TRIVIAL_LP_PARM(fc) ((FiltTrivLpK*fc)/(FiltTrivLpK*fc+1))

/* FIXME: approximate these? pow unavailable here
	dc	(1-@POW(E,-1.0/(At*RATE)))
	dc	(1-@POW(E,-1.0/(Dt*RATE)))
	dc	Sl
	dc	(1-@POW(E,-1.0/(Rt*RATE)))
*/

BassInstrument bass = {
	{
		bass_init,
		osc_dpw_eval,
		bass_filt,
		{ 0.0004534119168875158,0.00004535044555269668,0.6,0.00002267547986504189  } //ADSRBLOCK(0.05, 0.5, 0.8, 0.1),
	},
	{ TRIVIAL_LP_PARM(5000) }
};

NoiseInstrument noise = {
	{
		noise_init,
		osc_noise_eval,
		NULL,
		{ 0.0004534119168875158, 0.00004535044555269668, 0.6, 0.00002267547986504189 }
	}
};

Instrument* instruments[] = {
	(Instrument*)&bass,
	(Instrument*)&noise
};



/***** Core implementation *****/

#define NUM_CHANNELS 8
static Channel channels[NUM_CHANNELS];

static sample eval_channel(Channel* ch) {
	Instrument* instr = ch->instr;
	sample a = instr->oscfunc(instr, ch->oscstate);
	sample b = instr->filtfunc ? instr->filtfunc(instr, ch->filtstate, a) : a;
	sample c = adsreval(&instr->adsrparams, &ch->adsrstate, ch->note);
	b *= c;
	if (c < 0.0)
		ch->note |= DEADBIT;
	return ch->velocity * b;
}

/* public interface */

int32_t synth_sample(void) {
	sample out = 0;
	for (int i = 0; i < NUM_CHANNELS; i++) {
		if (!(channels[i].note & DEADBIT))
			out += eval_channel(&channels[i]);
	}
	return 0x7fff * out;
}

void synth_dump(void) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		Channel* ch = &channels[i];
		printf("ch=%d n=%d adsr=%d:%f\r\n", i, ch->note,
				ch->adsrstate.mode, (double)ch->adsrstate.val);
	}
}

void synth_init(void) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		channels[i].note |= DEADBIT;
	}
}

int synth_note_on(int midinote, int instrument) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		Channel* ch = &channels[i];
		if (ch->note & DEADBIT) {
			ch->note = midinote;
			adsr_init(&ch->adsrstate);
			ch->velocity = 1.0;
			ch->instrunum = instrument;
			ch->instr = instruments[instrument];
			ch->instr->initfunc(ch);
			return 0;
		}
	}
	return -1;
}

int synth_note_off(int midinote, int instrument) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		Channel* ch = &channels[i];
		if (ch->instrunum == instrument &&
				ch->note == midinote) {
			ch->note |= KEYOFFBIT;
			return 0;
		}
	}
	return -1;
}

void synth_kill(void) {
	for (int i = 0; i < NUM_CHANNELS; i++)
		channels[i].note |= DEADBIT;
}

void synth_setparams(float f) {
	bass.lp.coef = TRIVIAL_LP_PARM(f/0xfff*5000);
}


// random old notes

#if 0
float lp(float x, float a,float* mem) {
	*mem += a * (x - *mem);
	return *mem;
}
float tejeezfilt(float x) {
	static float q,w,e;
	float a = (float)ankka/0xfff;

	float y = x-e*2;
	if (y>1)y=1;
	else if (y<-1)y=-1;
	x = lp(y, a, &q);
	x = lp(x, a, &w);
	x = lp(x, a, &e);
	return x;
}

// samplerate 48khz
// ~21 microseconds between samples
// cpu 100MHz = 0.01 microseconds per instruction
// ~2083 instructions/sample

// TODO: time units
// TODO: status flags into bitfields
// TODO: fixedpoint
// TODO: instruments in memory X, states in memory Y to parallelize fetching
//

float midifreq(int note) {
	return (1 << ((note - 69) / 12)) * 440.0;
}

#define LOWPASS_COEF(freq) (DT / (freq / (2 * M_PI) + DT))
// TODO: function or not? get rid of division
#define lowpass_coef(freq) LOWPASS_COEF(freq)

sample lfo_iexp(int time, int freq) {
	return exp(-time * freq);
}

#endif
