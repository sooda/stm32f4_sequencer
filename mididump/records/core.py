"""Records within records
Chunks
Blocks
Structures

Generic record tree (not necessarily!) structure, kinda like serialization

Data format configurable, for example:
- stream of serialized data
- a binary file
- s-expression

Field read/write talks to the raw data structure, which is:
- string/filelike/what? for serialized stream/binary file
- string values for an s-expression tree (?) that are then concatenated..?

Array data:
- sometimes have to be declared explicitly for a constant numbered shit
  - explicit class with a size parameter
  - array of separate field classes
- sometimes the number of items can be read from the data stream
- NOTE that plain arrays aren't supported, explicit field classes must be used

Field objects:
- they "don't" exist "at runtime" in live objects, only their values are there
- they are _just one_ raw type that maps to builtin python types and simple bytestreams or such.
- they actually do exist in a magic dict to be able to load and save but user shouldn't worry.

Block objects:
- collections of fields.
- when a block needs another block inside it, wrap it with BlockField
"""

# TODO: error checking, tests
# TODO: Blokki.create(stream) not b=Blokki(); b.read(stream)

class Field(object):
	"""Generic creation-counted baseclass for declaring ordered fields together
	
	This doesn't hold the actual value, but acts as a read/write proxy instead.
	Subclasses must define read and write methods.
	"""

	# fields inside a block must be read by creation order
	# we don't need an additional list when we keep track of the orders with numbers
	# btw, cross-order between blocks is not important.
	creation_counter = 0

	def __init__(self):
		self.creation_counter = Field.creation_counter
		Field.creation_counter += 1

	def __repr__(self):
		return "<%s #%d>" % (type(self).__name__, self.creation_counter)

	def read(self, infile):
		"""Take input, return a value"""

	def write(self, outfile, value):
		"""Put a value to output"""

class ArrayField(Field):
	"""contains a constant number of items like 'field'"""
	# no need to worry about the field being just one instance
	# (except when it's blockfield but it creates its own instances)
	# be careful!
	def __init__(self, field, amount):
		super(ArrayField, self).__init__()
		self.field = field
		self.amount = amount
		
	def read(self, infile):
		values = []
		for x in range(self.amount):
			values.append(self.field.read(infile))
		return values

	def write(self, outfile, values):
		for value in values:
			outfile.write(self.fmt, value)

# if you create something like this, be sure that it holds only the class and not the instance or you'll accidentally read several times to the same block (as would with arrayfield e.g.)
class BlockField(Field):
	def __init__(self, blockcls, args=()):
		super(BlockField, self).__init__()
		self.blockcls = blockcls
		self.args = args

	def read(self, infile):
		block = self.blockcls(*self.args)
		block.read(infile)
		self.value = block
		return block

	def write(self, outfile, value):
		return value.write(outfile)

"""
in s-expr shit
class ConstField(Field):
	def __init__(self, value):
		super(ConstField, self).__init__()
		self.value = value
		
	def parse(self, value):
		if not isinstance(value, tuple):
			raise TypeError("Expected tuple, got %s" % value)
		if value[0] != self.value:
			raise ValueError("Invalid value %s, expected %s" % (value[0], self.value))
		return value[0]
		
	def write(self, value):
		return value
	
	def default(self):
		return self.value
"""

# a trick borrowed from Django
def get_declared_fields(bases, attrs):
	"""Get all fields!"""
	fields = [(field_name, attrs[field_name])
			for field_name, obj in attrs.items()
			if isinstance(obj, Field)]
	fields.sort(key=lambda x: x[1].creation_counter)
	# TODO: document ::-1 or move this before sorting
	#for base in bases[::-1]:
	#	if hasattr(base, 'base_fields'):
	#		#fields = base.base_fields.items() + fields # TODO: WHAT?
	#		fields = base.base_fields + fields
	return fields
	
class BlockMetaClass(type):
	"""Get field specifiers, add them to a list called 'base_fields'"""
	def __new__(cls, name, bases, attrs):
		attrs['base_fields'] = get_declared_fields(bases, attrs)
		new_class = super(BlockMetaClass, cls).__new__(cls, name, bases, attrs)
		return new_class

# TODO: don't flatten the field items
class Block(object):
	"""Block is an ordered collection of fields"""
	__metaclass__ = BlockMetaClass

	def __init__(self, *a, **kw):
		"""Create records on the fly without reading them first"""
		for value, (name, field) in zip(a, self.base_fields):
			setattr(self, name, value)

		for k, v in kw:
			if v in self.base_fields:
				setattr(self, k, v)

	def read(self, instream):
		# TODO: optimize to read all at once
		for (fieldname, fieldinstance) in self.base_fields:
			setattr(self, fieldname, fieldinstance.read(instream))

	def write(self, outstream):
		for (fieldname, fieldinstance) in self.base_fields:
			fieldinstance.write(outstream, getattr(self, fieldname))

	@classmethod
	def create(cls, instream):
		obj = cls()
		obj.read(instream)
		return obj

	def __repr__(self):
		return str(self.aslist())

	def aslist(self):
		return [(field, name, getattr(self, name))
				for name, field in self.base_fields]
