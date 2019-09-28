# OpenIndoor Opto-TRIAC Raspberry Pi HAT driver

User-mode and Kernel-mode driver for Opto-TRIAC HAT



## Getting Started

Board can be purchased online:

* [eBay](https://www.ebay.com/itm/113873656775) - International buyers
* [MercadoLibre](https://articulo.mercadolibre.com.ar/MLA-812350924-hat-shield-raspberry-pi-quad-opto-triac-220v-_JM) - Argentina


**Opto-TRIAC board is not a toy for begginers. Despite all precautory measures taken into account during design stage, board works with AC mains. So, improper mounting or touching live parts of the board while it is plugged in can lead to electrocution, serious injury or death.**




### Prerequisites

* It is expected that you already have some experience with Raspberry Pi.
* Only 40-pin GPIO header supported. HAT will not work with old 26-pin header.
* Due to multi-threaded Kernel module driver, only Quad-core ARM processors are supported, that means this driver will only work on Raspberry Pi 2 model B or better.


Installation support is based on official Raspbian Buster Lite image. It can be downloaded on the following link:

https://downloads.raspberrypi.org/raspbian_lite_latest


For instructions on how to write the image to the SD card read the following link:

https://www.raspberrypi.org/documentation/installation/installing-images/README.md


**WAIT!!:** Before booting the Raspi with your freshly downloaded Raspbian image, we need to do some minor changes.

Mount the SD card on your PC/Laptop computer and create an empty file named `ssh` inside `boot` partition of SD card. If that file is not present, SSH won't be enabled on boot time, so we will have no access to our headless Raspi.



Ensure your Raspi has a working Internet connection. I prefer cabled one so don't have to mess with WiFi passwords.

Boot your Raspi and wait till it finishes doing all stuff (rezising partition, etc).

You will need to know your Raspberry Pi IP address. Either assign a static IP based on Raspi MAC address or check your router's DHCP pool to see what IP got assigned.

Login (SSH) to your Raspi and perform packages update and upgrades (Reboot after upgrade):

```
sudo apt update && sudo apt upgrade
```


**NOTE:** Do not perform `rpi-update`. There is no need to do that, and it can f*ck up Kernel


### Installing

#### Hardware
Power off Raspi.
Mount Opto-TRIAC HAT on top of Raspberry Pi. Use supplied 40-pin header extender, 20mm nylon hex-spacers, nylon nuts and nylon bolts. **DO NOT USE metallic spacers, or shorter ones.**
You can also mount HAT next to Raspberry Pi, connecting them with a 40-pin ribbon cable.

Power on Raspberry Pi. Login again and check if HAT was properly autodetected:

```
cat /proc/device-tree/triacboard/product
```
Output should read:

```
Quad Opto-TRIAC board
```

If `triacboard/product` does not exist, it means HAT was not detected during boot-up.
Things to check in that case:

1) Are **JSDA1** and **JSCL1** on place?

2) If still not working, check this link: https://www.raspberrypi.org/forums/viewtopic.php?f=29&t=108134

3) Check that no other peripherals are messing with I²C-0 bus

4) Check for proper HAT header contact

5) Check if EEPROM is properly soldered to HAT PCB


#### Software
You will need to compile from sources, sorry for that. But it's quite easy.


Install Kernel headers and build tools:

```
sudo apt install raspberrypi-kernel-headers build-essential git
```

Now clone Git repository:

```
git clone https://github.com/vpreatoni/triacd.git

cd triacd/
```

Start building the easy part: the user-mode `triacd` daemon (Install script will also create a `systemd` entry, so everytime your Raspi starts up, daemon will be automatically loaded):

```
make

sudo make install
```



Now the hard thing, kernel modules (do not proceed to `sudo make install` if `make` was not successfull. Module compilation must be a clean and straight forward process, otherwise you can f*ck up things):

```
cd modules

make

sudo make install

```


## Running the tests

Plug in your HAT to AC mains.
Reboot Raspi with `sudo reboot` command. After boot up, login (SSH) again and run:

```
dmesg
```

Last lines should read something like:

```
[   12.232407] AC LINE: optocoupler hysteresis = 289us
[   12.232457] AC LINE: ready
[   19.009843] TRIAC1: GPIO 06 - /sys/triacd/TRIAC1
[   19.010330] TRIAC1: ready
[   19.839931] TRIAC2: GPIO 13 - /sys/triacd/TRIAC2
[   19.840728] TRIAC2: ready
[   19.876487] TRIAC3: GPIO 19 - /sys/triacd/TRIAC3
[   19.880765] TRIAC3: ready
[   19.912769] TRIAC4: GPIO 26 - /sys/triacd/TRIAC4
[   19.913193] TRIAC4: ready
```
That means your HAT was successfully detected on boot-up! 

- AC LINE phase feedback input was detected and optocoupler successfully calibrated
- TRIAC1 to 4 outputs were detected, and they are user-accesible on `/sys/triac/TRIAC1-4` sysfs node


## Using `triacd` daemon

`triacd` daemon should automatically start every time Raspi boots-up.

To test if daemon is running:

```
service triacd status
```

Output:

```
● triacd.service - Opto-TRIAC board daemon
Loaded: loaded (/etc/systemd/system/triacd.service; enabled; vendor preset: enabled)
Active: active (running) since Sat 2019-09-28 00:04:46 -03; 20min ago
Main PID: 279 (triacd)
Tasks: 1 (limit: 2200)
Memory: 948.0K
CGroup: /system.slice/triacd.service
└─279 /usr/local/bin/triacd

Sep 28 00:04:46 raspberrypi systemd[1]: Started Opto-TRIAC board daemon.
```

To stop daemon:

```
service triacd stop
```

To start it again:

```
service triacd start
```

### Sending commands to daemon
`triacd` daemon can be used in stand-alone mode to send commands thru a message queue to running daemon.
Stand-alone executable do not need root privileges to send commands, so any high level API can call `triacd` with apropiate parameters to control TRIAC channels.

For usage info, run `triacd -?`

Examples:

```
triacd -c4 -f -t5000 -p110			to start fading channel 4 for 5sec up to 110deg
triacd -c1 -p110 -n30				to set channel 1 to 110deg positive / 30deg negative
triacd -c2							to turn off channel 2
triacd -c3 -t3000					to turn off channel 3 after 3sec			**TODO, still not working
triacd -c1 -t20000 -p180			to fully turn on channel 1 after 20sec		**TODO, still not working
```

## Contributing and bug reporting

Please contact me at "my GitHub user" at gmail dot com


## Authors

* **Victor Preatoni** - https://github.com/vpreatoni


## License

This project is licensed under the GPL License

