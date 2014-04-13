#!/usr/bin/python

from Adafruit_BBIO.SPI import SPI
from time import sleep
spi = SPI(0,0)
spi.bpw = 12
spi.msh = 100000
spi.lsbfirst = False
spi.mode = 0
spi.open

tlc5947_count = 2
tlc5947_channels = 24
# buffer = [0x000] * 48
buffer = [0x000] * (tlc5947_count * tlc5947_channels)

#spi.writebytes(buffer)
#print buffer


spi.writebytes(buffer)


#sleep(1)

spi.close


# CS_0  P9_17 lat
# DO    P9_21 din
# DI    P9_18 n/c
# SCLK  P9_22 clk

