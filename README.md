# Seismic-sensor
seismic sensor based on esp32

seismic v1 - simple data viewer on screen + serial

ESP32_ADXL_OLED_SPIFFS_DataLogger_01 :
  - read sensor 
  - store in spiffs 
  - handle web server with data publication 
  - send email to gmail address if above a threshold
  - TODO:
    - dual core for server+email
    - sim phone support
