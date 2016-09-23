#!/bin/sh
if [ $# -lt 1 ]; then
    echo "Usage: $0 up|down"
    exit 1
fi

METHOD=
if [ "$1" = "up" ]; then
    METHOD=net.connman.vpn.Connection.Connect
elif [ "$1" = "down" ]; then
    METHOD=net.connman.vpn.Connection.Disconnect
else
    echo "Usage: $0 up|down"
    exit 2
fi

logger "$0 $1"
for FILE in $(find /home/nemo/.local/share/system/vpn -mindepth 1 -maxdepth 1); do
    TOKEN=$(basename $FILE)
    OBJECTPATH=/net/connman/vpn/connection/$TOKEN
    /bin/dbus-send --system --dest=net.connman.vpn --print-reply $OBJECTPATH $METHOD
    logger "Invoked $METHOD for $OBJECTPATH"
done

exit 0
