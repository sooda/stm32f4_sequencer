# freq / (rate / 2), midi notes 0..127
# freq = 2^((midinote-69)/12) * 440
# TODO: rate/2?
RATE = 48000
data = [2 ** ((note-69)/12.0)*440/(RATE/2) for note in range(128)]
print "float sawticks[128] = { %s };" % ", ".join(map(str, data))
