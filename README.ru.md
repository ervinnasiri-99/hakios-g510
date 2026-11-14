# hakios-g510

English · [Русский](README.ru.md) · [Українська](README.uk.md)

HakiOS для **General Mobile GM 21 Plus (G510)**: MediaTek Helio G35 (**MT6765**), PowerVR GE8320 GPU, eMMC.
iOS 16.5's own user space (launchd → backboardd → SpringBoard → apps) on Apple's open XNU kernel
(xnu-8796.121.2), с загрузчиком и драйверами этого телефона. Первый MediaTek-порт проекта на MT6765.

Ничего на телефоне не заменяется навсегда: `mtkloader` сидит в `boot_a` перед Android-ядром,
запускает XNU один раз по команде из Android, после сессии телефон возвращается в Android. iOS
системный образ живёт в своём разделе (`xnusys`, вырезан из конца `userdata`), и ядро читает его
с eMMC по мере необходимости.

## Что работает

| | | |
| --- | --- | --- |
| boot | works | mtkloader (`mtk/loader/` общего дерева): MMU off, eMMC через MSDC0, дерево устройств для XNU; 8 ядер |
| CPU | works | большие ядра разогнаны (`mtk_cpufreq=3`) |
| storage | works, read only | системный образ читается с eMMC по мере надобности (`hakios.mtk_emmc.c`, `emmc_root=1`): ~2.5 GB из 4 под iOS |
| display | works | 720×1600, iOS в масштабе 2 |
| touch | works | Novatek NT36525 по SPI0 (`hakios.tcm.c`; загрузчик шлёт прошивку) |
| buttons | works | power, volume (`hakios.mtk_keys.c`) |
| GPU | missing | PowerVR GE8320 — пока только fb-only (fbscale=2 fbfit=3) |
| sensors | missing | акселерометр через SCP sensor hub |
| Wi-Fi | partial | CONNSYS поднят, WMT канал через BTIF/STP; сети пока нет (connsys_step=8) |
| Bluetooth | partial | bluetoothd iOS 16.5 на MT6631 (HCI over STP): включён, BLE скан видит приложения, Настройки видят устройства; подтверждённое парирование, профили и аудио пока нет |
| audio, camera, haptics, cellular | missing | |

## Что здесь есть

- `mtkloader/` — bare-metal загрузчик (MIT): встраивается в `boot_a` перед Android-ядром, поднимает
  то, чего XNU не умеет (прошивка тача по SPI, PMIC, такты), читает eMMC, строит дерево устройств XNU и boot-args.
- `kernel/` — драйверы этого телефона в XNU (APSL 2.0, см. `kernel/README.md`).
- `tools/` — запуск: `xnu-cycle.sh` (одна прогонка: сборка, прошивка `boot_a`, запуск XNU, сбор лога),
  `boot-xnu.sh`, `chainboot.py`, `system-flash.sh` (системный образ в `xnusys`), `gpt-carve.py`,
  пробы (`gpu-probe.sh`, `touch-probe.sh`, `msdc-test.sh`), читатели логов (`fbshot.py`, `frames.py`,
  `cache-name.py`, `rwm-recipes.py`).
- `configs/` — `g510_defconfig` и `g510.config`: конфигурация ядра для этого телефона в общем дереве.
- `pull-firmware.sh` — прошивка телефона из **вашего** Android в `fw-*/` (нужен root).
- `docs/` — как каждая часть была найдена и что показывала каждая прогонка.
- `device.conf` — телефон и состояние каждого компонента.

Общее дерево ветки `mtk` ([hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel), ветка `mtk`) имеет
тот же загрузчик и скрипты (`mtk/loader/`, `mtk/tools/`) и эти драйверы под именами `hakios` (`mtk/mt6765/`), и собирается/запускается `make`.

## Нет здесь, по умыслу

Нет файлов iOS, нет прошивки телефона, нет скриншотов (LEGAL.md). Сборка загрузчика делает два заголовка,
которые git игнорирует: `touchfw.h` из `hdl_firmware.img` вашего телефона (`pull-firmware.sh`) и `font8x16.h`,
шрифт загрузчика 8x16, растеризованный на Mac.

## Запуск

В общем дереве, ветка `mtk` (что нужно:
- General Mobile GM 21 Plus (G510) с разблокированным загрузчиком, рутованный Android (Magisk), его стоковый
  `boot_a` сохранён (`backup/boot_a_magisk.img` в корне дерева);
- Mac, свой Darwin userland 16.5 IPSW (для `xnuloader/identity.h`).

```sh
git clone -b mtk https://github.com/oosexgang/hakios-xnu-kernel.git hakios && cd hakios
tools/hakios-device.py sync g510      # этот репозиторий, в devices/g510
export HAKIOS_SERIAL=...                # ваш телефон, как показывает `adb devices`
devices/g510/pull-firmware.sh         # fw-touch/, fw-connsys/
xnuloader/mkidentity.sh your.ipsw
make g510_defconfig && make kernel    # hakios-kernel: mtk_emmc mtk_connsys mtk_keys tcm mtk_scp
make phone SECS=300 RAMDISK=...         # boot_a прошит, XNU раз, Android обратно, лог в logs/
```

Пользовательское пространство, которое бежит на этом телефоне (Mesa panfrost, драйвер Metal), пока читает
старые имена проекта (`kern.redwood.*`, `RedwoodMali`) и пока не в общем дереве; `RAMDISK=` и системный образ
в `xnusys` — его.

Собственными `tools/`, в структуре дерева, где они писались (`mtkloader/` и `tools/` на вершине,
рядом с `xnuloader/`, `xnu-deps/` и `userland/`):

```sh
./pull-firmware.sh                   # fw-touch/, fw-connsys/ рядом с mtkloader/ этого репозитория
XNU_SECS=300 XNU_ARGS="cpus=8 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1" \
    tools/xnu-cycle.sh                # одна прогонка; лог падает в logs/
```

## Лицензии

`kernel/` — APSL 2.0 (`LICENSE.APSL`), как код, встроенный в XNU; всё остальное — MIT (`LICENSE`).
См. `NOTICE` и `LEGAL.md`.