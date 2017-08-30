#!/vendor/bin/sh

multisim=`getprop persist.radio.multisim.config`

if [ "$multisim" = "dsds" ] || [ "$multisim" = "dsda" ]; then
    start vendor.ril-daemon2
fi
