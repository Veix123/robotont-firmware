#include "mbed.h"
#include "motor.h"
#include "odom.h"
#include "DS1820.h"
#include "VL53L0X.h"
#include <sstream>
#include <vector>

// Common parameters for all motors
#define ENC_CPR 64
#define GEAR_RATIO 18.75
#define WHEEL_RADIUS 0.035
#define WHEEL_POS_R 0.127
#define PID_KP 0.8
#define PID_TI 0.05
#define PID_TD 0.0
#define PID_DELTA_T 0.01
#define MAIN_DELTA_T 0.02

#define MAX_CMD_ARGS 5
#define MOTOR_COUNT 3
#define CMD_TIMEOUT_MS 1000 // If velocity command is not received within this period all motors are stopped.

// Include motor configurations
//#include "motor_config_v0_6.h"
#include "motor_config_v1_1.h"
// Include LED STRIP Library
//#include <FastLED.h>
//#define LED_PIN	7 		
//#define NUM_LEDS 11
//#define CHIPSET WS2812
//CRGB leds[NUM_LEDS];

//CONFIGURE i2c pins!
I2C i2c(PB_9, PB_8);

// Initialize motors
Motor m[] = { { cfg0 }, { cfg1 }, { cfg2 } };

// Initialize odometry
Odom odom_(cfg0, cfg1, cfg2, MAIN_DELTA_T);

// Timeout
Timer cmd_timer, main_timer;
Ticker cmd_timeout_checker;
VL53L0X     sensor(i2c, main_timer);
#define HIGH_SPEED
// Variables for serial connection
RawSerial serial_pc(USBTX, USBRX);  // tx, rx
char serial_buf[256];        // Buffer for incoming serial data
volatile uint8_t serial_arrived = 0;  // Number of bytes arrived
volatile bool packet_received_b = false;

// For parsing command with arguments received over serial
std::vector<std::string> cmd;

// This method processes a received serial packet
void processPacket(const std::string& packet)
{
  std::istringstream ss(packet);
  std::string arg;
  cmd.clear();

  for (int i = 0; i <= MAX_CMD_ARGS; i++)
  {
    arg.clear();
    std::getline(ss, arg, ':');
    if (arg.length())
    {
      cmd.push_back(arg);
      //serial_pc.printf("Got arg %s\r\n", arg.c_str());
    }
    else
    {
      break;
    }
  }

  if (!cmd.size())
  {
    return;
  }

  // MS - Set motor speeds manually (linear speed on wheel m/s)
  /* MS:motor1_speed:motor2_speed:motor3_speed */
  if (cmd[0] == "MS")
  {
    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
    {
      float speed_setpoint = std::atof(cmd[i + 1].c_str());
      //serial_pc.printf("Setpoint %d, %f\r\n", i, speed_setpoint);
      m[i].setSpeedSetPoint(speed_setpoint);
    }
    cmd_timer.reset();
  }

  // RS - Set motor speeds based on robot velocities. We use ROS coordinate convention: x-forward,
  // y-left, theta-CCW rotation.
  /* RS:robot_speed_x(m/s):robot_speed_y(m/s):robot_speed_theta(rad/s) */
  else if (cmd[0] == "RS")
  {
    float lin_speed_x = std::atof(cmd[1].c_str());
    float lin_speed_y = std::atof(cmd[2].c_str());
    float angular_speed_z = std::atof(cmd[3].c_str());

    float lin_speed_dir = atan2(lin_speed_y, lin_speed_x);
    float lin_speed_mag = sqrt(lin_speed_x * lin_speed_x + lin_speed_y * lin_speed_y);

    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
    {
      float speed = lin_speed_mag * sin(lin_speed_dir - m[i].getWheelPosPhi()) +
                    m[i].getWheelPosR() * angular_speed_z;
      if (abs(speed) < 1e-5)
      {
        m[i].stop();
      }
      else
      {
        m[i].setSpeedSetPoint(speed);
      }
    }
    cmd_timer.reset();
  }
  else if (cmd[0] == "PID")  // Update PID parameters
  {
    float k_p = 0.0f;
    float tau_i = 0.0f;
    float tau_d = 0.0f;
    // sscanf(ss.str().c_str(), "%f:%f:%f", &k_p, &tau_i, &tau_d);
    // for (uint8_t i = 0; i < 3; i++)
    //{
    //  m[i].setPIDTunings(k_p, tau_i, tau_d);
    //}
  }
}

// Process an incoming serial byte
void pc_rx_callback()
{
  // Store bytes from serial in our buffer until packet
  // termination byte 'enter', '\n', '\r' etc has arrived
  while (serial_pc.readable())
  {
    char c = serial_pc.getc();
    serial_buf[serial_arrived++] = c;
    serial_buf[serial_arrived] = '\0';
    if (serial_arrived > 254)
    {
      serial_arrived = 0;
    }

    if (c == '\n' || c == '\r')  // command terminated
    {
      if (serial_arrived > 3)
      {
        // signal that the packet is complete for processing
	packet_received_b = true;
      }

    }

    // if escape is received, clear the buffer and stop the motors for now
    if (c == 27)  // esc
    {
      for (uint8_t i = 0; i < MOTOR_COUNT; i++)
      {
        m[i].stop();
      }
      serial_buf[0] = '\0';
      serial_arrived = 0;
    }
  }
}

void check_for_timeout()
{
  if ((cmd_timer.read_ms()) > CMD_TIMEOUT_MS)
  {
    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
    {
      m[i].stop();
    }
  }
}

void i2cScanner() 
{
    serial_pc.printf("\nI2C Scanner\r\n");
    i2c.frequency(100000);
    while(1) {
        int error, address;
        int nDevices;
 
        serial_pc.printf("Scanning...\r\n");
 
         nDevices = 0;
 
          for(address = 1; address < 127; address++ )
          {
            //i2c.start();
            error = i2c.write(address << 1, NULL, 0); //We shift it left because mbed takes in 8 bit addreses
            //i2c.stop();
            if (error == 0)
            {
              serial_pc.printf("I2C device found at address 0x%X\r\n", address); //Returns 7-bit addres
              nDevices++;
	      //break;
            }
            if (error == 2) {
              serial_pc.printf("TIMEOUTED...\r\n");
            }
 
          }
          if (nDevices == 0)
            serial_pc.printf("No I2C devices found\r\n");
          else
            serial_pc.printf("\ndone\r\n");
        }
}

void i2cGPIOexpanderConfigure() 
{
    serial_pc.printf("\nSIIIIIIIIIIIIIIIIIIIIIIIIIN\r\n");
    i2c.frequency(100000);
    char conf_reg[2] = {0x03, 0x00}; 
    int error = i2c.write(0x20 << 1, conf_reg, sizeof(conf_reg));
    serial_pc.printf("Result: %s\n", (error == 0?"ACK \r\n":"NAK \r\n"));
    char conf_reg2[2] = {0x01, 0b00001111}; //Last 4bits for PIN configure
    error = i2c.write(0x20 << 1, conf_reg2, sizeof(conf_reg2));
    serial_pc.printf("Result: %s\n", (error == 0?"ACK \r\n":"NAK \r\n"));
}

//void initLEDS() {
  //FastLED.addLeds<CHIPSET, LED_PIN, GRB>(leds, NUM_LEDS);
  //FastLED.setBrightness(128);

  //leds[0] = CRGB(255,0,0); //punane
  //leds[1] = CRGB(255,0,0); //punane
  //leds[2] = CRGB(255,0,0); //punane
  //leds[3] = CRGB(255,0,0); //punane
  //leds[4] = CRGB(255,0,0); //punane
  //FastLED.show();
//}

void readSensorValue() 
{ 
   serial_pc.printf("Start...\r\n");
    sensor.init();
    serial_pc.printf("Initialisation completed!\r\n");
    sensor.setTimeout(500);
#if defined LONG_RANGE
  // lower the return signal rate limit (default is 0.25 MCPS)
  sensor.setSignalRateLimit(0.1);
  // increase laser pulse periods (defaults are 14 and 10 PCLKs)
  sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
  sensor.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);
#endif

#if defined HIGH_SPEED
  // reduce timing budget to 20 ms (default is about 33 ms)
  sensor.setMeasurementTimingBudget(20000);
#elif defined HIGH_ACCURACY
  // increase timing budget to 200 ms
  sensor.setMeasurementTimingBudget(200000);
#endif
    while (1)
    {
        serial_pc.printf("%u\r\n", sensor.readRangeSingleMillimeters());
        if (sensor.timeoutOccurred())
        {
            serial_pc.printf("TIMEOUT!\r\n");
        }
    }

}

int main()
{ 
  // Initialize serial connection
  serial_pc.baud(115200);
  serial_buf[0] = '\0';
  serial_pc.attach(&pc_rx_callback);
  serial_pc.printf("**** MAIN ****\r\n");

  cmd_timeout_checker.attach(check_for_timeout, 0.1);
  cmd_timer.start();
  //SCAN i2c devices!
  //i2cScanner();
  //configure gpio expander pins
  //i2cGPIOexpanderConfigure();
  readSensorValue();
  //initLEDS();

  // MAIN LOOP
  while (true)
  {
    main_timer.reset();
    main_timer.start();
    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
    {
      // MOTOR DEBUG
      // serial_pc.printf("\r\n");
//      serial_pc.printf("MOTOR %d: \r\n", i);
//      serial_pc.printf("Speed[%d]: %f (%f): \r\n", i, m[i].getMeasuredSpeed(),
//                       m[i].getSpeedSetPoint());
//      // serial_pc.printf("Effort: %f: \r\n", m[i].getEffort());
//      serial_pc.printf("Fault: %u: \r\n", m[i].getFaultPulseCount());
//      // serial_pc.printf("Temp: %f: \r\n", m[i].getTemperature());
//      serial_pc.printf("Current[%d]: %f: \r\n", i, m[i].getCurrent());
    }

//    serial_pc.printf("Serial arrived: %d\r\n", serial_arrived);
    
    if (packet_received_b) // packet was completeted with \r \n
    {
      std::string packet(serial_buf);
      serial_buf[0] = '\0';
      serial_arrived = 0;
      processPacket(packet);
      packet_received_b = false;
    }
    
    // Update odometry
    odom_.update(m[0].getMeasuredSpeed(), m[1].getMeasuredSpeed(), m[2].getMeasuredSpeed());
    serial_pc.printf("ODOM1:%f:%f:%f:%f:%f:%f\r\n", odom_.getPosX(), odom_.getPosY(),
                     odom_.getOriZ(), odom_.getLinVelX(), odom_.getLinVelY(), odom_.getAngVelZ());
    // Synchronize to given MAIN_DELTA_T
    wait_us(MAIN_DELTA_T*1000*1000 - main_timer.read_us());
  }
}
