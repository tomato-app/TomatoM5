# Tomato M5

M5STACK FOR TOMATO AND NIGHTSCOUT.


## Features
- Support Night scout and Tomato server as a data source to display realtime BG data and send alarms
- Support Setting the config via web page
- Support OAT to update the firmware

## WIP: Instruction to building 

### Build from the source code using PlatformIO
#### 0. Install [Vs Code](https://code.visualstudio.com/) amd [PlatformIO IDE for VSCode](https://platformio.org/install/ide?install=vscode).
#### 1. Clone the source code .
`git clone  https://github.com/tomato-app/TomatoM5.git`
#### 2. Enter the path and open it with the Vs Code .
```
cd TomatoM5
code  .
```
#### 23. Build the source code and upload it to the device using PlatformIO in PlatformIO CLI.
```
 pio run -v -t upload
 pio run -t uploadfs
```
### Build with the flash download tool from ESP
### Build with the M5Burner

## WIP: Instruction to use

### links

- M5stack
- [MiaoMiao Smart Reader for Freestyle Libre](https://miaomiao.cool/?source=github)
- [Tomato App](http://tomato.cool)
- [Nightscout](https://github.com/nightscout/cgm-remote-monitor)

This project refers to the [M5_NightscoutMon](https://github.com/mlukasek/M5_NightscoutMon)