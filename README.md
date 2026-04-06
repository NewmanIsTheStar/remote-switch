# Remote Switch

## Description
Remote switch for controlling power via a web interface or base on a schedule based on Raspberry Pi Pico2 W. 
- Supports up to 8 relays.
- Provides a web inteface.
- Supports scheduling on/off times.
- Integrates with a home controller that co-ordinates multiple devices

## Installation of tools on Ubuntu Linux
```
sudo apt install git build-essential cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```
## Clone and build the code
```
git clone --recurse-submodules https://github.com/NewmanIsTheStar/remote-switch.git
cd remote-switch
mkdir build
cd build
cmake ..
make
```
Upon completion of a successful build the file remote-switch.uf2 should be created.  This may be loaded onto the Pico2 W by dragging and dropping in the usual manner.

## Initial Configuration
- The Pico will initially create a WiFi network called **pluto**.  Connect to this WiFi network and then point your web browser to http://192.168.4.1
  - Note that many web browsers automatically change the URL from http:// to https:// so if it is not connecting you might need to reenter the URL.
- Set the WiFi country, network and password then hit save and reboot.  The Pico will attempt to connect to the WiFi network.  If it fails then it will fall back to AP mode and you can once again connect to the pluto network and correct your mistakes.  
- Use the GPIO settings page to configure the hardware connections for relays, temperature sensor, display and buttons

## Hardware
- Raspberry Pi Pico W or Pico2 W
- Raspberry Pi Pico Relay Board (1 to 8 relays)

## Licenses
- SPDX-License-Identifier: BSD-3-Clause
- SPDX-License-Identifier: MIT 
