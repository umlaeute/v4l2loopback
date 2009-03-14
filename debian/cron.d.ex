#
# Regular cron jobs for the v4l2loopback package
#
0 4	* * *	root	[ -x /usr/bin/v4l2loopback_maintenance ] && /usr/bin/v4l2loopback_maintenance
