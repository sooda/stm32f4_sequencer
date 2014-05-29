#!/usr/bin/python2

def midifreq(p):
	return pow(2, (p - 69) / 12.0) * 440

rate = 48e3
notes = range(128)
freqs = map(midifreq, notes)

coefs = [rate / (4 * freq * (1 - freq / rate))
		for freq in freqs]

ticks = [freq / (rate / 2)
		for freq in freqs]

open("dpwcoefs.c", "w").write(
    "float dpwcoefs[128] = { %s };\n" % ", ".join(map(str, coefs)))

open("sawticks.c", "w").write(
    "float sawticks[128] = { %s };\n" % ", ".join(map(str, ticks)))
