#!/usr/bin/env bash

srcpath=$2
vendor=$3
target=$4

funclist=(
wpa_driver_nl80211_driver_cmd
wpa_driver_set_p2p_noa
wpa_driver_get_p2p_noa
wpa_driver_set_p2p_ps
wpa_driver_set_ap_wps_p2p_ie
wpa_driver_wext_combo_scan
wpa_driver_wext_driver_cmd
wpa_driver_signal_poll
)

source=$srcpath/$(basename $target)

echo vendor: $vendor
echo source: $source
echo target: $target

mkdir -p $(dirname $target)
echo "/* Auto-generated */"    > $target
echo "/* source: $source */"  >> $target
echo "/* target: $target */"  >> $target
echo "/* date  : $(date) */"  >> $target
[ -f $source ] && cat $source >> $target
for func in ${funclist[@]}; do
	sed -i "s/\([^,\s]\)${func}\s*(/\1${vendor}_${func}(/g" $target
done

if [ "x$(basename $target)" == "xdriver_cmd_nl80211.c" ]; then

cat << EOF >> $target

#include "type.h"

driver_cmd_nl80211_cb ${vendor}_nl80211_cb = {
	${vendor}_wpa_driver_nl80211_driver_cmd,
	${vendor}_wpa_driver_set_p2p_noa,
	${vendor}_wpa_driver_get_p2p_noa,
	${vendor}_wpa_driver_set_p2p_ps,
	${vendor}_wpa_driver_set_ap_wps_p2p_ie
};
EOF

elif [ "x$(basename $target)" == "xdriver_cmd_wext.c" ]; then

cat << EOF >> $target

#include "type.h"

driver_cmd_wext_cb ${vendor}_wext_cb = {
	${vendor}_wpa_driver_wext_combo_scan,
	${vendor}_wpa_driver_wext_driver_cmd,
	${vendor}_wpa_driver_signal_poll
};
EOF

fi
