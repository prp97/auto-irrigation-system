#### WiFi credentials

Set Wifi credential and Mosquitto broker in `./main/Kconfig.projbuild` for each node.


#### Load environment variables

```bash
source ~/sed/esp-idf/export.sh
```

#### Create project

```bash
cd ~/sed/esp-workspace/auto-irrigation-system
idf.py --version # Must be 5.5.3
idf.py create-project sensor-node
cd sensor-node
```

```bash
cd ~/sed/esp-workspace/auto-irrigation-system
idf.py --version # Must be 5.5.3
idf.py create-project actuator-node
cd actuator-node
```

#### Add sensors dependencies

```bash 
idf.py add-dependency "esp-idf-lib/si7021"
idf.py add-dependency "esp-idf-lib/i2cdev"
```

#### Install nodes

```bash
cd ~/sed/esp-idf
./install.sh esp32,esp32c3
```

#### Connections 

| Sensor | ESP32 C3 Pin | Función |
| --- | --- | --- |
VCC/ VIN | 3V3| Energía |
GND | GND | Tierra |
SDA | GPIO 21  / 10 | Datos |
SCL | GPIO 22 / 8 | Reloj |

#### Build and flash for sensor node

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```


#### Build and flash for actuator node

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

#### Mosquitto
```bash

# Publisher
mosquitto_sub -h test.mosquitto.org -t "sed/G03/sensor/temp" -v

# Subscriber
mosquitto_pub -h test.mosquitto.org -t "sed/G03/actuador/led" -m "ON"
```

#### Mosquitto alternative 
```bash
# Publisher
mosquitto_sub -h broker.hivemq.com -t "sed/G03/sensor/temp" -v

# Subscriber
mosquitto_pub -h broker.hivemq.com -t "sed/G03/actuador/led" -m "ON"
``` 

#### Node Red

###### Docker install
```bash
sudo apt update
sudo apt install docker.io
sudo usermod -aG docker $USER
```

###### Create Node Red container
```bash
mkdir -p ~/sed/node-red/data
cd ~/sed/node-red
docker run -it -p 1880:1880 -v ./data:/data --name mynodered nodered/node-red
```

###### Node Red mangement

```bash
docker stop mynodered
docker start mynodered
docker logs --tail=100 -f mynodered
```

###### Dashboard and flow

URL: http://127.0.0.1:1880/

Dashboard: http://127.0.0.1:1880/dashboard/page2 


##### Over-The-Air Update with Mender

```bash
sudo apt install mender-artifact docker-compose-v2

mkdir -p external/mender-mcu-client

```

Descargamos el repositorio: 
```bash
git clone --branch 0.12.3 --recursive https://github.com/joelguittet/mender-mcu-client.git external/mender-mcu-client/
```

Editamos el CMakeList.txt raíz incluyendo la orden list:
```bash 
...
cmake_minimum_required(...)
list(APPEND EXTRA_COMPONENT_DIRS "external/mender-mcu-client/esp-idf")
```

###### idf.py menuconfig 

En el menuconfig, Component Config, Mender Firmware ..., General
Configuration, hacemos:
Mender Server Host URL: https://eu.hosted.mender.io
Mender Servar Tenant Token: el que hemos obtenido anteriormente.

```
Bootloader config -> Application Rollback -> Activar Enable app rollback support.
• Serial Flasher Config -> Flash size -> 4 MB
• Partition Table -> Partition Table -> (X) Custom
• Partition Table -> Custom partition ... -> partitions.csv
``` 

Navega a Component config → ESP-TLS.
3. Marca la opción [*] Allow potentially insecure options.
4. Justo debajo, marca [*] Skip server certificate verification by default

####### Create `partitions.csv` in the project root with the following content:

#Name,    Type,  SubType, Offset, Size,    Flags
nvs,      data,  nvs,           , 0x4000,
otadata,  data,  ota,           , 0x2000,
phy_init, data,  phy,           , 0x1000,
ota_0,    app,   ota_0,         , 1500K,
ota_1,    app,   ota_1,         , 1500K,  


####### Mender Artifact creation

######## Actuator node
```bash
mender-artifact write rootfs-image \
--compression none \
--device-type esp32 \
--artifact-name a_1.0.1 \
--file build/actuator-node.bin \
--output-path a_1.0.1.mender
```

######## Sensor node
```bash
mender-artifact write rootfs-image \
--compression none \
--device-type esp32c3 \
--artifact-name s_1.0.1 \
--file build/sensor-node.bin \
--output-path s_1.0.1.mender
```
