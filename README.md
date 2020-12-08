# Tomato M5

M5STACK FOR TOMATO AND NIGHTSCOUT.
![img](docs/Tomato%20M5%20Munual%2073ca32c36b874e46a59190df1c112ff0/IMG_4827.JPG)

## Features
- Support Night scout and Tomato server as a data source to display realtime BG data and send alarms
- Support Setting the config via web page
- Support OAT to update the firmware

## Instruction to building 

### Build from the source code using PlatformIO
#### 0. Install [Vs Code](https://code.visualstudio.com/) amd [PlatformIO IDE for VSCode](https://platformio.org/install/ide?install=vscode).
#### 1. Clone the source code .
`git clone  https://github.com/tomato-app/TomatoM5.git`
#### 2. Enter the path and open it with the Vs Code .
```
cd TomatoM5
code  .
```
#### 3. Build the source code and upload it to the device using PlatformIO in PlatformIO CLI.
```
 pio run -v -t upload
 pio run -t uploadfs
```
### WIP: Build with the flash download tool from ESP
### WIP: Build with the M5Burner

## Instruction to use
- [Tomato M5 Manual](docs/TomatoM5Munual.md)
- [Tomato M5 使用说明](docs/TomatoM5使用说明.md)
### links

- M5stack
- [MiaoMiao Smart Reader for Freestyle Libre](https://miaomiao.cool/?source=github)
- [Tomato App](http://tomato.cool)
- [Nightscout](https://github.com/nightscout/cgm-remote-monitor)

This project refers to the [M5_NightscoutMon](https://github.com/mlukasek/M5_NightscoutMon)