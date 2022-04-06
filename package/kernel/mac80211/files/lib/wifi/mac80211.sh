#!/bin/sh

append DRIVERS "mac80211"

lookup_phy() {
	[ -n "$phy" ] && {
		[ -d /sys/class/ieee80211/$phy ] && return
	}

	local devpath
	config_get devpath "$device" path
	[ -n "$devpath" ] && {
		phy="$(iwinfo nl80211 phyname "path=$devpath")"
		[ -n "$phy" ] && return
	}

	local macaddr="$(config_get "$device" macaddr | tr 'A-Z' 'a-z')"
	[ -n "$macaddr" ] && {
		for _phy in /sys/class/ieee80211/*; do
			[ -e "$_phy" ] || continue

			[ "$macaddr" = "$(cat ${_phy}/macaddress)" ] || continue
			phy="${_phy##*/}"
			return
		done
	}
	phy=
	return
}

find_mac80211_phy() {
	local device="$1"

	config_get phy "$device" phy
	lookup_phy
	[ -n "$phy" -a -d "/sys/class/ieee80211/$phy" ] || {
		echo "PHY for wifi device $1 not found"
		return 1
	}
	config_set "$device" phy "$phy"

	config_get macaddr "$device" macaddr
	[ -z "$macaddr" ] && {
		config_set "$device" macaddr "$(cat /sys/class/ieee80211/${phy}/macaddress)"
	}

	return 0
}

check_mac80211_device() {
	config_get phy "$1" phy
	[ -z "$phy" ] && {
		find_mac80211_phy "$1" >/dev/null || return 0
		config_get phy "$1" phy
	}
	[ "$phy" = "$dev" ] && found=1
}


__get_band_defaults() {
	local phy="$1"

	( iw phy "$phy" info; echo ) | awk '
BEGIN {
        bands = ""
}

($1 == "Band" || $1 == "") && band {
        if (channel) {
		mode="NOHT"
		if (ht) mode="HT20"
		if (vht && band != "1:") mode="VHT80"
		if (he) mode="HE80"
		if (he && band == "1:") mode="HE20"
                sub("\\[", "", channel)
                sub("\\]", "", channel)
                bands = bands band channel ":" mode " "
        }
        band=""
}

$1 == "Band" {
        band = $2
        channel = ""
	vht = ""
	ht = ""
	he = ""
}

$0 ~ "Capabilities:" {
	ht=1
}

$0 ~ "VHT Capabilities" {
	vht=1
}

$0 ~ "HE Iftypes" {
	he=1
}

$1 == "*" && $3 == "MHz" && $0 !~ /disabled/ && band && !channel {
        channel = $4
}

END {
        print bands
}'
}

get_band_defaults() {
	local phy="$1"

	for c in $(__get_band_defaults "$phy"); do
		local band="${c%%:*}"
		c="${c#*:}"
		local chan="${c%%:*}"
		c="${c#*:}"
		local mode="${c%%:*}"

		case "$band" in
			1) band=2g;;
			2) band=5g;;
			3) band=60g;;
			4) band=6g;;
			*) band="";;
		esac

		[ -n "$band" ] || continue
		[ -n "$mode_band" -a "$band" = "6g" ] && return

		mode_band="$band"
		channel="$chan"
		htmode="$mode"
	done
}

detect_mac80211() {
	if [ $(cat /sys/bus/coresight/devices/coresight-stm/enable) -eq 0 ]
	then
		chipset=$(grep -o "IPQ.*" /proc/device-tree/model | awk -F/ '{print $1}')
		board=$(grep -o "IPQ.*" /proc/device-tree/model | awk -F/ '{print $2}')
		if [ "$chipset" == "IPQ9574" ]; then
			echo 0 > /sys/bus/coresight/devices/coresight-stm/enable
			echo "q6mem" > /sys/bus/coresight/devices/coresight-tmc-etr/out_mode
			echo 1 > /sys/bus/coresight/devices/coresight-tmc-etr/curr_sink
			echo 1 > /sys/bus/coresight/devices/coresight-stm/enable
		fi
	fi
	devidx=0

	config_load wireless

	if [ ! -f "/etc/config/wireless" ] || ! grep -q "enable_smp_affinity" "/etc/config/wireless"; then
		cat <<EOF
config smp_affinity  mac80211
	option enable_smp_affinity	1
	option enable_color		1

EOF
	fi
	devidx=0
	config_load wireless
	while :; do
		config_get type "radio$devidx" type
		[ -n "$type" ] || break
		devidx=$(($devidx + 1))
	done

	for _dev in /sys/class/ieee80211/*; do
		[ -e "$_dev" ] || continue

		dev="${_dev##*/}"

		found=0
		config_foreach check_mac80211_device wifi-device
		[ "$found" -gt 0 ] && continue

		mode_band=""
		channel=""
		htmode=""
		ht_capab=""

		get_band_defaults "$dev"

		path="$(iwinfo nl80211 path "$dev")"
		if [ -n "$path" ]; then
			dev_id="set wireless.radio${devidx}.path='$path'"
		else
			dev_id="set wireless.radio${devidx}.macaddr=$(cat /sys/class/ieee80211/${dev}/macaddress)"
		fi

		uci -q batch <<-EOF
			set wireless.radio${devidx}=wifi-device
			set wireless.radio${devidx}.type=mac80211
			${dev_id}
			set wireless.radio${devidx}.channel=${channel}
			set wireless.radio${devidx}.band=${mode_band}
			set wireless.radio${devidx}.htmode=$htmode
			set wireless.radio${devidx}.disabled=0
			set wireless.radio${devidx}.country=CN

			set wireless.default_radio${devidx}=wifi-iface
			set wireless.default_radio${devidx}.device=radio${devidx}
			set wireless.default_radio${devidx}.network=lan
			set wireless.default_radio${devidx}.mode=ap
			set wireless.default_radio${devidx}.ssid=OpenWrt_$(cat /sys/class/ieee80211/${dev}/macaddress | awk -F ":" '{print $4""$5""$6 }'| tr a-z A-Z)_${mode_band}
			set wireless.default_radio${devidx}.encryption=none
EOF
		uci -q commit wireless

		devidx=$(($devidx + 1))
	done
}

start_lbd() {
	local band_24g
	local band_5g
	local i=0

	driver=$(lsmod | cut -d' ' -f 1 | grep ath10k_core)

	if [ "$driver" == "ath10k_core" ]; then
		while [ $i -lt 10 ]
		do
			BANDS=$(/usr/sbin/iw dev 2> /dev/null | grep channel | cut -d' ' -f 2 | cut -d'.' -f 1)
			for channel in $BANDS
			do
				if [ "$channel" -le "14" ]; then
					band_24g=1
				elif [ "$channel" -ge "36" ]; then
					band_5g=1
				fi
			done

			if [ "$band_24g" == "1" ] && [ "$band_5g" == "1" ]; then
				/etc/init.d/lbd start
				return 0
			fi
			sleep 1
			i=$(($i + 1))
		done
	fi
	return 0
}

post_mac80211() {
	local action=${1}

	config_get enable_smp_affinity mac80211 enable_smp_affinity 0

	if [ "$enable_smp_affinity" -eq 1 ]; then
		[ -f "/lib/smp_affinity_settings.sh" ] && {
                        . /lib/smp_affinity_settings.sh
                        enable_smp_affinity_wifi
                }
		[ -f "/lib/update_smp_affinity.sh" ] && {
			. /lib/update_smp_affinity.sh
			enable_smp_affinity_wigig
		}
	fi

	case "${action}" in
		enable)
			[ -f "/usr/sbin/fst.sh" ] && {
				/usr/sbin/fst.sh start
			}
			if [ -f "/etc/init.d/lbd" ]; then
				start_lbd &
			fi
		;;
	esac

	chipset=$(grep -o "IPQ.*" /proc/device-tree/model | awk -F/ '{print $1}')
	board=$(grep -o "IPQ.*" /proc/device-tree/model | awk -F/ '{print $2}')
	if [ "$chipset" == "IPQ5018" ]; then
		echo "q6mem" > /sys/bus/coresight/devices/coresight-tmc-etr/out_mode
		echo 1 > /sys/bus/coresight/devices/coresight-tmc-etr/curr_sink
		echo 5 > /sys/bus/coresight/devices/coresight-funnel-mm/funnel_ctrl
		echo 7 >/sys/bus/coresight/devices/coresight-funnel-in0/funnel_ctrl
		echo 1 > /sys/bus/coresight/devices/coresight-stm/enable
	elif [ "$chipset" == "IPQ8074" ] && [ "$board" != "AP-HK10-C2" ]; then
		echo "q6mem" > /sys/bus/coresight/devices/coresight-tmc-etr/out_mode
		echo 1 > /sys/bus/coresight/devices/coresight-tmc-etr/curr_sink
		echo 5 > /sys/bus/coresight/devices/coresight-funnel-mm/funnel_ctrl
		echo 6 > /sys/bus/coresight/devices/coresight-funnel-in0/funnel_ctrl
		echo 1 > /sys/bus/coresight/devices/coresight-stm/enable
	elif [ "$chipset" == "IPQ6018" ] || [ "$chipset" == "IPQ807x" ]; then
		echo "q6mem" > /sys/bus/coresight/devices/coresight-tmc-etr/out_mode
		echo 1 > /sys/bus/coresight/devices/coresight-tmc-etr/curr_sink
		echo 5 > /sys/bus/coresight/devices/coresight-funnel-mm/funnel_ctrl
		echo 6 > /sys/bus/coresight/devices/coresight-funnel-in0/funnel_ctrl
		echo 1 > /sys/bus/coresight/devices/coresight-stm/enable
	elif [ "$chipset" == "IPQ9574" ]; then
                echo 0 > /sys/bus/coresight/devices/coresight-stm/enable
                echo "q6mem" > /sys/bus/coresight/devices/coresight-tmc-etr/out_mode
                echo 1 > /sys/bus/coresight/devices/coresight-tmc-etr/curr_sink
                echo 1 > /sys/bus/coresight/devices/coresight-stm/enable
	fi

	return 0
}

pre_mac80211() {
	local action=${1}

	case "${action}" in
		disable)
			[ -f "/usr/sbin/fst.sh" ] && {
				/usr/sbin/fst.sh set_mac_addr
				/usr/sbin/fst.sh stop
			}
			[ ! -f /etc/init.d/lbd ] || /etc/init.d/lbd stop

			extsta_path=/sys/module/mac80211/parameters/extsta
			[ -e $extsta_path ] && echo 0 > $extsta_path
		;;
	esac
	return 0
}
