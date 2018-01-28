///////////////////////////////////////////////////////////////////////////////////////
//Terms of use
///////////////////////////////////////////////////////////////////////////////////////
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.
///////////////////////////////////////////////////////////////////////////////////////


// https://github.com/richardFirth/SelfBalanceRobot

// remix of joop brokkings thing. 
// http://www.brokking.net/yabr_main.html

// https://www.youtube.com/watch?v=6WWqo-Yr8lA
// https://www.youtube.com/watch?v=VxpMWncBKZ
// https://www.youtube.com/watch?v=mG4OtAiY_wU


#include <SoftwareSerial.h>  // RF
#include <EEPROM.h> // PID Settings are stored in EEProm
#include <Wire.h>                                            //Include the Wire.h library so we can communicate with the gyro

int gyro_address = 0x68;                                     //MPU-6050 I2C address (0x68 or 0x69)

#define XBEE_RX 2   //RX Pin For Software Serial
#define XBEE_TX 3   //TX Pin For Software Serial

#define LEFT_DIR 4   //direction
#define LEFT_STP 5   //step
#define RIGHT_DIR 6   //direction
#define RIGHT_STP 7   //step

#define ENABLE 8   //
#define PUSH_BUTTON 9   //

#define VBATT A0
#define CALIB_LED A1   

#define EDIT_MODE 10   //
#define P_GAIN 13   //
#define I_GAIN 12   //
#define D_GAIN 11   //

#define lowVoltageSetting 693 // 11.1 lowV
// #define R_Batt 10000.0 resistor from battery to pin
// #define R_GND 4700.0    resistor from pin to gnd



SoftwareSerial XBee(XBEE_RX,XBEE_TX); // RX, TX
boolean UP_BUTTON, DOWN_BUTTON, LEFT_BUTTON, RIGHT_BUTTON,L_TRIG,R_TRIG,JOYSTICK_BUTTON;
int verticalValue = 512, horizontalValue=512;
boolean JoystickBatteryGood;

struct operatingValues {
  int acc_calibration_value;                            //Enter the accelerometer calibration value (default: 1000)
  float pid_p_gain;                                       //Gain setting for the P-controller (default: 15)
  float pid_i_gain;                                      //Gain setting for the I-controller (default: 1.5)
  float pid_d_gain;                                       //Gain setting for the D-controller (default: 30)
  float turning_speed;                                    //Turning speed (default: 30)
  float max_target_speed;                                //Max target speed (default: 150)
};

operatingValues myOperatingValues;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Declaring global variables
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
boolean startProg;
boolean timingProblem;

int left_motor, throttle_left_motor, throttle_counter_left_motor, throttle_left_motor_memory;
int right_motor, throttle_right_motor, throttle_counter_right_motor, throttle_right_motor_memory;

long gyro_yaw_calibration_value, gyro_pitch_calibration_value;

unsigned long loop_timer;

float angle_gyro, angle_acc, angle, self_balance_pid_setpoint;
float pid_error_temp, pid_i_mem, pid_setpoint, gyro_input, pid_output, pid_last_d_error;
float pid_output_left, pid_output_right;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Setup basic functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup(){
  Serial.begin(9600);                                                       //Start the serial port at 9600 kbps
  Serial.println(F("Balancing_robot_Attempt2"));
  Serial.println(F("Last Modified Jan 27 2018"));
  
  Wire.begin();                                                             //Start the I2C bus as master
  TWBR = 12;                                                                //Set the I2C clock speed to 400kHz
  XBee.begin(9600);

  setPinModes();
  
  digitalWrite(ENABLE,HIGH); // stop current through steppers
  
  checkForResetValues(); // if you hold the pushbutton at power up it resets myOperatingValues to default
  loadOperatingValues();
  
  setupTimer();         // set the arduino timing registers
  setGyroRegisters();  // set the registers via I2C
  GyroCalibration();  // calibrate gyro
  
  loop_timer = micros() + 4000; //Set the loop_timer variable at the next end loop time

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Main program loop
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop(){
   getLatestXBeeData();

   checkBatteryVoltage(analogRead(VBATT));
   angleCalculation();

   if(startProg){
        balancingLoop();  
   } else {
        atRestLoop();
   }
}


void balancingLoop() // when the robot is balanced do this loop
{
 PIDCalculations();
 controlCalculation();
 MotorPulseCalc();
 loopTimer();
}

void atRestLoop() // when the robot is tipped over do this loop
{
    delay(10);
    if (JOYSTICK_BUTTON) SetAllLEDS(1,0,0,0,0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Interrupt routine  TIMER2_COMPA_vect
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ISR(TIMER2_COMPA_vect){
  //Left motor pulse calculations
  throttle_counter_left_motor ++;                                           //Increase the throttle_counter_left_motor variable by 1 every time this routine is executed
  if(throttle_counter_left_motor > throttle_left_motor_memory){             //If the number of loops is larger then the throttle_left_motor_memory variable
    throttle_counter_left_motor = 0;                                        //Reset the throttle_counter_left_motor variable
    throttle_left_motor_memory = throttle_left_motor;                       //Load the next throttle_left_motor variable
    if(throttle_left_motor_memory < 0){                                     //If the throttle_left_motor_memory is negative
      PORTD &= 0b11101111;                                                  //Set output 4 low to reverse the direction of the stepper controller
      throttle_left_motor_memory *= -1;                                     //Invert the throttle_left_motor_memory variable
    }
    else PORTD |= 0b00010000;                                               //Set output 4 high for a forward direction of the stepper motor
  }
  else if(throttle_counter_left_motor == 1)PORTD |= 0b00100000;             //Set output 5 high to create a pulse for the stepper controller
  else if(throttle_counter_left_motor == 2)PORTD &= 0b11011111;             //Set output 5 low because the pulse only has to last for 20us 
  
  //right motor pulse calculations
  throttle_counter_right_motor ++;                                          //Increase the throttle_counter_right_motor variable by 1 every time the routine is executed
  if(throttle_counter_right_motor > throttle_right_motor_memory){           //If the number of loops is larger then the throttle_right_motor_memory variable
    throttle_counter_right_motor = 0;                                       //Reset the throttle_counter_right_motor variable
    throttle_right_motor_memory = throttle_right_motor;                     //Load the next throttle_right_motor variable
    if(throttle_right_motor_memory < 0){                                    //If the throttle_right_motor_memory is negative
       PORTD &= 0b10111111;                                                  //Set output 6 low to reverse the direction of the stepper controller
      throttle_right_motor_memory *= -1;                                    //Invert the throttle_right_motor_memory variable
    }
    else PORTD |= 0b01000000;                                               //Set output 6 high for a forward direction of the stepper motor
  }
  else if(throttle_counter_right_motor == 1)PORTD |= 0b10000000;            //Set output 7 high to create a pulse for the stepper controller
  else if(throttle_counter_right_motor == 2)PORTD &= 0b01111111;            //Set output 7 low because the pulse only has to last for 20us
}


