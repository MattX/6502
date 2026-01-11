## **pico\_status\_led**

Enables access to the on-board status LED(s)

### **Detailed Description**

Boards usually have access to one or two on-board status LEDs which are configured via the board header (PICO\_DEFAULT\_LED\_PIN, CYW43\_WL\_GPIO\_LED\_PIN and/or PICO\_DEFAULT\_WS2812\_PIN). This library hides the low-level details so you can use the status LEDs for all boards without changing your code.

| Note | If your board has both a single-color LED and a colored LED, you can independently control the single-color LED with the `status_led_` APIs, and the colored LED with the `colored_status_led_` APIs |
| :---- | :---- |

### **Macros**

* `#define PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))`

* `#define PICO_COLORED_STATUS_LED_COLOR_FROM_WRGB(w, r, g, b) (((w) << 24) | ((r) << 16) | ((g) << 8) | (b))`

### **Functions**

`bool status_led_init (void)`

Initialize the status LED(s)

`bool status_led_init_with_context (struct async_context *context)`

Initialise the status LED(s)

`static bool colored_status_led_supported (void)`

Determine if the `colored_status_led_` APIs are supported (i.e. if there is a colored status LED, and its use isn’t disabled via PICO\_COLORED\_STATUS\_LED\_AVAILABLE being set to 0\.

`static bool status_led_via_colored_status_led (void)`

Determine if the colored status LED is being used for the single-color `status_led_` APIs.

`static bool status_led_supported (void)`

Determine if the single-color `status_led_` APIs are supported (i.e. if there is a regular LED, and its use isn’t disabled via PICO\_STATUS\_LED\_AVAILABLE being set to 0, or if the colored status LED is being used for the single-color `status_led_` APIs.

`bool colored_status_led_set_state (bool led_on)`

Set the colored status LED on or off.

`bool colored_status_led_get_state (void)`

Get the state of the colored status LED.

`bool colored_status_led_set_on_with_color (uint32_t color)`

Ensure the colored status LED is on, with the specified color.

`uint32_t colored_status_led_get_on_color (void)`

Get the color used for the status LED value when it is on.

`static bool status_led_set_state (bool led_on)`

Set the status LED on or off.

`static bool status_led_get_state ()`

Get the state of the status LED.

`void status_led_deinit ()`

De-initialize the status LED(s)

### **Macro Definition Documentation**

#### **PICO\_COLORED\_STATUS\_LED\_COLOR\_FROM\_RGB**

`#define PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))`

Generate an RGB color value for /ref colored\_status\_led\_set\_on\_with\_color.

#### **PICO\_COLORED\_STATUS\_LED\_COLOR\_FROM\_WRGB**

`#define PICO_COLORED_STATUS_LED_COLOR_FROM_WRGB(w, r, g, b) (((w) << 24) | ((r) << 16) | ((g) << 8) | (b))`

Generate an WRGB color value for [colored\_status\_led\_set\_on\_with\_color](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_status_led_1ga3225d411a023e2457e58492b6d1ff077).

| Note | If your hardware does not support a white pixel, the white component is ignored |
| :---- | :---- |

### **Function Documentation**

#### **colored\_status\_led\_get\_on\_color**

`uint32_t colored_status_led_get_on_color (void)`

Get the color used for the status LED value when it is on.

| Note | If your hardware does not support a colored status LED (PICO\_DEFAULT\_WS2812\_PIN), this function always returns 0x0. |
| :---- | :---- |

**Returns**

The color used for the colored status LED when it is on, in 0xWWRRGGBB format

#### **colored\_status\_led\_get\_state**

`bool colored_status_led_get_state (void)`

Get the state of the colored status LED.

| Note | If your hardware does not support a colored status LED (PICO\_DEFAULT\_WS2812\_PIN), this function returns false. |
| :---- | :---- |

**Returns**

true if the colored status LED is on, or false if the colored status LED is off

#### **colored\_status\_led\_set\_on\_with\_color**

`bool colored_status_led_set_on_with_color (uint32_t color)`

Ensure the colored status LED is on, with the specified color.

| Note | If your hardware does not support a colored status LED (PICO\_DEFAULT\_WS2812\_PIN), this function does nothing and returns false. |
| :---- | :---- |

**Parameters**

| `color` | The color to use for the colored status LED when it is on, in 0xWWRRGGBB format |
| :---- | :---- |

**Returns**

true if the colored status LED could be set, otherwise false on failure

#### **colored\_status\_led\_set\_state**

`bool colored_status_led_set_state (bool led_on)`

Set the colored status LED on or off.

| Note | If your hardware does not support a colored status LED (PICO\_DEFAULT\_WS2812\_PIN), this function does nothing and returns false. |
| :---- | :---- |

**Parameters**

| `led_on` | true to turn the colored LED on. Pass false to turn the colored LED off |
| :---- | :---- |

**Returns**

true if the colored status LED could be set, otherwise false

#### **colored\_status\_led\_supported**

`static bool colored_status_led_supported (void) [inline], [static]`

Determine if the `colored_status_led_` APIs are supported (i.e. if there is a colored status LED, and its use isn’t disabled via PICO\_COLORED\_STATUS\_LED\_AVAILABLE being set to 0\.

**Returns**

true if the colored status LED API is available and expected to produce visible results

**See also**

PICO\_COLORED\_STATUS\_LED\_AVAILABLE

#### **status\_led\_deinit**

\[.memname\]\`void status\_led\_deinit \`

De-initialize the status LED(s)

De-initializes the status LED(s) when they are no longer needed.

#### **status\_led\_get\_state**

`static bool status_led_get_state [inline], [static]`

Get the state of the status LED.

| Note | If your hardware does not support a status LED, this function always returns false. |
| :---- | :---- |

**Returns**

true if the status LED is on, or false if the status LED is off

#### **status\_led\_init**

`bool status_led_init (void)`

Initialize the status LED(s)

Initialize the status LED(s) and the resources they need before use. On some devices (e.g. Pico W, Pico 2 W) accessing the status LED requires talking to the WiFi chip, which requires an [async\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#structasync_context). This method will create an [async\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#structasync_context) for you.

However an application should only use a single [async\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#structasync_context) instance to talk to the WiFi chip. If the application already has an async context (e.g. created by cyw43\_arch\_init) you should use [status\_led\_init\_with\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_status_led_1ga1d39f6237f4c2eb0b7d89938f379bd41) instead and pass it the [async\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#structasync_context) already created by your application

| Note | You must call this function (or [status\_led\_init\_with\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_status_led_1ga1d39f6237f4c2eb0b7d89938f379bd41)) before using any other pico\_status\_led functions. |
| :---- | :---- |

**Returns**

Returns true if the LED was initialized successfully, otherwise false on failure

**See also**

[status\_led\_init\_with\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_status_led_1ga1d39f6237f4c2eb0b7d89938f379bd41)

#### **status\_led\_init\_with\_context**

`bool status_led_init_with_context (struct async_context * context)`

Initialise the status LED(s)

Initialize the status LED(s) and the resources they need before use.

| Note | You must call this function (or [status\_led\_init](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_status_led_1gaf83057f40dddf57bbaa9845c2c56201c)) before using any other pico\_status\_led functions. |
| :---- | :---- |

**Parameters**

| `context` | An [async\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#structasync_context) used to communicate with the status LED (e.g. on Pico W or Pico 2 W) |
| :---- | :---- |

**Returns**

Returns true if the LED was initialized successfully, otherwise false on failure

**See also**

[status\_led\_init\_with\_context](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_status_led_1ga1d39f6237f4c2eb0b7d89938f379bd41)

#### **status\_led\_set\_state**

`static bool status_led_set_state (bool led_on) [inline], [static]`

Set the status LED on or off.

| Note | If your hardware does not support a status LED, this function does nothing and returns false. |
| :---- | :---- |

**Parameters**

| `led_on` | true to turn the LED on. Pass false to turn the LED off |
| :---- | :---- |

**Returns**

true if the status LED could be set, otherwise false

#### **status\_led\_supported**

`static bool status_led_supported (void) [inline], [static]`

Determine if the single-color `status_led_` APIs are supported (i.e. if there is a regular LED, and its use isn’t disabled via PICO\_STATUS\_LED\_AVAILABLE being set to 0, or if the colored status LED is being used for the single-color `status_led_` APIs.

**Returns**

true if the single-color status LED API is available and expected to produce visible results

**See also**

PICO\_STATUS\_LED\_AVAILABLE

PICO\_STATUS\_LED\_VIA\_COLORED\_STATUS\_LED

#### **status\_led\_via\_colored\_status\_led**

`static bool status_led_via_colored_status_led (void) [inline], [static]`

Determine if the colored status LED is being used for the single-color `status_led_` APIs.

**Returns**

true if the colored status LED is being used for the single-color `status_led_` API

**See also**

PICO\_STATUS\_LED\_VIA\_COLORED\_STATUS\_LED

