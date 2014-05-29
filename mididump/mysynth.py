def makeinstrus(inpfilenames):
	instrus = "\n".join(["static const uint8_t instrument%d[] = {\n#include \"%s\"\n};\n" % (i, fname)
			for i, fname in enumerate(inpfilenames)])
	prglist = "static const uint8_t *instru_programs[] = { %s };\n" % ",".join(
				["instrument%d" % i for i, _ in enumerate(inpfilenames)])
	other = "#define NUM_INSTRUMENTS %d\n" % len(inpfilenames)
	open("instruments.h", "w").write(instrus + prglist + other)

def makenotes(bps, notelists):
	# same amount of length in lists assumed
	notestr = ("#define NUM_NOTES " + str(len(notelists[0])) + "\n"
			"#define BPS " + str(bps) + "\n"
			"static uint8_t notes[NUM_INSTRUMENTS][NUM_NOTES] = {" +
				",".join(["{ %s }" % ",".join(map(str, notes)) for notes in notelists]) +
			"};\n")
	open("notes.h", "w").write(notestr)

def midi2notestr(num):
	"""midi number to notestring"""
	notes = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
	note = notes[num % len(notes)]
	octave = num // len(notes) - 1
	return "%s%d" % (note, octave)

def notestr2synanote(note):
	# rotate notes so that A440 is index 0
	notes = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
	nnotes = 12
	notemap = dict((note, i) for i, note in enumerate(notes))
	notenum = (notemap[note[:2]] + 3) % nnotes
	octave = int(note[2])
	if octave < 0 or octave > 7:
		raise ValueError("octave %d not supported" % octave)
	synanote = octave * nnotes + notenum
	return synanote
