#### Load environment variables

```bash
source ~/sed/esp-idf/export.sh
```

#### Create project

```bash
cd ~/sed/esp-workspace
idf.py --version # Must be 5.5.3
idf.py create-project auto-irrigation-system
cd auto-irrigation-system
idf.py set-target esp32
```

#### Add sensors dependencies

```bash 
idf.py add-dependency "esp-idf-lib/si7021"
idf.py add-dependency "esp-idf-lib/i2cdev"
```

```bash
cd ~/sed/esp-idf
./install.sh esp32,esp32c3
```
