from platformio.public import DeviceMonitorFilterBase

class Newline(DeviceMonitorFilterBase):
    NAME = "newline"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.last_cr = False

    def tx(self, text):
        return text.replace('\n', '\r')

    def rx(self, text):
        return text.replace('\r\n', '\n')
