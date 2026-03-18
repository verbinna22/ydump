# DMP

Реализация тестировалась на Linux Fedora Server 6.18.10-200.fc43.x86_64.

## Установка модуля
Используется make

Необходимо выполнить следующие команды, находясь в корне репозитория:
```
make
insmod dmp.ko
dmsetup create zero1 --table "0 1000 zero"
dmsetup create dmp1 --table "0 1000 dmp /dev/mapper/zero1"
```
Также можно воспользоваться скриптом `setup.sh`

## Тестирование
```

```
