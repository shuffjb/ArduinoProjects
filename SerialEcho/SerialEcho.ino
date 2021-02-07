/* SerialEcho - echo Serial Monitor input to Serial Monitor out  */
/* 2021-01-20 JBS Cloned from sample code                        */
/*                                                               */
/*

*/

static const uint32_t GPSBaud = 9600;    // Using baud from NeoGPS


void setup()
{
  // Open serial communications and wait for port to open:
  Serial.begin(GPSBaud);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Native USB only
  }


  Serial.println("Goodnight moon!");


}

void loop() // run over and over
{
  if (Serial.available())
    Serial.write(Serial.read());  // Copy serial monitor to Serial out
}
