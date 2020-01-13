# MQTT IR Controller

please see this awesome project [https://github.com/mdhiggins/ESP8266-HTTP-IR-Blaster](ESP8266-HTTP-IR-Blaster)

I'd just removed HTTP / NTP / Alexa stuff et put MQTT inside.

# Publish example : pulse wait pulse

topic  : ir/json
payload: {"type":"nec","length":32,"data":"2FD28D7"}

topic  : ir/json
payload: {"type":"delay","rdelay":750}

topic  : ir/json
payload: {"type":"nec","length":32,"data":"2FD28D7"}

using [https://mosquitto.org/man/mosquitto_pub-1.html](mosquitto) :
```bash
$ mosquitto_pub -h mqtt.local -t ir/json -m '{"type":"nec","length":32,"data":"2FD28D7"}'
$ mosquitto_pub -h mqtt.local -t ir/json -m '{"type":"delay","rdelay":750}'
$ mosquitto_pub -h mqtt.local -t ir/json -m '{"type":"nec","length":32,"data":"2FD28D7"}'
```

# Subscribe example : receiving pulse JSON data

topic  : ir/receive

using [https://mosquitto.org/man/mosquitto_sub-1.html](mosquitto) :
```bash
$ mosquitto_sub -h mqtt.local -t ir/receive
{"type":"NEC","data":"2FDD827","length":32}
{"type":"NEC","data":"2FD00FF","length":32}
^C
```