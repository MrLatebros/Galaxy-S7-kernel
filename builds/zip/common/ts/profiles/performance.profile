# Performance
   
   # Little CPU
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   write /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor interactive
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
   write /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq 442000
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
   write /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 1586000
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/go_hispeed_load
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/go_hispeed_load 74
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/above_hispeed_delay
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/above_hispeed_delay "19000"
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/timer_rate
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/timer_rate 10000
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/hispeed_freq
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/hispeed_freq 754000
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/timer_slack
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/timer_slack 10000
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/target_loads
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/target_loads "75"
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/min_sample_time
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/min_sample_time 105000
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/mode
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/mode 0
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/boost
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/boost 0
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/io_is_busy
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/io_is_busy 1
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/param_index
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/param_index 0
   chmod 0664 /sys/devices/system/cpu/cpu0/cpufreq/interactive/boostpulse_duration
   write /sys/devices/system/cpu/cpu0/cpufreq/interactive/boostpulse_duration 1000000

   # Big CPU
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
   write /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor interactive
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq
   write /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq 728000
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
   write /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq 2600000
   chmod 0444 /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/go_hispeed_load
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/go_hispeed_load 80
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/above_hispeed_delay
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/above_hispeed_delay "20000"
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/timer_rate
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/timer_rate 10000
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/hispeed_freq
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/hispeed_freq 1040000
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/timer_slack
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/timer_slack 10000
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/target_loads
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/target_loads "75"
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/min_sample_time
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/min_sample_time 90000
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/mode
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/mode 0
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/boost
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/boost 0
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/io_is_busy
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/io_is_busy 1
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/param_index
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/param_index 0
   chmod 0664 /sys/devices/system/cpu/cpu4/cpufreq/interactive/boostpulse_duration
   write /sys/devices/system/cpu/cpu4/cpufreq/interactive/boostpulse_duration 1000000

   # CPU HOTPLUG
   write /sys/power/cpuhotplug/enabled 1

   # HMP
   chmod 0664 /sys/kernel/hmp/up_threshold
   write /sys/kernel/hmp/up_threshold 430
   chmod 0664 /sys/kernel/hmp/down_threshold
   write /sys/kernel/hmp/down_threshold 150

   # GPU
   chmod 0664 /sys/devices/14ac0000.mali/max_clock
   write /sys/devices/14ac0000.mali/max_clock 650
   chmod 0664 /sys/devices/14ac0000.mali/min_clock
   write /sys/devices/14ac0000.mali/min_clock 260
   chmod 0664 /sys/devices/14ac0000.mali/power_policy
   write /sys/devices/14ac0000.mali/power_policy coarse_demand
   chmod 0664 /sys/devices/14ac0000.mali/dvfs_governor
   write /sys/devices/14ac0000.mali/dvfs_governor 1
   chmod 0664 /sys/devices/14ac0000.mali/highspeed_clock
   write /sys/devices/14ac0000.mali/highspeed_clock 419
   chmod 0664 /sys/devices/14ac0000.mali/highspeed_load
   write /sys/devices/14ac0000.mali/highspeed_load 95
   chmod 0664 /sys/devices/14ac0000.mali/highspeed_delay
   write /sys/devices/14ac0000.mali/highspeed_delay 1

   # IO Scheduler
   write /sys/block/sda/queue/scheduler zen
   write /sys/block/sda/queue/read_ahead_kb 1024
   write /sys/block/mmcblk0/queue/scheduler zen
   write /sys/block/mmcblk0/queue/read_ahead_kb 2048

   # Wakelocks
   write /sys/module/wakeup/parameters/enable_ssp_wl 1
   write /sys/module/wakeup/parameters/enable_sensorhub_wl 1
   write /sys/module/sec_battery/parameters/wl_polling 10
   write /sys/module/sec_nfc/parameters/wl_nfc 2

   # Misc
   write /sys/module/sync/parameters/fsync_enabled 0
   write /sys/kernel/dyn_fsync/Dyn_fsync_active 0
   write /sys/kernel/sched/gentle_fair_sleepers 0
   write /sys/kernel/sched/arch_power 0
   write /sys/kernel/power_suspend/power_suspend_mode 2
   write /proc/sys/net/ipv4/tcp_congestion_control westwood

   # SWAP
   write /proc/sys/vm/swappiness 190

   # LMK
   write /sys/module/lowmemorykiller/parameters/minfree "20432,26040,32648,42256,59064,101152"

