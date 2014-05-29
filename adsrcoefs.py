#!/usr/bin/python2
from sys import argv
from math import e

rate = 48000

def coef(x):
    return 1 - e ** (-1.0 / (x * rate))

a, d, s, r = map(float, argv[1:])
print "{ %s, %s, %s, %s }" % (coef(a), coef(d), s, coef(d))
