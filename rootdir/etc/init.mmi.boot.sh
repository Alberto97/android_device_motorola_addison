#!/vendor/bin/sh

PATH=/sbin:/vendor/sbin:/vendor/bin:/vendor/xbin
export PATH

scriptname=${0##*/}

notice()
{
	echo "$*"
	echo "$scriptname: $*" > /dev/kmsg
}

start_copying_prebuilt_qcril_db()
{
    if [ -f /vendor/radio/qcril_database/qcril.db -a ! -f /data/vendor/radio/qcril.db ]; then
        # [MOTO] - First copy db from the old N path to O path for upgrade
        if [ -f /data/misc/radio/qcril.db ]; then
            cp /data/misc/radio/qcril.db /data/vendor/radio/qcril.db
            # copy the backup db from the old N path to O path for upgrade
            if [ -f /data/misc/radio/qcril_backup.db ]; then
                cp /data/misc/radio/qcril_backup.db /data/vendor/radio/qcril_backup.db
            fi
            # Now delete the old folder
            rm -fr /data/misc/radio
        else
            cp /vendor/radio/qcril_database/qcril.db /data/vendor/radio/qcril.db
        fi
        chown -h radio.radio /data/vendor/radio/qcril.db
    else
        # [MOTO] if qcril.db's owner is not radio (e.g. root),
        # reset it for the recovery
        qcril_db_owner=`stat -c %U /data/misc/radio/qcril.db`
        echo "qcril.db's owner is $qcril_db_owner"
        if [ $qcril_db_owner != "radio" ]; then
            echo "reset owner to radio for qcril.db"
            chown -h radio.radio /data/misc/radio/qcril.db
        fi
    fi
}

# We take this from cpuinfo because hex "letters" are lowercase there
set -A cinfo `cat /proc/cpuinfo | sed -n "/Revision/p"`
hw=${cinfo[2]#?}

# Now "cook" the value so it can be matched against devtree names
m2=${hw%?}
minor2=${hw#$m2}
m1=${m2%?}
minor1=${m2#$m1}
if [ "$minor2" == "0" ]; then
	minor2=""
	if [ "$minor1" == "0" ]; then
		minor1=""
	fi
fi
setprop ro.hw.revision p${hw%??}$minor1$minor2
unset hw cinfo m1 m2 minor1 minor2

# Let kernel know our image version/variant/crm_version
if [ -f /sys/devices/soc0/select_image ]; then
    image_version="10:"
    image_version+=`getprop ro.build.id`
    image_version+=":"
    image_version+=`getprop ro.build.version.incremental`
    image_variant=`getprop ro.product.name`
    image_variant+="-"
    image_variant+=`getprop ro.build.type`
    oem_version=`getprop ro.build.version.codename`
    echo 10 > /sys/devices/soc0/select_image
    echo $image_version > /sys/devices/soc0/image_version
    echo $image_variant > /sys/devices/soc0/image_variant
    echo $oem_version > /sys/devices/soc0/image_crm_version
fi

#
# Copy qcril.db if needed for RIL
#
start_copying_prebuilt_qcril_db
echo 1 > /data/vendor/radio/db_check_done

#
# Make modem config folder and copy firmware config to that folder for RIL
#
if [ -f /data/vendor/radio/ver_info.txt ]; then
    prev_version_info=`cat /data/vendor/radio/ver_info.txt`
else
    prev_version_info=""
fi

cur_version_info=`cat /firmware/verinfo/ver_info.txt`
if [ ! -f /firmware/verinfo/ver_info.txt -o "$prev_version_info" != "$cur_version_info" ]; then
    rm -rf /data/vendor/radio/modem_config
    mkdir /data/vendor/radio/modem_config
    chmod 770 /data/vendor/radio/modem_config
    cp -r /firmware/image/modem_pr/mcfg/configs/* /data/vendor/radio/modem_config
    chown -hR radio.radio /data/vendor/radio/modem_config
    cp /firmware/verinfo/ver_info.txt /data/vendor/radio/ver_info.txt
    chown radio.radio /data/vendor/radio/ver_info.txt
fi

cp /firmware/image/modem_pr/mbn_ota.txt /data/vendor/radio/modem_config
chown radio.radio /data/vendor/radio/modem_config/mbn_ota.txt
echo 1 > /data/vendor/radio/copy_complete
