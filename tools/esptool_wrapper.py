#!/usr/bin/env python3
import os.path
import sys
sys.path.append(os.path.join(sys.argv.pop(1), 'tool-esptoolpy'))
import esptool

# Workaround for https://github.com/MichaelZaidman/hid-ft260/issues/27
def noop(*args): pass
esptool.reset.ResetStrategy._setDTR.__code__ = noop.__code__
esptool.reset.ResetStrategy._setRTS.__code__ = noop.__code__
esptool.reset.ResetStrategy._setDTRandRTS.__code__ = noop.__code__

esptool._main()
