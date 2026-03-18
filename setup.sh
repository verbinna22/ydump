insmod dmp.ko &&
dmsetup create zero1 --table "0 1000 zero" &&
dmsetup create dmp1 --table "0 1000 dmp /dev/mapper/zero1"
