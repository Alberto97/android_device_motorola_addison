#!/sbin/sh

variant=`getprop ro.boot.radio`

if [ $variant == "China" ]; then
    rm /system/vendor/etc/libnfc-nxp.conf
    mv /system/vendor/etc/libnfc-nxp_ds.conf /system/vendor/etc/libnfc-nxp.conf
fi
