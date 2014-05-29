#if 0
#include <math.h>

float saw(float time) {
	return time-floorf(time);
}
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
#endif

#include "synth.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>

float sawlol() {
	static float time;
	time += 50.0/44100;
	return time-floorf(time);
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
//#define FIX(x) (int)((x) * INT_MAX)
#define FIX(x) (x)
#define SAMPLERATE 44100
#define DT (1.0f / SAMPLERATE)
#define PI 3.14159265358979323846

typedef float sample;

typedef struct AdsrParams {
	float attack, decay, sustain, release; // magic coefs lol
} AdsrParams;

// TODO: instrument or channel here?
typedef struct Instrument {
	void (*initfunc)(void *ch, void* inst);
	sample (*oscfunc)(void* state);
	sample (*filtfunc)(void* state, sample input);
	AdsrParams adsrparams;
} Instrument;

#define ADSR_ATTACK 1
#define ADSR_DECAY 2
#define ADSR_RELEASE 4
#define ADSR_KILLED 8

typedef struct AdsrState {
	int mode;
	float val;
	float tgt;
} AdsrState;

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


#if 0


// shouldn't need to check alive bit - never called for dead voices (no zombie channels possible)
// FIXME: bloat
int adsr(const AdsrParams* params, AdsrState* state) {
	int out = state->value;
	// TODO: use time constant to precalc coefs, skip to next if value == zing
	// TODO: precalculate coefficients g = 1 - exp(1/(T*fs))
	// TODO: use linear ramp at attack stage?
	state->value = lowpass_juttu(state->currentcoef, state->nextvalue);
	if (++state->time >= state->nexttime) {
		switch (state->section) {
			case ADSR_ATTACK:
				state->currentcoef = params->decay;
				state->nextvalue = params->sustain;
				break;
			case ADSR_DECAY:
				state->currentcoef = FIX(1);
				state->value = params->sustain; // TODO: do i need this when it has went there already
				break;
			case ADSR_SUSTAIN:
				state->currentcoef = params->release;
				break;
			case ADSR_RELEASE:
				break;
			default:
				assert(0); // no zombie channels
		}
		state->nexttime = params->nexttime[state->section];
		state->section++;
	}
	return out;
}
int adsr_init(const AdsrParams* params, AdsrState* state) {
	memset(state, 0, sizeof(*state));
	state->currentcoef = params->attack;
}

#endif

#define DEADBIT (1<<15)
#define KEYOFFBIT (1<<14)

#define E 2.718281828
#define TGTCOEF (E/(E-1))

sample adsreval(AdsrParams *params, AdsrState *state, int note) {
	if (note & KEYOFFBIT) {
		if (state->mode == ADSR_RELEASE) {
		} else {
			state->mode = ADSR_RELEASE;
			state->tgt = (1 - TGTCOEF) * state->val;
		}
	}
	switch (state->mode) {
		case ADSR_ATTACK:
			state->val += params->attack * (TGTCOEF - state->val);
			if (state->val >= 1.0) {
				state->mode = ADSR_DECAY;
			}
			break;
		case ADSR_DECAY:
			state->val += params->decay * (params->sustain - state->val);
			break;
		case ADSR_RELEASE:
			state->val += params->release * (state->tgt - state->val);
			if (state->val < 0) {
				state->mode = ADSR_KILLED;
				state->val = 0.0; // ?
			}
			break;
		case ADSR_KILLED:
			return -1.0;
			break;
	}
	return state->val;
}

void adsr_init(AdsrState* state) {
	state->mode = ADSR_ATTACK;
	state->val = 0.0;
}

float midifreq(int note) {
	return (1 << ((note - 69) / 12)) * 440.0;
}

// sample osc_tri(int time, int note);
// sample osc_saw(int time, int note);
// sample osc_pulse(int time, int note);
// sample osc_rnd(RandomState* state);


// lowpass filter coefficients may change over time due to lfo's

typedef struct LowpassParams {
//	int freq;
//	int coef; // here for initialization
} LowpassParams;

typedef struct LowpassState {
	int coef; // this here for lfos and stuff, init'd at the beginning of the note
	sample prev;
} LowpassState;

typedef struct BassState {
} BassState;

typedef struct {
	Instrument base;
	LowpassParams lp;
	//int lfofreq; // should be pretty constant
} BassInstrument;

// TODO: inline these if needed
sample lowpass(LowpassState* state, sample x) {
	return state->prev + state->coef * (x - state->prev);
}
#define LOWPASS_COEF(freq) (DT / (freq / (2 * M_PI) + DT))
// TODO: function or not? get rid of division
#define lowpass_coef(freq) LOWPASS_COEF(freq)




sample lfo_iexp(int time, int freq) {
	return exp(-time * freq);
}

void trivialsaw_init(void* p) {
}

void bass_init(void* p, void*q) {
	trivialsaw_init(p);
	// TODO
}

#if 0
sample bassosc(const Bass* instru, BassState* state, int note) {
	//return osc_sin(&state->osc, note);
}

#endif
sample osc_dpwsaw_eval(void* v) {
	float *time = v;
	*time += 123*DT;
	return *time-floorf(*time);
}

sample bass_filt(void* state, sample in) {
	float *y = state;
	*y += 0.4 * (in - *y);
	return *y;
#if 0
	const int lowfreq = 100; // TODO: this also to the instrument? lp low freq?
	int lfo = lfo_iexp(state->lfotime, instru->lfofreq);
	state->lfotime++; // TODO: decide units
	state->lp.coef = lowpass_coef(lowfreq + lfo * (instru->lp.freq - lowfreq));
	return lowpass(&state->lp, s);
#endif
	return in;
}

#define NUM_CHANNELS 8
static Channel channels[NUM_CHANNELS];

#define FiltTrivLpK (DT*2*PI)
#define TRIVIAL_LP_PARM(fc) ((FiltTrivLpK*fc)/(FiltTrivLpK*fc+1))

//#define ADSRBLOCK(At,Dt,Sl,Rt) (1

/*
	dc	(1-@POW(E,-1.0/(At*RATE)))
	dc	(1-@POW(E,-1.0/(Dt*RATE)))
	dc	Sl
	dc	(1-@POW(E,-1.0/(Rt*RATE)))
*/

// TODO: INSTRUMENT(x, y) --> { (funccast)x,  etc}
// blah, should be constants
//
BassInstrument bass = {
	{
		bass_init,
		osc_dpwsaw_eval,
		bass_filt,
		{ 0.0004534119168875158,0.00004535044555269668,0.6,0.00002267547986504189  } //ADSRBLOCK(0.05, 0.5, 0.8, 0.1),
		//{ 0.0004534119168875158,0.00004535044555269668,0.8,0.00002267547986504189  } //ADSRBLOCK(0.05, 0.5, 0.8, 0.1),
	},
	//TRIVIAL_LP_PARM(5000)
};
	//440, LOWPASS_COEF(440)}, 1};
Instrument* instruments = {
	(Instrument*)&bass
};
#if 0
Instrument instruments[] = {
	{
		bass_init,
		osc_dpwsaw_eval,
		bass_filt,
		ADSR_PARAMS(0.1, 0.1, 0.5, 0.1),
		// filttriviallpparams
	},
};
#endif

sample eval_channel(Channel* ch) { // ch=r1
	Instrument* instr = ch->instr; // instr=r4
	sample a = instr->oscfunc(ch->oscstate);// instr->oscfunc(instr, ch->instrstate, ch->note);
	sample b = instr->filtfunc(ch->filtstate, a);// instr->filtfunc(instr, ch->instrstate, a);
	sample c = adsreval(&instr->adsrparams, &ch->adsrstate, ch->note);
	b *= c;
	if (c < 0.0)
		ch->note |= DEADBIT;
	return ch->velocity * b;
}

void synth_init(void) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		channels[i].note |= DEADBIT;
	}
}

// render
int32_t synth_sample(void) {
	sample out = 0;
	for (int i = 0; i < NUM_CHANNELS; i++) {
		if (!(channels[i].note & DEADBIT))
			out += eval_channel(&channels[i]);
	}
	return 0x7fff * out;
}


int synth_note_on(int midinote, int instrument) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		Channel* ch = &channels[i];
		if (ch->note & DEADBIT) {
			ch->note = midinote;
			adsr_init(&ch->adsrstate);
			ch->velocity = 1.0;
			ch->instrunum = instrument;
			ch->instr = &instruments[instrument];
			ch->instr->initfunc(ch, ch->instr);
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
