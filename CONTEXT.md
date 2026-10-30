# BBB Clock

A BeagleBone Black driving a four-digit seven-segment LED clock through two
daisy-chained TLC5947 constant-current PWM LED drivers. This glossary fixes the
shared language for the display hardware and the two display faults we track.
(How it is wired and the diagnostic notes live in `README.org`, not here.)

## Display

**Digit**:
One of the four seven-segment characters. Numbered 0–3 from the right: digit 0 =
minutes units, digit 1 = minutes tens, digit 2 = hours units, digit 3 = hours tens.

**Segment**:
One of the seven bars (A–G) of a digit. Each segment is a single LED on one channel.

**Colon**:
The pair of dots between the hours and minutes.

**Decimal point**:
A per-digit dot LED. Present in hardware but not used by the clock display.

**Channel**:
One of a driver's 24 constant-current outputs, driving one segment or dot LED.
48 channels total across the two chips.

**Grayscale value**:
The 12-bit (0–4095) PWM value commanded for a channel.
_Avoid_: brightness (reserve "brightness" for the ambient-light-driven overall level)

**Ambient lux**:
The illuminance of the room as measured by the light sensor, in lux. The *input*
that drives dimming.
_Avoid_: "brightness" for this — that is the output, not the sensor reading.

**Display brightness**:
The overall level the whole display is currently lit at, driven by ambient lux —
the *output* of the dimming loop. One level for the display, not per channel.
_Avoid_: using it for a single channel's grayscale value, or for the raw sensor
reading (that is ambient lux).

**Digit trim**:
A fixed per-digit multiplier (0–1) applied to the display brightness before a
digit's segments are written, so one physically brighter display can be matched
to the others by eye. Read at startup from `/etc/bbb-clock/clock.conf`
(`digit_trim_N = V`); built-in default 1.0 for every digit. Needed because the
TLC5947 has one current-set resistor per chip and no per-channel dot correction,
so the grayscale value is the only per-digit lever. Currently digit 2 (hours
units, replaced Oct 2026) = 0.5, calibrated 2026-10-31 (0.7 by night, 0.5 in daylight).
A matching `colon_trim` key scales the four colon LEDs (0.7 since the hours digit was dimmed).

**Grayscale latch**:
The on-chip register holding the grayscale values the driver is actively
displaying, held by the chip's internal oscillator until the next latch.

## Drivers

**Driver / Chip**:
A TLC5947 24-channel constant-current-sink PWM LED driver. Two are daisy-chained.
_Avoid_: TLC, LED driver

**Chip A**:
The driver carrying channels 0–23 — the **right three digits** (digits 0, 1, 2).

**Chip B**:
The driver carrying channels 24–47 — the **leftmost digit** (digit 3) and the colon.

**Constant-current sink**:
The drive scheme: LED anodes tie to VLED, cathodes to a channel output; the driver
turns an LED on by sinking its current to ground. Brightness is set by PWM duty,
not by supply voltage — so a supply sag can only *dim* an LED, never brighten it.

**Blanking**:
Forcing all of a chip's outputs off via its BLANK input, independent of the
grayscale latch contents.
_Avoid_: /OE, output-enable (the board labels the pin /OE; we say "blank"/"unblank")

## Refresh model

**Refresh**:
Re-sending the current grayscale values and re-latching with no change to what is
displayed. The clock currently refreshes ~twice a second.
_Avoid_: update (reserve for a real change)

**Update**:
Sending grayscale values because what should be shown has changed (a new minute,
a dimming step).

**Latch**:
Transferring shifted-in data into the grayscale latch (rising edge of the LAT
line). Every refresh and every update ends in a latch.

## Faults

**Glitch frame**:
A single displayed frame in which one or more channels show the wrong — typically
much brighter — grayscale value.

**Self-heal**:
A glitch frame being corrected by the next refresh, so the wrong value shows for
only ~half a second.

**P1 / power-up garbage**:
Random bright LEDs shown from power-on until the software writes valid data,
caused by the grayscale latch holding undefined values while nothing keeps the
chips blanked during boot.

**P2 / runtime glitch**:
Rare, random, self-healing bright-channel glitches during normal running.
Established to be physical-layer corruption of grayscale data (not a
display-logic bug); root cause still open.
