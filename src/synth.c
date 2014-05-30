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

typedef struct HighpassParams {
	float coef;
} HighpassParams;

typedef struct HighpassState {
	float val;
	float coef;
} HighpassState;


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

void trivial_hp_init(void* st, HighpassParams* params) {
	HighpassState *state = st;
	state->coef = params->coef;
	state->val = 0.0;
}

// y1 = g * (y0 + x1 - x0)
//    = g * (x1 + (y0 - x0))
//    = g * (x1 + stored) [in = x1]
sample trivial_hp_eval(void* st, sample in) {
	HighpassState *state = st;
	float b = in + state->val; // b = x1 + (y0 - x0)
	float y = state->coef * b;
	return state->val = y;
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
	OscDpwState saw0;
	OscDpwState saw1;
	float duty; // 0=0% (1:0), 1=50% (1:1)
} PlsDpwState;

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
	OscDpwState *state = st;
	sample a = osc_saw_eval(self, st);
	a *= a;
	sample dif = state->val - a;
	state->val = a;
	return dif * state->coef;
}

void pls_dpw_init(void* st, int note, float duty) {
	PlsDpwState* state = st;
	osc_dpw_init(&state->saw0, note);
	osc_dpw_init(&state->saw1, note);
	// first advances a bit for phase difference
	// (starts at 0)
	state->saw1.saw.val += duty;
	state->duty = duty;
}

sample pls_dpw_eval(Instrument *self, void* st) {
	PlsDpwState *state = st;
	sample a = osc_dpw_eval(self, &state->saw0);

	// first advances a bit for phase difference
	// fix duty diff cycle here in case it's haxd with lfo
	state->saw1.saw.val = state->saw0.saw.val + state->duty;
	if (state->saw1.saw.val > 1.0)
		state->saw1.saw.val -= 2.0;

	sample b = osc_dpw_eval(self, &state->saw1);
	sample c = b - a; // originally -1+duty...duty
	return c;// + state->duty; // ???
}


/***** Instruments *****/

typedef struct {
	Instrument base;
	LowpassParams lp;
} BassInstrument;

typedef struct {
	Instrument base;
	HighpassParams hp;
} NoiseInstrument;

typedef struct {
	Instrument base;
	LowpassParams lp;
	float dutybase;
	float dutyampl;
	AdsrParams lfoadsr;
} PulseBassInstrument;

typedef struct {
	Instrument base;
	LowpassParams lp;
	float freqampl;
	int lfonote;
} VibratoInstrument;

void bass_init(Channel *ch) {
	BassInstrument *ins = (BassInstrument*)ch->instr;
	trivial_lp_init(ch->filtstate, &ins->lp);
	osc_dpw_init(ch->oscstate, ch->note);
}

sample bass_filt(Instrument *self, void* st, sample in) {
	BassInstrument *bass = (BassInstrument*)self;
	LowpassState *state = st;
	state->coef = bass->lp.coef;
	return trivial_lp_eval(state, in);
}

void noise_init(Channel *ch) {
	NoiseInstrument *ins = (NoiseInstrument*)ch->instr;
	trivial_hp_init(ch->filtstate, &ins->hp);
	osc_noise_init(ch->oscstate);
}

sample noise_filt(Instrument *self, void* st, sample in) {
	NoiseInstrument *inst = (NoiseInstrument*)self;
	HighpassState *state = st;
	state->coef = inst->hp.coef;
	return trivial_hp_eval(state, in);
}

typedef struct PulseBassState {
	PlsDpwState osc;
	AdsrState lfoadsr;
} PulseBassState;

void pulsebass_init(Channel *ch) {
	PulseBassInstrument *ins = (PulseBassInstrument*)ch->instr;
	trivial_lp_init(ch->filtstate, &ins->lp);
	pls_dpw_init(ch->oscstate, ch->note, ins->dutybase);
	PulseBassState *state = (PulseBassState*)ch->oscstate;
	adsr_init(&state->lfoadsr);
}

sample pulsebass_osc(Instrument *self, void* st) {
	PulseBassInstrument *ins = (PulseBassInstrument*)self;
	PulseBassState* state = st;

	sample adsrval = adsreval(&ins->lfoadsr, &state->lfoadsr, 0);

	// copy in case of pot/lfo update
	state->osc.duty = ins->dutybase + ins->dutyampl * adsrval;

	return pls_dpw_eval(self, st);
}

sample pulsebass_filt(Instrument *self, void* st, sample in) {
	return in; // no filt yet
	PulseBassInstrument *bass = (PulseBassInstrument*)self;
	LowpassState *state = st;
	state->coef = bass->lp.coef;
	return trivial_lp_eval(state, in);
}

typedef struct {
	PlsDpwState osc;
	OscDpwState lfo;
	int orignote;
} VibratoState;

void vibrato_init(Channel *ch) {
	VibratoInstrument *ins = (VibratoInstrument*)ch->instr;

	trivial_lp_init(ch->filtstate, &ins->lp);

	VibratoState *state = (VibratoState*)ch->oscstate;
	pls_dpw_init(&state->osc, ch->note, 0.5); // hardcoded osc duty
	osc_dpw_init(&state->lfo, ins->lfonote);
	state->orignote = ch->note;
}

float dpwcoef(float freq) {
	return SAMPLERATE / (4 * freq * (1 - freq * DT));
}

float midifreq(int note) {
	return pow(2, (note - 69) / 12.0) * 440;
}

sample vibrato_osc(Instrument *self, void* st) {
	VibratoInstrument *ins = (VibratoInstrument*)self;
	VibratoState* state = st;

#if 0
	// copy from pot
	// 0.1 is arbitrarily chosen to make lfo slower than normal notes
	state->lfo.saw.tick =  sawticks[ins->lfonote];
	// FIXME vol correction coef
	state->lfo.coef = dpwcoef(midifreq(ins->lfonote)); // dpwcoefs[ins->lfonote];
#endif

	// execute lfo
	sample lfoval = osc_dpw_eval(self, &state->lfo);

	// modulate osc freq
	int n = state->orignote;
	float slope = (dpwcoefs[n+1] - dpwcoefs[n]);
	float newcoef = dpwcoefs[n] + ins->freqampl * lfoval * slope;
	state->osc.saw0.coef = newcoef;
	state->osc.saw1.coef = newcoef;

	return pls_dpw_eval(self, &state->osc);
}

sample vibrato_filt(Instrument *self, void* st, sample in) {
	return trivial_lp_eval(st, in);
}

typedef struct {
	LowpassState lp[3];
} TejeezFilt;

void tejeez_init(Channel *ch) {
	BassInstrument *ins = (BassInstrument*)ch->instr;
	TejeezFilt *tjz = (TejeezFilt*)ch->filtstate;
	trivial_lp_init(&tjz->lp[0], &ins->lp);
	trivial_lp_init(&tjz->lp[1], &ins->lp);
	trivial_lp_init(&tjz->lp[2], &ins->lp);
	osc_dpw_init(ch->oscstate, ch->note);
}

sample tejeez_filt(Instrument *self, void* st, sample in) {
	BassInstrument *bass = (BassInstrument*)self;
	TejeezFilt *tjz = st;
	tjz->lp[0].coef = bass->lp.coef;
	tjz->lp[1].coef = bass->lp.coef;
	tjz->lp[2].coef = bass->lp.coef;
	sample x = in - 2 * tjz->lp[2].val;
	if (x < -1) x = -1;
	else if (x > 1) x = 1;
	x = trivial_lp_eval(&tjz->lp[0], x);
	x = trivial_lp_eval(&tjz->lp[1], x);
	x = trivial_lp_eval(&tjz->lp[2], x);
	return x;
}

#define FiltTrivLpK (DT*2*PI)
#define TRIVIAL_LP_PARM(fc) ((FiltTrivLpK*fc)/(FiltTrivLpK*fc+1))
#define TRIVIAL_HP_PARM(fc) (1/(1+FiltTrivLpK*fc))

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
		{ 0.0004534119168875158,0.00004535044555269668,0.6,0.002267547986504189  } //ADSRBLOCK(0.05, 0.5, 0.8, 0.1),
	},
	{ TRIVIAL_LP_PARM(5000) }
};

NoiseInstrument noise = {
	{
		noise_init,
		osc_noise_eval,
		noise_filt,
		{ 0.188063653849, 6.94420332348e-05, 0.0, 6.94420332348e-05 }
	},
	{ TRIVIAL_HP_PARM(5000) }
};

PulseBassInstrument pulsebass = {
	{
		pulsebass_init,
		pulsebass_osc,
		pulsebass_filt,
		{ 0.000208311633451, 0.000208311633451, 0.5, 0.000208311633451 }
	},
	{ TRIVIAL_LP_PARM(5000) },
	0.1, 0.9,
	{ 6.94442033189e-06, 1.0, 1.0, 1.0 }
};

VibratoInstrument vibrato = {
	{
		vibrato_init,
		vibrato_osc,
		vibrato_filt,
		{ 0.000208311633451, 0.000208311633451, 0.5, 0.000208311633451 }
	},
	{ TRIVIAL_LP_PARM(1000) },
	6.0,
	0
};

BassInstrument tejeez = {
	{
		tejeez_init,
		osc_dpw_eval,
		tejeez_filt,
		{ 0.0004534119168875158,0.00004535044555269668,0.6,0.002267547986504189  } //ADSRBLOCK(0.05, 0.5, 0.8, 0.1),
	},
	{ TRIVIAL_LP_PARM(5000) }
};


Instrument* instruments[] = {
	(Instrument*)&bass,
	(Instrument*)&noise,
	(Instrument*)&pulsebass,
	(Instrument*)&vibrato,
	(Instrument*)&tejeez,
};



/***** Core implementation *****/

#define NUM_CHANNELS 16
static Channel channels[NUM_CHANNELS];

float mastervol = 1.0;

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
	return 0x7fff * mastervol * 0.1 * out; // FIXME: adaptive filter
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

int synth_note_on(int midinote, int instrument, float notevel) {
	for (int i = 0; i < NUM_CHANNELS; i++) {
		Channel* ch = &channels[i];
		if (ch->note & DEADBIT) {
			ch->note = midinote;
			adsr_init(&ch->adsrstate);
			ch->velocity = notevel;
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

void synth_setparams(float f, int chan) {
	switch (chan) {
	case 0:
		bass.lp.coef = TRIVIAL_LP_PARM(f/0xfff*5000);
		noise.hp.coef = TRIVIAL_HP_PARM(f/0xfff*8000);
		pulsebass.dutybase = f/0xfff;
		vibrato.freqampl = 10*f/0xfff;
		break;
	case 1:
		vibrato.lfonote = 10*f/0xfff;
		tejeez.lp.coef = TRIVIAL_LP_PARM(f/0xfff*8000);
		break;
	}
}

void synth_setvolume(float f) {
	mastervol = f;
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
