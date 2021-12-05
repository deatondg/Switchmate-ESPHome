# Use your Switchmate switches with Home Assistant through ESPHome.

### Why would you use this?

Switchmates are great for renters and students. If your Home Assistant server is not close to your switches, then your server might not be able to communicate to your switches through BLE. I have heard of people running auxiliary Home Assistant instances which communicate over MQTT to the main instance, but that is much more complicated than I would like. With this project, you can plop an ESP32 near your Switchmates so that you can communicate Home Assistant <--(WiFi)--> ESP32 <--(BLE)--> Switchmates.

### How do you set this up?

These instructions assume you are already comfortable with ESPHome (and probably C++). You will need to include the `switchmate.h` file in your ESPHome directory. For the yaml configuration, see the example. You will need configure four things:
1. Create a BLE client with the MAC address of your Switchmate. You can find the address in the Switchmate app or with a BLE scanning utility.
2. Create a `SwitchmateController` as a custom component. The constructor for `SwitchmateController` takes four arguments: A name used for logging, a boolean determining whether or not the ESP should ask for notifications (it does not appear my models support this, so I pass in `false`, but maybe yours do?), an interval in ms determining how often the ESP will poll the state of the switch, and the ID of the BLE client created in the last step. Be sure to give each `SwitchmateController` an ID in your yaml as you need it for the next step.
3. Create a custom switch by getting the `state_switch` member of the `SwitchmateController` created earlier.
4. Optionally create a custom sensor by getting the `battery_sensor` member of the `SwitchmateController` created ealier. Unsurprisingly, this reports the battery level of the Switchmate.

### Caveats

- I haven't used this for long enough to know what it does to the battery of the Switchmate. This will maintain a connection to the Switchmate 24/7, but it is called BLE for a reason.
- The battery level reported by the Switchmate seems to flucuate randomly. Mine will sit at 84% for a few minutes and then suddenly jump to 97%.
- If you set the polling interval to be shorter than 1000ms, Home Assistant more quickly reports a new state when you manually press the switch, but it seems to overwhelm either the Switchmate or the ESP32 I'm using, making the write requests take much longer or even fail. If you want to use Home Assistant to control the switch rather than just read the state, I would not set the polling rate to faster than this, but your mileage my vary.
- I don't know if this will work with the original Switchmate; I've only used this with my Switchmates Bright/Slim. Reading other people's code for dealing with Switchmates makes me think this will work for the original, but other people also had special-cases for original Switchmates, so idk. If it doesn't work for you, file an issue.
- The official party line is that ESPHome will not support more than 3 BLE clients, so you may need to run multiple ESPs if you have more Switchmates than that.

