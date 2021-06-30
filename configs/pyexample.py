# Import the MIDIMonster Python API
import midimonster

def channel1(value):
    # Print current input value
    print("Python channel 1 is at %s" % (value,))
    # Send inverse on py1.out1
    midimonster.output("out1", 1.0 - value)
