/*
  Wireless PIR sensor firmware for MySensor project
  revision 1.0
 
  Copyright (c) 2014, Andrey Shigapov, All rights reserved
 
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  http://www.gnu.org/licenses/gpl.txt
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
  
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Example wireless PIR sensor for MySensors IoT network . 
// For more information about MySensors project, please, visit http://www.mysensors.org/
// This program puts MCU in sleep mode and uses two interrupts to process events in order to save energy
// The first interrupt is generated by PIR sensor output connected to D2 input, the interrupt handler schedules 
// transmission of a report to MySensors gateway and disables PIR sensor for a number of WDT interrupts
// The second interrupt is generated by WDT and it fires about every 8 sec. 
// This interrupt handler decreases PIR sensor disable counter
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <avr/sleep.h>
#include <avr/power.h>
#include <Sensor.h>
#include <SPI.h>
#include <EEPROM.h>  
#include <RF24.h>


#define ENABLE_SERIAL

//MySensor network node ID
#define NODE_RADIO_ID     0x51

//this sensor has only one child sensor
#define CHILD_ID          1

#define WDT_INTERVAL      8 //interval between WDT interrupts in seconds
#define REPORT_TIMEOUT    (10/WDT_INTERVAL)

//defines number of WDT intervals (~8 sec) to keep PIR sensor disabled 
//after a report has been sent
#define PIR_DISABLE_INTERVAL 4

#define BUTTON_PIN        A1 // Arduino Digital I/O pin for button/reed switch
#define PHOTO_CELL_PIN    A2 // Arduino Digital I/O pin for photo cell input
#define PHOTO_CELL_POWER  A3 // Arduino Digital I/O pin for photo cell power

#define STATUS_LED        A4 // the pin that the front LED is attached to
#define LED1              A5
#define BUZZER            5  // the pin that the Buzzer is attached to
#define PIR_ENABLE        6  // the pin that controls PIR output
#define PIR_INPUT         2  // the pin that connected to PIT output
#define BATTERY_LEVEL     A0 // the pin that connected to battery


uint16_t wdt_counter = 0;
uint16_t report_counter = 1;

uint8_t pir_disable_counter = 1;  //disable the sensor for the first 8 sec, let all transition precesses to settle down
uint8_t pir_enable_counter = 0;
uint8_t dataToSend = 0;
uint8_t dataSent = 0;

Sensor gw;


/////////////////////////////////////////////////////////////////
//  Returns:     Nothing.
//  Parameters:  None.
//
//  Description: PIR output signal Interrupt Service. 
//               This is executed when PIR sensor sense a movement,
//               it disables the PIR sensor for several WDT intervals
//               in order to conserve the energy
/////////////////////////////////////////////////////////////////
void processPIR()
{
  if(pir_disable_counter == pir_enable_counter) {
    pir_disable_counter += PIR_DISABLE_INTERVAL;
    dataToSend++;
  }
}

/////////////////////////////////////////////////////////////////
//  Returns:     Nothing.
//  Parameters:  None.
//
//  Description: Watchdog Interrupt Service. This
//               is executed when watchdog times out, 
//               approximately every 8 seconds
/////////////////////////////////////////////////////////////////
ISR(WDT_vect)
{
  if(pir_disable_counter != pir_enable_counter) {
    pir_enable_counter += 1;
  }
  wdt_counter++;
}


/////////////////////////////////////////////////////////////////
//  Returns:     Nothing.
//  Parameters:  None.
//
//  Description: Enters the arduino into sleep mode.
/////////////////////////////////////////////////////////////////
void PowerDownSleep(void)
{
  //power down the radio
  gw.powerDown();
  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);   /* EDIT: could also use SLEEP_MODE_PWR_DOWN for lowest power consumption. */
  sleep_enable();
  
  //turn off peripherals
  volatile uint8_t old_ADCSRA = ADCSRA;
  ADCSRA = 0;
  PRR = 0
      | (1<<PRTWI)     // turn off TWI
      | (1<<PRTIM0)    // turn off Timer/Counter0
      | (1<<PRTIM1)    // turn off Timer/Counter1 (leave Timer/Counter2 on)
      | (1<<PRSPI)     // turn off SPI
      | (1<<PRUSART0)  // turn off USART (will turn on again when reset)
      | (1<<PRADC)     // turn off ADC
      | 0;

  //shut off BOD:
  MCUCR = _BV (BODS) | _BV (BODSE);  // turn on brown-out enable select
  MCUCR = _BV (BODS);        // this must be done within 4 clock cycles of above

  //-------------------------------------------
  // Now enter sleep mode.
  //-------------------------------------------
  sleep_cpu();
  
  // The program will continue from here after the WDT timeout
  // First thing to do is disable sleep.
  sleep_disable(); 
  
  // Re-enable the peripherals.
  power_all_enable();
  ADCSRA = old_ADCSRA;
  PRR = 0
#ifndef ENABLE_SERIAL
      | (1<<PRUSART0)  // turn off USART
#endif //#ifndef ENABLE_SERIAL
      | 0;
}


uint16_t readLightLevel()
{
  uint16_t Result = 0;
  
  pinMode(PHOTO_CELL_PIN, OUTPUT);
  pinMode(PHOTO_CELL_PIN, INPUT);
  
  pinMode(PHOTO_CELL_POWER, OUTPUT);
  digitalWrite(PHOTO_CELL_POWER, HIGH);
  delay(20);
  Result = analogRead(PHOTO_CELL_PIN);

  // Setup the photo cell pins as input pins
  pinMode(PHOTO_CELL_POWER, INPUT);
  // Activate internal pull-down
  digitalWrite(PHOTO_CELL_POWER, LOW);
  digitalWrite(PHOTO_CELL_PIN, LOW);
  
  return Result;
}

void setup()
{
#ifdef ENABLE_SERIAL
  Serial.begin(115200);
  Serial.println("====================================");
  Serial.println("wireless PIR binary switch node v1.0");
  Serial.println("====================================");
  Serial.println("");
#endif //#ifdef ENABLE_SERIAL


  // Setup the button
  pinMode(PIR_ENABLE, OUTPUT);
  // Activate internal pull-up
  digitalWrite(BUTTON_PIN, HIGH);
  

  // Setup the button
  pinMode(BUTTON_PIN, INPUT);
  // Activate internal pull-up
  digitalWrite(BUTTON_PIN, HIGH);
  
  
  // Setup the photo cell pins as input pins
  pinMode(PHOTO_CELL_PIN, OUTPUT);
  pinMode(PHOTO_CELL_POWER, OUTPUT);
  // Activate internal pull-down
  digitalWrite(PHOTO_CELL_PIN, LOW);
  digitalWrite(PHOTO_CELL_POWER, LOW);
  

  // Setup battery level input pin
  pinMode(BATTERY_LEVEL, INPUT);
  
  // Setup the LED1
  pinMode(LED1, OUTPUT);
  // blink it
  digitalWrite(LED1, HIGH);
  delay(100);
  digitalWrite(LED1, LOW);
  
  
  // Set radioId to EEPROM if necessary
  if(0xff == EEPROM.read(EEPROM_RADIO_ID_ADDRESS)) {
    EEPROM.write(EEPROM_RADIO_ID_ADDRESS, NODE_RADIO_ID);
  }

  if(LOW == digitalRead(BUTTON_PIN)) {
    Serial.println("Erasing EEPROM settings");
    EEPROM.write(EEPROM_RADIO_ID_ADDRESS, 0xff);
    EEPROM.write(EEPROM_RELAY_ID_ADDRESS, 0xff);
    EEPROM.write(EEPROM_DISTANCE_ADDRESS, 0xff);
  }


  delay(200);
  gw.begin();
  
  // Register binary input sensor to gw (they will be created as child devices)
  // You can use S_DOOR, S_MOTION or S_LIGHT here depending on your usage. 
  // If S_LIGHT is used, remember to update variable type you send in below.
  gw.sendSensorPresentation(CHILD_ID, S_DOOR);  
  
  //////////////////////////////////////////////////////////////////////////
  //     Setup the WDT interrupt
  //////////////////////////////////////////////////////////////////////////
  // Clear the reset flag.
  MCUSR &= ~(1<<WDRF);
  
  // In order to change WDE or the prescaler, we need to
  // set WDCE (This will allow updates for 4 clock cycles).
  WDTCSR |= (1<<WDCE) | (1<<WDE);

  // set new watchdog timeout prescaler value
  WDTCSR = 1<<WDP0 | 1<<WDP3; // 8.0 seconds 
  
  // Enable the WD interrupt (note no reset).
  WDTCSR |= _BV(WDIE);
  
  
  //////////////////////////////////////////////////////////////////////////
  //     Setup PIR interrupts
  //////////////////////////////////////////////////////////////////////////
  attachInterrupt(0, processPIR, FALLING);

#ifdef ENABLE_SERIAL
  Serial.println("");
  Serial.println("================================");
  Serial.println("Setup complete. Sensor is ready");
  Serial.println("================================");
  Serial.println("");
  delay(200);
#endif //#ifdef ENABLE_SERIAL
  
}


//  Check if digital input has changed and send in new value
void loop() 
{
  //enable/disable PIR
  digitalWrite(PIR_ENABLE, (pir_disable_counter == pir_enable_counter)?1:0);
  
  //send motion detected report
  if(dataToSend != dataSent) {
    // Send in the new value
#ifdef ENABLE_SERIAL
    Serial.println("=================================");
    Serial.println("send motion detected event");
#endif //#ifdef ENABLE_SERIAL
    gw.sendVariable(CHILD_ID, V_TRIPPED, "1");  
    dataSent = dataToSend;
  }
 
  // send regular report
  if(report_counter == wdt_counter) {
    report_counter += REPORT_TIMEOUT;
    
    uint16_t batteryLevel = analogRead(BATTERY_LEVEL);
    uint16_t lightLevel = readLightLevel();
#ifdef ENABLE_SERIAL
    Serial.println("=================================");
    Serial.println("send a report");
    Serial.print("battery level: ");
    Serial.println(batteryLevel);
    Serial.print("light level: ");
    Serial.println(lightLevel);
    Serial.println("go to sleep");
#endif //#ifdef ENABLE_SERIAL

    gw.sendBatteryLevel(batteryLevel);
    gw.sendVariable(CHILD_ID, V_LIGHT_LEVEL, lightLevel);
  }


#ifdef ENABLE_SERIAL
  delay(100);
#endif //#ifdef ENABLE_SERIAL
  
  //power down the sensor till next interrupt
  PowerDownSleep();
  
#ifdef ENABLE_SERIAL
  Serial.println("woke up");
#endif //#ifdef ENABLE_SERIAL

} 

