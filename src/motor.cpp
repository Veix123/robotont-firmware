#include "mbed.h"
#include "motor.h"
#include <algorithm>

Motor::Motor(const MotorConfig& cfg)
  : dir1_(cfg.pin_dir1)
  , dir2_(cfg.pin_dir2)
  , pwm_(cfg.pin_pwm)
  , enc_(cfg.pin_enca, cfg.pin_encb, NC, cfg.enc_cpr, QEI::X4_ENCODING)
  , fault_(cfg.pin_fault)
  , pid_(cfg.pid_k_p, cfg.pid_tau_i, cfg.pid_tau_d, cfg.pid_dt)
  , temp_sensor_(NULL)
  , status_(STATUS_UNINITIALIZED)
{

  // initialize pid to defaults
  pid_.setInputLimits(-1.0, 1.0);  // set default limits to 10 cm/s
  pid_.setOutputLimits(-1.0, 1.0);
  pid_.setBias(0.0);
  pid_.setMode(1);

  // Calculate the relation between an encoder pulse and 
  // linear speed on the wheel where it contacts the ground
  pulse_to_speed_ratio_ =
      1.0f / cfg.enc_cpr / cfg.gear_ratio * 2 * M_PI / cfg.pid_dt * cfg.wheel_radius;

  // Check for the temperature sensor and initialize if connected
  if (cfg.pin_temp != NC)
  {
    temp_sensor_ = new DS1820(cfg.pin_temp);

    int retries = 5;
    if (temp_sensor_)
    {
      while (!temp_sensor_->begin() && retries > 0)
      {
        printf("Temp sensor not found retrying... %d", retries);
        retries--;
      }
      if (retries)
      {
        temp_sensor_->startConversion();
      }
    }
  }

  // Attach the PID ISR ticker
  pidTicker_.attach(callback(this, &Motor::processPID), cfg.pid_dt);

  // store config in case we need it later.
  config_ = cfg; 
}

Motor::~Motor()
{
  delete temp_sensor_;
  temp_sensor_ = NULL;
}


// Stop the motor
void Motor::stop()
{
  setSpeedSetPoint(0);
  stopped_ = true;
  dir1_ = 0;
  dir2_ = 0;
}

void Motor::setEffortLimit(float effort_limit)
{
  effort_limit_ = std::max(std::min(effort_limit, 1.f), 0.f); 
  pid_.setOutputLimits(0, effort_limit_);
}

void Motor::setPIDTunings(float k_p, float tau_i, float tau_d)
{
  pid_.setTunings(k_p, tau_i, tau_d);
}

// Limit angular velocity (in rads/s)
void Motor::setSpeedLimit(float speed_limit)
{
  speed_limit_ = speed_limit;
  pid_.setInputLimits(-speed_limit_, speed_limit_);
}

void Motor::setEffort(float effort)
{
  if (stopped_)
  {
    return;
  }

  // limit value to [-1...1]
  effort_ = std::max(std::min(effort, 1.f), -1.f); 

  if (effort_ == 0)
  {
    stop();
  }
  else if (effort_ > 0)
  {
    dir1_ = 1;
    dir2_ = 0;
    pwm_ = effort_;
  }
  else
  {
    dir1_ = 0;
    dir2_ = 1;
    pwm_ = -effort_;
  }
}

void Motor::setSpeedSetPoint(float speed)
{
  if (stopped_)
  {
    pid_.reset();
    stopped_ = false;
  }
  speed_setpoint_ = speed;
  pid_.setSetPoint(speed_setpoint_);
}


void Motor::processPID()
{
  speed_measured_ = enc_.getPulses() * pulse_to_speed_ratio_;
  enc_.reset();
  pid_.setProcessValue(speed_measured_);
  setEffort(pid_.compute());
}

float Motor::getTemperature()
{
  if (!temp_sensor_)
    return 0.0f;

  float temp = 0.0f;
  if (temp_sensor_->isPresent())
  {
    int retries = 5;
    while (temp_sensor_->read(temp) != 0 && retries > 0)
    {
      retries--;
    }
    temp_sensor_->startConversion();  // start temperature conversion
  }
  return temp;
}