# DMP

Реализация тестировалась на Fedora Linux 43 (Server Edition)

Ядро 6.18.10-200.fc43.x86_64.

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

Затем можно посмотреть на само устройство (у меня это - `dm-3`)

```
# ls -la /dev/mapper/dmp1
lrwxrwxrwx. 1 root root 7 мар 19 14:27 /dev/mapper/dmp1 -> ../dm-X
```

## Тестирование
### Чтение и запись (вместо `dm-X` вставить свой номер):
```
dd if=/dev/urandom of=/dev/mapper/dmp1 oflag=direct bs=4K count=1
dd if=/dev/mapper/dmp1 of=/dev/null iflag=direct bs=4K count=1
cat /sys/block/dm-X/statistics
```

Ожидаемый вывод:
```
Output:
        read:
                reqs:   1
                avg size:       4096
        write:
                reqs:   1
                avg size:       4096
        total:
                reqs:   2
                avg size:       4096
```

### Повторные чтение и запись:
```
dd if=/dev/urandom of=/dev/mapper/dmp1 oflag=direct bs=2K count=1
dd if=/dev/mapper/dmp1 of=/dev/null iflag=direct bs=2K count=1
cat /sys/block/dm-X/statistics
```

Ожидаемый вывод:
```
Output:
        read:
                reqs:   2
                avg size:       3072
        write:
                reqs:   2
                avg size:       3072
        total:
                reqs:   4
                avg size:       3072
```

### Повторные чтения:
```
dd if=/dev/mapper/dmp1 of=/dev/null iflag=direct bs=2K count=1
dd if=/dev/mapper/dmp1 of=/dev/null iflag=direct bs=2K count=1
cat /sys/block/dm-X/statistics
```

Ожидаемый вывод:
```
Output:
        read:
                reqs:   4
                avg size:       3072
        write:
                reqs:   2
                avg size:       3072
        total:
                reqs:   6
                avg size:       3072
```

### Повторная запись:
```
dd if=/dev/urandom of=/dev/mapper/dmp1 oflag=direct bs=3K count=1
cat /sys/block/dm-X/statistics
```

Ожидаемый вывод:
```
Output:
        read:
                reqs:   4
                avg size:       3072
        write:
                reqs:   3
                avg size:       3072
        total:
                reqs:   7
                avg size:       3072
```
## Примечание

Я не фиксирую в статистике упреждающие чтения

Также я фильтрую чтения, производимые `udev-worker` при попытке прочитать информацию с устройства сразу после его загрузки, для получения такого же результата, как в примере
