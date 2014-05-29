"""Binary stream fields, implemented with the struct module"""
from records.core import Field, Block, ArrayField, BlockField
from struct import pack, unpack, calcsize

# 1) raw string/file input, BEInt/LEInt, read(open("file")), filelike.read(sz)
# 2) raw string/file input, Int(Int.LE), read(open("file")), filelike.read(sz)
# 3) binary proxy class, Int, read(BigEndianFile(open("file")), binaryfile.read(myfmt)
# 3!
# 4) inherit block from a big/little endian thing, passing options to its fields

# NOTE: mixing of big and little endian data is not possible with this approach.
# NOTE: this is probably not very efficient with very big data (order of megabytes)

class BinField(Field):
	"""Base class for all of the other binary formats here"""
	FORMAT = ""

	def read(self, infile):
		v = infile.readvalue(self.FORMAT)
		if len(v) == 1: # UGLY HACK for intu24field
			v = v[0]
		self.value = v
		return v

	def write(self, outfile, value):
		outfile.writevalue(self.FORMAT, value)

	def binlength(self):
		return calcsize(self.FORMAT)

class StringField(BinField):
	"""A string of a constant length of characters
	
	Reads exactly the specified amount, writing pads with nul bytes or cuts if
	given string is of wrong length.
	"""
	def __init__(self, length):
		super(StringField, self).__init__()
		self.FORMAT = "%ds" % length

class IntS8Field(BinField):
	FORMAT = "b"

class IntU8Field(BinField):
	FORMAT = "B"

class IntS16Field(BinField):
	FORMAT = "h"

class IntU16Field(BinField):
	FORMAT = "H"

class IntS32Field(BinField):
	FORMAT = "i"

class IntU32Field(BinField):
	FORMAT = "I"

class FileProxy(object):
	BYTEFORMAT = "@"
	def __init__(self, fh):
		self.fh = fh

	def readvalue(self, valueformat):
		"""Read the given value format from the stream"""
		raw = self.fh.read(calcsize(self.BYTEFORMAT + valueformat))
		if len(raw) == 0:
			print "yhy",self.fh.tell()
		return unpack(self.BYTEFORMAT + valueformat, raw)

	def writevalue(self, valueformat, value):
		"""Write the given value with appropriate format to the stream"""
		self.fh.write(pack(self.BYTEFORMAT + valueformat, value))

class LittleEndianFile(FileProxy):
	BYTEFORMAT = "<"

class BigEndianFile(FileProxy):
	BYTEFORMAT = ">"

class BinBlock(Block):
	def binlength(self):
		return sum(field.binlength() for _, field in self.base_fields)

class BinArrayField(ArrayField):
	def binlength(self):
		return self.field.binlength() * self.amount

class BinBlockField(BlockField):
	def binlength(self):
		if self.args:
			return self.value.binlength()
		else:
			return self.blockcls().binlength()
