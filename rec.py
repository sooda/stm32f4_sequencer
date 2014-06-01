import alsaseq, serial
alsaseq.client( 'Recorder', 1, 0, True )
alsaseq.connectfrom( 1, 129, 0 )
alsaseq.start()
events = []

EV_KEYDOWN = 6
EV_KEYUP = 7

s = serial.Serial("/dev/ttyUSB0", 115200)

ch = 1

def sendmsg(chan,note,vel):
    print "msg going",chan,note
    onoff = 0x40 if note >= 0 else 0
    note = abs(note)
    s.write(chr(0x80 | chan | onoff))
    s.write(chr(0x60 | (note  >> 4)))
    s.write(chr(0x50 | (note & 0xf)))
    s.write(chr(0x20 | (vel   >> 4)))
    s.write(chr(0x10 | (vel  & 0xf)))

while 1:
    if alsaseq.inputpending():
        ch = int(open("ch.txt", "r").read())
        event = alsaseq.input()
        evtype = event[0]
        if evtype == EV_KEYDOWN:
            sendmsg(ch, event[7][1], event[7][2])
        elif evtype == EV_KEYUP:
            sendmsg(ch, -event[7][1], event[7][2])
        else:
            print event
