import socket
import serial

HOST = "0.0.0.0"
PORT = 11880

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))

ser = serial.Serial('/dev/ttyFT0', 115200, timeout=0.5)

while True:
   while ser.in_waiting > 0:
      print('>', ser.read_until(), ser.in_waiting)
   rdata, addr = sock.recvfrom(10)
   print('<', rdata)
   ser.write(rdata)
   tdata = ser.read_until(b'\r')
   print('>', tdata)
   sock.sendto(tdata, addr)
