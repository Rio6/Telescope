#!/usr/bin/env python3
# Workaround for https://github.com/MichaelZaidman/hid-ft260/issues/27
import os.path
import sys
import gpiod
sys.path.append(os.path.join(sys.argv.pop(1), 'tool-esptoolpy'))
import esptool

esptool.reset.ResetStrategy.gpiopath = '/dev/gpiochip0'
esptool.reset.ResetStrategy.RTS = 7
esptool.reset.ResetStrategy.DTR = 11

def setDTR(self, state):
    import gpiod
    with gpiod.request_lines(self.gpiopath, { self.DTR: self.line_settings }) as req:
        req.set_value(self.DTR, gpiod.line.Value(state))

def setRTS(self, state):
    import gpiod
    with gpiod.request_lines(self.gpiopath, { self.RTS: self.line_settings }) as req:
        req.set_value(self.RTS, gpiod.line.Value(state))

def setDTRandRTS(self, dtr=False, rts=False):
    import gpiod
    with gpiod.request_lines(self.gpiopath, {
        self.DTR: self.line_settings,
        self.RTS: self.line_settings,
    }) as req:
        req.set_values({
            self.DTR: gpiod.line.Value(dtr),
            self.RTS: gpiod.line.Value(rts),
        })

esptool.reset.ResetStrategy.line_settings          = gpiod.LineSettings(direction=gpiod.line.Direction.OUTPUT, active_low=True)
esptool.reset.ResetStrategy._setDTR.__code__       = setDTR.__code__
esptool.reset.ResetStrategy._setRTS.__code__       = setRTS.__code__
esptool.reset.ResetStrategy._setDTRandRTS.__code__ = setDTRandRTS.__code__

if __name__ == '__main__':
    esptool._main()
