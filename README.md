# Seismic-sensor

seismic sensor based on esp32 :

seismic v1 - simple data viewer on screen + serial

ESP32_ADXL_OLED_SPIFFS_DataLogger_01 :
  - read sensor 
  - store in spiffs 
  - handle web server with data publication 
  - send email to gmail address if above a threshold
  - TODO:
    - dual core for server+email
    - sim phone support

Other platform possibility:
	- ESP32
	- RASPBERRY PI
	- Smartphone
	
	
Data transfert/monitoring:
	- SEEDLink http://ds.iris.edu/ds/nodes/dmc/services/seedlink/
	- https://manual.raspberryshake.org/traces.html
	
Other options:
	- commercial:
		- PALERT+:
			- price?
			- https://www.earthquakeearlywarning.systems/palert1.html
		- TREMASENSE:
			- Wellington based
			- price?
			- https://survive-it.co.nz/wp-content/uploads/2018/06/TremAsense-Brochure.pdf
			- https://survive-it.co.nz/product-category/seismic/seismic-sensors/
		- BECA "BEACON"
			- https://www.beca.com/what-we-do/projects/buildings/the-beacon-system
		- GURALP:
			- http://www.csi.net.nz/index.php/general/products/
		- GEOSIG:
			- https://www.geosig.com/
		- IESE
			- https://iese.co.nz/instrumentation-sales/
		- REFTEK:
			- https://www.reftek.com/building-monitoring/
		- GEOBIT:
			- https://geobit-instruments.com/seedlink-digitizers-sri32/
	- APP :
		- MyShake:
			- global, no device alert?
			- https://myshake.berkeley.edu/
		- https://play.google.com/store/apps/details?id=com.finazzi.distquakenoads
	- RASPBERRY :
		- Shake:
			- https://raspberryshake.org/
			- https://manual.raspberryshake.org/specifications.html#how-does-the-raspberry-shake-compare-to-a-broadband-seismometer
	- Software:
		- http://quakecatcher.net/index.php/
		