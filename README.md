# OLD CLOCK 24v

## Startup

1. Reads SSID and password from EEPROM and tries to connect.
2. If cannot connect it starts in AP mode (SSID: "CLOCK").
   It will stay in this mode ("pause") for about 5 minutes before restarting.
3. If it can connect it blinks last byte of the IP address


## Web server

    http://<ipaddress>/

Returns all settings and (if connected to wifi and NTP) shows the NTP time (minutes).

Settings (one or all can be set):

    http://<ipaddress>/
        ?ssid=ssidname
        &password=ssidpassword
        &dials=int              - set clock dials: 0=00:00, 334=05:34, 23:59=719
        &dialsaddress=int       - internal used for wear-leveling... should be 0 or max 2
        &pause=int              - != 0 makes it pause for 5 minutes and then restart


## Internals

1. (Updates and) Reads the NTP time
2. Reads the dial clock time from memory
3. Compares dials and ntp:
   3.1 If dials and ntp is the same we do nothing
   3.2 Dials are "before" the NTP time we wait
       (where "before" means < 70 minutes before - this is to handle DST)
   3.2 Dials are "after" we make it tick and save the new dial position

If wifi is dropped we restart to try and connect again (see startup).

We restart once every day at 04:00.
   