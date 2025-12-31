import socket
import serial

HOST = "0.0.0.0"
PORT = 11880

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))

ser = serial.Serial('/dev/ttyFT0', 115200, timeout=0.5)
ser.timeout = 0.1

while True:
   while ser.in_waiting > 0:
      print('>', ser.read_until(size=ser.in_waiting))
   rdata, addr = sock.recvfrom(128)
   print('<', rdata)
   ser.write(rdata)
   tdata = ser.read(128)
   print('>', tdata)
   sock.sendto(tdata, addr)
