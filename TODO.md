  * [x] ensure the display blanking circuit actually works, fix it if it doesn't. then analyse why I didn't just include it in the clock executable.
  * [x] document the boot process, and how the clock bin gets launched.
  * [x] simplify the boot process, ideally just launch the clock bin as early as possible.
  * [ ] upgrade to a newer version of the bbb image and debian that its based off see https://www.beagleboard.org/distros
  * [x] consider redesigning the driver board myself. using same chip? using a newer more advanced chip if such a thing exists.
  * [x] consider if our code does the blank -> update image -> blank approach.
  * [ ] reduce all the things on the bbb that would write to the sd card and possibly cause it to wear out over time 
  * [ ] analyse how we do the brightness client/server in tsl2561-daemon. can we add or move to an mqtt framework so that we can report room brightness to home assistant?
  * [x] rename master to main in git.
  * [ ] originally I made the dimming/brightening so slow its imperceptible, but its too slow. speed it up a little
  * [ ] modify how the display works, only send updates to the TLC5947 when the display changes brightness or time.
  * [ ] add instrumentation to the system so we can see it in homeassistant.
  * [ ] turn off flashing LED on the wifi adapter - after say 30 seconds after boot 
  * [ ] turn off the BBB flashing LEDs 30 seconds after boot (to make it easier to tell its booting)
  
