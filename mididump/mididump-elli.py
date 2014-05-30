from records.binary import BigEndianFile, StringField, IntU16Field, IntU32Field, IntS8Field, IntU8Field, BinBlock, BinArrayField, BinBlockField, BinField
from records.core import Field
from mysynth import makenotes, notestr2synanote, midi2notestr
import serial

class HeaderChunk(BinBlock):
	chkid = StringField(4)
	chksize = IntU32Field()
	fmttype = IntU16Field()
	ntracks = IntU16Field()
	timediv = IntU16Field()

class TrackChunkHeader(BinBlock):
	chkid = StringField(4)
	chksize = IntU32Field()

class VarLengthField(BinField):
	def read(self, infile):
		x = self._readchar(infile)
		self.nbytes = 1
		total = 0
		bts = [x]
		while x & 0x80:
			total |= x & ~0x80
			total <<= 7
			x = self._readchar(infile)
			self.nbytes += 1
			bts.append(x)
		total |= x
		if self.nbytes > 1:
			pass#print "hii",total,bts,self.nbytes
		return total

	def _readchar(self, infile):
		return infile.readvalue(IntU8Field.FORMAT)[0]

	def binlength(self):
		try:
			return self.nbytes
		except AttributeError:
			raise RuntimeError("Cannot calculate variable size before reading it")

class BitsField(Field):
	def __init__(self, containerfield, mask):
		super(BitsField, self).__init__()
		self.containerfield = containerfield
		assert mask >= 0
		self.mask = mask
		self.shamt = self._ctz(mask)
	
	@staticmethod
	def _ctz(mask):
		"""Count trailing zeros"""
		n = 0
		while (mask & 1) == 0:
			mask >>= 1
			n += 1
		return n

	def read(self, infile):
		self.value = (self.containerfield.value & self.mask) >> self.shamt
		return self.value

	def binlength(self):
		return 0 # value reserves storage in containerfield

# NOTE: binlength() can be called only after reading for some variable length types

class IntU24Field(BinField):
	FORMAT = "HB"
	def read(self, infile):
		x = BinField.read(self, infile)
		return (x[0] << 8) | x[1]

class EventParamsField(BinField):
	def __init__(self, param1, param2):
		super(EventParamsField, self).__init__()
		self.param1 = param1
		self.param2 = param2
	def read(self, infile):
		if self.evtype.value != 0xf:
			self.valuelen = 0
			return
		if self.param1.value in (1, 2, 3, 4, 5, 6, 7):
			# string data
			# TODO: subclassify
			self.valuelen = self.param2.value
			return StringField(self.param2.value).read(infile) # infile.readvalue(str(self.param2.value) + "s")
		elif self.param1.value == 47:
			# end of track
			self.valuelen = 0
			return "(end of track)"
		#elif self.param1.value == 81:
		#	# tempo
		#	self.valuelen = 3
		#	return IntU24Field().read(infile)
		else:
			raise ValueError("Unknown meta event: %d" % self.param1.value)
	def binlength(self):
		return self.valuelen


class EventParamsChunk(BinBlock):
	param1 = IntU8Field()
	param2 = IntU8Field()
	rest = EventParamsField(param1, param2)
	def __init__(self, evtype):
		self.evtype = evtype
		self.rest.evtype = evtype
	

class TrackEventChunk(BinBlock):
	deltatime = VarLengthField()
	typechan = IntU8Field()
	evtype = BitsField(typechan, 0xf0)
	channel = BitsField(typechan, 0x0f)
	params = BinBlockField(EventParamsChunk, (evtype,)) # FIXME: binblockfield wants class not instance

def read(fname):
	fh = open(fname)
	reader = BigEndianFile(fh)
	hdr = HeaderChunk.create(reader)
	timediv = hdr.timediv
	if timediv & 0x8000:
		raise ValueError("Bad timediv, unimplemented for frames per sec")
	# Ticks per beat translate to the number of clock ticks or track delta positions in every quarter note of music. 
	#  If no tempo is define, 120 beats per minute is assumed.
	bps = 120
	ticks_per_beat = timediv
	chk = TrackChunkHeader.create(reader)
	sz = chk.chksize
	x = 0
	evs = []
	while x != sz:
		ev = TrackEventChunk.create(reader)
		evs.append(ev)
		x += ev.binlength()
		if x > sz:
			raise RuntimeError("bug, track size doesn't match event sizes")
	chans = printnotes(evs, ticks_per_beat, bps, 10) # FIXME: 10/20? divide by 8?
	ins = 5
	chans = chans[0:ins]
	makenotes(120/8, chans)

#            X              X   x
CHAN_MAP = [2, -1, -1, -1,  0, -1, -1]
s = serial.Serial("/dev/ttyUSB0",115200)
def sendmsg(chan,note,vel):
	print "msg going",chan,note
	onoff = 0x40 if note >= 0 else 0
	note = abs(note)
	s.write(chr(0x80 | chan | onoff))
	s.write(chr(0x60 | (note  >> 4)))
	s.write(chr(0x50 | (note & 0xf)))
	s.write(chr(0x20 | (vel   >> 4)))
	s.write(chr(0x10 | (vel  & 0xf)))


def printnotes(evs, tpb, bps, endtime):
	#maxch = max(evs, key=lambda ev: ev.channel)
	#print maxch
	time = 0
	lasttime = -1
	# TODO: hax the 7
	line = [(None, "......")] * 7
	print "#beat #sec notes"
	firstnote = -1
	chans_out = [[], [], [], [], [], [], []]
	import time as timemod
	startime = timemod.time()

	for ev in evs:
		timemod.sleep(0.01)
		time += ev.deltatime # in ticks
		c = ev.channel - 1
		if ev.evtype == 8:
			devicech = CHAN_MAP[c]
			if devicech != -1:
				sendmsg(devicech,-ev.params.param1,ev.params.param2)
			line[c] = ("", "---_--")
			if firstnote == -1:
				raise ValueError("wat, off before on")
		elif ev.evtype == 9:
			devicech = CHAN_MAP[c]
			if devicech != -1:
				sendmsg(devicech,ev.params.param1,ev.params.param2)
			#line[c] = "%03x_%02x" % (ev.params.param1, ev.params.param2)
			line[c] = (midi2notestr(ev.params.param1), "%s_%02x" % (midi2notestr(ev.params.param1), ev.params.param2))
			if firstnote == -1: firstnote = time
		elif ev.evtype == 12:
			print time, ev.channel, "-", "instrument: %s" % ev.params.param1
		elif ev.evtype == 0xf:
			pass
		elif ev.evtype == 0:
			pass # !?!?
		else:
			raise ValueError("wat? %d" % ev.evtype)
		if time != lasttime and firstnote != -1:
			t = time - firstnote
			# 4 quarter notes
			if float(t) / tpb / (bps / 4) > endtime:
				break
			print "%04d %.2f %s" % (float(t) / tpb, float(t) / tpb / (bps / 4), " ".join([x[1] for x in line]))
			le_sec = float(t) / tpb / (bps / 4)
			curtime=timemod.time()
			irlduration=curtime-startime
			if irlduration < le_sec:
				#timemod.sleep(le_sec - irlduration)
				#4timemod.sleep(0.1)
				pass
			for (chan, (notestr, descr)) in enumerate(line):
				if notestr is None:
					n = 0
				elif notestr == "":
					n = -1
				else:
					n = notestr2synanote(notestr)
				chans_out[chan].append(n)
			line = [(None, "......")] * 7
			lasttime = time
	return chans_out


if __name__ == "__main__":
	read("isi.mid")
