# IMU-Based Gesture Controlled Motor System

An embedded systems project that enables real-time DC motor control through hand gestures using an MPU6050 IMU sensor and an ESP8266 microcontroller. The system is implemented using bare-metal C programming, direct register manipulation, software-based I2C communication, and PWM generation without relying on high-level sensor libraries.

---

## Project Overview

Traditional motor control systems rely on physical switches, joysticks, or other manual interfaces. This project explores an alternative approach using inertial motion sensing to control motor direction through hand gestures.

An MPU6050 sensor detects changes in orientation and provides accelerometer readings to the ESP8266. The microcontroller calculates the pitch angle and compares it against predefined angular thresholds. Based on the detected tilt, appropriate PWM signals are generated to control the direction and operation of a DC geared motor through a motor driver.

The primary focus of this implementation is low-level embedded programming, including direct GPIO register access, bit-banged I2C communication, cycle-accurate timing, and software PWM generation.

## Key Features

- Real-time gesture detection using the MPU6050.
- ESP8266-based embedded control system.
- Bare-metal C programming and direct register-level GPIO manipulation.
- Software-based I2C communication without external sensor libraries.
- Accelerometer-based pitch angle calculation.
- Reference-angle calibration during initialization.
- Threshold-based gesture recognition (±20°).
- Software PWM generation using the ESP8266 CCOUNT register.
- Bidirectional DC motor control.
- Hardware implementation and simulation.

---

## Hardware Components

| Component | Function |
|---|---|
| ESP8266 (ESP-12E) | Main microcontroller |
| MPU6050 | 6-axis inertial measurement sensor |
| BTS7960 Motor Driver | Bidirectional motor control |
| DC Geared Motor | Mechanical actuation |
| Power Supply | Provides power to the system |
| Buck Converter | Voltage regulation |
| Connecting Wires / PCB | Hardware interfacing |

---

## Software and Technologies

- **Programming Language:** Embedded C
- **Microcontroller:** ESP8266
- **Sensor Communication:** I2C (Bit-Banged)
- **Motor Control:** Software PWM
- **GPIO Configuration:** Direct Register Access
- **Timing Mechanism:** ESP8266 CCOUNT Register
- **Sensor:** MPU6050

---

## System Architecture

The system operates through the following sequence:

1. **Motion Detection:** The MPU6050 detects changes in hand orientation using its accelerometer.
2. **Sensor Communication:** The ESP8266 retrieves accelerometer readings through software-implemented I2C communication.
3. **Angle Calculation:** The microcontroller calculates the pitch angle using the Y-axis and Z-axis acceleration values.
4. **Gesture Recognition:** The calculated angle is compared against predefined thresholds of +20° and -20°.
5. **PWM Generation:** Based on the detected gesture, the ESP8266 generates PWM signals.
6. **Motor Actuation:** The motor driver receives the control signals and drives the DC geared motor in the corresponding direction.

### Block Diagram

<img width="1600" height="856" alt="image" src="https://github.com/user-attachments/assets/eb81ce17-d2b5-46d4-b0e1-bd6e30869ad1" />


---

## Working Principle

### 1. MPU6050 Interfacing

The MPU6050 communicates with the ESP8266 through I2C communication.

The implementation uses bit-banged I2C, where communication signals are generated manually through GPIO control rather than using a hardware I2C peripheral.

The following operations are implemented:

- I2C START and STOP conditions.
- Byte transmission and reception.
- ACK/NACK detection.
- MPU6050 register read and write operations.

The MPU6050 is initialized by writing to its Power Management Register (`0x6B`) to bring the sensor out of sleep mode.

### 2. Pitch Angle Calculation

The accelerometer readings are used to estimate the tilt angle.

The pitch angle is calculated using:

\[
\theta = \tan^{-1}\left(\frac{a_y}{a_z}\right)
\]

In the implementation, the `atan2()` function is used to calculate the angle in degrees.

A reference angle is recorded during system
