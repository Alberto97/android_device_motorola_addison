#!/sbin/sh

variant=`getprop ro.boot.radio`

if [ $variant == "China" ]; then
    rm /system/etc/libnfc-nxp.conf
    mv /system/etc/libnfc-nxp_ds.conf /system/etc/libnfc-nxp.conf
fi
