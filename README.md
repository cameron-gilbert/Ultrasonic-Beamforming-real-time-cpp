## Rough Implementation Overview (In-Progress)



### ***main/*** 



###### **block accumulation (for further processing)**

* as packets arrive, they are grouped by frameNumber
* one frame is complete when a packet (512 samples) for all 102 mics is received



* these frames are appended to a block buffer

&nbsp;	- block size, x, is a user configurable parameter (frames/block) = 102 mics \* x \* 512 samples



* when x frames are added to the buffer, the block is complete
* swap/move used to pass completed block (mic\*time matrices) to processing without copying memory

&nbsp;	- immediate reuse of preallocated memory

&nbsp;	- safe lifetime management with shared\_ptr

* **completed blocks are immediately discarded as further processing is not implemented yet**
* **when it is implemented -> processing stage will receive completed blocks asynchronously with the blockReady signal**



&nbsp;	

### ***network/***<i> → data acquisition layer (TCP/simulation)</i>



**idataprovider.h**

* **defines abstract interface for any provider of raw frames to UI**
* start and stop are virtual left to be implemented by provider
* frameRecieved emitted when a new packet arrives
* errorOccurred emitted on connection or decoding error
* abstract host app from origin of data being provided



**simulateddataprovider.h**

* **concrete implementation of idataprovider to generate fake data for testing**
* m\_timer drives periodic, fixed interval packet generation
* generatePacket slot called by timer to create/emit one packet



**simulateddataprovider.cpp**

* **constructor:** sets timer interval, connects timout signal to generatePacket
* **start():** resets frame and mic counters and starts timer
* **stop():** halts packet generation
* **generatePacket():** allocates buffer of exactly 1044 bytes (1024 data + 20 header)
   	- writes 512 samples with sin wave to emulate real signal
   	- for testing: each mic has different phase offset for better vizualisation
   	- emits finished packet to frameRecieved listened to by UI
   	- increments mic index
   	- at 102, wraps back to 0 and increments frame number





### ***model/***<i> → data definition (frame parsing)</i>



**frame.h**

* **defines data structure for a single TCP packet for one microphone**
* each packet = 10 × 16-bit header fields + 512 × 16-bit samples
* **header** holds 10 raw header fields
* **samples** holds 512 decoded time domain samples for that single microphone



**framedecoder.h**

* **defines utility class for converting raw byte array into structured frame**



**framedecoder.cpp**

* **implements the decode function**
* ensures packet length and uses pointer to the raw bytes
* decodes header fields (reads the first 10 consecutive 16bit values from buffer)
* extracts mic and frame number
* skips over header bytes and reads the 512 integers into samples
* returns true if succeeded





### ***visualization/***<i> → oscilloscope widget</i>



**oscilloscopewidget.h**

* **custom widget subclass for drawing time domain signals**
* exposes setsignal to update waveform display
* stores latest signal samples internally and triggers repaints on update



**oscilloscopewidget.cpp**

* **implements simple oscilloscope style rendering with QPainter**
* scales sample amplitudes to widget height
* draws horizonatal midline and continuous polyline for waveform
* midline rendered only if no data
* different plots for each microphone depending on selected one



### ***ui/***<i> → control interface</i>



**mainwindow.cpp**

* **visualization aspect:**

  * maintains rolling history buffer of samples for each mic

 	- appends new recieved samples

 	- **updateOscilloscope():** looks up history for selected microphone and passes buffer to setSignal

 	- **on\_micComboBox\_currentIndexChanged():** connected to dropdown in UI, calls updateOscilloscope() with mic index to immediately refresh and show history for selected mic

