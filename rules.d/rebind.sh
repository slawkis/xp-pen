#!/bin/sh

HIDPATH="/sys/bus/usb/drivers/usbhid"
USBPATH="/sys/bus/usb/drivers/xppen"

if  [ ! -d $HIDPATH ]; then logger "rebind: $HIDPATH not found"; exit; fi
if  [ ! -d $USBPATH ]; then logger "rebind: $USBPATH not found"; exit; fi

DEV=$1
if [ "x$DEV" == "x" ]; then logger "rebind: no device"; exit; fi
logger "rebind: device = $DEV"

declare -a PARTS
readarray -n 0 -t -d '/' PARTS <<< $(echo $DEV)					#index=6

read DEV <<< $( echo ${PARTS[6]} )  # no extra \n chars...
if [ "x$DEV" == "x" ]; then logger "rebind: no device"; exit; fi

NUM=${DEV:${#DEV}-1}
if [ $NUM -ne 0 ]; then exit; fi

if [   -L "$HIDPATH/$DEV" ]; then logger "rebind: trying to unbind $DEV from HID"; echo $DEV > $HIDPATH/unbind ; sleep 1; fi
if [ ! -L "$USBPATH/$DEV" ]; then logger "rebind: trying to bind $DEV to XPPEN"  ; echo $DEV > $USBPATH/bind   ; sleep 1; fi
